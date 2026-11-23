// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "mcdu/McduPages.hpp"
#include "mcdu/McduPagesInternal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

#include <linux/input-event-codes.h>

#include "desktop/DesktopLauncher.hpp"

namespace lr::mcdu {

using namespace pages;

std::vector<desktop::Shortcut> defaultShortcuts() {
    return {
        {"VOL UP", {KEY_VOLUMEUP}},       {"VOL DOWN", {KEY_VOLUMEDOWN}},    {"MUTE", {KEY_MUTE}},
        {"PLAY", {KEY_PLAYPAUSE}},        {"NEXT", {KEY_NEXTSONG}},          {"PREV", {KEY_PREVIOUSSONG}},
        {"ALT TAB", {KEY_LEFTALT, KEY_TAB}}, {"DESKTOP", {KEY_LEFTMETA, KEY_D}}, {"LOCK", {KEY_LEFTMETA, KEY_L}},
        {"SCREENSHOT", {KEY_SYSRQ}},      {"COPY", {KEY_LEFTCTRL, KEY_C}},   {"PASTE", {KEY_LEFTCTRL, KEY_V}},
    };
}

std::vector<desktop::Shortcut> defaultShortcuts2(bool azerty) {
    return {
        {"CUT", {KEY_LEFTCTRL, KEY_X}},   {"UNDO", {KEY_LEFTCTRL, azerty ? KEY_W : KEY_Z}}, {"REDO", {KEY_LEFTCTRL, KEY_Y}},
        {"SAVE", {KEY_LEFTCTRL, KEY_S}},  {"FIND", {KEY_LEFTCTRL, KEY_F}},                  {"SELECT ALL", {KEY_LEFTCTRL, azerty ? KEY_Q : KEY_A}},
        {"NEW TAB", {KEY_LEFTCTRL, KEY_T}}, {"CLOSE TAB", {KEY_LEFTCTRL, azerty ? KEY_Z : KEY_W}}, {"NEXT TAB", {KEY_LEFTCTRL, KEY_TAB}},
        {"REFRESH", {KEY_F5}},            {"FULLSCREEN", {KEY_F11}},                        {"OVERVIEW", {KEY_LEFTMETA, azerty ? KEY_Z : KEY_W}},
    };
}

std::vector<desktop::Shortcut> defaultCommsShortcuts() {
    return {{"", {}}};
}

std::vector<std::string> defaultDirectApps() { return {"Firefox", "Steam", "GoofCord", "OBS Studio", "Dolphin", "Konsole"}; }

std::vector<int> typedKeys(Key key, bool azerty) {
    static const int kLetters[26] = {KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
                                     KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
    if (key >= Key::A && key <= Key::Z) {
        if (azerty) {
            switch (key) {
                case Key::A: return {KEY_Q};
                case Key::Q: return {KEY_A};
                case Key::Z: return {KEY_W};
                case Key::W: return {KEY_Z};
                case Key::M: return {KEY_SEMICOLON};
                default: break;
            }
        }
        return {kLetters[static_cast<int>(key) - static_cast<int>(Key::A)]};
    }
    if ((key >= Key::D1 && key <= Key::D9) || key == Key::D0) {
        const int code = key == Key::D0 ? KEY_0 : KEY_1 + (static_cast<int>(key) - static_cast<int>(Key::D1));
        if (azerty) return {KEY_LEFTSHIFT, code};  // the top row gives digits with shift on a French keyboard
        return {code};
    }
    switch (key) {
        case Key::Dot: return azerty ? std::vector<int>{KEY_LEFTSHIFT, KEY_COMMA} : std::vector<int>{KEY_DOT};
        case Key::Slash: return azerty ? std::vector<int>{KEY_LEFTSHIFT, KEY_DOT} : std::vector<int>{KEY_SLASH};
        case Key::PlusMinus: return azerty ? std::vector<int>{KEY_6} : std::vector<int>{KEY_MINUS};
        case Key::Space: return {KEY_SPACE};
        case Key::Clr: return {KEY_BACKSPACE};
        case Key::Ovfy: return {KEY_ENTER};
        case Key::LeftArrow: return {KEY_LEFT};
        case Key::RightArrow: return {KEY_RIGHT};
        case Key::UpArrow: return {KEY_UP};
        case Key::DownArrow: return {KEY_DOWN};
        default: return {};
    }
}

void PageHost::configure(const desktop::McduConfig& cfg) {
    shortcuts_ = cfg.shortcuts.empty() ? defaultShortcuts() : cfg.shortcuts;
    if (shortcuts_.size() > static_cast<size_t>(desktop::kShortcutSlots)) shortcuts_.resize(static_cast<size_t>(desktop::kShortcutSlots));
    azerty_ = cfg.keyboardLayout == "azerty";
    shortcuts2_ = cfg.shortcuts2.empty() ? defaultShortcuts2(azerty_) : cfg.shortcuts2;
    if (shortcuts2_.size() > static_cast<size_t>(desktop::kShortcutSlots)) shortcuts2_.resize(static_cast<size_t>(desktop::kShortcutSlots));
    comms_ = cfg.commsShortcuts.empty() ? defaultCommsShortcuts() : cfg.commsShortcuts;
    if (comms_.size() > static_cast<size_t>(desktop::kCommsSlots)) comms_.resize(static_cast<size_t>(desktop::kCommsSlots));
    if (cfg.homeAirport != airport_) {
        airport_ = cfg.homeAirport;
        weather_ = WeatherResult{};
        weatherRequested_ = !airport_.empty();
    }
    netBits_ = cfg.netBits;
    commsApp_ = cfg.commsApp;
    directApps_ = cfg.directApps.empty() ? defaultDirectApps() : cfg.directApps;
    look_ = cfg.systemLook;
    gpuView_ = cfg.gpuView;
    diskView_ = cfg.diskView;
    diskMount_ = cfg.diskMount;
    gpuIndex_ = cfg.gpuIndex;
    refreshSeconds_ = cfg.refreshSeconds;
    utc_ = cfg.clockUtc;
    h12_ = cfg.clock12h;
    loadColors_ = cfg.loadColors;
    const bool airbus = cfg.pageStyle == "airbus";
    if (airbus != airbus_) {
        airbus_ = airbus;
        go(PageId::Menu);
        scratch_.clear();
    }
}

Color PageHost::levelColor(int percent) const {
    const desktop::LoadColors& c = loadColors_;
    if (c.on && c.alert.on && percent > c.alert.above) return colorNamed(c.alert.color);
    if (c.on && c.warn.on && percent > c.warn.above) return colorNamed(c.warn.color);
    return colorNamed(c.normal);
}

bool PageHost::takePrefsChanged() {
    const bool changed = prefsChanged_;
    prefsChanged_ = false;
    return changed;
}

void PageHost::exportPrefs(desktop::McduConfig& cfg) const {
    cfg.diskMount = diskMount_;
    cfg.gpuIndex = gpuIndex_;
    cfg.refreshSeconds = refreshSeconds_;
    cfg.clockUtc = utc_;
    cfg.clock12h = h12_;
    cfg.homeAirport = airport_;
    cfg.netBits = netBits_;
}

bool PageHost::takeWeatherRequest() {
    const bool wanted = weatherRequested_;
    weatherRequested_ = false;
    return wanted;
}

bool PageHost::wantsSystem() const {
    return page_ == PageId::System || page_ == PageId::Network || page_ == PageId::Cpu || page_ == PageId::Memory || page_ == PageId::Gpus || page_ == PageId::Disks || page_ == PageId::Power || page_ == PageId::Comms;
}

int PageHost::refreshMs() const {
    if (wantsSystem()) return std::clamp(refreshSeconds_, 1, 5) * 1000;
    switch (page_) {
        case PageId::Clock: return 1000;
        case PageId::Progress: return 1000;
        case PageId::Media: return 1500;
        default: return 0;
    }
}

void PageHost::go(PageId page) {
    page_ = page;
    toast_.clear();
    pendingRunner_.clear();
    diskScroll_ = 0;
}

void PageHost::cycleRate() {
    refreshSeconds_ = refreshSeconds_ == 1 ? 2 : refreshSeconds_ == 2 ? 5 : 1;
    prefsChanged_ = true;
}

size_t PageHost::selectedDisk() const {
    for (size_t i = 0; i < system_.disks.size(); ++i)
        if (system_.disks[i].mount == diskMount_) return i;
    return 0;
}

int PageHost::chronoSeconds() const {
    if (!chronoRunning_) return chronoHeld_;
    return chronoHeld_ + static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - chronoStart_).count());
}

