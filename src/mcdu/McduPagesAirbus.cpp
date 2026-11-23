// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// The desktop pages in the Airbus style: the same function keys as the classic pages, laid out like the aircraft's MCDU.
// Small white labels over large values; green: actual values and what is on, cyan: what can be changed or pressed, white:
// actions; amber boxes for an entry still to make. Typing goes to the scratchpad; a side key next to a field takes it, or copies
// the field into an empty scratchpad. Left and right slew through the monitor pages.
#include <linux/input-event-codes.h>

#include "desktop/DesktopLauncher.hpp"
#include "mcdu/McduPages.hpp"
#include "mcdu/McduPagesInternal.hpp"

namespace lr::mcdu {

using namespace pages;

namespace {
constexpr const char* kSlew = "\xe2\x86\x90\xe2\x86\x92";          // ←→: more pages of this kind
constexpr const char* kUpDown = "\xe2\x86\x91\xe2\x86\x93";        // ↑↓
constexpr const char* kBoxes = "\xe2\x98\x90\xe2\x98\x90\xe2\x98\x90\xe2\x98\x90";  // an airport still to enter
constexpr size_t kScratch = 22;

// the small label over a line's left and right values
void label(Screen& s, int line, const std::string& left, const std::string& right = {}) {
    if (!left.empty()) s.put(line * 2 - 1, 1, fit(left, 22), Color::White, true);
    if (!right.empty()) s.putRight(line * 2 - 1, fit(right, 22) + " ", Color::White, true);
}

// two choices: the one in use large and green, the other small and cyan ("ON/OFF")
void choice(Screen& s, int row, bool left, const std::string& a, const std::string& b, bool aChosen) {
    int col = left ? 0 : kCols - static_cast<int>(a.size() + b.size() + 1);
    if (aChosen) {
        s.put(row, col, a, Color::Green);
        s.put(row, col + static_cast<int>(a.size()), "/" + b, Color::Cyan, true);
    } else {
        s.put(row, col, a + "/", Color::Cyan, true);
        col += static_cast<int>(a.size()) + 1;
        s.put(row, col, b, Color::Green);
    }
}

std::string degrees(int c) { return c >= 0 ? std::to_string(c) + "\xc2\xb0" "C" : std::string("--"); }

std::string percentText(int p) {
    std::string t = std::to_string(std::min(p, 100)) + "%";
    return std::string(t.size() < 4 ? 4 - t.size() : 0, ' ') + t;
}

// The name of a key as printed on it. On an AZERTY keyboard the letters sit on other physical keys.
std::string keyName(int code, bool azerty) {
    static const int kLetters[26] = {KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
                                     KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
    if (azerty) {
        switch (code) {
            case KEY_Q: return "A";
            case KEY_A: return "Q";
            case KEY_W: return "Z";
            case KEY_Z: return "W";
            case KEY_SEMICOLON: return "M";
            case KEY_M: return ",";
            default: break;
        }
    }
    for (int i = 0; i < 26; ++i)
        if (code == kLetters[i]) return std::string(1, static_cast<char>('A' + i));
    if (code >= KEY_1 && code <= KEY_9) return std::string(1, static_cast<char>('1' + code - KEY_1));
    if (code >= KEY_F1 && code <= KEY_F10) return "F" + std::to_string(code - KEY_F1 + 1);
    switch (code) {
        case KEY_0: return "0";
        case KEY_F11: return "F11";
        case KEY_F12: return "F12";
        case KEY_LEFTCTRL: case KEY_RIGHTCTRL: return "CTRL";
        case KEY_LEFTALT: return "ALT";
        case KEY_RIGHTALT: return "ALTGR";
        case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: return "SHIFT";
        case KEY_LEFTMETA: case KEY_RIGHTMETA: return "META";
        case KEY_TAB: return "TAB";
        case KEY_ENTER: return "ENTER";
        case KEY_ESC: return "ESC";
        case KEY_SPACE: return "SPACE";
        case KEY_BACKSPACE: return "BKSP";
        case KEY_DELETE: return "DEL";
        case KEY_SYSRQ: return "PRINT";
        case KEY_VOLUMEUP: return "VOL+";
        case KEY_VOLUMEDOWN: return "VOL-";
        case KEY_MUTE: return "MUTE";
        case KEY_PLAYPAUSE: return "PLAY";
        case KEY_NEXTSONG: return "NEXT";
        case KEY_PREVIOUSSONG: return "PREV";
        case KEY_UP: return "UP";
        case KEY_DOWN: return "DOWN";
        case KEY_LEFT: return "LEFT";
        case KEY_RIGHT: return "RIGHT";
        case KEY_HOME: return "HOME";
        case KEY_END: return "END";
        case KEY_PAGEUP: return "PGUP";
        case KEY_PAGEDOWN: return "PGDN";
        default: return "?";
    }
}

std::string comboText(const std::vector<int>& keys, bool azerty) {
    std::string out;
    for (int k : keys) out += (out.empty() ? "" : "+") + keyName(k, azerty);
    return out;
}

// the monitor pages, in the order left and right go through them
struct Monitor {
    PageId page;
    bool cores;
};
constexpr Monitor kMonitors[] = {{PageId::System, false}, {PageId::Cpu, false}, {PageId::Cpu, true}, {PageId::Memory, false},
                                 {PageId::Gpus, false},   {PageId::Disks, false}, {PageId::Network, false}, {PageId::Power, false}};
}  // namespace

bool PageHost::slew(int step) {
    constexpr int count = static_cast<int>(std::size(kMonitors));
    for (int i = 0; i < count; ++i) {
        if (kMonitors[i].page != page_ || (page_ == PageId::Cpu && kMonitors[i].cores != cpuCores_)) continue;
        const Monitor& next = kMonitors[(i + step + count) % count];
        go(next.page);
        cpuCores_ = next.cores;
        return true;
    }
    return false;
}

void PageHost::airbusKey(Key key) {
    PageId to = page_;
    bool pageKey = true;
    switch (key) {
        case Key::Menu: to = PageId::Menu; break;
        case Key::Init: to = PageId::Clock; break;
        case Key::Data: to = PageId::DataIndex; break;
        case Key::Perf: to = PageId::Media; break;
        case Key::Fpln: to = PageId::Keys; break;
        case Key::Rad: to = PageId::Type; break;
        case Key::Fuel: to = PageId::Power; break;
        case Key::Sec: to = PageId::Keys2; break;
        case Key::Atc: to = PageId::Comms; break;
        case Key::Airport: to = PageId::Weather; break;
        case Key::Dir: to = PageId::Direct; break;
        case Key::Prog: to = PageId::Progress; break;
        default: pageKey = false; break;
    }
    if (pageKey) {
        go(to);
        if (to != PageId::Menu) lastPage_ = to;
        return;
    }
    if (key >= Key::L1 && key <= Key::R6) {
        sideKey(key);
        return;
    }
    if (key == Key::LeftArrow || key == Key::RightArrow) {
        if (!slew(key == Key::RightArrow ? 1 : -1) && page_ == PageId::Type) typeKey(key);
        return;
    }
    if (key == Key::UpArrow || key == Key::DownArrow) {
        if (page_ == PageId::Media) actions_.tapKeys({key == Key::UpArrow ? KEY_VOLUMEUP : KEY_VOLUMEDOWN});
        else if (page_ == PageId::Disks) scrollDisks(key == Key::DownArrow);
        else if (page_ == PageId::Type) typeKey(key);
        return;
    }
    if (page_ == PageId::Type) {
        typeKey(key);
        return;
    }
    // the scratchpad
    if (key == Key::Clr) {
        if (!toast_.empty()) toast_.clear();
        else if (!scratch_.empty()) scratch_.pop_back();
        return;
    }
    toast_.clear();
    if (key == Key::Ovfy) {
        if (page_ == PageId::Direct && !scratch_.empty()) {
            runnerPress(scratch_);
            if (pendingRunner_.empty()) scratch_.clear();
        } else {
            say("NOT ALLOWED");
        }
        return;
    }
    if (key == Key::PlusMinus) {
        if (!scratch_.empty() && scratch_[0] == '-') scratch_.erase(0, 1);
        else if (scratch_.size() < kScratch) scratch_.insert(0, "-");
        return;
    }
    char c = 0;
    if (key == Key::Space) c = ' ';
    else if ((key >= Key::A && key <= Key::Z) || (key >= Key::D1 && key <= Key::D0) || key == Key::Dot || key == Key::Slash) c = keyLabel(key)[0];
    if (c && scratch_.size() < kScratch) scratch_ += c;
}

// A side key next to the home airport: takes four letters from the scratchpad, or copies the airport into it.
std::function<void()> PageHost::airportField() {
    return [this] {
        if (scratch_.empty()) {
            if (airport_.empty()) say("NOT ALLOWED");
            else scratch_ = airport_;
            return;
        }
        if (!validAirportCode(scratch_)) {
            say("FORMAT ERROR");
            return;
        }
        airport_ = scratch_;
        scratch_.clear();
        weather_ = WeatherResult{};
        weatherRequested_ = true;
        prefsChanged_ = true;
    };
}

void PageHost::buildAirbus(Frame& f) {
    switch (page_) {
        case PageId::Menu: buildAMenu(f); break;
        case PageId::Clock: buildAClock(f); break;
        case PageId::DataIndex: buildADataIndex(f); break;
        case PageId::System: buildAStatus(f); break;
        case PageId::Cpu: buildACpu(f); break;
        case PageId::Memory: buildAMemory(f); break;
        case PageId::Gpus: buildAGpus(f); break;
        case PageId::Disks: buildADisks(f); break;
        case PageId::Network: buildANetwork(f); break;
        case PageId::Power: buildAPower(f); break;
        case PageId::Media: buildAMedia(f); break;
        case PageId::Keys: buildAKeys(f, "KEYS", shortcuts_); break;
        case PageId::Keys2: buildAKeys(f, "SEC KEYS", shortcuts2_); break;
        case PageId::Type: buildAType(f); break;
        case PageId::Comms: buildAComms(f); break;
        case PageId::Weather: buildAWeather(f); break;
        case PageId::Direct: buildADirect(f); break;
        case PageId::Progress: buildAProgress(f); break;
    }
    Screen& s = f.screen;
    if (!toast_.empty()) s.put(13, 0, fit(toast_, 24), Color::White);
    else if (page_ != PageId::Type) s.put(13, 0, scratch_, Color::White);
}

void PageHost::buildAMenu(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "MCDU MENU");
    s.put(2, 0, "<DESKTOP", Color::Green);
    s.put(2, 9, "(SEL)", Color::Green, true);
    f.side(0, "Desktop", [this] { go(lastPage_); });
    label(s, 1, "", "SOUND");
    s.putRight(2, "MUTE>");
    f.side(6, "Mute", [this] { actions_.tapKeys({KEY_MUTE}); say("MUTE"); });
    s.put(10, 0, "<LOCK PC");
    f.side(4, "Lock PC", [this] { actions_.tapKeys({KEY_LEFTMETA, KEY_L}); say("PC LOCKED"); });
    s.putRight(10, "SCREEN OFF>");
    f.side(10, "Screen off", [this] { actions_.sleepScreen(); });
    if (toast_.empty() && scratch_.empty()) s.put(13, 0, "SELECT DESIRED SYSTEM");
}

void PageHost::buildAClock(Frame& f) {
    static const char* const kDays[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char* const kMonths[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    Screen& s = f.screen;
    s.putCentered(0, "INIT");
    std::tm utc{};
    const std::time_t raw = std::time(nullptr);
    gmtime_r(&raw, &utc);
    auto local = [this](const std::tm& t) {
        int hour = t.tm_hour;
        std::string suffix;
        if (h12_) {
            suffix = hour < 12 ? "A" : "P";
            hour = hour % 12 == 0 ? 12 : hour % 12;
        }
        return two(hour) + ":" + two(t.tm_min) + ":" + two(t.tm_sec) + suffix;
    };
    const std::string utcText = two(utc.tm_hour) + ":" + two(utc.tm_min) + ":" + two(utc.tm_sec) + "Z";
    label(s, 1, utc_ ? "UTC" : "LOCAL", utc_ ? "LOCAL" : "UTC");
    s.put(2, 0, utc_ ? utcText : local(now_), Color::Green);
    s.putRight(2, utc_ ? local(now_) : utcText, Color::Cyan, true);
    f.side(6, utc_ ? "Local" : "UTC", [this] { utc_ = !utc_; prefsChanged_ = true; });

    label(s, 2, "FORMAT", "DATE");
    choice(s, 4, true, "24H", "12H", !h12_);
    f.side(1, h12_ ? "24H" : "12H", [this] { h12_ = !h12_; prefsChanged_ = true; });
    const std::tm& d = utc_ ? utc : now_;
    char date[24];
    snprintf(date, sizeof date, "%s %02d %s", kDays[std::clamp(d.tm_wday, 0, 6)], d.tm_mday, kMonths[std::clamp(d.tm_mon, 0, 11)]);
    s.putRight(4, date, Color::Green);

    label(s, 3, "CHRONO");
    const int c = chronoSeconds();
    s.put(6, 0, two(c / 3600) + ":" + two(c / 60 % 60) + ":" + two(c % 60), chronoRunning_ ? Color::Green : Color::White);
    s.putRight(6, chronoRunning_ ? "STOP>" : "START>", Color::Cyan);
    f.side(8, chronoRunning_ ? "Stop" : "Start", [this] {
        if (chronoRunning_) chronoHeld_ = chronoSeconds();
        else chronoStart_ = std::chrono::steady_clock::now();
        chronoRunning_ = !chronoRunning_;
    });
    s.putRight(8, "RESET>", Color::Cyan);
    f.side(9, "Reset", [this] { chronoRunning_ = false; chronoHeld_ = 0; });

    label(s, 5, "HOME AIRPORT");
    if (airport_.empty()) s.put(10, 0, kBoxes, Color::Amber);
    else s.put(10, 0, airport_, Color::Cyan);
    f.side(4, "Home airport", airportField());
}

void PageHost::buildADataIndex(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "DATA INDEX");
    auto open = [this](PageId page, bool cores = false) { return [this, page, cores] { go(page); cpuCores_ = cores; }; };
    struct Item { const char* name; const char* text; PageId page; };
    const Item left[] = {{"PC", "<STATUS", PageId::System}, {"CPU", "<MONITOR", PageId::Cpu}, {"MEMORY", "<MONITOR", PageId::Memory},
                         {"GPU", "<MONITOR", PageId::Gpus}, {"DISK", "<MONITOR", PageId::Disks}};
    for (int i = 0; i < 5; ++i) {
        label(s, i + 1, left[i].name);
        s.put(2 + i * 2, 0, left[i].text);
        f.side(i, std::string(left[i].name) + (i ? " monitor" : " status"), open(left[i].page));
    }
    label(s, 1, "", "NETWORK");
    s.putRight(2, "MONITOR>");
    f.side(6, "Network monitor", open(PageId::Network));
    label(s, 2, "", "POWER");
    s.putRight(4, "MONITOR>");
    f.side(7, "Power monitor", open(PageId::Power));
    label(s, 6, "", "REFRESH");
    s.putRight(12, std::to_string(refreshSeconds_) + "S>", Color::Cyan);
    f.side(11, "Rate " + std::to_string(refreshSeconds_) + "S", [this] { cycleRate(); });
}

void PageHost::buildAStatus(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "PC STATUS");
    s.putRight(0, kSlew);
    s.put(0, 0, std::to_string(system_.uptimeSeconds / 3600) + "H" + two(static_cast<int>(system_.uptimeSeconds % 3600 / 60)), Color::White, true);
    // a load: "<" then the percentage and a bar in the colour of its stage
    auto load = [&](int row, int percent) {
        s.put(row, 0, "<");
        if (percent < 0) { s.put(row, 1, "--"); return; }
        s.put(row, 1, percentText(percent), levelColor(percent));
        s.put(row, 6, barText(percent, 10), levelColor(percent));
    };
    label(s, 1, "CPU", "TEMP");
    load(2, system_.cpuPercent < 0 ? -1 : static_cast<int>(std::lround(system_.cpuPercent)));
    s.putRight(2, degrees(system_.cpuTempC), Color::Green);
    f.side(0, "CPU", [this] { go(PageId::Cpu); cpuCores_ = false; });

    const int memPercent = system_.memTotalGb > 0 ? static_cast<int>(std::lround(system_.memUsedGb * 100.0 / system_.memTotalGb)) : -1;
    label(s, 2, "RAM " + format("%.1f", system_.memUsedGb) + "/" + format("%.0fG", system_.memTotalGb), system_.swapTotalGb > 0 ? (system_.zram ? "ZRAM" : "SWAP") : "");
    load(4, memPercent);
    if (system_.swapTotalGb > 0) s.putRight(4, format("%.1fG", system_.swapUsedGb), Color::Green);
    f.side(1, "Memory", [this] { go(PageId::Memory); });

    const auto& gpus = system_.gpus;
    if (gpus.empty()) {
        label(s, 3, "GPU");
        s.put(6, 1, "--");
    } else {
        const GpuSample& g = gpus[static_cast<size_t>(gpuIndex_) % gpus.size()];
        label(s, 3, g.name, "TEMP");
        load(6, g.loadPercent);
        s.putRight(6, degrees(g.tempC), Color::Green);
        f.side(2, "GPU", [this] { go(PageId::Gpus); });
    }

    const auto& disks = system_.disks;
    if (disks.empty()) {
        label(s, 4, "DISK");
        s.put(8, 1, "--");
    } else {
        const size_t i = selectedDisk();
        const DiskSample& d = disks[i];
        label(s, 4, "DISK " + std::to_string(i + 1) + "/" + std::to_string(disks.size()) + " " + d.name, "FREE");
        load(8, d.usedPercent);
        s.putRight(8, sizeText(d.freeGb), Color::Green);
        f.side(3, "Next disk", [this, i] {
            diskMount_ = system_.disks[(i + 1) % system_.disks.size()].mount;
            prefsChanged_ = true;
        });
    }

    label(s, 5, "DOWN", "UP");
    s.put(10, 0, "<" + netSpeed(system_.downBytesPerSecond), Color::Green);
    s.putRight(10, netSpeed(system_.upBytesPerSecond), Color::Green);
    f.side(4, "Network", [this] { go(PageId::Network); });

    s.put(12, 0, "<RETURN");
    f.side(5, "Return", [this] { go(PageId::DataIndex); });
    label(s, 6, "", "REFRESH");
    s.putRight(12, std::to_string(refreshSeconds_) + "S>", Color::Cyan);
    f.side(11, "Rate " + std::to_string(refreshSeconds_) + "S", [this] { cycleRate(); });
}

void PageHost::buildACpu(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, cpuCores_ ? "CPU CORES" : "CPU MONITOR");
    s.putRight(0, kSlew);
    if (cpuCores_) {
        drawCores(s);
        s.put(12, 0, "<RETURN");
        f.side(5, "Return", [this] { cpuCores_ = false; });
        return;
    }
    label(s, 1, "LOAD", "TEMP");
    if (system_.cpuPercent < 0) s.put(2, 0, "--");
    else {
        const int p = static_cast<int>(std::lround(system_.cpuPercent));
        s.put(2, 0, std::to_string(p) + "%", levelColor(p));
    }
    s.putRight(2, degrees(system_.cpuTempC), Color::Green);
    label(s, 2, "CLOCK", system_.cpuWatts >= 0 ? "POWER" : "");
    s.put(4, 0, system_.cpuGhz < 0 ? "--" : format("%.2fGHZ", system_.cpuGhz), Color::Green);
    if (system_.cpuWatts >= 0) s.putRight(4, format("%.0fW", system_.cpuWatts), Color::Green);
    label(s, 3, "LOAD AVERAGE");
    s.put(6, 0, format("%.2f  ", system_.load1) + format("%.2f  ", system_.load5) + format("%.2f", system_.load15), Color::Green);
    label(s, 4, "", "CORES");
    s.putRight(8, std::to_string(system_.corePercent.size()) + ">", Color::Cyan);
    f.side(9, "Cores", [this] { cpuCores_ = true; });
    s.put(12, 0, "<RETURN");
    f.side(5, "Return", [this] { go(PageId::DataIndex); });
}

