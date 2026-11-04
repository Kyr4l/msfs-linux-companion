// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// Catalogs and guides made by the program: the plain .fsarchive reader, finding an aircraft's package, the labels guessed from the names and
// the tooltips, and the guide from a checklist. The labels are also scored against the hand-made catalogs of data/.
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>

#include <nlohmann/json.hpp>

#include "app/AutoCatalog.hpp"
#include "app/Catalog.hpp"
#include "app/SimPackages.hpp"
#include "sim/FsArchive.hpp"

using namespace lr;
using namespace lr::app;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond)                                                                         \
    do {                                                                                    \
        if (!(cond)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } \
    } while (0)

// A package archive as MSFS 2024 writes it (the list length carries one byte of padding, like the real ones).
static std::string makeArchive(const std::vector<std::pair<std::string, std::string>>& files, uint16_t flags = 0, const char* scheme = "notEncrypted") {
    nlohmann::json list = {{"encryptionSetup", {{"scheme", scheme}, {"version", 0}}}, {"fileInfoList", nlohmann::json::array()}};
    std::string data;
    for (const auto& [path, bytes] : files) {
        list["fileInfoList"].push_back({{"path", path}, {"byteOffset", data.size()}, {"byteSize", bytes.size()}, {"uncompressed_size", bytes.size()}, {"hash", 1}});
        data += bytes;
    }
    const std::string text = list.dump() + std::string(1, '\0');
    std::string out = "RASA";
    auto u16 = [&](uint16_t v) { out += static_cast<char>(v & 0xff); out += static_cast<char>(v >> 8); };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out += static_cast<char>((v >> (8 * i)) & 0xff); };
    u16(2);
    u16(flags);
    u32(1);
    u32(static_cast<uint32_t>(text.size()));
    out += std::string(16, '\0');
    return out + text + data;
}

static void writeFile(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

static const char* kLocpak = R"({"LocalisationPackage":{"Language":"en-US","Strings":{
  "AIRCRAFT.UI_MANUFACTURER":"Cirrus","AIRCRAFT.UI_MODEL":"Vision Jet G2",
  "SF50.ENGINE_KNOB_TITLE":"Engine Knob","SF50.ENGINE_KNOB_ACTION":"Toggle engine RUN/STOP mode",
  "SF50.ECS_FAN_KNOB_TITLE":"Fan Control","SF50.ECS_FAN_KNOB_ACTION":"Adjust fan speed",
  "SF50.FUEL_CAP_TITLE":"%s Fuel Cap","SF50.FUEL_CAP_ACTION":"Open/close fuel cap",
  "SF50.CAPS_HANDLE_TITLE":"CAPS Activation Handle","SF50.CAPS_HANDLE_ACTION":"Pull handle down to activate parachute",
  "INPUT.ELECTRICAL_BATTERY_1_ON":"BATTERY MASTER ON","INPUT.ELECTRICAL_BATTERY_1_OFF":"BATTERY MASTER OFF",
  "INPUT.ELECTRICAL_BATTERY_1_TOGGLE":"BATTERY MASTER TOGGLE","INPUT.ELECTRICAL_BATTERY_1_TOGGLE_DESC":"Toggle battery master.",
  "INPUT.ELECTRICAL_BATTERY_STBY_2_INC":"STANDBY BATTERY INCREASE","INPUT.ELECTRICAL_BATTERY_STBY_2_DEC":"STANDBY BATTERY DECREASE",
  "INPUT.FUEL_MIXTURE_1_INC":"MIXTURE 1 INCREASE","INPUT.FUEL_MIXTURE_1_DEC":"MIXTURE 1 DECREASE","INPUT.FUEL_MIXTURE_1_SET_DESC":"Set fuel mixture 1."}}})";

static const char* kChecklist = R"(<?xml version="1.0" encoding="UTF-8"?>
<Checklist default-group-name="Normal Procedures">
  <Group name="Emergency Procedures">
    <List name="Engine Fire">
      <Item type="actionable"><LabelText>1. Engine Knob</LabelText><ActionText>OFF</ActionText></Item>
    </List>
  </Group>
  <!-- the default group comes first in the guide -->
  <Group name="Normal Procedures">
    <List name="Before Start">
      <Item type="note"><Text>CAUTION: Do not start with a tailwind.</Text></Item>
      <Item type="actionable"><LabelText>
          2. Engine Knob
        </LabelText><ActionText>"RUN&#xa0;&amp; CHECK"</ActionText></Item>
      <Item type="note" justify="center"><Text>Procedure Complete</Text></Item>
      <Item type="note"><Text>Brief the passengers.</Text></Item>
    </List>
  </Group>