void PageHost::sideKey(Key key) {
    Frame f;
    build(f);
    const auto& action = f.actions[static_cast<size_t>(static_cast<int>(key) - static_cast<int>(Key::L1))];
    if (action) {
        toast_.clear();
        action();
    }
}

void PageHost::scrollDisks(bool down) {
    const size_t count = system_.disks.size();
    if (down && diskScroll_ + kDisksPerPage < count) diskScroll_ += kDisksPerPage;
    if (!down) diskScroll_ -= std::min(diskScroll_, kDisksPerPage);
}

void PageHost::onKey(Key key) {
    if (airbus_) {
        airbusKey(key);
        return;
    }
    switch (key) {  // the page keys work everywhere
        case Key::Menu: go(PageId::Menu); return;
        case Key::Init: go(PageId::Clock); return;
        case Key::Data: go(PageId::System); return;
        case Key::Perf: go(PageId::Media); return;
        case Key::Fpln: go(PageId::Keys); return;
        case Key::Rad: go(PageId::Type); return;
        case Key::Fuel: go(PageId::Power); return;
        case Key::Sec: go(PageId::Keys2); return;
        case Key::Atc: go(PageId::Comms); return;
        case Key::Airport: go(PageId::Weather); return;
        case Key::Dir: go(PageId::Direct); return;
        case Key::Prog: go(PageId::Progress); return;
        default: break;
    }
    if (key >= Key::L1 && key <= Key::R6) {
        sideKey(key);
        return;
    }
    if (page_ == PageId::Disks && (key == Key::UpArrow || key == Key::DownArrow)) {
        scrollDisks(key == Key::DownArrow);
        return;
    }
    if (page_ == PageId::Weather) {  // the scratchpad, as on the real unit: type the airport, then press the key next to it
        toast_.clear();
        if (key == Key::Clr) { if (!scratch_.empty()) scratch_.pop_back(); return; }
        const char* label = keyLabel(key);
        const bool letterOrDigit = (key >= Key::A && key <= Key::Z) || (key >= Key::D1 && key <= Key::D9) || key == Key::D0;
        if (letterOrDigit && scratch_.size() < 4) scratch_ += label[0];
        return;
    }
    if (page_ == PageId::Direct) {
        toast_.clear();
        if (key == Key::Clr) { if (!directText_.empty()) directText_.pop_back(); return; }
        if (key == Key::Ovfy) {
            if (directText_.empty()) say("TYPE FIRST");
            else runnerPress(directText_);
            return;
        }
        char c = 0;
        if (key == Key::Space) c = ' ';
        else if (key == Key::PlusMinus) c = '-';
        else if ((key >= Key::A && key <= Key::Z) || (key >= Key::D1 && key <= Key::D0) || key == Key::Slash) c = keyLabel(key)[0];
        if (c && directText_.size() < 40) directText_ += c;
        return;
    }
    if (page_ == PageId::Type) typeKey(key);
}