void PageHost::buildAMemory(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "MEMORY");
    s.putRight(0, kSlew);
    const int percent = system_.memTotalGb > 0 ? static_cast<int>(std::lround(system_.memUsedGb * 100.0 / system_.memTotalGb)) : -1;
    const Color used = percent < 0 ? Color::Green : levelColor(percent);
    label(s, 1, "USED", "AVAILABLE");
    s.put(2, 0, format("%.1fGB", system_.memUsedGb), used);
    s.putRight(2, format("%.1fGB", system_.memAvailableGb), Color::Green);
    label(s, 2, "TOTAL", "IN USE");
    s.put(4, 0, format("%.1fGB", system_.memTotalGb), Color::Green);
    s.putRight(4, percent < 0 ? std::string("--") : std::to_string(percent) + "%", used);
    if (system_.swapTotalGb > 0) {
        label(s, 3, system_.zram ? "ZRAM" : "SWAP", system_.zram ? "RATIO" : "");
        s.put(6, 0, format("%.1f", system_.swapUsedGb) + "/" + format("%.0fGB", system_.swapTotalGb), Color::Green);
        if (system_.zram) {
            s.putRight(6, system_.zramMemoryGb > 0.0005 ? format("%.1f:1", system_.zramStoredGb / system_.zramMemoryGb) : std::string("--"), Color::Green);
            label(s, 4, "STORED", "IN MEMORY");
            s.put(8, 0, format("%.2fGB", system_.zramStoredGb), Color::Green);
            s.putRight(8, format("%.2fGB", system_.zramMemoryGb), Color::Green);
        }
    } else {
        label(s, 3, "SWAP");
        s.put(6, 0, "NONE", Color::Green);
    }
    s.put(12, 0, "<RETURN");
    f.side(5, "Return", [this] { go(PageId::DataIndex); });
}

