// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "desktop/DesktopService.hpp"

#include <algorithm>
#include <cctype>
#include <thread>

#include "app/Engine.hpp"
#include "desktop/SimPresence.hpp"
#include "haptics/WinwingHid.hpp"
#include "sim/AircraftId.hpp"
#include "sim/SimBridge.hpp"
#include "util/Log.hpp"

namespace lr::desktop {

namespace {
constexpr auto kPollInterval = std::chrono::seconds(1);
constexpr auto kScanInterval = std::chrono::seconds(5);
constexpr auto kSimGoneAfter = std::chrono::seconds(15);  // a sim that restarts or reconnects does not hand the throttle to the desktop in between
constexpr auto kTapHold = std::chrono::milliseconds(12);

bool looksLikeAirbus(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name.find("airbus") != std::string::npos || name.find("beluga") != std::string::npos) return true;
    for (size_t i = 0; i + 3 < name.size(); ++i)  // a3 and two digits: A310, A319, a320neo, A330 ...
        if (name[i] == 'a' && name[i + 1] == '3' && std::isdigit(static_cast<unsigned char>(name[i + 2])) && std::isdigit(static_cast<unsigned char>(name[i + 3])) &&
            (i == 0 || !std::isalnum(static_cast<unsigned char>(name[i - 1]))))
            return true;
    return false;
}
}  // namespace

DesktopService::DesktopService(sim::SimBridge& bridge, app::Engine& engine, haptics::WinwingHidManager& winwing)
    : bridge_(bridge), simEngine_(engine), winwing_(winwing),
      mcdu_(mcdu::McduService::Hooks{
          [this](const std::vector<int>& keys) { tapKeys(keys); },
          [this](const std::string& hEvent) {
              app::Action a;
              a.kind = app::ActionKind::HEvent;
              a.name = hEvent;
              simEngine_.fire({a}, "MCDU");
          },
          [this](const std::string& code) {
              app::Action a;
              a.kind = app::ActionKind::CalcCode;
              a.name = code;
              simEngine_.fire({a}, "MCDU");
          },
          [this](const std::vector<std::string>& codes) { bridge_.watchLVars(codes, "mcdu-avionics"); },
          [this](const std::string& code) { return bridge_.lvarValue(code); },
          [this](bool on) { bridge_.setWaypointsWanted(on); },
          [this] { return bridge_.waypoints(); }}) {}

DesktopService::~DesktopService() { stop(); }

void DesktopService::stop() {
    {
        std::lock_guard<std::mutex> lock(m_);
        if (stopped_) return;
        stopped_ = true;
    }
    mcdu_.stop();
    engine_.setActive(false);
    std::lock_guard<std::mutex> lock(tapMutex_);
    tapSink_.close();
    tapOpen_ = false;
}

void DesktopService::setKeypads(std::vector<std::pair<std::string, std::string>> prefixes) {
    std::lock_guard<std::mutex> lock(m_);
    prefixes_ = std::move(prefixes);
}

void DesktopService::configure(const Config& cfg, const McduConfig& mcdu) {
    engine_.setBindings(cfg.bindings);
    mcdu_.configure(mcdu);
    std::lock_guard<std::mutex> lock(m_);
    enabled_ = cfg.enabled;
    nextPoll_ = Clock::time_point{};  // a switch takes effect now, not in a second
}

void DesktopService::tapKeys(const std::vector<int>& keys) {
    std::lock_guard<std::mutex> lock(tapMutex_);
    if (!tapOpen_) return;
    for (int k : keys) tapSink_.key(k, true);
    tapSink_.flush();
    std::this_thread::sleep_for(kTapHold);  // programs miss a key that is down for no time at all
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) tapSink_.key(*it, false);
    tapSink_.flush();
}

