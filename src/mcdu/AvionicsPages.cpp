// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "mcdu/AvionicsPages.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace lr::mcdu {

namespace {
// The values, in the order of codes()
enum V : int {
    ComA1, ComS1, ComA2, ComS2, NavA1, NavS1, NavA2, NavS2, Obs1, Obs2,
    Squawk, XpdrState, BaroHg, BaroMb, BaroStd,
    ApHdg, ApAlt, ApVs, ApSpd, ApMaster, ApFd, ApHdgOn, ApAltOn, ApNavOn, ApAprOn, ApVsOn, ApFlcOn,
    EngType, Rpm, N1, OilT, OilP, Egt, Cht, FuelL, FuelR, FuelTot, Flow,
    Gs, Tas, WindDir, WindKt, AltInd, Oat, WpDist, WpBrg, WpEte, WpDtk, WpXtk, FplActive,
    kCount
};

const char* const kCodes[kCount] = {
    "(A:COM ACTIVE FREQUENCY:1, MHz)", "(A:COM STANDBY FREQUENCY:1, MHz)", "(A:COM ACTIVE FREQUENCY:2, MHz)", "(A:COM STANDBY FREQUENCY:2, MHz)",
    "(A:NAV ACTIVE FREQUENCY:1, MHz)", "(A:NAV STANDBY FREQUENCY:1, MHz)", "(A:NAV ACTIVE FREQUENCY:2, MHz)", "(A:NAV STANDBY FREQUENCY:2, MHz)",
    "(A:NAV OBS:1, Degrees)", "(A:NAV OBS:2, Degrees)",
    "(A:TRANSPONDER CODE:1, Bco16)", "(A:TRANSPONDER STATE:1, Number)",
    "(A:KOHLSMAN SETTING HG:1, inHg)", "(A:KOHLSMAN SETTING MB:1, Millibars)", "(A:KOHLSMAN SETTING STD:1, Bool)",
    "(A:AUTOPILOT HEADING LOCK DIR, Degrees)", "(A:AUTOPILOT ALTITUDE LOCK VAR, Feet)", "(A:AUTOPILOT VERTICAL HOLD VAR, Feet per minute)",
    "(A:AUTOPILOT AIRSPEED HOLD VAR, Knots)", "(A:AUTOPILOT MASTER, Bool)", "(A:AUTOPILOT FLIGHT DIRECTOR ACTIVE, Bool)",
    "(A:AUTOPILOT HEADING LOCK, Bool)", "(A:AUTOPILOT ALTITUDE LOCK, Bool)", "(A:AUTOPILOT NAV1 LOCK, Bool)", "(A:AUTOPILOT APPROACH HOLD, Bool)",
    "(A:AUTOPILOT VERTICAL HOLD, Bool)", "(A:AUTOPILOT FLIGHT LEVEL CHANGE, Bool)",
    "(A:ENGINE TYPE, Enum)", "(A:GENERAL ENG RPM:1, Rpm)", "(A:TURB ENG N1:1, Percent)", "(A:GENERAL ENG OIL TEMPERATURE:1, Fahrenheit)",
    "(A:GENERAL ENG OIL PRESSURE:1, Psi)", "(A:GENERAL ENG EXHAUST GAS TEMPERATURE:1, Fahrenheit)", "(A:RECIP ENG CYLINDER HEAD TEMPERATURE:1, Fahrenheit)",
    "(A:FUEL LEFT QUANTITY, Gallons)", "(A:FUEL RIGHT QUANTITY, Gallons)", "(A:FUEL TOTAL QUANTITY, Gallons)", "(A:ENG FUEL FLOW GPH:1, Gallons per hour)",
    "(A:GPS GROUND SPEED, Knots)", "(A:AIRSPEED TRUE, Knots)", "(A:AMBIENT WIND DIRECTION, Degrees)", "(A:AMBIENT WIND VELOCITY, Knots)",
    "(A:INDICATED ALTITUDE, Feet)", "(A:AMBIENT TEMPERATURE, Celsius)", "(A:GPS WP DISTANCE, Nautical miles)", "(A:GPS WP BEARING, Degrees)",
    "(A:GPS WP ETE, Seconds)", "(A:GPS WP DESIRED TRACK, Degrees)", "(A:GPS WP CROSS TRK, Nautical miles)", "(A:GPS IS ACTIVE FLIGHT PLAN, Bool)",
};

constexpr int kScratchMax = 22;
constexpr double kHgToMb = 33.8639;

std::string format(const char* f, ...) {
    char buf[64];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

bool digitsOnly(const std::string& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; }); }