void PageHost::buildAGpus(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "GPU MONITOR");
    s.putRight(0, kSlew);
    s.put(12, 0, "<RETURN");
    f.side(5, "Return", [this] { go(PageId::DataIndex); });
    const auto& gpus = system_.gpus;
    if (gpus.empty()) {
        s.putCentered(6, "NO GPU FOUND", Color::Amber);
        return;
    }
    const size_t i = static_cast<size_t>(gpuIndex_) % gpus.size();
    const GpuSample& g = gpus[i];
    label(s, 1, g.name, "TEMP");
    if (g.loadPercent < 0) s.put(2, 0, "--");
    else {
        s.put(2, 0, percentText(g.loadPercent), levelColor(g.loadPercent));
        s.put(2, 5, barText(g.loadPercent, 10), levelColor(g.loadPercent));
    }
    s.putRight(2, degrees(g.tempC), Color::Green);
    label(s, 2, "VRAM", "POWER");
    s.put(4, 0, g.memTotalGb > 0 ? format("%.1f", g.memUsedGb) + "/" + format("%.0fGB", g.memTotalGb) : std::string("--"), Color::Green);
    s.putRight(4, g.powerWatts < 0 ? std::string("--") : format("%.0fW", g.powerWatts), Color::Green);
    label(s, 3, "FAN");
    s.put(6, 0, g.fanRpm >= 0 ? std::to_string(g.fanRpm) + "RPM" : g.fanPercent >= 0 ? std::to_string(g.fanPercent) + "%" : std::string("--"), Color::Green);
    if (gpus.size() > 1) {
        label(s, 4, "", "GPU " + std::to_string(i + 1) + "/" + std::to_string(gpus.size()));
        s.putRight(8, "NEXT GPU>", Color::Cyan);
        f.side(9, "Next GPU", [this, count = gpus.size()] {
            gpuIndex_ = static_cast<int>((static_cast<size_t>(gpuIndex_) + 1) % count);
            prefsChanged_ = true;
        });
    }
}