// RAD NAV: the key goes to the PC, and what it types is echoed
void PageHost::typeKey(Key key) {
    std::vector<int> keys = typedKeys(key, azerty_);
    if (keys.empty()) return;
    if (shift_ && key >= Key::A && key <= Key::Z) keys.insert(keys.begin(), KEY_LEFTSHIFT);
    actions_.tapKeys(keys);
    toast_.clear();
    if (key == Key::Clr) { if (!typed_.empty()) typed_.pop_back(); }
    else if (key == Key::Ovfy) typed_.clear();
    else if (key == Key::Space) typed_ += ' ';
    else if (key == Key::PlusMinus) typed_ += '-';
    else if (key < Key::LeftArrow || key > Key::DownArrow) {
        const char* label = keyLabel(key);
        if (label[0] && !label[1]) typed_ += label[0];
    }
    if (typed_.size() > kTypedEcho) typed_.erase(0, typed_.size() - kTypedEcho);
}

void PageHost::render(Screen& out, const std::tm& now) {
    now_ = now;
    Frame f;
    build(f);
    out = f.screen;
    labels_ = f.labels;
}

void PageHost::build(Frame& f) {
    if (airbus_) {
        buildAirbus(f);
        return;
    }
    switch (page_) {
        case PageId::DataIndex:
        case PageId::Network: buildSystem(f); break;  // pages of the Airbus style only
        case PageId::Menu: buildMenu(f); break;
        case PageId::Clock: buildClock(f); break;
        case PageId::System: buildSystem(f); break;
        case PageId::Cpu: buildCpu(f); break;
        case PageId::Memory: buildMemory(f); break;
        case PageId::Gpus: buildGpus(f); break;
        case PageId::Disks: buildDisks(f); break;
        case PageId::Media: buildMedia(f); break;
        case PageId::Keys: buildKeys(f); break;
        case PageId::Type: buildType(f); break;
        case PageId::Power: buildPower(f); break;
        case PageId::Keys2: buildKeys2(f); break;
        case PageId::Comms: buildComms(f); break;
        case PageId::Weather: buildWeather(f); break;
        case PageId::Direct: buildDirect(f); break;
        case PageId::Progress: buildProgress(f); break;
    }
    if (!toast_.empty()) f.screen.put(13, 0, fit(toast_, 24), Color::Amber);
}

void PageHost::buildMenu(Frame& f) {
    f.screen.putCentered(0, "MCDU MENU");
    f.left(1, "Clock", [this] { go(PageId::Clock); });
    f.left(2, "System", [this] { go(PageId::System); });
    f.left(3, "Media", [this] { go(PageId::Media); });
    f.left(4, "GPU", [this] { go(PageId::Gpus); });
    f.left(5, "Lock PC", [this] { actions_.tapKeys({KEY_LEFTMETA, KEY_L}); say("PC LOCKED"); });
    f.right(1, "Keys", [this] { go(PageId::Keys); });
    f.right(2, "Type", [this] { go(PageId::Type); });
    f.right(3, "Disks", [this] { go(PageId::Disks); });
    f.right(4, "Mute", [this] { actions_.tapKeys({KEY_MUTE}); say("MUTE"); });
    f.right(5, "Screen off", [this] { actions_.sleepScreen(); });
}