// "123", "-500", "+500"
std::optional<int> wholeNumber(const std::string& s) {
    if (s.empty()) return std::nullopt;
    const bool negative = s[0] == '-';
    const std::string digits = s[0] == '-' || s[0] == '+' ? s.substr(1) : s;
    if (!digitsOnly(digits) || digits.size() > 6) return std::nullopt;
    const int v = std::stoi(digits);
    return negative ? -v : v;
}

std::string calc(const char* event) { return format("(>K:%s)", event); }
std::string calc(const char* event, long v) { return v < 0 ? format("%ld neg (>K:%s)", -v, event) : format("%ld (>K:%s)", v, event); }

std::string squawkText(int bco16) { return format("%x%x%x%x", (bco16 >> 12) & 0xf, (bco16 >> 8) & 0xf, (bco16 >> 4) & 0xf, bco16 & 0xf); }

// The values of one screen
class Values {
public:
    explicit Values(const AvionicsPages::Read& read) : read_(read) {}
    std::optional<double> get(V v) const { return read_ ? read_(kCodes[v]) : std::nullopt; }
    bool on(V v) const {
        const auto x = get(v);
        return x && *x > 0.5;
    }
    std::string text(V v, const char* f, const char* none = "---") const {
        const auto x = get(v);
        return x ? format(f, *x) : std::string(none);
    }
    std::string degrees(V v) const {
        const auto x = get(v);
        if (!x) return "---";
        int d = static_cast<int>(std::lround(*x)) % 360;
        if (d <= 0) d += 360;
        return format("%03d\xc2\xb0", d);
    }

private:
    const AvionicsPages::Read& read_;
};

std::string hoursMinutes(double seconds) {
    const long m = std::lround(std::max(seconds, 0.0) / 60.0);
    return format("%ld:%02ld", m / 60, m % 60);
}

// a page's line: a small white label above (row 2n-1), the value under it (row 2n)
void labels(Screen& s, int line, const std::string& left, const std::string& right) {
    if (!left.empty()) s.put(line * 2 - 1, 1, left, Color::White, true);
    if (!right.empty()) s.putRight(line * 2 - 1, right + " ", Color::White, true);
}

Color engaged(bool on) { return on ? Color::Green : Color::White; }
}  // namespace

const std::vector<std::string>& AvionicsPages::codes() {
    static const std::vector<std::string> all(std::begin(kCodes), std::end(kCodes));
    return all;
}

void AvionicsPages::reset() {
    page_ = AvionicsPage::Menu;
    last_ = AvionicsPage::Prog;
    scratch_.clear();
    message_.clear();
}

std::optional<int> AvionicsPages::frequencyHz(const std::string& typed, bool nav) {
    double mhz;
    if (digitsOnly(typed)) {  // the first three digits are the MHz: "11830" = 118.30
        if (typed.size() < 3 || typed.size() > 6) return std::nullopt;
        mhz = std::stod(typed.substr(0, 3)) + (typed.size() > 3 ? std::stod("0." + typed.substr(3)) : 0.0);
    } else {
        const size_t dot = typed.find('.');
        if (dot == std::string::npos || dot != 3 || !digitsOnly(typed.substr(0, 3)) || (typed.size() > 4 && !digitsOnly(typed.substr(4))) || typed.size() > 7) return std::nullopt;
        mhz = std::stod(typed);
    }
    // COM: 8.33 kHz channels fall on 5 kHz steps; NAV: 50 kHz
    const long khz = nav ? std::lround(mhz * 20.0) * 50 : std::lround(mhz * 200.0) * 5;
    if (nav ? (khz < 108000 || khz > 117950) : (khz < 118000 || khz > 136990)) return std::nullopt;
    return static_cast<int>(khz * 1000);
}

std::optional<int> AvionicsPages::squawkBco16(const std::string& typed) {
    if (typed.size() != 4 || !std::all_of(typed.begin(), typed.end(), [](char c) { return c >= '0' && c <= '7'; })) return std::nullopt;
    int v = 0;
    for (char c : typed) v = v * 16 + (c - '0');
    return v;
}