</Checklist>)";

static std::map<std::string, AutoLabel> labelsByName(const std::vector<std::string>& names, const PackageText* text, std::map<std::string, size_t>* count = nullptr) {
    std::map<std::string, AutoLabel> out;
    const auto all = autoLabels(names, text);
    for (size_t i = 0; i < names.size(); ++i) {
        out[names[i]] = all[i][0];
        if (count) (*count)[names[i]] = all[i].size();
    }
    return out;
}

int main(int argc, char** argv) {
    const std::string srcDir = argc > 1 ? argv[1] : ".";
    const fs::path tmp = fs::temp_directory_path() / ("auto_catalog_" + std::to_string(::getpid()));
    fs::remove_all(tmp);

    // ---- the archive reader: plain archives are read, encrypted ones are refused at the header
    {
        sim::FsArchive a;
        std::string err, out;
        CHECK(a.parse(makeArchive({{"en-us.locpak", "hello"}, {"html_ui\\A\\checklist.xml", "<x/>"}}), "mem", err));
        CHECK(a.files().size() == 2);
        const auto* f = a.find("HTML_UI/a/Checklist.XML");  // the case and the slashes do not matter
        CHECK(f && a.read(*f, out, err) && out == "<x/>");
        CHECK(a.find("en-us.locpak") && a.read(*a.find("en-us.locpak"), out, err) && out == "hello");
        CHECK(!a.find("missing"));
        sim::FsArchive e;
        CHECK(!e.parse(makeArchive({{"a", "b"}}, 3), "enc", err) && err.find("encrypted") != std::string::npos);
        CHECK(!e.parse(makeArchive({{"a", "b"}}, 0, "aes"), "enc2", err) && err.find("encrypted") != std::string::npos);
        CHECK(!e.parse("RASB" + std::string(40, '\0'), "bad", err));
        // a file list that points past the end is refused, not read
        std::string cut = makeArchive({{"a", "0123456789"}});
        cut.resize(cut.size() - 4);
        CHECK(e.parse(cut, "cut", err) && !e.read(*e.find("a"), out, err));
    }

    // ---- where the sim keeps its packages: InstalledPackagesPath inside the Proton prefix, whatever the case of the Windows path
    {
        const fs::path pfx = tmp / "pfx";
        const fs::path pk = pfx / "drive_c/users/steamuser/AppData/Roaming/Microsoft Flight Simulator 2024/Packages";
        fs::create_directories(pk);
        fs::create_directories(pfx / "dosdevices");
        fs::create_symlink("../drive_c", pfx / "dosdevices/c:");
        const std::string cfg = "Version 1\nInstalledPackagesPath \"c:\\Users\\steamuser\\AppData\\Roaming\\Microsoft Flight Simulator 2024\\packages\"\n";
        CHECK(fs::equivalent(packagesPathFromUserCfg(cfg, pfx.string()), pk));
        CHECK(packagesPathFromUserCfg("nothing here", pfx.string()).empty());
    }

    // ---- the package of an aircraft: a Community package wins over the Official one; its tooltips, checklist and name
    {
        const fs::path root = tmp / "Packages";
        const fs::path off = root / "Official2024/Steam/microsoft-aircraft-sf50";
        fs::create_directories(off / "simobjects/airplanes/microsoft_sf50");
        writeFile(off / "minimal.fsarchive", makeArchive({{"_texture_headers.bin.fsc", "zz"}, {"en-us.locpak", kLocpak}}));
        writeFile(off / "html_ui/html_ui.fsarchive", makeArchive({{"airplanes\\microsoft_sf50\\checklist\\checklist.xml", kChecklist}}));
        fs::create_directories(root / "Official2024/Steam/asobo-aircraft-c172/simobjects/airplanes/asobo_c172sp_as1000");
        fs::create_directories(root / "Community/my-c172-mod/SimObjects/Airplanes/Asobo_C172SP_AS1000");
        const std::vector<std::string> roots{root.string()};
        const auto sf50 = findAircraftPackage(roots, "Microsoft_SF50");
        CHECK(sf50 && sf50->name == "microsoft-aircraft-sf50" && sf50->folder == "microsoft_sf50");
        const auto c172 = findAircraftPackage(roots, "asobo_c172sp_as1000");
        CHECK(c172 && c172->name == "my-c172-mod");
        CHECK(!findAircraftPackage(roots, "nothing_like_it"));

        const PackageText text = readPackageText(*sf50);
        CHECK(text.title == "Cirrus Vision Jet G2");
        CHECK(text.strings.count("SF50.ENGINE_KNOB_TITLE") == 1);
        CHECK(!text.checklistXml.empty() && text.checklistSource.find("checklist.xml") != std::string::npos);
        CHECK(text.sources.size() == 2);

        // ---- labels from the tooltips
        std::map<std::string, size_t> count;
        const auto l = labelsByName({"SF50_ENGINE_RUN_KNOB", "SF50_CABIN_ECS_FAN_KNOB_AFT", "SF50_CABIN_ECS_FAN_KNOB_FORWARD", "SF50_EXT_FUEL_CAP_LEFT",
                                     "SF50_CAPS_HANDLE", "SF50_SAFE_RETURN_AUTOPILOT_INTERCEPTS", "SF50_ELECTRICAL_BATTERY_1_SWITCH"},
                                    &text, &count);
        CHECK(l.at("SF50_ENGINE_RUN_KNOB").function == "Engine Knob");
        CHECK(l.at("SF50_ENGINE_RUN_KNOB").behavior == "latched");  // "Toggle ..." beats the KNOB of the name
        CHECK(l.at("SF50_ENGINE_RUN_KNOB").panel == "Engine");
        CHECK(l.at("SF50_ENGINE_RUN_KNOB").note.find("Toggle engine RUN/STOP mode") != std::string::npos);
        CHECK(l.at("SF50_CABIN_ECS_FAN_KNOB_AFT").function == "Fan Control (aft): up");  // twins told apart, "Adjust" is a knob
        CHECK(l.at("SF50_CABIN_ECS_FAN_KNOB_FORWARD").function == "Fan Control (forward): up");
        CHECK(count.at("SF50_CABIN_ECS_FAN_KNOB_AFT") == 2);
        CHECK(l.at("SF50_CABIN_ECS_FAN_KNOB_AFT").panel == "Air and pressurisation");
        CHECK(l.at("SF50_EXT_FUEL_CAP_LEFT").function == "Left Fuel Cap");
        CHECK(l.at("SF50_EXT_FUEL_CAP_LEFT").panel == "Walkaround");
        CHECK(l.at("SF50_CAPS_HANDLE").note.rfind("DANGER", 0) == 0);
        CHECK(l.at("SF50_SAFE_RETURN_AUTOPILOT_INTERCEPTS").behavior.empty());  // internal logic: no tooltip, no behaviour, still a warning
        CHECK(l.at("SF50_SAFE_RETURN_AUTOPILOT_INTERCEPTS").note.rfind("DANGER", 0) == 0);
        CHECK(l.at("SF50_ELECTRICAL_BATTERY_1_SWITCH").behavior == "latched");
        CHECK(l.at("SF50_ELECTRICAL_BATTERY_1_SWITCH").panel == "Electrical");
        // the sim's own control names (INPUT.<event>_<action>): the name, and the actions say switch or knob; a lever stays unknown
        const auto in = labelsByName({"ELECTRICAL_BATTERY_1", "ELECTRICAL_BATTERY_STBY_2", "FUEL_MIXTURE_1"}, &text);
        CHECK(in.at("ELECTRICAL_BATTERY_1").function == "Battery master" && in.at("ELECTRICAL_BATTERY_1").behavior == "latched");
        CHECK(in.at("ELECTRICAL_BATTERY_1").note == "Sim: \"Toggle battery master.\"");
        CHECK(in.at("ELECTRICAL_BATTERY_STBY_2").function == "Standby battery: up" && in.at("ELECTRICAL_BATTERY_STBY_2").behavior == "knob_up");
        CHECK(in.at("FUEL_MIXTURE_1").function == "Mixture 1" && in.at("FUEL_MIXTURE_1").behavior.empty());

        // ---- the guide from the checklist: the normal procedures first, numbering and quotes gone, cautions on the next step
        Catalog cat;
        cat.aircraft = "Microsoft_SF50";
        cat.entries.push_back(makeInputEventEntry("SF50_ENGINE_RUN_KNOB", false));
        cat.entries.push_back(makeInputEventEntry("SF50_ELECTRICAL_BATTERY_1_SWITCH", false));
        applyAutoLabels(cat, &text, text.title);
        CHECK(cat.title == "Cirrus Vision Jet G2 (automatic)");
        Guide g;
        std::string err;
        CHECK(buildAutoGuide("Microsoft_SF50", text.title, text, cat, g, err));
        CHECK(g.automatic && g.topics.size() == 2);
        if (g.topics.size() == 2) {
            CHECK(g.topics[0].title == "Before Start" && g.topics[0].summary == "Normal Procedures");
            CHECK(g.topics[1].title == "Engine Fire");
            const GuideNode* n = g.node(g.topics[0].start);
            CHECK(n && n->type == "steps" && n->steps.size() == 2);
            if (n && n->steps.size() == 2) {
                CHECK(n->steps[0].text == "Engine Knob: RUN & CHECK");
                CHECK(n->steps[0].warn == "CAUTION: Do not start with a tailwind.");
                CHECK(n->steps[0].controls.size() == 1 && n->steps[0].controls[0].api == "SF50_ENGINE_RUN_KNOB");
                CHECK(n->steps[1].text == "Brief the passengers." && n->steps[1].controls.empty());
                CHECK(g.node(n->next) && g.node(n->next)->type == "end");
            }
        }
        PackageText none;
        CHECK(!buildAutoGuide("x", "x", none, cat, g, err));
    }

    // ---- names alone (no package): the usual MSFS words
    {
        std::map<std::string, size_t> count;
        const auto l = labelsByName({"AS1000_COM_LARGE_PFD", "ELECTRICAL_BREAK_GPS", "HANDLING_ElevatorTrim_YOKE", "LIGHTING_STROBE_1", "FMC_CJ4_1_Keyboard_A",
                                     "AS1000_AUTOPILOT_HEADING_MFD", "ENGINE_THROTTLE_LEFT", "HANDLING_YOKE_HIDER_1", "LIGHTING_POTENTIOMETER_15",
                                     "AS1000_AUTOPILOT_VERTICALSPEED_UP_PFD", "AS1000_FMS_UPPER_PFD", "UNKNOWN_LANDING_MAIN_STRUT_L", "AIRLINER_ELEC_BAT1",
                                     "AIRLINER_FIRE_ENG1_AGENT_1", "AIRLINER_ND_BRT_CPT", "SWITCH_START", "CB_GPS"},
                                    nullptr, &count);
        CHECK(l.at("AS1000_COM_LARGE_PFD").panel == "Radios" && l.at("AS1000_COM_LARGE_PFD").section == "PFD");
        CHECK(l.at("AS1000_COM_LARGE_PFD").function == "COM large: up" && count.at("AS1000_COM_LARGE_PFD") == 2);
        CHECK(l.at("ELECTRICAL_BREAK_GPS").panel == "Circuit breakers" && l.at("ELECTRICAL_BREAK_GPS").function == "GPS");
        CHECK(l.at("ELECTRICAL_BREAK_GPS").behavior == "latched");
        CHECK(l.at("HANDLING_ElevatorTrim_YOKE").behavior == "knob_up" && l.at("HANDLING_ElevatorTrim_YOKE").panel == "Flight controls");
        CHECK(l.at("LIGHTING_STROBE_1").behavior == "latched" && l.at("LIGHTING_STROBE_1").panel == "Lights");
        CHECK(l.at("FMC_CJ4_1_Keyboard_A").behavior == "press" && l.at("FMC_CJ4_1_Keyboard_A").panel == "Avionics");
        CHECK(l.at("AS1000_AUTOPILOT_HEADING_MFD").panel == "Autopilot" && l.at("AS1000_AUTOPILOT_HEADING_MFD").section == "MFD");
        CHECK(l.at("ENGINE_THROTTLE_LEFT").behavior.empty() && l.at("ENGINE_THROTTLE_LEFT").panel == "Engine");  // range not known
        CHECK(l.at("HANDLING_YOKE_HIDER_1").section == "Cockpit model");
        CHECK(l.at("LIGHTING_POTENTIOMETER_15").behavior == "level");
        CHECK(l.at("AS1000_AUTOPILOT_HEADING_MFD").function == "Heading: up");
        CHECK(l.at("AS1000_AUTOPILOT_VERTICALSPEED_UP_PFD").behavior == "press");  // one direction: a button, not a knob
        CHECK(l.at("AS1000_FMS_UPPER_PFD").behavior == "knob_up");
        CHECK(l.at("UNKNOWN_LANDING_MAIN_STRUT_L").panel == "Walkaround" && l.at("UNKNOWN_LANDING_MAIN_STRUT_L").section == "Walkaround");
        CHECK(l.at("AIRLINER_ELEC_BAT1").panel == "Electrical" && l.at("AIRLINER_ELEC_BAT1").behavior == "latched");
        CHECK(l.at("AIRLINER_FIRE_ENG1_AGENT_1").note.rfind("DANGER", 0) == 0 && l.at("AIRLINER_FIRE_ENG1_AGENT_1").behavior == "press");
        CHECK(l.at("AIRLINER_ND_BRT_CPT").behavior == "knob_up");
        CHECK(l.at("SWITCH_START").behavior == "latched" && l.at("CB_GPS").panel == "Circuit breakers");
    }

    // ---- in a catalog: the knob's second direction is an extra entry that is never cached and does not count as a change
    {
        CatalogSet set;
        std::vector<CatalogEntry> fresh{makeInputEventEntry("AS1000_COM_LARGE_PFD", false), makeInputEventEntry("LIGHTING_STROBE_1", false)};
        CHECK(set.applyLive("asobo_test", "Test", fresh));
        Catalog* cat = set.find("asobo_test");
        applyAutoLabels(*cat, nullptr, "");
        size_t inputs = 0, twins = 0;
        for (const auto& e : cat->entries) {
            if (e.kind != "input_event") continue;
            ++inputs;
            twins += e.autoTwin;
            CHECK(e.automatic);
        }
        CHECK(inputs == 3 && twins == 1);
        CHECK(!set.applyLive("asobo_test", "Test", fresh));  // same names: nothing changed
        applyAutoLabels(*cat, nullptr, "");
        twins = 0;
        for (const auto& e : cat->entries) twins += e.autoTwin;
        CHECK(twins == 1);  // relabelling does not pile up twins
        const std::string cache = (tmp / "live.json").string();
        std::string err;
        CHECK(cat->saveLive(cache, err));
        Catalog back;
        CHECK(Catalog::loadLive(cache, back, err) && back.entries.size() == 2);
        CHECK(set.forgetLive("asobo_test"));
        twins = 0;
        for (const auto& e : cat->entries) twins += e.autoTwin;
        CHECK(twins == 0);
    }

    // ---- against the hand-made catalogs: the behaviour guessed from the name alone must agree with most of them
    {
        size_t agree = 0, total = 0;
        for (const char* id : {"microsoft_sf50", "asobo_cj4", "asobo_cabri_g2", "asobo_fa18e"}) {
            Catalog cat;
            std::string err;
            if (!cat.load(srcDir + "/data/" + id + "_catalog.json", err)) { std::printf("  FAIL %s: %s\n", id, err.c_str()); ++failures; continue; }
            std::map<std::string, std::string> want;
            for (const auto& e : cat.entries) {
                if (e.kind != "input_event" || e.behavior.empty()) continue;
                want[e.name] = e.behavior == "knob_up" || e.behavior == "knob_down" ? "knob" : e.behavior;
            }
            std::vector<std::string> names;
            for (const auto& [n, b] : want) names.push_back(n);
            const auto got = labelsByName(names, nullptr);
            size_t a = 0;
            for (const auto& [n, b] : want) {
                std::string g = got.at(n).behavior;
                if (g == "knob_up") g = "knob";
                a += g == b;
            }
            std::printf("  %s: %zu of %zu behaviours agree with the hand-made catalog\n", id, a, want.size());
            agree += a;
            total += want.size();
        }
        CHECK(total > 400 && agree * 100 >= total * 75);
    }

    fs::remove_all(tmp);
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("auto_catalog: all checks passed\n");
    return 0;
}