void PageHost::buildClock(Frame& f) {
    static const char* const kDays[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char* const kMonths[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    std::tm t = now_;
    if (utc_) {
        const std::time_t raw = std::time(nullptr);
        gmtime_r(&raw, &t);
    }
    f.screen.putCentered(0, utc_ ? "CLOCK UTC" : "CLOCK");
    int hour = t.tm_hour;
    std::string suffix;
    if (h12_) {
        suffix = hour < 12 ? " AM" : " PM";
        hour = hour % 12 == 0 ? 12 : hour % 12;
    }
    f.screen.putCentered(2, two(hour) + ":" + two(t.tm_min) + ":" + two(t.tm_sec) + suffix, Color::Green);
    char date[32];
    snprintf(date, sizeof date, "%s %02d %s %04d", kDays[std::clamp(t.tm_wday, 0, 6)], t.tm_mday, kMonths[std::clamp(t.tm_mon, 0, 11)], t.tm_year + 1900);
    f.screen.putCentered(4, date);
    f.left(1, utc_ ? "Local" : "UTC", [this] { utc_ = !utc_; prefsChanged_ = true; });
    f.right(1, h12_ ? "24H" : "12H", [this] { h12_ = !h12_; prefsChanged_ = true; });

    const int c = chronoSeconds();
    f.screen.putCentered(7, "CHRONO", Color::White, true);
    f.screen.putCentered(8, two(c / 3600) + ":" + two(c / 60 % 60) + ":" + two(c % 60), chronoRunning_ ? Color::Green : Color::White);
    f.left(4, chronoRunning_ ? "Stop" : "Start", [this] {
        if (chronoRunning_) chronoHeld_ = chronoSeconds();
        else chronoStart_ = std::chrono::steady_clock::now();
        chronoRunning_ = !chronoRunning_;
    });
    f.right(4, "Reset", [this] { chronoRunning_ = false; chronoHeld_ = 0; });
    f.left(6, "Return", [this] { go(PageId::Menu); });
}

void PageHost::buildSystem(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "SYSTEM");
    s.putRight(0, std::to_string(system_.uptimeSeconds / 3600) + "H" + two(static_cast<int>(system_.uptimeSeconds % 3600 / 60)), Color::White, true);
    const bool bars = look_ == "bars";
    // One line of numbers. A line with a side key starts with its arrow.
    auto metric = [&](int line, const std::string& name, int percent, const std::string& rightText, const std::string& keyLabelText, std::function<void()> fn) {
        f.tag(line, name);
        const int row = line * 2;
        if (fn) {
            s.put(row, 0, "<", Color::Cyan);
            f.side(line - 1, keyLabelText, std::move(fn));
        }
        if (percent < 0) s.put(row, 1, "--", Color::White);
        else if (bars) {
            s.put(row, 1, barText(percent, 12), levelColor(percent));
            s.put(row, 14, std::to_string(percent) + "%", levelColor(percent));
        } else {
            s.put(row, 1, std::to_string(percent) + " %", levelColor(percent));
        }
        if (!rightText.empty()) s.putRight(row, rightText, Color::Green);
    };
    auto degrees = [](int c) { return c >= 0 ? std::to_string(c) + "\xc2\xb0" "C" : std::string(); };

    metric(1, "CPU", system_.cpuPercent < 0 ? -1 : static_cast<int>(std::lround(system_.cpuPercent)), degrees(system_.cpuTempC), "CPU", [this] { go(PageId::Cpu); });
    const int memPercent = system_.memTotalGb > 0 ? static_cast<int>(std::lround(system_.memUsedGb * 100.0 / system_.memTotalGb)) : -1;
    std::string ram = "RAM " + format("%.1f", system_.memUsedGb) + "/" + format("%.0fG", system_.memTotalGb);
    if (system_.swapTotalGb > 0) ram += (system_.zram ? " ZRAM " : " SWAP ") + format("%.1fG", system_.swapUsedGb);
    metric(2, ram, memPercent, "", "Memory", [this] { go(PageId::Memory); });

    const auto& gpus = system_.gpus;
    if (gpus.empty()) {
        f.tag(3, "GPU");
        s.put(6, 1, "--");
    } else if (gpuView_ == "switch") {
        const size_t i = static_cast<size_t>(gpuIndex_) % gpus.size();
        const std::string name = "GPU " + std::to_string(i + 1) + "/" + std::to_string(gpus.size()) + " " + gpus[i].name;
        metric(3, name, gpus[i].loadPercent, degrees(gpus[i].tempC), "Next GPU", [this, count = gpus.size()] {
            gpuIndex_ = static_cast<int>((static_cast<size_t>(gpuIndex_) + 1) % count);
            prefsChanged_ = true;
        });
    } else if (gpuView_ == "page") {
        const size_t i = static_cast<size_t>(gpuIndex_) % gpus.size();
        metric(3, "GPU " + gpus[i].name, gpus[i].loadPercent, degrees(gpus[i].tempC), "GPUs", [this] { go(PageId::Gpus); });
    } else {
        for (size_t i = 0; i < gpus.size() && i < 2; ++i)
            metric(static_cast<int>(i) + 3, gpus[i].name, gpus[i].loadPercent, degrees(gpus[i].tempC), "GPUs", [this] { go(PageId::Gpus); });
    }

    const auto& disks = system_.disks;
    if (disks.empty()) {
        f.tag(5, "DISK");
        s.put(10, 1, "--");
    } else {
        const size_t i = selectedDisk();
        const DiskSample& d = disks[i];
        s.put(10, 0, "<", Color::Cyan);
        if (diskView_ == "page") {
            f.tag(5, "DISKS");
            s.put(10, 1, std::to_string(disks.size()) + " MOUNTED", Color::Cyan);
            f.side(4, "Disks", [this] { go(PageId::Disks); });
        } else {
            f.tag(5, diskView_ == "switch" ? "DISK " + std::to_string(i + 1) + "/" + std::to_string(disks.size()) + " " + d.name : "DISK " + d.name);
            s.put(10, 1, sizeText(d.freeGb) + " FREE", levelColor(d.usedPercent));
            if (diskView_ == "switch")
                f.side(4, "Next disk", [this, i] {
                    diskMount_ = system_.disks[(i + 1) % system_.disks.size()].mount;
                    prefsChanged_ = true;
                });
            else f.side(4, "Choose disk", [this] { go(PageId::Disks); });
        }
    }
    f.left(6, "Return", [this] { go(PageId::Menu); });
    f.right(6, "Rate " + std::to_string(refreshSeconds_) + "S", [this] { cycleRate(); });
}

// two columns of cores: "12 ||||||  57"; the right one from row 3 (row 2 has a side key on the classic page)
void PageHost::drawCores(Screen& s) const {
    const auto& cores = system_.corePercent;
    const size_t shown = std::min<size_t>(cores.size(), 20);
    for (size_t i = 0; i < shown; ++i) {
        const int row = i < 11 ? 1 + static_cast<int>(i) : 3 + static_cast<int>(i - 11), col = i < 11 ? 0 : 12;
        const int p = cores[i];
        auto pad = [](std::string t, size_t w) { return std::string(w > t.size() ? w - t.size() : 0, ' ') + t; };
        s.put(row, col, pad(std::to_string(i), 2), Color::White, true);
        if (p < 0) { s.put(row, col + 3, "--"); continue; }
        s.put(row, col + 3, barText(p, 6), levelColor(p));
        s.put(row, col + 9, pad(std::to_string(std::min(p, 100)), 3), levelColor(p), true);
    }
    if (cores.size() > shown) s.putRight(0, "+" + std::to_string(cores.size() - shown), Color::Amber, true);
    if (cores.empty()) s.putCentered(5, "NOT AVAILABLE", Color::Amber);
}

std::string PageHost::netSpeed(double bytes) const {
    if (bytes < 0) return "--";
    if (netBits_) return format("%.1f MBIT", bytes * 8.0 / 1e6);
    return bytes >= 1e6 ? format("%.1f MB/S", bytes / 1e6) : format("%.0f KB/S", bytes / 1e3);
}

void PageHost::buildCpu(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, cpuCores_ ? "CPU CORES" : "CPU");
    if (cpuCores_) drawCores(s);
    else {
        f.tag(1, "LOAD");
        s.put(2, 0, system_.cpuPercent < 0 ? "--" : format("%.0f %%", system_.cpuPercent), Color::Green);
        f.tag(2, "TEMPERATURE");
        s.put(4, 0, system_.cpuTempC < 0 ? "--" : std::to_string(system_.cpuTempC) + "\xc2\xb0" "C", Color::Green);
        f.tag(3, "CLOCK");
        s.put(6, 0, system_.cpuGhz < 0 ? "--" : format("%.2f GHZ", system_.cpuGhz), Color::Green);
        if (system_.cpuWatts >= 0) {  // only where the power counter is readable (root-only by default)
            f.tag(4, "POWER");
            s.put(8, 0, format("%.0f W", system_.cpuWatts), Color::Green);
        }
        f.tag(5, "LOAD AVERAGE");
        s.put(10, 0, format("%.2f ", system_.load1) + format("%.2f ", system_.load5) + format("%.2f", system_.load15), Color::Green);
    }
    f.right(1, cpuCores_ ? "Summary" : "Cores", [this] { cpuCores_ = !cpuCores_; });
    f.left(6, "Return", [this] { go(PageId::System); });
    f.right(6, "Rate " + std::to_string(refreshSeconds_) + "S", [this] { cycleRate(); });
}