void PageHost::buildADisks(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "DISK MONITOR");
    s.putRight(0, kSlew);
    const auto& disks = system_.disks;
    if (disks.empty()) s.putCentered(6, "NO DISK FOUND", Color::Amber);
    const size_t selected = selectedDisk();
    for (size_t i = diskScroll_; i < disks.size() && i < diskScroll_ + kDisksPerPage; ++i) {
        const DiskSample& d = disks[i];
        const int line = static_cast<int>(i - diskScroll_) + 1, row = line * 2;
        s.put(row - 1, 1, fit(d.name + " " + upper(d.mount), 16), i == selected ? Color::Green : Color::White, true);
        s.putRight(row - 1, "FREE ", Color::White, true);
        s.put(row, 0, percentText(d.usedPercent), levelColor(d.usedPercent));
        s.put(row, 5, barText(d.usedPercent, 10), levelColor(d.usedPercent));
        s.putRight(row, sizeText(d.freeGb), Color::Green);
        f.side(line - 1, "Show " + d.name, [this, mount = d.mount] {
            diskMount_ = mount;
            prefsChanged_ = true;
        });
    }
    if (disks.size() > kDisksPerPage) s.putRight(12, kUpDown);
    s.put(12, 0, "<RETURN");
    f.side(5, "Return", [this] { go(PageId::DataIndex); });
}