std::optional<double> AvionicsPages::baroMillibars(const std::string& typed) {
    double v;
    if (digitsOnly(typed)) {
        if (typed.size() > 4) return std::nullopt;
        const int n = std::stoi(typed);
        if (n >= 2700 && n <= 3200) return n / 100.0 * kHgToMb;  // 2992: inHg
        v = n;
    } else {
        const size_t dot = typed.find('.');
        if (dot == std::string::npos || dot == 0 || !digitsOnly(typed.substr(0, dot)) || (dot + 1 < typed.size() && !digitsOnly(typed.substr(dot + 1))) || typed.size() > 7) return std::nullopt;
        v = std::stod(typed);
        if (v >= 27.0 && v <= 32.0) return v * kHgToMb;
    }
    if (v >= 900 && v <= 1100) return v;
    return std::nullopt;
}

void AvionicsPages::render(Screen& s, const Read& read) const {
    s.clear();
    const Values v(read);
    switch (page_) {
        case AvionicsPage::Menu:
            s.putCentered(0, "MCDU MENU");
            s.put(2, 0, "<AVIONICS", Color::Green);
            s.put(4, 0, "<G1000 PFD");
            s.put(6, 0, "<G1000 MFD");
            s.putCentered(12, "SELECT DESIRED SYSTEM", Color::White, true);
            break;
        case AvionicsPage::Comm:
        case AvionicsPage::Nav: {
            const bool nav = page_ == AvionicsPage::Nav;
            const char* f = nav ? "%.2f" : "%.3f";
            s.putCentered(0, nav ? "RADIO NAV" : "RADIO COMM");
            s.putRight(0, "\xe2\x86\x90\xe2\x86\x92 ");
            labels(s, 1, nav ? "NAV1" : "COM1", nav ? "NAV2" : "COM2");
            s.put(2, 0, v.text(nav ? NavA1 : ComA1, f), Color::Green);
            s.putRight(2, v.text(nav ? NavA2 : ComA2, f), Color::Green);
            labels(s, 2, "STBY", "STBY");
            s.put(4, 0, v.text(nav ? NavS1 : ComS1, f), Color::Cyan);
            s.putRight(4, v.text(nav ? NavS2 : ComS2, f), Color::Cyan);
            s.put(6, 0, "<SWAP");
            s.putRight(6, "SWAP>");
            if (nav) {
                labels(s, 4, "CRS", "CRS");
                s.put(8, 0, v.degrees(Obs1), Color::Cyan);
                s.putRight(8, v.degrees(Obs2), Color::Cyan);
            }
            break;
        }
        case AvionicsPage::Fcu: {
            s.putCentered(0, "FCU");
            labels(s, 1, "SPD", "HDG");
            s.put(2, 0, v.text(ApSpd, "%.0fKT"), Color::Cyan);
            s.putRight(2, v.degrees(ApHdg), Color::Cyan);
            labels(s, 2, "ALT", "V/S");
            s.put(4, 0, v.text(ApAlt, "%.0f"), Color::Cyan);
            const auto vs = v.get(ApVs);
            s.putRight(4, vs ? (std::lround(*vs) == 0 ? std::string("0") : format("%+.0f", *vs)) : std::string("---"), Color::Cyan);
            s.put(6, 0, "<HDG", engaged(v.on(ApHdgOn)));
            s.putRight(6, "ALT>", engaged(v.on(ApAltOn)));
            s.put(8, 0, "<NAV", engaged(v.on(ApNavOn)));
            s.putRight(8, "V/S>", engaged(v.on(ApVsOn)));
            s.put(10, 0, "<APR", engaged(v.on(ApAprOn)));
            s.putRight(10, "FLC>", engaged(v.on(ApFlcOn)));
            // the modes, as the flight mode annunciator: lateral | vertical | AP FD
            const char* lateral = v.on(ApAprOn) ? "APR" : v.on(ApNavOn) ? "NAV" : v.on(ApHdgOn) ? "HDG" : "ROL";
            const char* vertical = v.on(ApAltOn) ? "ALT" : v.on(ApVsOn) ? "VS" : v.on(ApFlcOn) ? "FLC" : "PIT";
            s.put(11, 1, lateral, Color::Green, true);
            s.put(11, 8, "|", Color::White, true);
            s.put(11, 10, vertical, Color::Green, true);
            s.put(11, 15, "|", Color::White, true);
            s.putRight(11, std::string(v.on(ApMaster) ? "AP " : "   ") + (v.on(ApFd) ? "FD " : "   "), Color::White, true);
            s.put(12, 0, "<AP", engaged(v.on(ApMaster)));
            s.putRight(12, "FD>", engaged(v.on(ApFd)));
            break;
        }
        case AvionicsPage::Xpdr: {
            s.putCentered(0, "XPDR");
            labels(s, 1, "CODE", "MODE");
            const auto code = v.get(Squawk);
            s.put(2, 0, code ? squawkText(static_cast<int>(std::lround(*code))) : std::string("----"), Color::Cyan);
            // the modes as a list: the one in use green, the others to select
            const auto state = v.get(XpdrState);
            const long now = state ? std::lround(*state) : -1;
            const struct { const char* name; long state; int row; } modes[] = {{"STBY", 1, 2}, {"ON", 3, 4}, {"ALT", 4, 6}};
            for (const auto& m : modes) s.putRight(m.row, now == m.state ? std::string(m.name) : std::string(m.name) + ">", now == m.state ? Color::Green : Color::Cyan);
            s.put(4, 0, "<IDENT");
            labels(s, 4, "QNH", "");
            if (v.on(BaroStd)) {
                s.put(8, 0, "STD", Color::Green);
                s.putRight(8, "QNH>", Color::Cyan);
            } else {
                s.put(8, 0, v.text(BaroHg, "%.2f"), Color::Cyan);
                s.put(8, 5, v.text(BaroMb, "/%.0f", ""), Color::Cyan, true);
                s.putRight(8, "STD>", Color::Cyan);
            }
            break;
        }
        case AvionicsPage::Fuel: {
            s.putCentered(0, "FUEL PRED");
            const auto fob = v.get(FuelTot), ff = v.get(Flow), ete = v.get(WpEte), gs = v.get(Gs);
            const bool plan = v.on(FplActive);
            labels(s, 1, "TO", "EFOB");
            s.put(1, 9, "ETE", Color::White, true);
            s.put(2, 0, plan ? "NEXT" : "----", Color::Green);
            s.put(2, 9, plan && ete ? hoursMinutes(*ete) : std::string("---"), Color::Green);
            s.putRight(2, plan && ete && fob && ff ? format("%.1f", std::max(*fob - *ff * *ete / 3600.0, 0.0)) : std::string("---"), Color::Green);
            labels(s, 2, "FOB", "FF");
            s.put(4, 0, v.text(FuelTot, "%.1f"), Color::Green);
            s.put(4, 5, "GAL", Color::Green, true);
            s.putRight(4, v.text(Flow, "%.1fGPH"), Color::Green);
            labels(s, 3, "LEFT", "RIGHT");
            s.put(6, 0, v.text(FuelL, "%.1f"), Color::Green);
            s.putRight(6, v.text(FuelR, "%.1f"), Color::Green);
            labels(s, 4, "ENDURANCE", "RANGE");
            const bool flowing = fob && ff && *ff > 0.1;
            s.put(8, 0, flowing ? hoursMinutes(*fob / *ff * 3600.0) : std::string("---"), Color::Green);
            s.putRight(8, flowing && gs && *gs > 20 ? format("%.0fNM", *fob / *ff * *gs) : std::string("---"), Color::Green);
            break;
        }
        case AvionicsPage::Engine: {
            s.putCentered(0, "ENGINE");
            const auto type = v.get(EngType);
            const bool piston = !type || std::lround(*type) == 0;
            labels(s, 1, piston ? "RPM" : "N1", "FF");
            s.put(2, 0, piston ? v.text(Rpm, "%.0f") : v.text(N1, "%.1f%%"), Color::Green);
            s.putRight(2, v.text(Flow, "%.1fGPH"), Color::Green);
            labels(s, 2, "OIL T", "OIL P");
            s.put(4, 0, v.text(OilT, "%.0f\xc2\xb0" "F"), Color::Green);
            s.putRight(4, v.text(OilP, "%.0fPSI"), Color::Green);
            if (piston) {
                labels(s, 3, "EGT", "CHT");
                s.put(6, 0, v.text(Egt, "%.0f\xc2\xb0" "F"), Color::Green);
                s.putRight(6, v.text(Cht, "%.0f\xc2\xb0" "F"), Color::Green);
            }
            break;
        }
        case AvionicsPage::Prog: {
            s.putCentered(0, "PROG");
            const bool plan = v.on(FplActive);
            labels(s, 1, "TO", "BRG /DIST");
            s.put(2, 0, plan ? "NEXT" : "----", Color::Green);
            s.putRight(2, plan ? v.degrees(WpBrg) + "/" + v.text(WpDist, "%.1fNM") : std::string("---/----"), Color::Green);
            labels(s, 2, "DTK", "XTK");
            s.put(4, 0, plan ? v.degrees(WpDtk) : std::string("---"), Color::Green);
            s.putRight(4, plan ? v.text(WpXtk, "%.1fNM") : std::string("---"), Color::Green);
            labels(s, 3, "GS", "TAS");
            s.put(6, 0, v.text(Gs, "%.0fKT"), Color::Green);
            s.putRight(6, v.text(Tas, "%.0fKT"), Color::Green);
            labels(s, 4, "WIND", "OAT");
            const auto wk = v.get(WindKt);
            s.put(8, 0, wk ? v.degrees(WindDir) + format("/%.0f", *wk) : std::string("---/--"), Color::Green);
            s.putRight(8, v.text(Oat, "%+.0f\xc2\xb0"), Color::Green);
            labels(s, 5, "ALT", "");
            s.put(10, 0, v.text(AltInd, "%.0f"), Color::Green);
            break;
        }
        case AvionicsPage::Pfd:
        case AvionicsPage::Mfd: {
            s.putCentered(0, page_ == AvionicsPage::Pfd ? "G1000 PFD" : "G1000 MFD");
            for (int i = 0; i < 6; ++i) {
                s.put(2 + i * 2, 0, format("<%d", i + 1), Color::Cyan);
                s.putRight(2 + i * 2, format("%d>", i + 7), Color::Cyan);
            }
            // what the other keys do here, between the softkeys (the softkey labels are the G1000's own: it does not tell them)
            static const char* const legend[][2] = {{"DIR", "D\xe2\x86\x92"}, {"F-PLN", "FPL"}, {"PERF", "PROC"}, {"DATA", "MENU"}, {"CLR", "CLR"},
                                                    {"OVFY", "ENT"}, {"\xe2\x86\x90\xe2\x86\x92", "FMS OUTER"}, {"\xe2\x86\x91\xe2\x86\x93", "FMS INNER"},
                                                    {"+/-", "CRSR"}, {"MENU", "EXIT"}};
            for (int i = 0; i < 10; ++i) {
                s.put(1 + i, 4, legend[i][0], Color::White, true);
                s.put(1 + i, 10, legend[i][1], Color::White, true);
            }
            break;
        }
    }
    s.put(13, 0, message_.empty() ? scratch_ : message_);
}

