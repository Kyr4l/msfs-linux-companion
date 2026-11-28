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
    Gs, Tas, WindDir, WindKt, AltInd, Oat, WpDist, WpBrg, WpEte, WpDtk, WpXtk, FplActive, FuelDensity, WpCount, WpIndex, HvBatt, LvBatt,
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
    "(A:FUEL WEIGHT PER GALLON, Pounds)", "(A:GPS FLIGHT PLAN WP COUNT, Number)", "(A:GPS FLIGHT PLAN WP INDEX, Number)",
    "(A:ELECTRICAL BATTERY ESTIMATED CAPACITY PCT:3, Percent)", "(A:ELECTRICAL BATTERY VOLTAGE:1, Volts)",
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

// Fuel in the unit of the settings: "gal", "l", "kg", or a volume with the mass small on the label line ("gal_kg", "l_kg").
// The mass needs the fuel's weight per gallon (avgas about 6, jet fuel about 6.7 lb); without it a mass shows "---".
struct FuelText {
    std::string value, extra;
};
constexpr double kLitresPerGallon = 3.78541, kKgPerLb = 0.453592;
FuelText fuelText(std::optional<double> gallons, std::optional<double> lbPerGallon, const std::string& unit, bool flow) {
    if (!gallons) return {"---", ""};
    auto kg = [&]() -> std::string {
        if (!lbPerGallon || *lbPerGallon <= 0.1) return "---";
        return format("%.0f", *gallons * *lbPerGallon * kKgPerLb) + (flow ? "KG/H" : "KG");
    };
    const bool litres = unit == "l" || unit == "l_kg";
    std::string volume = litres ? format(*gallons * kLitresPerGallon >= 100 ? "%.0f" : "%.1f", *gallons * kLitresPerGallon) + (flow ? "L/H" : "L")
                                : format("%.1f", *gallons) + (flow ? "GPH" : "GAL");
    if (unit == "kg") return {kg(), ""};
    if (unit == "gal_kg" || unit == "l_kg") return {volume, " " + kg()};
    return {volume, ""};
}

constexpr const char* kSlew = "\xe2\x86\x90\xe2\x86\x92";  // ←→: more pages of this kind

// A temperature in the unit of the settings; with "both", the other unit goes small on the label line.
struct Temperature {
    std::string value, extra;
};
Temperature temperature(std::optional<double> x, bool readInFahrenheit, bool engine, bool sign, const std::string& unit) {
    if (!x) return {"---", ""};
    const double c = readInFahrenheit ? (*x - 32.0) * 5.0 / 9.0 : *x, f = readInFahrenheit ? *x : *x * 9.0 / 5.0 + 32.0;
    auto text = [sign](double v, const char* u) { return format(sign ? "%+.0f\xc2\xb0" : "%.0f\xc2\xb0", v) + u; };
    if (unit == "both") return {text(c, "C"), " " + text(f, "F")};
    const bool fahrenheit = unit == "f" || (unit != "c" && engine);
    return {fahrenheit ? text(f, "F") : text(c, "C"), ""};
}
}  // namespace

const std::vector<std::string>& AvionicsPages::codes() {
    static const std::vector<std::string> all(std::begin(kCodes), std::end(kCodes));
    return all;
}

