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
enum class AvionicsPage { Menu, Comm, Nav, Fcu, Xpdr, Fuel, Engine, Prog, Pfd, Mfd };

class AvionicsPages {
public:
    using Read = std::function<std::optional<double>(const std::string& code)>;

    // every value the pages show, as calculator code: watched while the pages are on the unit
    static const std::vector<std::string>& codes();

    void render(Screen& s, const Read& read) const;
    // the calculator code the key sends (none for a key that only changes the page or the scratchpad)
    std::vector<std::string> onKey(Key key, const Read& read);
    AvionicsPage page() const { return page_; }
    void reset();  // back to the menu, scratchpad empty (another aircraft)

    // typed values; nullopt: not valid
    static std::optional<int> frequencyHz(const std::string& typed, bool nav);  // "11830", "118.3", "118.305"
    static std::optional<int> squawkBco16(const std::string& typed);            // "7000" -> 0x7000
    static std::optional<double> baroMillibars(const std::string& typed);       // "2992", "29.92" (inHg), "1013" (hPa)

private:
    std::vector<std::string> lineKey(int line, bool right, const Read& read);
    std::vector<std::string> g1000Key(Key key);

    AvionicsPage page_ = AvionicsPage::Menu;
    AvionicsPage last_ = AvionicsPage::Prog;  // where AVIONICS in MCDU MENU goes back to
    std::string scratch_;
    std::string message_;  // shown in the scratchpad until the next key
};

}  // namespace lr::mcdu