std::vector<std::string> AvionicsPages::onKey(Key key, const Read& read) {
    const bool hadMessage = !message_.empty();
    message_.clear();
    const bool g1000 = page_ == AvionicsPage::Pfd || page_ == AvionicsPage::Mfd;
    auto open = [this](AvionicsPage p) {
        page_ = p;
        if (p != AvionicsPage::Menu && p != AvionicsPage::Pfd && p != AvionicsPage::Mfd) last_ = p;
    };
    if (key == Key::Menu) {  // MCDU MENU, from anywhere
        open(AvionicsPage::Menu);
        return {};
    }
    if (g1000) {
        auto out = g1000Key(key);
        if (!out.empty()) return out;
    }
    switch (key) {  // the function keys open their pages
        case Key::Rad: open(page_ == AvionicsPage::Comm ? AvionicsPage::Nav : AvionicsPage::Comm); return {};
        case Key::Perf: open(AvionicsPage::Fcu); return {};
        case Key::Fuel: open(AvionicsPage::Fuel); return {};
        case Key::Prog: open(AvionicsPage::Prog); return {};
        case Key::Atc: open(AvionicsPage::Xpdr); return {};
        case Key::Data: open(AvionicsPage::Engine); return {};
        case Key::LeftArrow:
        case Key::RightArrow:  // the radio pages are one page and its next
            if (page_ == AvionicsPage::Comm || page_ == AvionicsPage::Nav) open(page_ == AvionicsPage::Comm ? AvionicsPage::Nav : AvionicsPage::Comm);
            return {};
        default: break;
    }
    if (g1000) return {};

    // the scratchpad
    const auto k = static_cast<int>(key);
    char typed = 0;
    if (k >= static_cast<int>(Key::D1) && k <= static_cast<int>(Key::D9)) typed = static_cast<char>('1' + (k - static_cast<int>(Key::D1)));
    else if (key == Key::D0) typed = '0';
    else if (key == Key::Dot) typed = '.';
    else if (key == Key::Slash) typed = '/';
    else if (key == Key::Space) typed = ' ';
    else if (k >= static_cast<int>(Key::A) && k <= static_cast<int>(Key::Z)) typed = static_cast<char>('A' + (k - static_cast<int>(Key::A)));
    if (typed) {
        if (static_cast<int>(scratch_.size()) < kScratchMax) scratch_ += typed;
        return {};
    }
    if (key == Key::PlusMinus) {
        if (!scratch_.empty() && scratch_[0] == '-') scratch_[0] = '+';
        else if (!scratch_.empty() && scratch_[0] == '+') scratch_[0] = '-';
        else if (static_cast<int>(scratch_.size()) < kScratchMax) scratch_.insert(scratch_.begin(), '-');
        return {};
    }
    if (key == Key::Clr) {  // the message first, then the last character
        if (!hadMessage && !scratch_.empty()) scratch_.pop_back();
        return {};
    }
    if (k >= static_cast<int>(Key::L1) && k <= static_cast<int>(Key::L6)) return lineKey(k - static_cast<int>(Key::L1) + 1, false, read);
    if (k >= static_cast<int>(Key::R1) && k <= static_cast<int>(Key::R6)) return lineKey(k - static_cast<int>(Key::R1) + 1, true, read);
    return {};
}