void AvionicsPages::reset() {
    page_ = AvionicsPage::Menu;
    last_ = AvionicsPage::Prog;
    scratch_.clear();
    g1000Back_ = false;
    stdByPage_ = false;
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
            if (g3000_) {
                s.put(4, 0, "<GTC");
                s.put(4, 5, "(TOUCH CONTROLLERS)", Color::White, true);
            } else {
                s.put(4, 0, "<G1000 PFD");
                s.put(6, 0, "<G1000 MFD");
            }
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
            if (baroStd(v.get(BaroStd), v.get(BaroMb))) {
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
            const auto density = v.get(FuelDensity);
            auto fuel = [&](std::optional<double> gal, bool flow = false) { return fuelText(gal, density, fuelUnit_, flow); };
            labels(s, 1, "TO", "EFOB");
            s.put(1, 9, "ETE", Color::White, true);
            s.put(2, 0, plan ? "NEXT" : "----", Color::Green);
            s.put(2, 9, plan && ete ? hoursMinutes(*ete) : std::string("---"), Color::Green);
            const std::optional<double> efob = plan && ete && fob && ff ? std::optional<double>(std::max(*fob - *ff * *ete / 3600.0, 0.0)) : std::nullopt;
            s.putRight(2, fuel(efob).value, Color::Green);
            const FuelText total = fuel(fob), flowText = fuel(ff, true), left = fuel(v.get(FuelL)), right = fuel(v.get(FuelR));
            labels(s, 2, "FOB" + total.extra, "FF" + flowText.extra);
            s.put(4, 0, total.value, Color::Green);
            s.putRight(4, flowText.value, Color::Green);
            labels(s, 3, "LEFT" + left.extra, "RIGHT" + right.extra);
            s.put(6, 0, left.value, Color::Green);
            s.putRight(6, right.value, Color::Green);
            labels(s, 4, "ENDURANCE", "RANGE");
            const bool flowing = fob && ff && *ff > 0.1;
            s.put(8, 0, flowing ? hoursMinutes(*fob / *ff * 3600.0) : std::string("---"), Color::Green);
            s.putRight(8, flowing && gs && *gs > 20 ? format("%.0fNM", *fob / *ff * *gs) : std::string("---"), Color::Green);
            if (electric_) {  // the traction packs (ES-30: batteries 3 to 8, all at the same charge) and the 28 V battery
                labels(s, 5, "HV BATT", "LV BATT");
                s.put(10, 0, v.text(HvBatt, "%.0f%%"), Color::Green);
                s.putRight(10, v.text(LvBatt, "%.1fV"), Color::Green);
            }
            break;
        }
        case AvionicsPage::Engine: {
            s.putCentered(0, "ENGINE");
            const auto type = v.get(EngType);
            const bool piston = !type || std::lround(*type) == 0;
            const FuelText flow = fuelText(v.get(Flow), v.get(FuelDensity), fuelUnit_, true);
            labels(s, 1, piston ? "RPM" : "N1", "FF" + flow.extra);
            s.put(2, 0, piston ? v.text(Rpm, "%.0f") : v.text(N1, "%.1f%%"), Color::Green);
            s.putRight(2, flow.value, Color::Green);
            const Temperature oil = temperature(v.get(OilT), true, true, false, tempUnit_);
            labels(s, 2, "OIL T" + oil.extra, "OIL P");
            s.put(4, 0, oil.value, Color::Green);
            s.putRight(4, v.text(OilP, "%.0fPSI"), Color::Green);
            if (piston) {
                const Temperature egt = temperature(v.get(Egt), true, true, false, tempUnit_), cht = temperature(v.get(Cht), true, true, false, tempUnit_);
                labels(s, 3, "EGT" + egt.extra, "CHT" + cht.extra);
                s.put(6, 0, egt.value, Color::Green);
                s.putRight(6, cht.value, Color::Green);
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
            const Temperature oat = temperature(v.get(Oat), false, false, true, tempUnit_);
            labels(s, 4, "WIND", "OAT" + oat.extra);
            const auto wk = v.get(WindKt);
            s.put(8, 0, wk ? v.degrees(WindDir) + format("/%.0f", *wk) : std::string("---/--"), Color::Green);
            s.putRight(8, oat.value, Color::Green);
            labels(s, 5, "ALT", "");
            s.put(10, 0, v.text(AltInd, "%.0f"), Color::Green);
            break;
        }
        case AvionicsPage::Pfd:
        case AvionicsPage::Mfd: {  // the G1000's functions by name
            const bool mfd = page_ == AvionicsPage::Mfd;
            s.putCentered(0, mfd ? "G1000 MFD" : "G1000 PFD");
            s.putRight(0, kSlew);
            labels(s, 1, "FMS", "FMS");
            s.put(2, 0, "<DIRECT TO", Color::Cyan);
            s.putRight(2, "ENTER>", Color::Cyan);
            s.put(4, 0, "<FLIGHT PLAN", Color::Cyan);
            s.putRight(4, "CLEAR>", Color::Cyan);
            s.put(6, 0, "<PROCEDURES", Color::Cyan);
            s.putRight(6, "MENU>", Color::Cyan);
            labels(s, 4, "FMS KNOB", "RANGE");
            s.put(8, 0, "<OUTER  +/-", Color::Cyan);
            s.putRight(8, mfd ? "ZOOM IN>" : "IN>", Color::Cyan);
            s.put(10, 0, "<INNER  +/-", Color::Cyan);
            s.putRight(10, mfd ? "ZOOM OUT>" : "OUT>", Color::Cyan);
            s.put(12, 0, "<CURSOR");
            s.putRight(12, "SOFTKEYS>");
            break;
        }
        case AvionicsPage::Fpln: {  // the leg the GPS flies: the whole plan is the G1000's own, not readable from outside
            s.putCentered(0, "F-PLN");
            const bool plan = v.on(FplActive);
            const auto count = v.get(WpCount), index = v.get(WpIndex);
            if (plan && count && *count > 0 && index) s.putRight(0, format("%.0f/%.0f", *index, *count), Color::White, true);
            labels(s, 1, "FROM", "");
            s.put(2, 0, plan && !prevWp_.empty() ? prevWp_.substr(0, 11) : std::string("----"), Color::Green);
            labels(s, 2, "TO", "BRG /DIST");
            s.put(4, 0, plan && !nextWp_.empty() ? nextWp_.substr(0, 11) : std::string("----"), Color::Green);
            s.putRight(4, plan ? v.degrees(WpBrg) + "/" + v.text(WpDist, "%.1fNM") : std::string("---/----"), Color::Green);
            labels(s, 3, "DTK", "ETE");
            const auto ete = v.get(WpEte);
            s.put(6, 0, plan ? v.degrees(WpDtk) : std::string("---"), Color::Green);
            s.putRight(6, plan && ete ? hoursMinutes(*ete) : std::string("---"), Color::Green);
            labels(s, 4, "XTK", "GS");
            s.put(8, 0, plan ? v.text(WpXtk, "%.1fNM") : std::string("---"), Color::Green);
            s.putRight(8, v.text(Gs, "%.0fKT"), Color::Green);
            s.put(10, 0, "<G1000 FPL");
            s.putRight(10, "DIRECT TO>");
            if (pad_) {
                labels(s, 6, "KEYS TO G1000", "");
                const bool on = padKeys_;
                s.put(12, 0, on ? "ON" : "ON/", on ? Color::Green : Color::Cyan, !on);
                s.put(12, on ? 2 : 3, on ? "/OFF" : "OFF", on ? Color::Cyan : Color::Green, on);
            }
            break;
        }
        case AvionicsPage::Gtc: {  // a G3000 touch controller: its three softkeys and its knobs
            if (gtcVertical_) {  // the ES-30: two controllers, three knobs each, no softkeys
                s.putCentered(0, gtc_ == 1 ? "LEFT GTC" : "RIGHT GTC");
                s.putRight(0, kSlew);
                labels(s, 1, "KNOB 1", "FREQ KNOB");
                s.put(2, 0, "<TURN", Color::Cyan);
                s.putRight(2, "OUTER>", Color::Cyan);
                s.put(4, 0, "<PUSH", Color::Cyan);
                s.putRight(4, "INNER>", Color::Cyan);
                labels(s, 3, "KNOB 2", "");
                s.put(6, 0, "<TURN", Color::Cyan);
                s.putRight(6, "PUSH>", Color::Cyan);
                s.put(8, 0, "<PUSH", Color::Cyan);
                s.put(12, 1, "+/- FIRST: TURN BACK", Color::White, true);
                break;
            }
            s.putCentered(0, format("GTC %d", gtc_));
            s.putRight(0, kSlew);
            labels(s, 1, "SOFTKEYS", "FREQ KNOB");
            s.put(2, 0, "<PFD", Color::Cyan);
            s.putRight(2, "OUTER>", Color::Cyan);
            s.put(4, 0, "<MFD", Color::Cyan);
            s.putRight(4, "INNER>", Color::Cyan);
            s.put(6, 0, "<NAV/COM", Color::Cyan);
            s.putRight(6, "SWAP>", Color::Cyan);
            labels(s, 4, "MAP KNOB", "CHECKLIST");
            s.put(8, 0, "<TURN", Color::Cyan);
            s.putRight(8, "TURN>", Color::Cyan);
            s.put(10, 0, "<PUSH", Color::Cyan);
            s.putRight(10, "PUSH>", Color::Cyan);
            s.put(12, 1, "+/- FIRST: TURN BACK", Color::White, true);
            break;
        }
        case AvionicsPage::PfdKeys:
        case AvionicsPage::MfdKeys: {  // the 12 softkeys, in the order they sit under the screen; their labels are on the G1000
            const char* unit = page_ == AvionicsPage::MfdKeys ? "MFD" : "PFD";
            s.putCentered(0, std::string(unit) + " SOFTKEYS");
            s.putRight(0, kSlew);
            for (int i = 0; i < 6; ++i) {
                labels(s, i + 1, format("SOFTKEY %d", i + 1), format("SOFTKEY %d", i + 7));
                s.put(2 + i * 2, 0, format("<%d", i + 1), Color::Cyan);
                s.putRight(2 + i * 2, format("%d>", i + 7), Color::Cyan);
            }
            s.put(7, 8, "LABELS ON", Color::White, true);
            s.put(8, 8, std::string("THE ") + unit, Color::White, true);
            break;
        }
    }
    if (message_.empty() && g1000Back_ && (isG1000() || page_ == AvionicsPage::Gtc)) {
        s.put(13, 0, "-");
        return;
    }
    s.put(13, 0, message_.empty() ? scratch_ : message_);
}