void PageHost::buildANetwork(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "NETWORK");
    s.putRight(0, kSlew);
    label(s, 1, "DOWN", "UP");
    s.put(2, 0, netSpeed(system_.downBytesPerSecond), Color::Green);
    s.putRight(2, netSpeed(system_.upBytesPerSecond), Color::Green);
    label(s, 2, "UNITS");
    choice(s, 4, true, "BYTES", "BITS", !netBits_);
    f.side(1, "Units", [this] { netBits_ = !netBits_; prefsChanged_ = true; });
    s.put(12, 0, "<RETURN");
    f.side(5, "Return", [this] { go(PageId::DataIndex); });
}

void PageHost::buildAPower(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "POWER");
    s.putRight(0, kSlew);
    s.put(1, 1, "SOURCE", Color::White, true);
    s.put(1, 11, "POWER", Color::White, true);
    s.putRight(1, "TEMP ", Color::White, true);
    int row = 2;
    s.put(row, 0, "CPU", Color::Green);
    s.put(row, 11, system_.cpuWatts < 0 ? std::string("--") : format("%.0fW", system_.cpuWatts), Color::Green);
    s.putRight(row, degrees(system_.cpuTempC), Color::Green);
    for (size_t i = 0; i < system_.gpus.size() && i < 2; ++i) {
        const GpuSample& g = system_.gpus[i];
        row += 2;
        s.put(row, 0, fit(g.name, 10), Color::Green);
        s.put(row, 11, g.powerWatts < 0 ? std::string("--") : format("%.0fW", g.powerWatts), Color::Green);
        s.putRight(row, degrees(g.tempC), Color::Green);
        if (g.fanRpm >= 0) s.put(row + 1, 1, "FAN " + std::to_string(g.fanRpm) + "RPM", Color::White, true);
        else if (g.fanPercent >= 0) s.put(row + 1, 1, "FAN " + std::to_string(g.fanPercent) + "%", Color::White, true);
    }
    // every other temperature, small, two to a row
    row += 2;
    if (!system_.sensors.empty() && row <= 10) {
        s.put(row, 1, "SENSORS", Color::White, true);
        for (size_t i = 0; i < system_.sensors.size() && row + 1 + static_cast<int>(i / 2) <= 11; ++i) {
            const SensorSample& t = system_.sensors[i];
            const int r = row + 1 + static_cast<int>(i / 2), col = i % 2 ? 13 : 0;
            const Color c = t.tempC >= 85 ? Color::Red : t.tempC >= 70 ? Color::Amber : Color::Green;
            s.put(r, col, fit(upper(t.name), 6), Color::White, true);
            s.put(r, col + 7, std::to_string(t.tempC) + "\xc2\xb0", c, true);
        }
    }
    s.put(12, 0, "<RETURN");
    f.side(5, "Return", [this] { go(PageId::DataIndex); });
    label(s, 6, "", "REFRESH");
    s.putRight(12, std::to_string(refreshSeconds_) + "S>", Color::Cyan);
    f.side(11, "Rate " + std::to_string(refreshSeconds_) + "S", [this] { cycleRate(); });
}

