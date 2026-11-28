// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// MCDU pages of the companion's own for an aircraft without an MCDU (the C172 and its G1000 NXi, light aircraft, helicopters):
// radios, transponder and baro, autopilot, engine and fuel, progress, and a page that turns the unit's keys into the G1000's.
// Values are read from the sim as calculator code (through the MobiFlight module, like the profile's watches); keys send
// calculator code: standard K: events everywhere, the G1000's H: events on its page (names from the Working Title G1000 NXi
// source, microsoft/msfs-avionics-mirror). Typed values go through a scratchpad, as on the aircraft's own MCDU.
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "mcdu/McduProtocol.hpp"

namespace lr::mcdu {

// Laid out like the pages of an Airbus MCDU: function keys open them (RAD NAV, PERF = FCU, FUEL PRED, PROG, ATC COMM = XPDR,
// DATA = ENGINE, MCDU MENU), green = actual values and engaged modes, cyan = values to change, white = labels and actions.
enum class AvionicsPage { Menu, Comm, Nav, Fcu, Xpdr, Fuel, Engine, Prog, Pfd, Mfd, PfdKeys, MfdKeys, Gtc, Fpln };

class AvionicsPages {
public:
    using Read = std::function<std::optional<double>(const std::string& code)>;

    // every value the pages show, as calculator code: watched while the pages are on the unit
    static const std::vector<std::string>& codes();

    void render(Screen& s, const Read& read) const;
    // the calculator code the key sends (none for a key that only changes the page or the scratchpad)
    std::vector<std::string> onKey(Key key, const Read& read);
    AvionicsPage page() const { return page_; }
    void reset();
    // "aircraft" (engine temperatures in °F, outside air in °C, as the aircraft shows them), "c", "f", or "both" (the other unit small, on the label line)
    void setTemperatures(const std::string& unit) { tempUnit_ = unit; }
    // "gal", "l", "kg", or a volume with the mass small on the label line: "gal_kg", "l_kg"
    void setFuelUnit(const std::string& unit) { fuelUnit_ = unit; }
    // A G3000 with touch controllers (the Vision Jet): MCDU MENU offers the GTCs, whose softkeys and knobs are the aircraft's own input
    // events (sent as (>B:..._Set), checked on the SF50), instead of the G1000 pages.
    // vertical: the ES-30's two touch controllers (left and right), three knobs each (INSTRUMENT_G5000_CONTROL_n_*), no softkeys;
    // the dual knob tunes COM1 standby (checked in the aircraft), the other two knobs and the pushes are not checked.
    void setG3000(bool on, bool vertical = false) {
        g3000_ = on || vertical;
        gtcVertical_ = vertical;
        if (gtc_ > gtcCount()) gtc_ = 1;
    }
    // An aircraft with traction batteries (the ES-30): FUEL PRED also shows their charge.
    void setElectric(bool on) { electric_ = on; }
    // A G1000 with its control pad (the Corvalis): on F-PLN the MCDU's letters, digits, CLR and OVFY can type into it.
    void setControlPad(bool on) { pad_ = on; padKeys_ = padKeys_ && on; }
    // The GPS's previous and next waypoint names (F-PLN): text, so they come from SimConnect, not as calculator code.
    void setWaypoints(std::string prev, std::string next) { prevWp_ = std::move(prev); nextWp_ = std::move(next); }
    bool keysToG1000() const { return padKeys_; }
    int gtc() const { return gtc_; }

    // typed values; nullopt: not valid
    static std::optional<int> frequencyHz(const std::string& typed, bool nav);  // "11830", "118.3", "118.305"
    static std::optional<int> squawkBco16(const std::string& typed);            // "7000" -> 0x7000
    static std::optional<double> baroMillibars(const std::string& typed);       // "2992", "29.92" (inHg), "1013" (hPa)

private:
    std::vector<std::string> lineKey(int line, bool right, const Read& read);
    std::vector<std::string> g1000Key(Key key);
    std::vector<std::string> gtcKey(Key key);
    std::vector<std::string> padKey(Key key);
    bool baroStd(std::optional<double> flag, std::optional<double> mb) const;
    bool isG1000() const { return page_ == AvionicsPage::Pfd || page_ == AvionicsPage::Mfd || page_ == AvionicsPage::PfdKeys || page_ == AvionicsPage::MfdKeys; }

    AvionicsPage page_ = AvionicsPage::Menu;
    AvionicsPage last_ = AvionicsPage::Prog;  // where AVIONICS in MCDU MENU goes back to
    std::string tempUnit_ = "aircraft";
    std::string fuelUnit_ = "gal";
    bool g1000Back_ = false;  // +/- pressed on a G1000 or GTC page: the next knob key turns it back
    bool g3000_ = false, gtcVertical_ = false, electric_ = false;
    int gtcCount() const { return gtcVertical_ ? 2 : 3; }
    bool stdByPage_ = false;  // STD was set from the XPDR page (some G1000s show STD without the sim's STD flag)
    int gtc_ = 1;  // the touch controller on the GTC page, 1 to 3 (1 to 2 on the ES-30)
    bool pad_ = false, padKeys_ = false;  // the G1000 has a control pad; the MCDU's keys type into it (F-PLN)
    std::string prevWp_, nextWp_;  // the GPS's previous and next waypoint names (F-PLN)
    std::string scratch_;
    std::string message_;  // shown in the scratchpad until the next key
};

}  // namespace lr::mcdu