std::vector<std::string> AvionicsPages::onKey(Key key, const Read& read) {
    const bool hadMessage = !message_.empty();
    message_.clear();
    const bool g1000 = isG1000();
    auto open = [this](AvionicsPage p) {
        page_ = p;
        g1000Back_ = false;
        if (p != AvionicsPage::Menu && !isG1000()) last_ = p;
    };
    if (key == Key::Menu) {  // MCDU MENU, from anywhere
        open(AvionicsPage::Menu);
        return {};
    }
    if (page_ == AvionicsPage::Fpln) {
        auto out = padKey(key);
        if (!out.empty()) return out;
    }
    if (page_ == AvionicsPage::Gtc) {
        auto out = gtcKey(key);
        const int kk = static_cast<int>(key);
        const bool consumed = !out.empty() || (kk >= static_cast<int>(Key::L1) && kk <= static_cast<int>(Key::R6)) || key == Key::PlusMinus ||
                              key == Key::LeftArrow || key == Key::RightArrow;
        if (consumed) return out;
    }
    if (g1000) {
        if (key == Key::LeftArrow || key == Key::RightArrow) {  // PFD, PFD SOFTKEYS, MFD, MFD SOFTKEYS
            static const AvionicsPage kOrder[4] = {AvionicsPage::Pfd, AvionicsPage::PfdKeys, AvionicsPage::Mfd, AvionicsPage::MfdKeys};
            int i = 0;
            while (kOrder[i] != page_) ++i;
            open(kOrder[(i + (key == Key::RightArrow ? 1 : 3)) % 4]);
            return {};
        }
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
        case Key::Fpln: open(AvionicsPage::Fpln); return {};
        case Key::LeftArrow:
        case Key::RightArrow:  // the radio pages are one page and its next
            if (page_ == AvionicsPage::Comm || page_ == AvionicsPage::Nav) open(page_ == AvionicsPage::Comm ? AvionicsPage::Nav : AvionicsPage::Comm);
            return {};
        default: break;
    }
    if (g1000 || page_ == AvionicsPage::Gtc) return {};

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
            else if (line == 2) page_ = g3000_ ? AvionicsPage::Gtc : AvionicsPage::Pfd;
            else if (line == 3 && !g3000_) page_ = AvionicsPage::Mfd;
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
                // in STD the G1000 keeps its display on STD whatever K:KOHLSMAN_SET says: QNH> first
                if (baroStd(v.get(BaroStd), v.get(BaroMb)) && !typed.empty()) message_ = "NOT ALLOWED";
                else field(baroStd(v.get(BaroStd), v.get(BaroMb)) ? std::string() : v.text(BaroHg, "%.2f", ""), baroMillibars, [](double mb) { return calc("KOHLSMAN_SET", std::lround(mb * 16.0)); });
            } else if (right && line == 4) {
                // STD, and back: the G1000's STD event only sets STD, and one click of its baro knob leaves STD and restores the local
                // setting it remembered (checked on the C400: a second click would step it 1 hPa further)
                if (baroStd(v.get(BaroStd), v.get(BaroMb))) {
                    out.push_back("(>H:AS1000_PFD_BARO_INC)");
                    stdByPage_ = false;
                } else {
                    out.push_back(calc("BAROMETRIC_STD_PRESSURE"));
                    stdByPage_ = true;
                }
            }
            return out;
        default:
            return {};
    }
}