void PageHost::buildMemory(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "MEMORY");
    f.tag(1, "USED");
    s.put(2, 0, format("%.1f GB", system_.memUsedGb), Color::Green);
    f.tag(2, "AVAILABLE");
    s.put(4, 0, format("%.1f GB", system_.memAvailableGb), Color::Green);
    f.tag(3, system_.zram ? "SWAP (ZRAM)" : "SWAP");
    s.put(6, 0, system_.swapTotalGb > 0 ? format("%.1f", system_.swapUsedGb) + "/" + format("%.0f GB", system_.swapTotalGb) : std::string("NONE"), Color::Green);
    if (system_.zram) {
        // what was swapped out, the memory it takes once compressed, and how well it packs
        f.tag(4, "ZRAM STORED / IN MEMORY");
        s.put(8, 0, format("%.2f", system_.zramStoredGb) + " / " + format("%.2f GB", system_.zramMemoryGb), Color::Green);
        f.tag(5, "COMPRESSION");
        s.put(10, 0, system_.zramMemoryGb > 0.0005 ? format("%.1f : 1", system_.zramStoredGb / system_.zramMemoryGb) : std::string("--"), Color::Green);
    }
    f.left(6, "Return", [this] { go(PageId::System); });
    f.right(6, "Rate " + std::to_string(refreshSeconds_) + "S", [this] { cycleRate(); });
}