void PageHost::buildAMedia(Frame& f) {
    Screen& s = f.screen;
    if (!media_.present) {
        s.putCentered(0, "MEDIA");
        s.putCentered(6, "NO PLAYER", Color::Amber);
    } else {
        s.put(0, 5, "MEDIA");
        s.put(0, 11, media_.playing ? "PLAYING" : "PAUSED", Color::Green);
        label(s, 1, "PLAYER", "VOLUME");
        s.put(2, 0, fit(upper(media_.player), 21), Color::Cyan);  // up to the ↑↓ on the right
        f.side(0, "Player", [this] { actions_.media(desktop::MediaCommand::NextPlayer); });
        s.putRight(2, kUpDown);
        label(s, 2, "TRACK");
        s.put(4, 0, fit(upper(media_.title), 24), Color::Green);
        label(s, 3, "ARTIST");
        s.put(6, 0, fit(upper(media_.artist), 24), Color::Green);
        // only what the player can do: a radio stream has no shuffle or repeat and cannot skip
        label(s, 4, media_.hasShuffle ? "SHUFFLE" : "", media_.hasRepeat ? "REPEAT" : "");
        if (media_.hasShuffle) {
            choice(s, 8, true, "ON", "OFF", media_.shuffle);
            f.side(3, "Shuffle", [this] { actions_.media(desktop::MediaCommand::Shuffle); });
        }
        if (media_.hasRepeat) {
            choice(s, 8, false, "ON", "OFF", media_.repeat);
            f.side(9, "Repeat", [this] { actions_.media(desktop::MediaCommand::Repeat); });
        }
        s.put(10, 0, media_.playing ? "<PAUSE" : "<PLAY", Color::Cyan);
        f.side(4, media_.playing ? "Pause" : "Play", [this] { actions_.media(desktop::MediaCommand::PlayPause); });
        if (media_.canPrevious) {
            s.put(12, 0, "<PREV TRACK");
            f.side(5, "Prev", [this] { actions_.media(desktop::MediaCommand::Previous); say("PREVIOUS"); });
        }
        if (media_.canNext) {
            s.putRight(12, "NEXT TRACK>");
            f.side(11, "Next", [this] { actions_.media(desktop::MediaCommand::Next); say("NEXT"); });
        }
    }
    s.putRight(10, "MUTE>", Color::Cyan);
    f.side(10, "Mute", [this] { actions_.tapKeys({KEY_MUTE}); say("MUTE"); });
}