void DesktopService::poll() {
    const auto now = Clock::now();
    bool enabled;
    {
        std::lock_guard<std::mutex> lock(m_);
        if (stopped_ || now < nextPoll_) return;
        nextPoll_ = now + kPollInterval;
        enabled = enabled_;
    }
    const sim::SimStatus st = bridge_.status();
    const bool connected = st.state == sim::ConnState::Connected;
    if (connected) processRunning_ = true;
    else if (now >= nextScan_) {
        nextScan_ = now + kScanInterval;
        processRunning_ = simProcessRunning();
    }
    if (connected || processRunning_) lastSimSeen_ = now;
    const bool simUp = connected || processRunning_ || (lastSimSeen_ != Clock::time_point{} && now - lastSimSeen_ < kSimGoneAfter);
    if (simUp != simUp_) LOG_INFO("Desktop: MSFS %s", simUp ? "is running, the simulator has the controls" : "has stopped, the desktop has the controls");

    engine_.setActive(enabled && !simUp);

    // the MCDU
    mcdu::SimState sim;
    sim.active = simUp;
    if (simUp && !st.aircraft.empty()) {
        const sim::AircraftId id = sim::identifyAircraft(st.aircraft);
        sim.aircraft = id.label.empty() ? id.id : id.label;
        std::transform(sim.aircraft.begin(), sim.aircraft.end(), sim.aircraft.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        std::lock_guard<std::mutex> lock(m_);
        for (const auto& [aircraft, prefix] : prefixes_)
            if (sim::sameAircraft(aircraft, id.id)) sim.hEventPrefix = prefix;
        sim.simBridge = sim::sameAircraft("A32NX", id.id);
        sim.g3000 = sim::sameAircraft("Microsoft_SF50", id.id);
        sim.g1000Pad = sim::sameAircraft("Microsoft_C400_Corvalis", id.id);
        sim.wt21 = sim::sameAircraft("asobo_cj4", id.id);
        sim.g3000Vertical = sim::sameAircraft("asobo_es30", id.id);
        for (const char* aircraft : {"microsoft-a320neo", "microsoft-a321", "microsoft-a330", "microsoft-belugaxl"})
            sim.iniBuildsKeys = sim.iniBuildsKeys || sim::sameAircraft(aircraft, id.id);
        // Any other Airbus built on the iniBuilds systems has the same MCDU variables: found in its L: list, once per aircraft
        // (only the "is the list there" status is asked every second; the list itself, a thousand names, is copied once).
        // The sim keeps L: variables for the whole session, so after an iniBuilds Airbus every aircraft lists them: only an
        // aircraft that looks like an Airbus (A3xx in its name, Airbus, Beluga) is asked.
        if (!sim.iniBuildsKeys && looksLikeAirbus(id.id + " " + id.label)) {
            if (iniCheckedFor_ != id.id) iniCheckedFor_ = id.id, iniChecked_ = false, iniFound_ = false;
            if (!iniChecked_) {
                const sim::LiveStatus live = bridge_.liveStatus();
                if (live.lvarsDone && sim::sameAircraft(live.aircraftId, id.id)) {
                    const sim::LiveSnapshot snap = bridge_.liveSnapshot();
                    iniFound_ = std::any_of(snap.lvars.begin(), snap.lvars.end(), [](const std::string& n) { return n == "INI_MCDU1_LSK1L" || n == "L:INI_MCDU1_LSK1L"; });
                    iniChecked_ = true;
                    if (iniFound_) LOG_INFO("Desktop: %s has the iniBuilds MCDU keys", id.id.c_str());
                }
            }
            sim.iniBuildsKeys = iniFound_;
        }
    }
    const auto unit = winwing_.mcduDevice();
    // The lamps of the iniBuilds Airbuses (read only): the unit's seat picks the side, in the order of mcdu::kLamps
    std::vector<std::string> watch;
    if (sim.iniBuildsKeys) {
        const std::string side = unit.second == 0xbb3e ? "2" : "1";
        for (const char* n : {"FAIL", "FM", "MCDU", "FM1", "IND", "RDY", "DASH", "FM2"}) watch.push_back("L:INI_FMC" + side + "_" + n);
    }
    // The CJ4 (WT21): its FMC sets L:FMC_EXEC_ACTIVE while a change waits for EXEC (the unit's OVFY key); the RDY lamp shows it
    if (sim.wt21) watch.push_back("L:FMC_EXEC_ACTIVE");
    if (watch != lampWatch_) {
        bridge_.watchLVars(watch, "mcdu-lamps");  // its own list: the profile's watches stay
        lampWatch_ = watch;
    }
    if (!watch.empty()) {
        auto on = [&](size_t i) { const auto v = bridge_.lvarValue(watch[i]); return v && *v > 0.5; };
        // kLamps: FAIL, FM, MCDU, MENU, FM1, IND, RDY, line, FM2 (the aircraft's MCDU lamp lights the MCDU MENU pair)
        if (sim.wt21) sim.lamps = {false, false, false, false, false, false, on(0), false, false};  // EXEC waiting: RDY
        else sim.lamps = {on(0), on(1), on(2), on(2), on(3), on(4), on(5), on(6), on(7)};
        sim.hasLamps = std::any_of(watch.begin(), watch.end(), [&](const std::string& n) { return bridge_.lvarValue(n).has_value(); });
    }
    mcdu_.setSim(sim);
    mcdu_.setDevice(unit.first, unit.second);

    // The virtual keyboard of the MCDU's pages: there before the first key (a new device takes the desktop a moment to notice), gone while
    // the aircraft has the unit (back while the switch key gives the desktop pages the unit in flight).
    const auto mcduStatus = mcdu_.status();
    const bool wantTap = mcduStatus.running && (!simUp || mcduStatus.desktopInSim);
    {
        std::lock_guard<std::mutex> lock(tapMutex_);
        if (wantTap && !tapOpen_) {
            std::string err;
            tapOpen_ = tapSink_.open(err);
        } else if (!wantTap && tapOpen_) {
            tapSink_.close();
            tapOpen_ = false;
        }
    }
    std::lock_guard<std::mutex> lock(m_);
    simUp_ = simUp;
}

ServiceStatus DesktopService::status() const {
    ServiceStatus s;
    {
        std::lock_guard<std::mutex> lock(m_);
        s.simRunning = simUp_;
    }
    s.controlsActive = engine_.active();
    s.error = engine_.error();
    return s;
}

}  // namespace lr::desktop