void PageHost::buildGpus(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "GPU");
    const auto& gpus = system_.gpus;
    if (gpus.empty()) s.putCentered(6, "NO GPU FOUND", Color::Amber);
    for (size_t i = 0; i < gpus.size() && i < 3; ++i) {
        const GpuSample& g = gpus[i];
        const int line = static_cast<int>(i) * 2 + 1;
        const bool shown = static_cast<size_t>(gpuIndex_) % gpus.size() == i;
        f.tag(line, g.name);
        s.put(line * 2, 0, "<", shown ? Color::Green : Color::Cyan);
        s.put(line * 2, 1, g.loadPercent < 0 ? "--" : std::to_string(g.loadPercent) + " %", g.loadPercent < 0 ? Color::White : levelColor(g.loadPercent));
        if (g.tempC >= 0) s.putRight(line * 2, std::to_string(g.tempC) + "\xc2\xb0" "C", Color::Green);
        if (g.memTotalGb > 0) s.put(line * 2 + 1, 1, "VRAM " + format("%.1f", g.memUsedGb) + "/" + format("%.0f GB", g.memTotalGb), Color::White, true);
        f.side(line - 1, "Show " + g.name, [this, i, name = g.name] {
            gpuIndex_ = static_cast<int>(i);
            prefsChanged_ = true;
            say(name + " SHOWN");
        });
    }
    f.left(6, "Return", [this] { go(PageId::System); });
    f.right(6, "Rate " + std::to_string(refreshSeconds_) + "S", [this] { cycleRate(); });
}

void PageHost::buildDisks(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "DISKS");
    const auto& disks = system_.disks;
    if (disks.empty()) s.putCentered(6, "NO DISK FOUND", Color::Amber);
    const size_t selected = selectedDisk();
    for (size_t i = diskScroll_; i < disks.size() && i < diskScroll_ + kDisksPerPage; ++i) {
        const DiskSample& d = disks[i];
        const int line = static_cast<int>(i - diskScroll_) + 1, row = line * 2;
        const bool on = i == selected;
        s.put(row - 1, 1, d.name, on ? Color::Green : Color::White, true);
        s.putRight(row - 1, sizeText(d.totalGb), Color::White, true);
        s.put(row, 0, "<", on ? Color::Green : Color::Cyan);
        s.put(row, 1, sizeText(d.freeGb), levelColor(d.usedPercent));
        s.put(row, 9, barText(d.usedPercent, 10), levelColor(d.usedPercent));
        s.putRight(row, std::to_string(d.usedPercent) + "%", levelColor(d.usedPercent));
        f.side(line - 1, "Show " + d.name, [this, mount = d.mount] {
            diskMount_ = mount;
            prefsChanged_ = true;
            if (diskView_ == "pick") go(PageId::System);
        });
    }
    if (disks.size() > kDisksPerPage) {
        if (diskScroll_ + kDisksPerPage < disks.size()) s.at(13, 22) = Cell{0x2193, Color::White, false};
        if (diskScroll_ > 0) s.at(13, 23) = Cell{0x2191, Color::White, false};
    }
    f.left(6, "Return", [this] { go(PageId::System); });
}

void PageHost::buildMedia(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "MEDIA");
    if (media_.present) {
        s.put(1, 0, fit(upper(media_.player), 24), Color::White, true);
        s.put(3, 1, fit(upper(media_.title), 22), Color::Green, true);
        s.put(5, 1, fit(upper(media_.artist), 22), Color::Cyan, true);
    } else {
        s.put(1, 0, "NO PLAYER", Color::Amber, true);
    }
    if (!media_.present || media_.canPrevious) f.left(1, "Prev", [this] { actions_.media(desktop::MediaCommand::Previous); say("PREVIOUS"); });
    f.left(2, media_.playing ? "Pause" : "Play", [this] { actions_.media(desktop::MediaCommand::PlayPause); });
    if (!media_.present || media_.canNext) f.left(3, "Next", [this] { actions_.media(desktop::MediaCommand::Next); say("NEXT"); });
    f.right(1, "Vol up", [this] { actions_.tapKeys({KEY_VOLUMEUP}); say("VOLUME UP"); });
    f.right(2, "Vol down", [this] { actions_.tapKeys({KEY_VOLUMEDOWN}); say("VOLUME DOWN"); });
    f.right(3, "Mute", [this] { actions_.tapKeys({KEY_MUTE}); say("MUTE"); });
    f.left(4, "Player", [this] { actions_.media(desktop::MediaCommand::NextPlayer); });
    if (media_.hasShuffle) f.right(4, media_.shuffle ? "Shuffle on" : "Shuffle off", [this] { actions_.media(desktop::MediaCommand::Shuffle); }, media_.shuffle ? Color::Green : Color::Cyan);
    if (media_.hasRepeat) f.right(5, media_.repeat ? "Repeat on" : "Repeat off", [this] { actions_.media(desktop::MediaCommand::Repeat); }, media_.repeat ? Color::Green : Color::Cyan);
    f.left(6, "Return", [this] { go(PageId::Menu); });
}

void PageHost::buildShortcuts(Frame& f, const std::vector<desktop::Shortcut>& list) {
    for (size_t i = 0; i < list.size() && i < 12; ++i) {
        const desktop::Shortcut& sc = list[i];
        if (sc.label.empty()) continue;
        auto fn = [this, keys = sc.keys, label = sc.label] {
            if (keys.empty()) return;
            actions_.tapKeys(keys);
            say("SENT " + upper(label));
        };
        if (i < 6) f.left(static_cast<int>(i) + 1, sc.label, fn);
        else f.right(static_cast<int>(i) - 5, sc.label, fn);
    }
}

void PageHost::buildKeys(Frame& f) {
    f.screen.putCentered(0, "KEYS");
    buildShortcuts(f, shortcuts_);
}