void PageHost::buildAKeys(Frame& f, const char* title, const std::vector<desktop::Shortcut>& list) {
    Screen& s = f.screen;
    s.putCentered(0, title);
    for (size_t i = 0; i < list.size() && i < 12; ++i) {
        const desktop::Shortcut& sc = list[i];
        if (sc.label.empty()) continue;
        const int row = static_cast<int>(i % 6) * 2 + 2;
        const std::string combo = fit(comboText(sc.keys, azerty_), 11);
        if (i < 6) {
            s.put(row - 1, 1, combo, Color::White, true);
            s.put(row, 0, "<" + fit(upper(sc.label), 11), Color::Cyan);
        } else {
            s.putRight(row - 1, combo + " ", Color::White, true);
            s.putRight(row, fit(upper(sc.label), 11) + ">", Color::Cyan);
        }
        f.side(static_cast<int>(i), sc.label, [this, keys = sc.keys, label = sc.label] {
            if (keys.empty()) return;
            actions_.tapKeys(keys);
            say("SENT " + upper(label));
        });
    }
}

void PageHost::buildAType(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "TEXT");
    auto send = [this](const char* name, std::vector<int> keys) {
        return [this, name, keys = std::move(keys)] {
            actions_.tapKeys(keys);
            say(std::string("SENT ") + name);
        };
    };
    label(s, 1, "SHIFT");
    choice(s, 2, true, "ON", "OFF", shift_);
    f.side(0, shift_ ? "Shift off" : "Shift on", [this] { shift_ = !shift_; });
    s.put(4, 0, "<TAB", Color::Cyan);
    f.side(1, "Tab", send("TAB", {KEY_TAB}));
    s.put(6, 0, "<ESC", Color::Cyan);
    f.side(2, "Esc", send("ESC", {KEY_ESC}));
    struct Item { const char* name; std::vector<int> keys; };
    const Item right[] = {{"COPY", {KEY_LEFTCTRL, KEY_C}}, {"PASTE", {KEY_LEFTCTRL, KEY_V}},
                          {"UNDO", {KEY_LEFTCTRL, azerty_ ? KEY_W : KEY_Z}}, {"SELECT ALL", {KEY_LEFTCTRL, azerty_ ? KEY_Q : KEY_A}}};
    for (int i = 0; i < 4; ++i) {
        label(s, i + 1, "", right[i].name);
        s.putRight(2 + i * 2, comboText(right[i].keys, azerty_) + ">", Color::Cyan);
        f.side(6 + i, right[i].name, send(right[i].name, right[i].keys));
    }
    label(s, 5, "KEYS GO TO THE PC");
    s.put(10, 0, typed_, Color::Green);
    s.put(11, 1, "CLR BKSP  OVFY ENTER", Color::White, true);
}

void PageHost::buildAComms(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "COMMS");
    if (audio_.known && !audio_.available) {
        s.putCentered(6, "SOUND SERVER UNAVAILABLE", Color::Amber);
    } else if (audio_.available) {
        const bool inCall = audio_.voiceStreams > 0, hasSound = audio_.soundStreams > 0;
        const std::string app = fit(upper(commsApp_), 14);
        label(s, 1, "MIC");
        s.put(2, 0, audio_.micMuted ? "<MUTED" : "<LIVE", audio_.micMuted ? Color::Amber : Color::Green);
        f.side(0, audio_.micMuted ? "Mic muted" : "Mic live", [this] { actions_.audio(desktop::AudioCommand::ToggleMic); });
        label(s, 2, "VOICE  " + app);
        s.put(4, 0, !inCall ? "<--" : audio_.voiceMuted ? "<MUTED" : "<LIVE", !inCall ? Color::White : audio_.voiceMuted ? Color::Amber : Color::Green);
        f.side(1, !inCall ? "Voice --" : audio_.voiceMuted ? "Voice muted" : "Voice live", [this, inCall] {
            if (inCall) actions_.audio(desktop::AudioCommand::ToggleVoice);
            else say("NOT IN A CALL");
        });
        label(s, 3, "SOUND  " + app);
        s.put(6, 0, !hasSound ? "<--" : audio_.soundMuted ? "<DEAFENED" : "<ON", !hasSound ? Color::White : audio_.soundMuted ? Color::Amber : Color::Green);
        f.side(2, !hasSound ? "Sound --" : audio_.soundMuted ? "Deafened" : "Sound on", [this, hasSound] {
            if (hasSound) actions_.audio(desktop::AudioCommand::ToggleDeafen);
            else say("NO SOUND FROM APP");
        });
    }
    if (!comms_.empty() && !comms_[0].label.empty()) {
        label(s, 4, "YOUR KEY  " + comboText(comms_[0].keys, azerty_));
        s.put(8, 0, "<" + fit(upper(comms_[0].label), 11), Color::Cyan);
        f.side(3, comms_[0].label, [this, keys = comms_[0].keys, label = comms_[0].label] {
            if (keys.empty()) return;
            actions_.tapKeys(keys);
            say("SENT " + upper(label));
        });
    }
}