// STD: the sim's flag, or STD set from this page while the setting is still 29.92 (the G1000 of the C400 shows STD without raising the flag)
bool AvionicsPages::baroStd(std::optional<double> flag, std::optional<double> mb) const {
    if (flag && *flag > 0.5) return true;
    return stdByPage_ && mb && std::fabs(*mb - 1013.25) < 0.3;
}

// F-PLN: its line keys, and with KEYS TO G1000 on, the keys that type into the G1000's control pad (the aircraft's input events)
std::vector<std::string> AvionicsPages::padKey(Key key) {
    const int k = static_cast<int>(key);
    auto pad = [this](const std::string& name, const std::string& said) {
        message_ = "G1000 " + said;
        return std::vector<std::string>{"1 (>B:INSTRUMENT_AS1000_CONTROLPAD_" + name + "_Set)"};
    };
    if (key == Key::L5) {
        message_ = "SENT G1000 FPL";
        return {"(>H:AS1000_MFD_FPL_Push)"};
    }
    if (key == Key::R5) {
        message_ = "SENT DIRECT TO";
        return {"(>H:AS1000_MFD_DIRECTTO)"};
    }
    if (key == Key::L6 && pad_) {
        padKeys_ = !padKeys_;
        return {};
    }
    if (!padKeys_) return {};
    if (k >= static_cast<int>(Key::A) && k <= static_cast<int>(Key::Z)) {
        const std::string letter(1, static_cast<char>('A' + (k - static_cast<int>(Key::A))));
        return pad(letter, letter);
    }
    if (k >= static_cast<int>(Key::D1) && k <= static_cast<int>(Key::D9)) {
        const std::string digit(1, static_cast<char>('1' + (k - static_cast<int>(Key::D1))));
        return pad(digit, digit);
    }
    switch (key) {
        case Key::D0: return pad("0", "0");
        case Key::Dot: return pad("DOT", ".");
        case Key::Space: return pad("SPC", "SPACE");
        case Key::PlusMinus: return pad("PLUSMINUS", "+/-");
        case Key::Clr: return pad("BKSP", "BKSP");
        case Key::Ovfy: return pad("PUSH_ENTER", "ENT");
        case Key::Dir: return pad("PUSH_DIRECTTO", "DIRECT TO");
        case Key::Menu: return {};  // MCDU MENU still leaves
        // the FMS knob of the MFD, where the flight plan is edited
        case Key::RightArrow: message_ = "G1000 OUTER +"; return {"(>H:AS1000_MFD_FMS_Upper_INC)"};
        case Key::LeftArrow: message_ = "G1000 OUTER -"; return {"(>H:AS1000_MFD_FMS_Upper_DEC)"};
        case Key::UpArrow: message_ = "G1000 INNER +"; return {"(>H:AS1000_MFD_FMS_Lower_INC)"};
        case Key::DownArrow: message_ = "G1000 INNER -"; return {"(>H:AS1000_MFD_FMS_Lower_DEC)"};
        default: return {};
    }
}