void PageHost::buildKeys2(Frame& f) {
    f.screen.putCentered(0, "KEYS 2");
    buildShortcuts(f, shortcuts2_);
}

// The first press shows in KRunner what would open, the second one opens it: a key never launches something unseen, and Enter only goes out while the user is looking at it.
void PageHost::runnerPress(const std::string& text) {
    const auto now = std::chrono::steady_clock::now();
    if (pendingRunner_ == text && now - pendingRunnerAt_ < std::chrono::seconds(6)) {
        pendingRunner_.clear();
        actions_.tapKeys({KEY_ENTER});
        say("OPENING " + upper(text));
        return;
    }
    if (actions_.runnerQuery(text)) {
        pendingRunner_ = text;
        pendingRunnerAt_ = now;
        say("PRESS AGAIN TO OPEN");
    } else {
        pendingRunner_.clear();
        say("KRUNNER UNAVAILABLE");
    }
}

void PageHost::buildDirect(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "DIRECT TO");
    for (size_t i = 0; i < directApps_.size() && i < static_cast<size_t>(desktop::kDirectApps); ++i)
        if (!directApps_[i].empty()) f.left(static_cast<int>(i) + 1, directApps_[i], [this, app = directApps_[i]] { runnerPress(app); });
    auto search = [this](int kind) {
        return [this, kind] {
            if (directText_.empty()) { say("TYPE FIRST"); return; }
            if (kind == 3 && !validAirportCode(directText_)) { say("TYPE AN AIRPORT CODE"); return; }
            say(actions_.openUrl(desktop::searchAddress(kind, directText_)) ? "OPENED IN BROWSER" : "BROWSER UNAVAILABLE");
        };
    };
    f.right(1, "Google", search(0));
    f.right(2, "YouTube", search(1));
    f.right(3, "Wikipedia", search(2));
    f.right(4, "Charts", search(3));
    f.right(5, "Steam", search(4));
    f.right(6, "Run", [this] {
        if (directText_.empty()) say("TYPE FIRST");
        else runnerPress(directText_);
    });
    if (toast_.empty()) s.put(13, 0, directText_.size() > 24 ? directText_.substr(directText_.size() - 24) : directText_, Color::Amber);
}

void PageHost::buildProgress(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "PROGRESS");
    if (!tasksAvailable_) s.putCentered(6, "NOT AVAILABLE", Color::Amber);
    else if (tasks_.empty()) s.putCentered(6, "NO ACTIVE TASKS", Color::White);
    for (size_t i = 0; i < tasks_.size() && i < 5; ++i) {
        const desktop::ProgressTask& t = tasks_[i];
        const int line = static_cast<int>(i) + 1;
        f.tag(line, t.name);
        s.put(line * 2, 0, "<", Color::Cyan);
        s.put(line * 2, 1, barText(t.percent, 17), t.percent >= 100 ? Color::Green : Color::Cyan);
        s.putRight(line * 2, std::to_string(t.percent) + "%", t.percent >= 100 ? Color::Green : Color::White);
        f.side(line - 1, "Hide " + t.name, [this, key = t.key] { actions_.dismissTask(key); });
    }
    if (tasks_.size() > 5) s.put(11, 0, "+" + std::to_string(tasks_.size() - 5) + " MORE", Color::White, true);
    f.left(6, "Return", [this] { go(PageId::Menu); });
}

void PageHost::buildPower(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "POWER");
    for (size_t i = 0; i < system_.gpus.size() && i < 2; ++i) {
        const GpuSample& g = system_.gpus[i];
        const int line = static_cast<int>(i) + 1;
        f.tag(line, g.name);
        s.put(line * 2, 0, g.powerWatts < 0 ? "--" : format("%.0f W", g.powerWatts), g.powerWatts < 0 ? Color::White : Color::Green);
        if (g.fanRpm >= 0) s.putRight(line * 2, std::to_string(g.fanRpm) + " RPM", Color::Green);
        else if (g.fanPercent >= 0) s.putRight(line * 2, "FAN " + std::to_string(g.fanPercent) + " %", Color::Green);
    }
    s.put(5, 0, "TEMPERATURES", Color::White, true);
    for (size_t i = 0; i < system_.sensors.size() && i < 12; ++i) {
        const SensorSample& t = system_.sensors[i];
        const int row = 6 + static_cast<int>(i / 2), col = i % 2 ? 13 : 0;
        const Color c = t.tempC >= 85 ? Color::Red : t.tempC >= 70 ? Color::Amber : Color::Green;
        s.put(row, col, fit(t.name, 6), Color::White, true);
        s.put(row, col + 7, std::to_string(t.tempC) + "\xc2\xb0", c);
    }
    f.left(6, "Return", [this] { go(PageId::Menu); });
    f.right(6, "Rate " + std::to_string(refreshSeconds_) + "S", [this] { cycleRate(); });
}