void PageHost::buildAWeather(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "WEATHER");
    label(s, 1, "AIRPORT", airport_.empty() ? "" : "METAR");
    f.side(0, "Airport", airportField());
    if (airport_.empty()) {
        s.put(2, 0, kBoxes, Color::Amber);
        s.putCentered(7, "TYPE AIRPORT THEN 1L", Color::White, true);
        return;
    }
    s.put(2, 0, airport_, Color::Cyan);
    s.putRight(2, "REFRESH>", Color::Cyan);
    f.side(6, "Refresh", [this] { weatherRequested_ = true; });
    const MetarReport& m = weather_.report;
    switch (weather_.status) {
        case WeatherStatus::Ok:
            label(s, 2, "WIND", "VIS");
            s.put(4, 0, m.wind.empty() ? "--" : m.wind, Color::Green);
            s.putRight(4, m.visibility.empty() ? "--" : m.visibility, Color::Green);
            label(s, 3, "TEMP/DEW", "QNH");
            s.put(6, 0, m.temperature.empty() ? "--" : m.temperature + "/" + m.dewpoint, Color::Green);
            s.putRight(6, m.pressure.empty() ? "--" : m.pressure, Color::Green);
            label(s, 4, "", "TIME");
            s.putRight(8, m.time.empty() ? "--" : m.time, Color::Green);
            putWrapped(s, m.raw, 9, 12);
            break;
        case WeatherStatus::Loading: s.putCentered(7, "LOADING", Color::Amber); break;
        case WeatherStatus::NoReport: s.putCentered(7, "NO REPORT", Color::Amber); break;
        case WeatherStatus::Offline: s.putCentered(7, "NO CONNECTION", Color::Amber); break;
        case WeatherStatus::Unavailable: s.putCentered(7, "WEATHER UNAVAILABLE", Color::Amber); break;
        case WeatherStatus::None: break;
    }
}

void PageHost::buildADirect(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "DIR TO");
    const size_t apps = std::min(directApps_.size(), static_cast<size_t>(desktop::kDirectApps));
    label(s, 1, "RUN", apps > 5 && !directApps_[5].empty() ? "APP" : "");
    s.put(2, 0, "[          ]", Color::Cyan);
    f.side(0, "Run", [this] {
        if (scratch_.empty()) { say("TYPE FIRST"); return; }
        runnerPress(scratch_);
        if (pendingRunner_.empty()) scratch_.clear();
    });
    label(s, 2, "APPS", "WEB");
    for (size_t i = 0; i < apps && i < 5; ++i) {
        if (directApps_[i].empty()) continue;
        s.put(4 + static_cast<int>(i) * 2, 0, "<" + fit(upper(directApps_[i]), 11), Color::Cyan);
        f.side(static_cast<int>(i) + 1, directApps_[i], [this, app = directApps_[i]] { runnerPress(app); });
    }
    if (apps > 5 && !directApps_[5].empty()) {
        s.putRight(2, fit(upper(directApps_[5]), 11) + ">", Color::Cyan);
        f.side(6, directApps_[5], [this, app = directApps_[5]] { runnerPress(app); });
    }
    static const char* const kSites[5] = {"GOOGLE", "YOUTUBE", "WIKIPEDIA", "CHARTS", "STEAM"};
    for (int kind = 0; kind < 5; ++kind) {
        s.putRight(4 + kind * 2, std::string(kSites[kind]) + ">", Color::Cyan);
        f.side(7 + kind, kSites[kind], [this, kind] {
            if (scratch_.empty()) { say("TYPE FIRST"); return; }
            if (kind == 3 && !validAirportCode(scratch_)) { say("FORMAT ERROR"); return; }
            say(actions_.openUrl(desktop::searchAddress(kind, scratch_)) ? "OPENED IN BROWSER" : "BROWSER UNAVAILABLE");
        });
    }
}

void PageHost::buildAProgress(Frame& f) {
    Screen& s = f.screen;
    s.putCentered(0, "PROG");
    if (!tasksAvailable_) s.putCentered(6, "NOT AVAILABLE", Color::Amber);
    else if (tasks_.empty()) s.putCentered(6, "NO ACTIVE TASKS");
    for (size_t i = 0; i < tasks_.size() && i < 6; ++i) {
        const desktop::ProgressTask& t = tasks_[i];
        const int line = static_cast<int>(i) + 1;
        const bool done = t.percent >= 100;
        label(s, line, t.name);
        s.put(line * 2, 0, "<" + barText(t.percent, 17), done ? Color::White : Color::Green);
        s.putRight(line * 2, done ? "DONE" : std::to_string(t.percent) + "%", done ? Color::White : Color::Green);
        f.side(line - 1, "Hide " + t.name, [this, key = t.key] { actions_.dismissTask(key); });
    }
    if (tasks_.size() > 6) s.putRight(0, "+" + std::to_string(tasks_.size() - 6), Color::White, true);
}

}  // namespace lr::mcdu