std::vector<std::string> AvionicsPages::gtcKey(Key key) {
    if (key == Key::LeftArrow || key == Key::RightArrow) {  // GTC 1, 2, 3
        gtc_ = (gtc_ - 1 + (key == Key::RightArrow ? 1 : gtcCount() - 1)) % gtcCount() + 1;
        g1000Back_ = false;
        return {};
    }
    if (key == Key::PlusMinus) {
        g1000Back_ = !g1000Back_;
        return {};
    }
    // the aircraft's input events: a push takes 1, a knob turns one click with +1 or -1 (the sign picks the direction, G3000 GTC.xml)
    auto event = [this](const std::string& name, int value, const std::string& said) {
        message_ = "SENT " + said;
        return std::vector<std::string>{(value < 0 ? std::string("1 neg") : std::string("1")) + " (>B:" + name + "_Set)"};
    };
    const std::string tsc = format("AS3000_TSC_%d_", gtc_);
    const int k = static_cast<int>(key);
    const bool left = k >= static_cast<int>(Key::L1) && k <= static_cast<int>(Key::L6), right = k >= static_cast<int>(Key::R1) && k <= static_cast<int>(Key::R6);
    if (!left && !right) return {};
    const int line = left ? k - static_cast<int>(Key::L1) + 1 : k - static_cast<int>(Key::R1) + 1;
    const bool back = g1000Back_;
    g1000Back_ = false;
    const int turn = back ? -1 : 1;
    const std::string dir = back ? " -" : " +";
    if (gtcVertical_) {
        // INSTRUMENT_G5000_CONTROL_<n>[_INNER][_PUSH]_G5000_SECONDARY_SCREEN_<side>; the sim cuts the dual knob's push name at 63 characters
        const std::string side = gtc_ == 1 ? "LEFT" : "RIGHT";
        auto knob = [&](const std::string& part) { return "INSTRUMENT_G5000_CONTROL_" + part + "_G5000_SECONDARY_SCREEN_" + side; };
        if (left) {
            switch (line) {
                case 1: return event(knob("1"), turn, "KNOB 1" + dir);
                case 2: return event(knob("1_PUSH"), 1, "KNOB 1 PUSH");
                case 3: return event(knob("2"), turn, "KNOB 2" + dir);
                case 4: return event(knob("2_PUSH"), 1, "KNOB 2 PUSH");
                default: return {};
            }
        }
        switch (line) {
            case 1: return event(knob("3"), turn, "OUTER" + dir);
            case 2: return event(knob("3_INNER"), turn, "INNER" + dir);
            case 3: return event(knob("3_INNER_PUSH").substr(0, 63), 1, "FREQ PUSH");
            default: return {};
        }
    }
    if (left) {
        switch (line) {
            case 1: return event(tsc + "SOFTKEY_1", 1, "PFD");
            case 2: return event(tsc + "SOFTKEY_2", 1, "MFD");
            case 3: return event(tsc + "SOFTKEY_3", 1, "NAV/COM");
            case 4: return event(tsc + "MULTIFUNCTION_KNOB", turn, "MAP KNOB" + dir);
            case 5: return event(tsc + "MULTIFUNCTION_KNOB_BUTTON", 1, "MAP PUSH");
            default: return {};
        }
    }
    switch (line) {
        case 1: return event(tsc + "FREQUENCY_KNOB_MHZ", turn, "OUTER" + dir);
        case 2: return event(tsc + "FREQUENCY_KNOB_KHZ", turn, "INNER" + dir);
        case 3: return event(tsc + "FREQUENCY_SWAP", 1, "SWAP");
        case 4: return event("AS3000_CHECKLIST_WHEEL", turn, "CHECKLIST" + dir);
        case 5: return event("AS3000_CHECKLIST_WHEEL_PUSH", 1, "CHECKLIST PUSH");
        default: return {};
    }
}

