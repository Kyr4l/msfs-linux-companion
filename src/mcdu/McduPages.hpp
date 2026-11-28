// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The pages the MCDU shows on the desktop, while the simulator is not running. Plain logic: keys in, a screen out.
#include <array>
#include <chrono>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include "desktop/AudioControl.hpp"
#include "desktop/DesktopTypes.hpp"
#include "desktop/MediaControl.hpp"
#include "desktop/ProgressTracker.hpp"
#include "mcdu/McduProtocol.hpp"
#include "mcdu/Metar.hpp"
#include "mcdu/SystemInfo.hpp"

namespace lr::mcdu {

// DataIndex and Network are pages of the Airbus style only.
enum class PageId { Menu, Clock, System, Cpu, Memory, Gpus, Disks, Media, Keys, Type, Power, Keys2, Comms, Weather, Direct, Progress, DataIndex, Network };

// What the twelve line select keys do on the page on screen: L1..L6, then R1..R6. An empty label: nothing.
using SideLabels = std::array<std::string, 12>;

class PageActions {
public:
    virtual ~PageActions() = default;
    virtual void tapKeys(const std::vector<int>& keys) = 0;  // pressed together, then let go
    virtual void media(desktop::MediaCommand command) = 0;
    virtual void sleepScreen() = 0;
    virtual void audio(desktop::AudioCommand command) = 0;
    virtual bool runnerQuery(const std::string& text) = 0;   // KRunner opens with this text
    virtual bool openUrl(const std::string& url) = 0;
    virtual void dismissTask(const std::string& key) = 0;
};

std::vector<desktop::Shortcut> defaultShortcuts();
std::vector<desktop::Shortcut> defaultShortcuts2(bool azerty);  // physical keys: Ctrl+Z is another key on a French keyboard
std::vector<desktop::Shortcut> defaultCommsShortcuts();
std::vector<std::string> defaultDirectApps();
// The physical keys that type what is printed on an MCDU key. Empty: the key types nothing.
std::vector<int> typedKeys(Key key, bool azerty);

class PageHost {
public:
    explicit PageHost(PageActions& actions) : actions_(actions), shortcuts_(defaultShortcuts()), shortcuts2_(defaultShortcuts2(false)), comms_(defaultCommsShortcuts()) {}

    void configure(const desktop::McduConfig& cfg);
    void setSystem(const SystemSample& s) { system_ = s; }
    void setMedia(const desktop::MediaInfo& m) { media_ = m; }
    void setWeather(const WeatherResult& w) { weather_ = w; }
    void setAudio(const desktop::AudioState& a) { audio_ = a; }
    void setTasks(std::vector<desktop::ProgressTask> tasks, bool available) { tasks_ = std::move(tasks); tasksAvailable_ = available; }

    void onKey(Key key);
    void render(Screen& out, const std::tm& now);
    const SideLabels& sideLabels() const { return labels_; }

    PageId page() const { return page_; }
    int refreshMs() const;     // how often the page changes by itself; 0: only when a key is pressed
    bool wantsSystem() const;  // one of the pages with the computer's numbers is on screen
    bool wantsWeather() const { return page_ == PageId::Weather; }
    bool wantsAudio() const { return page_ == PageId::Comms; }
    bool wantsTasks() const { return page_ == PageId::Progress; }
    const std::string& homeAirport() const { return airport_; }
    bool takeWeatherRequest();  // the airport changed, or Refresh was pressed

    // What the side keys changed and the config should remember.
    bool takePrefsChanged();
    void exportPrefs(desktop::McduConfig& cfg) const;

private:
    struct Frame;
    void build(Frame& f);
    void buildMenu(Frame& f);
    void buildClock(Frame& f);
    Color levelColor(int percent) const;  // a load in the colour of its stage
    void buildSystem(Frame& f);
    void buildCpu(Frame& f);
    void buildMemory(Frame& f);
    void buildGpus(Frame& f);
    void buildDisks(Frame& f);
    void buildMedia(Frame& f);
    void buildKeys(Frame& f);
    void buildType(Frame& f);
    void buildPower(Frame& f);
    void buildKeys2(Frame& f);
    void buildComms(Frame& f);
    void buildWeather(Frame& f);
    void buildDirect(Frame& f);
    void buildProgress(Frame& f);
    void runnerPress(const std::string& text);
    void sideKey(Key key);
    void typeKey(Key key);
    void scrollDisks(bool down);
    void drawCores(Screen& s) const;
    std::string netSpeed(double bytes) const;

    // The Airbus style (McduPagesAirbus.cpp): the same function keys, pages laid out like the aircraft's, a scratchpad for typing.
    void airbusKey(Key key);
    bool slew(int step);  // through the monitor pages; false: not on one
    std::function<void()> airportField();
    void buildAirbus(Frame& f);
    void buildAMenu(Frame& f);
    void buildAClock(Frame& f);
    void buildADataIndex(Frame& f);
    void buildAStatus(Frame& f);
    void buildACpu(Frame& f);
    void buildAMemory(Frame& f);
    void buildAGpus(Frame& f);
    void buildADisks(Frame& f);
    void buildANetwork(Frame& f);
    void buildAPower(Frame& f);
    void buildAMedia(Frame& f);
    void buildAKeys(Frame& f, const char* title, const std::vector<desktop::Shortcut>& list);
    void buildAType(Frame& f);
    void buildAComms(Frame& f);
    void buildAWeather(Frame& f);
    void buildADirect(Frame& f);
    void buildAProgress(Frame& f);
    void buildShortcuts(Frame& f, const std::vector<desktop::Shortcut>& list);
    void go(PageId page);
    void say(std::string text) { toast_ = std::move(text); }
    void cycleRate();
    size_t selectedDisk() const;
    int chronoSeconds() const;

    PageActions& actions_;
    PageId page_ = PageId::Menu;
    bool airbus_ = false;
    PageId lastPage_ = PageId::DataIndex;  // where DESKTOP in MCDU MENU goes back to (Airbus style)
    std::vector<desktop::Shortcut> shortcuts_, shortcuts2_, comms_;
    bool azerty_ = false;
    std::string look_ = "list", gpuView_ = "both", diskView_ = "switch", diskMount_;
    int gpuIndex_ = 0, refreshSeconds_ = 2;
    bool utc_ = false, h12_ = false, prefsChanged_ = false;
    bool volumeAll_ = false;  // the Airbus media page: ↑ ↓ and MUTE on the PC's output (else on the player's program)
    desktop::LoadColors loadColors_;
    bool cpuCores_ = false;  // the CPU page shows every core
    SystemSample system_;
    desktop::MediaInfo media_;
    std::string typed_, toast_;
    WeatherResult weather_;
    desktop::AudioState audio_;
    std::vector<desktop::ProgressTask> tasks_;
    bool tasksAvailable_ = false;
    std::vector<std::string> directApps_ = defaultDirectApps();
    std::string directText_, pendingRunner_;
    std::chrono::steady_clock::time_point pendingRunnerAt_{};
    std::string commsApp_ = "goofcord";
    std::string airport_, scratch_;
    bool weatherRequested_ = false, netBits_ = false;
    bool shift_ = false;
    size_t diskScroll_ = 0;
    bool chronoRunning_ = false;
    std::chrono::steady_clock::time_point chronoStart_{};
    int chronoHeld_ = 0;
    std::tm now_{};
    SideLabels labels_;
};

}  // namespace lr::mcdu
