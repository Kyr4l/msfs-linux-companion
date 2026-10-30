// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// Per-aircraft live controls: aircraft identification, input event wire format, catalog merging and the on-disk cache.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

#include "app/Catalog.hpp"
#include "app/Profile.hpp"
#include "sim/AircraftId.hpp"
#include "sim/ByteBuffer.hpp"
#include "sim/ProbeSelect.hpp"
#include "sim/SimConnectClient.hpp"

using namespace lr;
using namespace lr::app;
using namespace lr::sim;

static int failures = 0;
#define CHECK(cond)                                                                         \
    do {                                                                                    \
        if (!(cond)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } \
    } while (0)

static CatalogEntry stdEntry(const char* name) {
    CatalogEntry e;
    e.panel = "Standard events";
    e.section = "Lights";
    e.function = name;
    e.api = e.name = name;
    e.kind = "sim_event";
    e.standard = true;
    return e;
}

int main() {
    // ---- which catalog an aircraft belongs to
    {
        auto id = [](const char* p) { return identifyAircraft(p).id; };
        CHECK(id("C:\\x\\SimObjects\\Airplanes\\Asobo_CJ4\\aircraft.CFG") == "Asobo_CJ4");
        CHECK(identifyAircraft("SimObjects/Airplanes/Asobo_CJ4/aircraft.cfg").label == "Asobo CJ4");
        CHECK(id("Asobo_FA18E\\aircraft.CFG") == "Asobo_FA18E");
        CHECK(id("C:\\p\\FlyByWire_A320_NEO\\aircraft.CFG") == "A32NX");
        CHECK(id("C:\\p\\_FlyByWire_A320_NEO-LIVERY\\aircraft.cfg") == "A32NX");
        CHECK(id("C:\\p\\FlyByWire_A380_842\\aircraft.CFG") == "A380X");
        // Asobo's own A320neo is a different cockpit: it must NOT pick the FlyByWire catalog
        CHECK(id("C:\\p\\Asobo_A320_NEO\\aircraft.CFG") == "Asobo_A320_NEO");
        CHECK(id("C:\\p\\Asobo_CJ4_LIVERY_01\\aircraft.CFG") == "Asobo_CJ4");
        CHECK(id("aircraft.CFG").empty());
        CHECK(id("").empty());
    }

    // ---- wire format of the input event commands (checked on a real socket)
    {
        int lsock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        CHECK(bind(lsock, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0);
        listen(lsock, 1);
        socklen_t len = sizeof a;
        getsockname(lsock, reinterpret_cast<sockaddr*>(&a), &len);
        SimConnectClient c;
        std::string err;
        CHECK(c.connect("127.0.0.1", ntohs(a.sin_port), 1000, err));
        int s = accept(lsock, nullptr, nullptr);
        c.enumerateInputEvents(7);
        c.getInputEvent(9, 0x1122334455667788ull);
        c.setInputEvent(0x1122334455667788ull, 2.5);
        uint8_t buf[512];
        size_t got = 0;
        for (int i = 0; i < 50 && got < 20 + 28 + 36; ++i) {
            ssize_t n = recv(s, buf + got, sizeof buf - got, MSG_DONTWAIT);
            if (n > 0) got += static_cast<size_t>(n);
            else std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(got == 20 + 28 + 36);
        auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, buf + o, 4); return v; };
        auto u64 = [&](size_t o) { uint64_t v; std::memcpy(&v, buf + o, 8); return v; };
        // enumerate: size 20, protocol 5, type 0xf000004f, send id 0, request id 7
        CHECK(u32(0) == 20 && u32(4) == 5 && u32(8) == 0xf000004fu && u32(16) == 7);
        // get: size 28, 0x50, request 9, hash
        CHECK(u32(20) == 28 && u32(28) == 0xf0000050u && u32(36) == 9 && u64(40) == 0x1122334455667788ull);
        // set: size 36, 0x51, hash, payload length 8, double
        CHECK(u32(48) == 36 && u32(56) == 0xf0000051u && u64(64) == 0x1122334455667788ull && u32(72) == 8);
        double d;
        std::memcpy(&d, buf + 76, 8);
        CHECK(d == 2.5);
        close(s);
        close(lsock);

        // reply parsing: two descriptors of 76 bytes, then a get reply
        ByteWriter w;
        w.u32(5); w.u32(2); w.u32(0); w.u32(1);
        w.str("LIGHTING_LANDING_1", 64); w.u64(0xAABBCCDDEEFF0011ull); w.u32(0);
        w.str("SOME_TEXT", 64); w.u64(42); w.u32(1);
        RecvMessage m;
        m.body = w.data();
        auto l = SimConnectClient::parseInputEventList(m);
        CHECK(l.requestId == 5 && l.events.size() == 2);
        CHECK(l.events[0].name == "LIGHTING_LANDING_1" && l.events[0].hash == 0xAABBCCDDEEFF0011ull && l.events[0].type == 0);
        CHECK(l.events[1].name == "SOME_TEXT" && l.events[1].type == 1);
        ByteWriter g;
        g.u32(9); g.u32(0); g.f64(3.0);
        RecvMessage gm;
        gm.body = g.data();
        auto v = SimConnectClient::parseGetInputEvent(gm);
        CHECK(v.requestId == 9 && v.type == 0 && v.number == 3.0);
        // a truncated list must not read past its end
        RecvMessage cut;
        cut.body.assign(w.data().begin(), w.data().begin() + 16 + 76 + 30);
        CHECK(SimConnectClient::parseInputEventList(cut).events.size() == 1);
    }

    // ---- merging what the sim reports into catalogs
    {
        CatalogSet set;
        Catalog fbw;
        fbw.aircraft = "A32NX";
        fbw.title = "FlyByWire A320neo (A32NX)";
        fbw.curated = true;
        CatalogEntry apu;
        apu.panel = "Overhead";
        apu.api = apu.name = "A32NX_OVHD_APU_MASTER_SW_PB_IS_ON";
        apu.kind = "lvar";
        fbw.entries.push_back(apu);
        set.addShipped(fbw);
        Catalog standard;
        standard.aircraft = kStandardCatalogId;
        standard.title = "Standard sim events";
        standard.curated = true;
        for (const char* n : {"GEAR_UP", "GEAR_DOWN", "TOGGLE_STARTER"}) standard.entries.push_back(stdEntry(n));
        set.addShipped(standard);
        set.refreshStandard();
        CHECK(set.find("A32NX")->entries.size() == 1);  // curated aircraft do not get the standard events

        auto fresh = CatalogSet::liveEntriesFor({{"LIGHTING_LANDING_1", false}, {"CJ4_AVIONICS_MASTER", false}, {"SOME_TEXT", true}},
                                                {"CJ4_PANEL_BRT", "ASOBO_X"});
        CHECK(fresh.size() == 4);  // the text-valued input event is not offered
        unsigned before = set.items.size();
        CHECK(set.applyLive("Asobo_CJ4", "Asobo CJ4", fresh));
        CHECK(set.items.size() == before + 1);
        const Catalog* cj4 = set.find("Asobo_CJ4");
        CHECK(cj4 && !cj4->curated);
        size_t live = 0, std_ = 0;
        for (const auto& e : cj4->entries) { live += e.live; std_ += e.standard; }
        CHECK(live == 4 && std_ == 3);
        CHECK(cj4->title == "Asobo CJ4 (from the sim)");
        CatalogSet reloaded;  // the cache stores the decorated title; loading it must not decorate it again
        reloaded.applyLive("Asobo_CJ4", cj4->title, fresh);
        CHECK(reloaded.find("Asobo_CJ4")->title == "Asobo CJ4 (from the sim)");
        CHECK(CatalogSet::liveEntriesFor({{"ANTI_ICE_ENGINE_1", false}}, {})[0].section == "ANTI ICE");
        // the sim's L: list is global: another aircraft's FlyByWire variables do not belong to the CJ4
        {
            const std::vector<std::string> all = {"A32NX_ENGINE_N2:1", "A380X_EFIS_L_BARO", "CJ4_SAI_State", "WT_PRESS_MAN_ALT_ON"};
            CHECK(CatalogSet::liveEntriesFor({}, all, "Asobo_CJ4").size() == 2);
            CHECK(CatalogSet::liveEntriesFor({}, all, "A32NX").size() == 4);
            CHECK(CatalogSet::liveEntriesFor({}, all, "A380X").size() == 4);
            CHECK(CatalogSet::liveEntriesFor({}, all).size() == 4);
        }
        const unsigned rev = cj4->revision;
        CHECK(!set.applyLive("Asobo_CJ4", "Asobo CJ4", fresh));  // identical report: nothing changes, no view rebuild
        CHECK(set.find("Asobo_CJ4")->revision == rev);
        // a later session reports fewer names (L: vars appear lazily): the cached ones stay
        CHECK(!set.applyLive("Asobo_CJ4", "Asobo CJ4", CatalogSet::liveEntriesFor({{"LIGHTING_LANDING_1", false}}, {})));
        CHECK(set.find("Asobo_CJ4")->entries.size() == 4 + 3);
        // ... and grow when it reports new ones
        CHECK(set.applyLive("Asobo_CJ4", "Asobo CJ4", CatalogSet::liveEntriesFor({{"NEW_EVENT", false}}, {})));
        CHECK(set.find("Asobo_CJ4")->entries.size() == 5 + 3);

        // curated aircraft take live entries but keep their own and add no standard events
        CHECK(set.applyLive("A32NX", "x", CatalogSet::liveEntriesFor({{"LIGHTING_STROBE_0", false}}, {})));
        const Catalog* a32 = set.find("A32NX");
        CHECK(a32->entries.size() == 2 && a32->curated && a32->entries[0].api == "A32NX_OVHD_APU_MASTER_SW_PB_IS_ON");
        // a name the curated catalog already lists is not duplicated, and reporting it is not a change
        CHECK(!set.applyLive("A32NX", "x", CatalogSet::liveEntriesFor({{"LIGHTING_STROBE_0", false}}, {"A32NX_OVHD_APU_MASTER_SW_PB_IS_ON"})));
        CHECK(set.find("A32NX")->entries.size() == 2);

        // what a profile for an aircraft without any catalog gets
        CHECK(set.forAircraft("Asobo_DA62") == set.standard());
        CHECK(set.forAircraft("Asobo_CJ4") == set.find("Asobo_CJ4"));

        // forgetting the cache removes the live entries only
        CHECK(set.forgetLive("Asobo_CJ4"));
        size_t liveLeft = 0;
        for (const auto& e : set.find("Asobo_CJ4")->entries) liveLeft += e.live;
        CHECK(liveLeft == 0 && set.find("Asobo_CJ4")->entries.size() == 3);

        // cache round trip: only live entries are written
        set.applyLive("Asobo_CJ4", "Asobo CJ4", fresh);
        const auto path = (std::filesystem::temp_directory_path() / "msfs-linuxrebind-live-test.json").string();
        std::string err;
        CHECK(set.find("Asobo_CJ4")->saveLive(path, err));
        Catalog back;
        CHECK(Catalog::loadLive(path, back, err));
        CHECK(back.aircraft == "Asobo_CJ4" && back.entries.size() == 4);
        for (const auto& e : back.entries) CHECK(e.live && !e.standard);
        std::filesystem::remove(path);

        // an entry becomes the action users expect
        const CatalogEntry* ev = nullptr;
        for (const auto& e : set.find("Asobo_CJ4")->entries)
            if (e.kind == "input_event" && e.api == "LIGHTING_LANDING_1") ev = &e;
        CHECK(ev != nullptr);
        if (ev) {
            Action act = ev->toAction();
            CHECK(act.kind == ActionKind::InputEventToggle && act.name == "LIGHTING_LANDING_1" && act.value == 2);
            CHECK(!actionNeedsMobiFlight(act.kind));
            CHECK(ev->panel == "Cockpit input events (from the sim)" && ev->section == "LIGHTING" && ev->function == "LANDING 1");
        }
        for (const auto& e : set.find("Asobo_CJ4")->entries)
            if (e.api == "CJ4_AVIONICS_MASTER") CHECK(e.section == "CJ4 AVIONICS" && e.function == "MASTER");
    }

    // ---- new action kinds survive the config file
    {
        Settings st;
        Profile p;
        p.name = "CJ4";
        p.aircraft = "Asobo_CJ4";
        Binding b;
        b.label = "Landing";
        b.input = {"1234:5678:x", input::InputKind::Button, 288, 0};
        Action set, tog;
        set.kind = ActionKind::InputEvent; set.name = "LIGHTING_LANDING_1"; set.value = 1;
        tog.kind = ActionKind::InputEventToggle; tog.name = "LIGHTING_STROBE_0"; tog.value = 3;
        b.onPress = {tog};
        b.onRelease = {set};
        p.bindings.push_back(b);
        st.profiles.push_back(p);
        st.activeProfile = "CJ4";
        const auto path = (std::filesystem::temp_directory_path() / "msfs-linuxrebind-b-test.json").string();
        std::string err;
        CHECK(saveSettings(path, st, err));
        Settings back;
        CHECK(loadSettings(path, back, err));
        CHECK(back.profiles[0].aircraft == "Asobo_CJ4");
        const Binding& r = back.profiles[0].bindings[0];
        CHECK(r.onPress[0].kind == ActionKind::InputEventToggle && r.onPress[0].value == 3 && r.onPress[0].name == "LIGHTING_STROBE_0");
        CHECK(r.onRelease[0].kind == ActionKind::InputEvent && r.onRelease[0].value == 1);
        CHECK(r.onPress[0].describe() == "B:LIGHTING_STROBE_0 cycle 3" && r.onRelease[0].describe() == "B:LIGHTING_LANDING_1 = 1");
        std::filesystem::remove(path);
    }

    // ---- which local listeners get a SimConnect handshake when the configured port does not answer
    {
        using lr::sim::ProbeTarget;
        const std::vector<ProbeTarget> listeners = {{"127.0.0.1", 1111, 10}, {"::1", 2222, 11}, {"127.0.0.1", 3333, 12}};
        CHECK(lr::sim::chooseProbeCandidates(listeners, false, {10, 11}).empty());  // no Wine process: the sim is not running
        CHECK(lr::sim::chooseProbeCandidates(listeners, false, {}).empty());
        const auto only = lr::sim::chooseProbeCandidates(listeners, true, {11});    // its own sockets only
        CHECK(only.size() == 1 && only[0].port == 2222);
        CHECK(lr::sim::chooseProbeCandidates(listeners, true, {99}).empty());       // sockets known, none of them listens
        CHECK(lr::sim::chooseProbeCandidates(listeners, true, {}).size() == 3);     // sockets unreadable: try the few that exist
        std::vector<ProbeTarget> many;
        for (unsigned i = 0; i < 20; ++i) many.push_back({"127.0.0.1", static_cast<unsigned short>(4000 + i), 100 + i});
        CHECK(lr::sim::chooseProbeCandidates(many, true, {}).size() == 6);          // ... but never twenty
    }

    std::printf(failures ? "\n%d check(s) FAILED\n" : "all live catalog checks passed\n", failures);
    return failures ? 1 : 0;
}