std::vector<std::string> AvionicsPages::g1000Key(Key key) {
    const bool mfd = page_ == AvionicsPage::Mfd || page_ == AvionicsPage::MfdKeys;
    const std::string unit = mfd ? "AS1000_MFD_" : "AS1000_PFD_";
    auto h = [&](const std::string& event, const std::string& name) {
        message_ = "SENT " + name;
        return std::vector<std::string>{"(>H:" + unit + event + ")"};
    };
    const int k = static_cast<int>(key);
    const bool left = k >= static_cast<int>(Key::L1) && k <= static_cast<int>(Key::L6), right = k >= static_cast<int>(Key::R1) && k <= static_cast<int>(Key::R6);
    const int line = left ? k - static_cast<int>(Key::L1) + 1 : right ? k - static_cast<int>(Key::R1) + 1 : 0;
    if (page_ == AvionicsPage::PfdKeys || page_ == AvionicsPage::MfdKeys) {
        if (line) {
            const int n = line + (right ? 6 : 0);
            return h("SOFTKEYS_" + std::to_string(n), "SOFTKEY " + std::to_string(n));
        }
    } else if (line) {
        // a knob turns one click per press; +/- first turns it back
        const bool back = g1000Back_;
        g1000Back_ = false;
        if (left) {
            switch (line) {
                case 1: return h("DIRECTTO", "DIRECT TO");
                case 2: return h("FPL_Push", "FPL");
                case 3: return h("PROC_Push", "PROC");
                case 4: return h(back ? "FMS_Upper_DEC" : "FMS_Upper_INC", back ? "OUTER -" : "OUTER +");
                case 5: return h(back ? "FMS_Lower_DEC" : "FMS_Lower_INC", back ? "INNER -" : "INNER +");
                default: return h("FMS_Upper_PUSH", "CURSOR");
            }
        }
        switch (line) {
            case 1: return h("ENT_Push", "ENT");
            case 2: return h("CLR", "CLR");
            case 3: return h("MENU_Push", "MENU");
            case 4: return h("RANGE_DEC", "RANGE IN");
            case 5: return h("RANGE_INC", "RANGE OUT");
            default:
                page_ = mfd ? AvionicsPage::MfdKeys : AvionicsPage::PfdKeys;
                return {};
        }
    }
    switch (key) {  // the unit's own keys keep their G1000 shortcuts on every G1000 page
        case Key::Dir: return h("DIRECTTO", "DIRECT TO");
        case Key::Fpln: return h("FPL_Push", "FPL");
        case Key::Perf: return h("PROC_Push", "PROC");
        case Key::Data: return h("MENU_Push", "MENU");
        case Key::Clr: g1000Back_ = false; return h("CLR", "CLR");
        case Key::Ovfy: return h("ENT_Push", "ENT");
        case Key::UpArrow: return h("FMS_Lower_INC", "INNER +");
        case Key::DownArrow: return h("FMS_Lower_DEC", "INNER -");
        case Key::PlusMinus:
            if (page_ == AvionicsPage::Pfd || page_ == AvionicsPage::Mfd) g1000Back_ = !g1000Back_;
            return {};
        default: return {};
    }
}

}  // namespace lr::mcdu