std::vector<std::string> AvionicsPages::lineKey(int line, bool right, const Read& read) {
    const Values v(read);
    const std::string typed = scratch_;
    std::vector<std::string> out;
    // A field: with an empty scratchpad its value is copied there (as on the aircraft's MCDU); with a value the value is checked
    // and sent, else a message and the scratchpad kept.
    auto field = [&](const std::string& shown, auto parse, auto send) {
        if (typed.empty()) {
            if (shown.empty()) message_ = "NOT ALLOWED";
            else scratch_ = shown;
            return;
        }
        const auto x = parse(typed);
        if (!x) {
            const bool numeric = std::all_of(typed.begin(), typed.end(), [](unsigned char c) { return std::isdigit(c) || c == '.' || c == '+' || c == '-'; });
            message_ = numeric ? "ENTRY OUT OF RANGE" : "FORMAT ERROR";
            return;
        }
        out.push_back(send(*x));
        scratch_.clear();
    };
    auto number = [](long lo, long hi) {
        return [lo, hi](const std::string& t) -> std::optional<long> {
            const auto n = wholeNumber(t);
            if (!n || *n < lo || *n > hi) return std::nullopt;
            return *n;
        };
    };
    auto plain = [&](V x) {  // a value as one would type it
        const auto n = v.get(x);
        return n ? format("%.0f", *n) : std::string();
    };

    switch (page_) {
        case AvionicsPage::Menu:
            if (right) return {};
            if (line == 1) page_ = last_;
            else if (line == 2) page_ = AvionicsPage::Pfd;
            else if (line == 3) page_ = AvionicsPage::Mfd;
            return {};
        case AvionicsPage::Comm:
        case AvionicsPage::Nav: {
            const bool nav = page_ == AvionicsPage::Nav;
            const int unit = right ? 1 : 0;
            if (line == 1 || line == 2) {  // active, standby
                static const char* const events[2][2][2] = {{{"COM_RADIO_SET_HZ", "COM2_RADIO_SET_HZ"}, {"COM_STBY_RADIO_SET_HZ", "COM2_STBY_RADIO_SET_HZ"}},
                                                           {{"NAV1_RADIO_SET_HZ", "NAV2_RADIO_SET_HZ"}, {"NAV1_STBY_SET_HZ", "NAV2_STBY_SET_HZ"}}};
                static const V shown[2][2][2] = {{{ComA1, ComA2}, {ComS1, ComS2}}, {{NavA1, NavA2}, {NavS1, NavS2}}};
                const char* event = events[nav][line - 1][unit];
                field(v.text(shown[nav][line - 1][unit], nav ? "%.2f" : "%.3f", ""), [nav](const std::string& t) { return frequencyHz(t, nav); },
                      [event](int hz) { return calc(event, hz); });
            } else if (line == 3) {
                static const char* const swaps[2][2] = {{"COM_STBY_RADIO_SWAP", "COM2_RADIO_SWAP"}, {"NAV1_RADIO_SWAP", "NAV2_RADIO_SWAP"}};
                out.push_back(calc(swaps[nav][unit]));
            } else if (line == 4 && nav) {
                const char* event = right ? "VOR2_SET" : "VOR1_SET";
                field(plain(right ? Obs2 : Obs1), number(0, 360), [event](long c) { return calc(event, c); });
            }
            return out;
        }
        case AvionicsPage::Fcu:
            switch (line * 2 + (right ? 1 : 0)) {
                case 2: field(plain(ApSpd), number(40, 400), [](long n) { return calc("AP_SPD_VAR_SET", n); }); break;
                case 3: field(plain(ApHdg), number(0, 360), [](long n) { return calc("HEADING_BUG_SET", n); }); break;
                case 4: field(plain(ApAlt), number(0, 50000), [](long n) { return calc("AP_ALT_VAR_SET_ENGLISH", n); }); break;
                case 5: field(plain(ApVs), number(-9900, 9900), [](long n) { return calc("AP_VS_VAR_SET_ENGLISH", n); }); break;
                case 6: out.push_back(calc("AP_PANEL_HEADING_HOLD")); break;
                case 7: out.push_back(calc("AP_PANEL_ALTITUDE_HOLD")); break;
                case 8: out.push_back(calc("AP_NAV1_HOLD")); break;
                case 9: out.push_back(calc("AP_PANEL_VS_HOLD")); break;
                case 10: out.push_back(calc("AP_APR_HOLD")); break;
                case 11: out.push_back(calc("FLIGHT_LEVEL_CHANGE")); break;
                case 12: out.push_back(calc("AP_MASTER")); break;
                case 13: out.push_back(calc("TOGGLE_FLIGHT_DIRECTOR")); break;
                default: break;
            }
            return out;
        case AvionicsPage::Xpdr:
            if (right && line <= 3) {  // STBY, ON, ALT
                static const int states[] = {1, 3, 4};
                out.push_back(format("%d (>A:TRANSPONDER STATE:1, Number)", states[line - 1]));
            } else if (!right && line == 1) {
                const auto c = v.get(Squawk);
                field(c ? squawkText(static_cast<int>(std::lround(*c))) : std::string(), squawkBco16, [](int bco) { return calc("XPNDR_SET", bco); });
            } else if (!right && line == 2) {
                out.push_back(calc("XPNDR_IDENT_ON"));
            } else if (!right && line == 4) {
                field(v.on(BaroStd) ? std::string() : v.text(BaroHg, "%.2f", ""), baroMillibars, [](double mb) { return calc("KOHLSMAN_SET", std::lround(mb * 16.0)); });
            } else if (right && line == 4) {
                // STD; back to the local setting the way the G1000's knob does it (its STD event only sets STD)
                if (v.on(BaroStd)) out.push_back("(>H:AS1000_PFD_BARO_INC) (>H:AS1000_PFD_BARO_DEC)");
                else out.push_back(calc("BAROMETRIC_STD_PRESSURE"));
            }
            return out;
        default:
            return {};
    }
}

