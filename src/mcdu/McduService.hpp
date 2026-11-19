// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Drives the MCDU: on the desktop its own pages, with the simulator the aircraft's screen and keys. Its thread only exists while a unit is plugged in or
// the panel on the MCDU page is open, and sleeps until a key, a page refresh or a screen update is due.
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "desktop/AudioControl.hpp"
#include "desktop/DesktopTypes.hpp"
#include "desktop/ProgressWatcher.hpp"
#include "mcdu/McduPages.hpp"
#include "mcdu/McduProtocol.hpp"
#include "mcdu/McduSimLink.hpp"
#include "mcdu/ScreenMirror.hpp"
#include "mcdu/WeatherClient.hpp"

namespace lr::mcdu {

struct ServiceStatus {
    bool running = false;
    bool deviceOpen = false;
    std::string devicePath, seat, error;
    bool simMode = false, simScreen = false, sleeping = false;
    int ambientPercent = -1;
    uint64_t screensSent = 0;
    SideLabels sideLabels;  // what the side keys do on the page on screen
    LampState lamps{};      // in the order of kLamps
};

struct SimState {
    bool active = false;           // the simulator runs: the unit belongs to the aircraft
    std::string aircraft;          // shown while there is no screen
    std::string hEventPrefix;      // keys become H events <prefix><key>; empty: this aircraft's keys are not known
    bool simBridge = false;        // the aircraft has a SimBridge screen (the A32NX)
    bool iniBuildsKeys = false;    // the keys are the L: variables of the iniBuilds Airbuses
    bool operator==(const SimState&) const = default;
};

class McduService final : private PageActions {
public:
    struct Hooks {
        std::function<void(const std::vector<int>&)> tapKeys;
        std::function<void(const std::string& hEvent)> fireHEvent;
        std::function<void(const std::string& code)> fireCode;  // calculator code
    };

    explicit McduService(Hooks hooks);
    ~McduService() override;

    void configure(const desktop::McduConfig& cfg);
    void setDevice(const std::string& hidrawPath, uint16_t productId);  // "" when no unit is plugged in
    void setSim(const SimState& sim);
    void setPanelOpen(bool open);  // the on-screen panel of the MCDU page
    void pressKey(Key key);        // a key of that panel

    Screen screen() const;
    ServiceStatus status() const;
    // What the keys of the unit changed (BRT and DIM, the disk shown, the refresh rate ...) goes into `cfg`; true when there was something.
    bool applyRuntimePrefs(desktop::McduConfig& cfg);
    void stop();

private:
    using Clock = std::chrono::steady_clock;
    void tapKeys(const std::vector<int>& keys) override;
    void media(desktop::MediaCommand command) override;
    void sleepScreen() override;
    void audio(desktop::AudioCommand command) override;
    bool runnerQuery(const std::string& text) override;
    bool openUrl(const std::string& url) override;
    void dismissTask(const std::string& key) override;
    void noteRuntimePrefs();

    void updateThread();
    void wake();
    void run();
    void openDevice(const std::string& path);
    void closeDevice(const char* reason);
    bool write(const uint8_t* data, size_t size);
    void readDevice();
    void handleKey(Key key);
    void renderScreen(Clock::time_point now);
    void sendScreen(Clock::time_point now);
    void applyLights(bool force);
    void blankDevice();

    Hooks hooks_;
    mutable std::mutex m_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    int wakeFd_ = -1;

    // set by other threads, read by the service thread under m_
    desktop::McduConfig cfg_;
    bool cfgChanged_ = true;
    std::string wantedPath_;
    uint16_t productId_ = 0;
    SimState sim_;
    bool panelOpen_ = false;
    std::deque<Key> queuedKeys_;
    Screen shown_;
    ServiceStatus status_;
    desktop::McduConfig runtimePrefs_;
    bool runtimeChanged_ = false;

    // the service thread only
    PageHost pages_;
    SimLink link_;
    ScreenMirror mirror_;  // the iniBuilds Airbuses: the text read off their pop-out window
    bool mirrorShown_ = false;
    SystemInfo systemInfo_;
    desktop::MediaControl mediaControl_;
    WeatherClient weather_;
    desktop::ProgressWatcher progress_;
    desktop::AudioControl audioControl_;
    desktop::AudioState audioState_;
    Clock::time_point nextAudio_{};
    bool deafenMutedVoice_ = false;  // deafening also muted the microphone: undeafening gives it back
    Clock::time_point nextWeather_{};
    desktop::McduConfig active_;
    SimState activeSim_;
    int fd_ = -1;
    std::string openPath_;
    Clock::time_point retryOpenAt_{};
    KeyBits keysDown_;
    Screen desired_, sent_;
    bool sentValid_ = false;
    Clock::time_point nextSend_{}, nextRefresh_{}, nextSystem_{}, nextMedia_{}, lastKey_{};
    bool sleeping_ = false;
    int ambient_ = -1;
    int lastLevels_[3] = {-1, -1, -1};
    LampState lastLamps_{};
    bool lampsValid_ = false;
};

}  // namespace lr::mcdu