void PageHost::buildComms(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "COMMS");
    if (audio_.known && !audio_.available) {
        s.put(9, 0, "SOUND SERVER UNAVAILABLE", Color::Amber, true);
    } else if (audio_.available) {
        const bool inCall = audio_.voiceStreams > 0, hasSound = audio_.soundStreams > 0;
        f.left(1, audio_.micMuted ? "Mic muted" : "Mic live", [this] { actions_.audio(desktop::AudioCommand::ToggleMic); }, audio_.micMuted ? Color::Amber : Color::Green);
        f.left(2, !inCall ? "Voice --" : audio_.voiceMuted ? "Voice muted" : "Voice live", [this, inCall] {
            if (inCall) actions_.audio(desktop::AudioCommand::ToggleVoice);
            else say("NOT IN A CALL");
        }, !inCall ? Color::White : audio_.voiceMuted ? Color::Amber : Color::Green);
        f.left(3, !hasSound ? "Sound --" : audio_.soundMuted ? "Deafened" : "Sound on", [this, hasSound] {
            if (hasSound) actions_.audio(desktop::AudioCommand::ToggleDeafen);
            else say("NO SOUND FROM APP");
        }, !hasSound ? Color::White : audio_.soundMuted ? Color::Amber : Color::Green);
        s.put(9, 0, fit(upper(commsApp_), 24), Color::White, true);
    }
    if (!comms_.empty() && !comms_[0].label.empty())
        f.left(4, comms_[0].label, [this, keys = comms_[0].keys, label = comms_[0].label] {
            if (keys.empty()) return;
            actions_.tapKeys(keys);
            say("SENT " + upper(label));
        });
    s.putRight(1, "DOWN", Color::White, true);
    s.putRight(2, netSpeed(system_.downBytesPerSecond) + ">", Color::Green);
    s.putRight(3, "UP", Color::White, true);
    s.putRight(4, netSpeed(system_.upBytesPerSecond) + ">", Color::Green);
    auto units = [this] { netBits_ = !netBits_; prefsChanged_ = true; };
    f.side(6, "Units", units);
    f.side(7, "Units", units);
    f.left(6, "Return", [this] { go(PageId::Menu); });
    f.right(6, "Rate " + std::to_string(refreshSeconds_) + "S", [this] { cycleRate(); });
}

void PageHost::buildWeather(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "WEATHER");
    f.tag(1, "AIRPORT");
    s.put(2, 0, "<" + (airport_.empty() ? std::string("____") : airport_), Color::Cyan);
    f.side(0, "Set airport", [this] {
        if (!validAirportCode(scratch_)) { say("TYPE 4 LETTERS"); return; }
        airport_ = scratch_;
        scratch_.clear();
        weather_ = WeatherResult{};
        weatherRequested_ = true;
        prefsChanged_ = true;
    });
    if (!airport_.empty()) f.right(1, "Refresh", [this] { weatherRequested_ = true; });
    f.left(6, "Return", [this] { go(PageId::Menu); });

    const MetarReport& m = weather_.report;
    switch (weather_.status) {
        case WeatherStatus::Ok: {
            auto field = [&](int row, int col, const char* name, const std::string& value) {
                s.put(row, col, name, Color::White, true);
                s.put(row + 1, col, value.empty() ? "--" : value, Color::Green);
            };
            field(3, 0, "WIND", m.wind);
            field(3, 13, "VISIBILITY", m.visibility);
            field(5, 0, "TEMP/DEW", m.temperature.empty() ? "" : m.temperature + "/" + m.dewpoint);
            field(5, 13, "QNH", m.pressure);
            s.put(7, 0, "METAR", Color::White, true);
            s.putRight(7, m.time, Color::White, true);
            putWrapped(s, m.raw, 8, 11);  // the report itself
            break;
        }
        case WeatherStatus::Loading: s.putCentered(7, "LOADING", Color::Amber); break;
        case WeatherStatus::NoReport: s.putCentered(7, "NO REPORT", Color::Amber); break;
        case WeatherStatus::Offline: s.putCentered(7, "NO CONNECTION", Color::Amber); break;
        case WeatherStatus::Unavailable: s.putCentered(7, "WEATHER UNAVAILABLE", Color::Amber); break;
        case WeatherStatus::None:
            if (airport_.empty()) s.putCentered(7, "TYPE AIRPORT THEN 1L", Color::Amber);
            break;
    }
    if (toast_.empty()) s.put(13, 0, scratch_, Color::Amber);
}

void PageHost::buildType(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "TYPE");
    s.put(1, 0, "KEYS GO TO THE PC", Color::White, true);
    auto send = [this](const char* name, std::vector<int> keys) {
        return [this, name, keys = std::move(keys)] {
            actions_.tapKeys(keys);
            say(std::string("SENT ") + name);
        };
    };
    f.left(1, shift_ ? "Shift on" : "Shift off", [this] { shift_ = !shift_; }, shift_ ? Color::Green : Color::Cyan);
    f.left(2, "Tab", send("TAB", {KEY_TAB}));
    f.left(3, "Esc", send("ESC", {KEY_ESC}));
    f.right(1, "Copy", send("COPY", {KEY_LEFTCTRL, KEY_C}));
    f.right(2, "Paste", send("PASTE", {KEY_LEFTCTRL, KEY_V}));
    f.right(3, "Undo", send("UNDO", {KEY_LEFTCTRL, azerty_ ? KEY_W : KEY_Z}));
    f.right(4, "Select all", send("SELECT ALL", {KEY_LEFTCTRL, azerty_ ? KEY_Q : KEY_A}));
    f.left(6, "Return", [this] { go(PageId::Menu); });
    if (toast_.empty()) s.put(13, 0, typed_, Color::Amber);
}

}  // namespace lr::mcdu