std::vector<std::string> AvionicsPages::g1000Key(Key key) {
    const std::string unit = page_ == AvionicsPage::Mfd ? "AS1000_MFD_" : "AS1000_PFD_";
    auto h = [&](const std::string& event) { return std::vector<std::string>{"(>H:" + unit + event + ")"}; };
    const int k = static_cast<int>(key);
    if (k >= static_cast<int>(Key::L1) && k <= static_cast<int>(Key::L6)) return h("SOFTKEYS_" + std::to_string(k - static_cast<int>(Key::L1) + 1));
    if (k >= static_cast<int>(Key::R1) && k <= static_cast<int>(Key::R6)) return h("SOFTKEYS_" + std::to_string(k - static_cast<int>(Key::R1) + 7));
    switch (key) {
        case Key::Dir: return h("DIRECTTO");
        case Key::Fpln: return h("FPL_Push");
        case Key::Perf: return h("PROC_Push");
        case Key::Data: return h("MENU_Push");
        case Key::Clr: return h("CLR");
        case Key::Ovfy: return h("ENT_Push");
        case Key::UpArrow: return h("FMS_Lower_INC");
        case Key::DownArrow: return h("FMS_Lower_DEC");
        case Key::RightArrow: return h("FMS_Upper_INC");
        case Key::LeftArrow: return h("FMS_Upper_DEC");
        case Key::PlusMinus: return h("FMS_Upper_PUSH");
        default: return {};
    }
}

}  // namespace lr::mcdu
