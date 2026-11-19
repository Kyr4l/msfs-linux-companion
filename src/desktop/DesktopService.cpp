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
          }}) {}

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
        for (const char* aircraft : {"microsoft-a320neo", "microsoft-a321", "microsoft-a330"})
            sim.iniBuildsKeys = sim.iniBuildsKeys || sim::sameAircraft(aircraft, id.id);
    }
    mcdu_.setSim(sim);
    const auto unit = winwing_.mcduDevice();
    mcdu_.setDevice(unit.first, unit.second);

    // The virtual keyboard of the MCDU's pages: there before the first key (a new device takes the desktop a moment to notice), gone while the sim runs.
    const bool wantTap = mcdu_.status().running && !simUp;
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
