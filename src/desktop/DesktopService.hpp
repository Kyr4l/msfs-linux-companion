// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Decides who owns the hardware: the simulator while it runs, the desktop otherwise. poll() runs on the main thread and does its work about once a second.
#include <chrono>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "desktop/DesktopEngine.hpp"
#include "desktop/DesktopTypes.hpp"
#include "desktop/Uinput.hpp"
#include "mcdu/McduService.hpp"

namespace lr::sim { class SimBridge; }
namespace lr::app { class Engine; }
namespace lr::haptics { class WinwingHidManager; }

namespace lr::desktop {

struct ServiceStatus {
    bool simRunning = false;      // MSFS is running: the desktop controls wait
    bool controlsActive = false;  // joystick input goes to the desktop right now
    std::string error;
};

class DesktopService {
public:
    DesktopService(sim::SimBridge& bridge, app::Engine& engine, haptics::WinwingHidManager& winwing);
    ~DesktopService();

    void setAxisProbe(DesktopEngine::AxisProbe probe) { engine_.setAxisProbe(std::move(probe)); }
    // Which aircraft's MCDU keys are known: aircraft id and the prefix of its H events (data/*_keypads.json).
    void setKeypads(std::vector<std::pair<std::string, std::string>> prefixes);
    void configure(const Config& cfg, const McduConfig& mcdu);
    void stop();

    void onInput(const input::InputEvent& ev) { engine_.onInput(ev); }  // input thread
    int tick() { return engine_.tick(); }                               // input thread

    void poll();
    ServiceStatus status() const;
    mcdu::McduService& mcdu() { return mcdu_; }

private:
    using Clock = std::chrono::steady_clock;
    void tapKeys(const std::vector<int>& keys);

    sim::SimBridge& bridge_;
    app::Engine& simEngine_;
    haptics::WinwingHidManager& winwing_;
    UinputSink sink_;
    DesktopEngine engine_{sink_};
    mcdu::McduService mcdu_;

    mutable std::mutex m_;
    bool enabled_ = false;
    std::vector<std::pair<std::string, std::string>> prefixes_;
    bool simUp_ = false, processRunning_ = false, stopped_ = false;
    Clock::time_point nextPoll_{}, nextScan_{}, lastSimSeen_{};

    std::mutex tapMutex_;
    UinputSink tapSink_;
    bool tapOpen_ = false;
};

}  // namespace lr::desktop
