// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/AutoCatalog.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace lr::app {

namespace {

using Tokens = std::vector<std::string>;

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string lowerStr(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isNumber(const std::string& t) {
    return !t.empty() && std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c); });
}

// "AS1000_COM_LARGE_PFD" -> AS1000 COM LARGE PFD; MSFS 2020 names mix the case ("HANDLING_ElevatorTrim_YOKE" -> HANDLING ELEVATOR TRIM YOKE).
Tokens tokens(const std::string& name) {
    Tokens out;
    std::string cur;
    auto flush = [&] { if (!cur.empty()) out.push_back(upper(cur)); cur.clear(); };
    for (size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (!std::isalnum(c)) { flush(); continue; }
        if (!cur.empty()) {
            // a word starts at "aB" and at the B of "ABc" ("ParkingBrake", "GPSMode"); letters and digits stay together ("AS1000", "COM1")
            const unsigned char p = static_cast<unsigned char>(cur.back());
            const bool nextLower = i + 1 < name.size() && std::islower(static_cast<unsigned char>(name[i + 1]));
            if ((std::islower(p) && std::isupper(c)) || (std::isupper(p) && std::isupper(c) && nextLower)) flush();
        }
        cur += static_cast<char>(c);
    }
    flush();
    return out;
}

bool in(const std::string& t, std::initializer_list<const char*> set) {
    for (const char* s : set)
        if (t == s) return true;
    return false;
}

// Tokens that say which side, which unit or which display, not what the control is.
bool qualifier(const std::string& t) {
    return isNumber(t) || t.size() == 1 ||
           in(t, {"LEFT", "RIGHT", "CENTER", "CENTRE", "PFD", "MFD", "MIDCOM", "CPT", "FO", "PILOT", "COPILOT", "INNER", "OUTER", "UPPER", "LOWER",
                  "FORWARD", "FWD", "AFT", "YOKE", "PEDESTRAL", "PEDESTAL", "OVHD", "PRIMARY", "SECONDARY", "MAIN", "AUX", "STBY", "ALL"});
}

bool avionicsUnit(const std::string& t);

// "BAT1" -> "BAT", "PACK2" -> "PACK" (not "AS1000", "G1000": those are units)
std::string stemOf(const std::string& t) {
    size_t end = t.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(t[end - 1]))) --end;
    if (end == t.size() || end < 2 || avionicsUnit(t)) return t;
    return t.substr(0, end);
}

// ---------------------------------------------------------------- behaviour from the name

const char* behaviorOfWord(const std::string& t);

const char* behaviorOfToken(const std::string& t) {
    if (const char* b = behaviorOfWord(t)) return b;
    const std::string st = stemOf(t);
    return st != t ? behaviorOfWord(st) : nullptr;
}

const char* behaviorOfWord(const std::string& t) {
    if (in(t, {"SWITCH", "SW", "TOGGLE", "GUARD", "GAURD"})) return "latched";
    if (in(t, {"PUSH", "BUTTON", "SOFTKEY", "SWAP", "SYNC", "TEST", "CLR", "ENT", "DIRECTTO", "MENU", "FPL", "PROC", "MSG", "CRSR", "DISCONNECT",
               "TOGA", "MODE", "PB", "BTN", "KEY", "RESET", "IDENT", "IDT", "DCT", "SNAP", "ARM", "EXEC", "PRESS", "CANCEL", "RCL", "MUTE", "CLEAR",
               "ACK", "KEYBOARD", "CAUTION", "WARNING", "STD", "AP", "FD", "YD", "LVL", "DISC", "STARTER", "MCT", "HOLD", "APPROACH", "APR",
               "NAVIGATION", "FLC", "VNAV", "VS", "BC", "BACKCOURSE", "FLIGHTLEVELCHANGE", "AUTOSTART", "ESC", "ENTER", "FUNC", "HOME",
               "SEL", "LSK", "PGE", "PRINT", "CHR", "RST", "AGENT", "DISCH", "EXPED", "LOC", "APPR", "ATHR", "MINUS", "PLUS", "STS", "CMD", "NUM",
               "CAUT", "WARN", "DISCO", "XFR", "CLICK", "RELEASE", "BFO", "IDG", "UP", "DOWN", "INC", "DEC"}))
        return "press";
    if (in(t, {"POTENTIOMETER"})) return "level";
    if (in(t, {"THROTTLE", "MIXTURE", "PROPELLER", "CONDITION", "COLLECTIVE", "FLAPS", "FLAP", "SPOILERS", "SPOILER", "LEVER", "AXIS"})) return "";
    if (in(t, {"KNOB", "LARGE", "SMALL", "VOLUME", "HEADING", "COURSE", "BARO", "RANGE", "OBS", "MHZ", "KHZ", "TRIM", "BRIGHTNESS", "DIMMER",
               "CALIBRATION", "WHEEL", "ZOOM", "ALTITUDE", "SPEED", "VERTICALSPEED", "FREQUENCY", "SQUELCH", "CRS", "HDG", "ALT", "SELECTOR",
               "ROTARY", "DIAL", "POT", "LUMINOSITY", "DIM", "INTENSITY", "TEMP", "TEMPERATURE", "FAN", "TILT", "DATA", "CODE", "BRT", "OFFBRT",
               "ELEV", "SLIDER", "FMS"}))
        return "knob";
    if (in(t, {"BATTERY", "ALTERNATOR", "GENERATOR", "GEN", "MASTER", "PUMP", "HEAT", "LIGHT", "LIGHTS", "BREAKER", "BREAK", "DOOR", "WINDOW",
               "COVER", "COVERS", "CB", "MAGNETO", "LANDING", "TAXI", "STROBE", "BEACON", "PITOT", "ICE", "DEICE", "BLEED", "AVIONICS", "CHOCKS",
               "HANDLE", "LOCK", "PIN", "VISOR", "SUNSHADE", "INVERTER", "FUSE", "CAP", "IGNITION", "CROSSFEED", "RUNSTOP", "BRAKE", "HIDER",
               "HIDE", "DEFOG", "BACKUP", "DISABLE", "SOUND", "TRANSMIT", "SPEAKER", "DUMP", "GEAR", "SIT", "EXIT", "WIPER", "WIPERS", "HORN",
               "BAT", "PACK", "HYD", "PTU", "ELAC", "FAC", "SEC", "GPWS", "GALLEY", "COMMERCIAL", "TIE", "DITCHING", "OXY", "MASK", "FADEC", "IFEC",
               "SYS", "ADR", "IR", "MAPLT", "LOUDSPEAKER", "FRIDGE", "LIGHTOFF", "LIGHTSOFF", "ONOFF", "INFLATE", "ARMREST", "FOOTREST", "TRAY"}))
        return "latched";
    return nullptr;
}

std::string behaviorFromName(const Tokens& t) {
    Tokens core;
    for (const auto& x : t)
        if (!qualifier(x)) core.push_back(x);
    // the last word says most ("..._PUSH", "..._KNOB", "..._SWITCH"); the first one is the category
    for (size_t i = core.size(); i-- > (core.size() > 1 ? 1u : 0u);)
        if (const char* b = behaviorOfToken(core[i])) return b;
    if (!core.empty()) {
        if (in(core[0], {"SWITCH", "SW", "CB"})) return "latched";
        if (in(core[0], {"BTN", "PB", "BUTTON"})) return "press";
    }
    for (const auto& x : t) {
        if (in(x, {"AUTOPILOT", "FMC", "CDU", "MCDU", "PROCEDURE", "SAFETY"}) || (x.size() > 2 && x.compare(0, 2, "AS") == 0 && std::isdigit(static_cast<unsigned char>(x[2]))))
            return "press";
        if (in(x, {"LIGHTING", "ELECTRICAL", "FUEL", "DEICE", "NAVCOM", "BREAKER", "PRESSURIZATION", "PASSENGER"})) return "latched";
    }
    return "";
}

// What the tooltip's action text says the control does ("Toggle ...", "Adjust ...", "Press and hold ...").
std::string behaviorFromAction(const std::string& action) {
    const std::string a = lowerStr(action);
    auto starts = [&](const char* w) { return a.rfind(w, 0) == 0; };
    if (starts("adjust") || starts("increase") || starts("decrease") || starts("rotate") || starts("scroll") || starts("turn to") || starts("change"))
        return "knob";
    if (starts("toggle") || starts("turn on") || starts("turn off") || starts("switch")) return "latched";
    if (starts("press") || starts("push") || starts("activate") || starts("start") || starts("stop") || starts("abort") || starts("open/close") ||
        starts("remove/replace") || starts("engage") || starts("disengage") || starts("display") || starts("enter") || starts("exit") ||
        starts("cycle") || starts("set ") || starts("pull"))
        return "press";
    return "";
}

// ---------------------------------------------------------------- panel, section, readable names

struct Category {
    const char* panel;
    std::vector<const char*> words;  // not an initializer_list: its array would not outlive the table
};

// Lower in the list wins when a name has words of several ("SF50_CABIN_ECS_FAN" is air, not cabin).
const std::vector<Category>& categories() {
    static const std::vector<Category> c = {
        {"Walkaround", {"UNKNOWN"}},
        {"Circuit breakers", {"BREAKER", "BREAKERS", "BREAK", "CB"}},
        {"Autopilot", {"AUTOPILOT", "AUTOTHROTTLE", "FCU", "MCP"}},
        {"Safety", {"CAPS", "PARACHUTE", "SAFE", "FIRE", "EXTINGUISHER", "SAFETY", "WARNING", "CAUTION", "ELT", "EJECT", "GPWS", "AGENT", "CAUT",
                    "WARN", "EVAC"}},
        {"Fuel", {"FUEL"}},
        {"Engine", {"ENGINE", "ENGINES", "ENG", "STARTER", "IGNITION", "MAGNETO", "PROPELLER", "THROTTLE", "MIXTURE", "APU", "ROTOR", "GOVERNOR", "FADEC"}},
        {"Hydraulics", {"HYD", "PTU", "RAT"}},
        {"Lights", {"LIGHTING", "LIGHT", "LIGHTS", "STROBE", "BEACON", "LT", "MAPLT"}},
        {"Ice protection", {"DEICE", "ICE", "ANTI", "PITOT", "IPS", "WIPER", "WIPERS"}},
        {"Air and pressurisation", {"PRESSURIZATION", "PRESSURISATION", "ECS", "BLEED", "AIRCON", "PNEUMATIC", "PNEUMATICS", "OXYGEN", "OXY", "CLIMATE", "PACK",
                                    "XBLEED", "VENT", "PRESS"}},
        {"Electrical", {"ELECTRICAL", "ELEC", "BATTERY", "BAT", "GENERATOR", "GEN", "IDG", "ALTERNATOR", "INVERTER", "BUS"}},
        {"Radios", {"NAVCOM", "AUDIO", "COM", "NAV", "RADIO", "XPNDR", "TRANSPONDER", "ADF", "DME", "INTERCOM", "ACP", "RMP", "VHF", "HF"}},
        {"Flight controls", {"HANDLING", "GEAR", "FLAPS", "FLAP", "TRIM", "BRAKE", "BRAKES", "SPOILERS", "ELEVATOR", "RUDDER", "AILERON", "COLLECTIVE", "ELAC", "SEC", "FAC",
                             "SPEEDBRAKE", "NWS", "STICK", "BRK"}},
        {"Avionics", {"FMC", "CDU", "MCDU", "FMS", "PFD", "MFD", "EFIS", "GTC", "TSC", "EFB", "TABLET", "CHECKLIST", "DCDU", "ECAM", "ISIS", "ND", "EIS",
                     "ADIRS", "TCAS"}},
        {"Instruments", {"INSTRUMENT", "INSTRUMENTS", "ALTIMETER", "CLOCK", "GYRO", "CHRONO"}},
        {"Cabin and airframe", {"AIRCRAFT", "AIRFRAME", "COMMON", "AUXILIARY", "PASSENGER", "CABIN", "DOOR", "DOORS", "ACCESS", "INT", "SEAT", "WINDOW", "COCKPITDOOR",
                                "ARMREST", "FOOTREST", "TRAY", "PAX", "CARGO"}},
        {"Procedures", {"PROCEDURE"}},
    };
    return c;
}

bool avionicsUnit(const std::string& t) {  // AS1000, AS3000, AS530, G1000, GNS430 ...
    if (t.size() < 3) return false;
    size_t letters = 0;
    while (letters < t.size() && std::isalpha(static_cast<unsigned char>(t[letters]))) ++letters;
    return letters >= 1 && letters <= 3 && letters < t.size() && std::isdigit(static_cast<unsigned char>(t[letters])) &&
           (t[0] == 'A' || t[0] == 'G');
}

bool categoryWord(const std::string& t) {
    for (const auto& c : categories())
        for (const char* w : c.words)
            if (t == w) return true;
    return avionicsUnit(t) || in(t, {"EXT", "UNKNOWN", "LANDING"});
}

// Short words and words with digits stay in capitals ("COM 1", "HDG"); the rest is written as a word.
std::string humanWord(const std::string& t) {
    if (t == "MHZ") return "MHz";
    if (t == "KHZ") return "kHz";
    const bool keep = t.size() <= 3 || std::any_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c); }) ||
                      in(t, {"VNAV", "LNAV", "TOGA", "XPDR", "AS1000", "AS3000", "ECAM"});
    return keep ? t : lowerStr(t);
}

std::string humanJoin(const Tokens& t) {
    std::string out;
    for (const auto& x : t) out += (out.empty() ? "" : " ") + humanWord(x);
    if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

// ---------------------------------------------------------------- tooltips

const char* synonym(const std::string& t) {
    if (in(t, {"PUSH", "PB", "BTN"})) return "BUTTON";
    if (t == "SW") return "SWITCH";
    if (t == "LIGHTS") return "LIGHT";
    if (t == "FLAPS") return "FLAP";
    if (t == "COVERS") return "COVER";
    if (t == "CHOCK") return "CHOCKS";
    if (t == "KNOBS") return "KNOB";
    return nullptr;
}

std::set<std::string> normSet(const Tokens& t) {
    std::set<std::string> s;
    for (const auto& x : t) {
        const char* y = synonym(x);
        s.insert(y ? y : x);
    }
    return s;
}

struct Tooltip {
    std::string stem, title;
    std::set<std::string> words;
    std::vector<std::string> actions;
};

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::char_traits<char>::length(suffix);
    return s.size() > n && s.compare(s.size() - n, n, suffix) == 0;
}

// "<PREFIX>.<NAME>_TITLE" names a control, "<PREFIX>.<NAME>[_<WHAT>]_ACTION" says what it does (Working Title aircraft); Carenado's are
// "AIRCRAFT.TOOLTIPS.<NAME>_TITLE" (or "_TITTLE") and "..._DESCRIPTION". The name is what follows the last dot.
std::vector<Tooltip> tooltipIndex(const std::map<std::string, std::string>& strings) {
    std::vector<Tooltip> out;
    std::unordered_map<std::string, size_t> byStem;
    for (const auto& [k, v] : strings) {
        const size_t dot = k.find_last_of('.');
        if (dot == std::string::npos || v.empty() || k.rfind("INPUT.", 0) == 0) continue;
        const size_t cut = endsWith(k, "_TITLE") ? 6 : endsWith(k, "_TITTLE") ? 7 : 0;
        if (!cut || k.size() <= dot + 1 + cut) continue;
        Tooltip t;
        t.stem = k.substr(dot + 1, k.size() - cut - dot - 1);
        t.title = v;
        byStem[t.stem] = out.size();
        out.push_back(std::move(t));
    }
    // a first word most names share ("CARENADO_...") is the maker's, not the control's
    std::map<std::string, size_t> first;
    for (const auto& t : out) {
        const Tokens w = tokens(t.stem);
        if (w.size() > 1) ++first[w[0]];
    }
    for (auto& t : out) {
        Tokens w = tokens(t.stem);
        if (w.size() > 1 && first[w[0]] >= 5 && first[w[0]] * 4 >= out.size()) w.erase(w.begin());
        t.words = normSet(w);
    }
    for (const auto& [k, v] : strings) {
        const size_t dot = k.find_last_of('.');
        if (dot == std::string::npos || v.empty()) continue;
        const size_t cut = endsWith(k, "_ACTION") ? 7 : endsWith(k, "_DESCRIPTION") ? 12 : 0;
        if (!cut || k.size() <= dot + 1 + cut) continue;
        std::string stem = k.substr(dot + 1, k.size() - cut - dot - 1);
        // the longest title stem the action key starts with
        for (;;) {
            if (auto it = byStem.find(stem); it != byStem.end()) { out[it->second].actions.push_back(v); break; }
            const size_t u = stem.find_last_of('_');
            if (u == std::string::npos) break;
            stem.resize(u);
        }
    }
    return out;
}

// The tooltip whose words the event's name contains (all of them, or most of both), when only one fits that well.
const Tooltip* matchTooltip(const std::vector<Tooltip>& index, const Tokens& nameTokens) {
    const std::set<std::string> words = normSet(nameTokens);
    if (words.empty()) return nullptr;
    const Tooltip* best = nullptr;
    double bestScore = 0;
    bool tie = false;
    for (const auto& t : index) {
        size_t shared = 0;
        for (const auto& w : t.words) shared += words.count(w);
        if (shared == 0 || t.words.empty()) continue;
        const double ofTitle = static_cast<double>(shared) / static_cast<double>(t.words.size());
        const double ofName = static_cast<double>(shared) / static_cast<double>(words.size());
        const bool good = (shared >= 2 && (ofTitle >= 0.99 || (ofTitle >= 0.66 && ofName >= 0.66))) || (shared == 1 && words.size() == 1 && ofTitle >= 0.99);
        if (!good) continue;
        const double score = ofTitle * 2 + ofName;
        if (score > bestScore + 1e-9) { best = &t; bestScore = score; tie = false; }
        else if (score > bestScore - 1e-9) tie = true;
    }
    return tie ? nullptr : best;
}

// "INPUT.<EVENT>_<ACTION>" (the MSFS 2024 Asobo aircraft): the sim's own name of a cockpit input event ("BATTERY MASTER ON") and the actions it
// takes (ON, OFF, TOGGLE, INC, DEC, SET ...); "..._DESC" says the same in a sentence ("Turn on battery master.").
struct InputName {
    std::string name, desc;
    std::set<std::string> actions;
};

std::unordered_map<std::string, InputName> inputIndex(const std::map<std::string, std::string>& strings) {
    std::unordered_map<std::string, std::vector<std::string>> texts;
    std::unordered_map<std::string, std::map<std::string, std::string>> descs;
    std::unordered_map<std::string, InputName> out;
    for (const auto& [k, v] : strings) {
        if (k.rfind("INPUT.", 0) != 0 || v.empty()) continue;
        std::string rest = k.substr(6);
        const bool desc = rest.size() > 5 && rest.compare(rest.size() - 5, 5, "_DESC") == 0;
        if (desc) rest.resize(rest.size() - 5);
        const size_t u = rest.find_last_of('_');
        if (u == std::string::npos || u == 0) continue;
        const std::string stem = upper(rest.substr(0, u)), act = upper(rest.substr(u + 1));
        if (desc) { descs[stem][act] = v; continue; }
        out[stem].actions.insert(act);
        texts[stem].push_back(v);
    }
    for (auto& [stem, in] : out) {
        // the words every action's text starts with ("BATTERY MASTER ON", "BATTERY MASTER OFF" -> "BATTERY MASTER")
        std::vector<Tokens> words;
        for (const auto& t : texts[stem]) {
            Tokens w;
            size_t p = 0;
            while (p < t.size()) {
                const size_t sp = t.find(' ', p);
                const std::string word = t.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
                if (!word.empty()) w.push_back(word);
                p = sp == std::string::npos ? t.size() : sp + 1;
            }
            words.push_back(std::move(w));
        }
        Tokens common = words.empty() ? Tokens{} : words[0];
        for (const auto& w : words) {
            size_t n = 0;
            while (n < common.size() && n < w.size() && common[n] == w[n]) ++n;
            common.resize(n);
        }
        if (words.size() == 1 && common.size() > 1) common.pop_back();  // one text: its last word is the action
        for (const auto& w : common) in.name += (in.name.empty() ? "" : " ") + w;
        const auto& d = descs[stem];
        for (const char* a : {"TOGGLE", "ON", "SET", "INC"})
            if (auto it = d.find(a); it != d.end()) { in.desc = it->second; break; }
        if (in.desc.empty() && !d.empty()) in.desc = d.begin()->second;
    }
    return out;
}

std::string behaviorFromActions(const std::set<std::string>& a) {
    auto has = [&](const char* w) { return a.count(w) > 0; };
    if (has("TOGGLE") || (has("ON") && has("OFF")) || (has("OPEN") && has("CLOSED")) || (has("SHOW") && has("HIDE"))) return "latched";
    if (has("INC") && has("DEC")) return "knob";
    if (has("PUSH") || has("PRESS")) return "press";
    return "";
}

struct TextIndex {
    std::vector<Tooltip> tips;
    std::unordered_map<std::string, InputName> inputs;
};

// ---------------------------------------------------------------- one event

std::string danger(const Tokens& t) {
    auto has = [&](const char* w) { return std::find(t.begin(), t.end(), w) != t.end(); };
    if (has("CAPS") || has("PARACHUTE")) return "DANGER: parachute.";
    if (has("SAFE") && has("RETURN")) return "DANGER: emergency autoland.";
    if (has("EXTINGUISHER") || has("EXTINGUISH") || has("DISCH") || (has("FIRE") && (has("BOTTLE") || has("AGENT") || has("DISCHARGE"))))
        return "DANGER: fire extinguisher.";
    if (has("EJECT") || has("EJECTION")) return "DANGER: ejection seat.";
    if (has("JETTISON")) return "DANGER: jettison.";
    return {};
}

std::vector<AutoLabel> labelOne(const std::string& name, const TextIndex* text, const std::string& aircraftPrefix) {
    Tokens t = tokens(name);
    if (!aircraftPrefix.empty() && t.size() > 1 && t[0] == aircraftPrefix) t.erase(t.begin());

    // panel: the most specific category word of the name; walkaround points and cockpit-model switches have their own
    std::string panel;
    size_t best = categories().size();
    for (const auto& x : t) {
        const std::string st = stemOf(x);
        for (size_t i = 0; i < categories().size() && i < best; ++i)
            for (const char* w : categories()[i].words)
                if (x == w || st == w) { best = i; break; }
    }
    if (best < categories().size()) panel = categories()[best].panel;
    else if (!t.empty() && avionicsUnit(t[0])) panel = "Avionics";
    else panel = "Other";
    const bool walkaround = !t.empty() && (t[0] == "EXT" || t[0] == "UNKNOWN");
    if (walkaround) panel = "Walkaround";
    const bool visibility = std::any_of(t.begin(), t.end(), [](const std::string& x) { return in(x, {"HIDE", "HIDER", "VISIBILITY", "BACKDROP"}); });
    if (visibility) panel = "Other";
    const bool logic = std::any_of(t.begin(), t.end(), [](const std::string& x) { return in(x, {"LOGIC", "INTERCEPT", "INTERCEPTS", "BIND"}); });
    // levers: the range the event takes is not known, whatever the sim's actions say (bind the axis events of the Control list instead)
    const bool lever = std::any_of(t.begin(), t.end(), [](const std::string& x) {
        return in(x, {"THROTTLE", "MIXTURE", "PROPELLER", "CONDITION", "COLLECTIVE", "FLAPS", "FLAP", "SPOILERS", "SPOILER", "LEVER", "AXIS"});
    });

    // section: the display the control belongs to, else the word after the category ("INSTRUMENT_ADF_KNOB" -> ADF)
    std::string section;
    Tokens rest = t;
    for (const char* d : {"PFD", "MFD", "MIDCOM"}) {
        auto it = std::find(rest.begin(), rest.end(), d);
        if (it != rest.end() && rest.size() > 2) {
            section = std::string(d) == "MIDCOM" ? "Audio panel" : d;
            rest.erase(it);
            break;
        }
    }
    if (!rest.empty() && (categoryWord(rest[0]) || walkaround) && rest.size() > 1) rest.erase(rest.begin());
    // "AS1000_AUTOPILOT_HEADING": the panel already says autopilot
    if (rest.size() > 1 && in(rest[0], {"AUTOPILOT", "ELECTRICAL", "LIGHTING", "HANDLING", "INSTRUMENT", "ENGINE", "FUEL", "DEICE"})) rest.erase(rest.begin());
    if (walkaround) {
        section = "Walkaround";
    } else if (panel == "Circuit breakers") {
        section = "Breakers";
        rest.erase(std::remove_if(rest.begin(), rest.end(), [](const std::string& x) { return in(x, {"BREAKER", "BREAK", "CB"}); }), rest.end());
        if (rest.empty()) rest = t;
    } else if (visibility) {
        section = "Cockpit model";
    } else if (section.empty()) {
        size_t words = 0;
        for (const auto& x : rest) words += !qualifier(x);
        if (words >= 2 && rest.size() >= 3 && !qualifier(rest[0])) {
            section = humanJoin({rest[0]});
            rest.erase(rest.begin());
        } else {
            section = panel == "Other" ? "Controls" : panel;
        }
    }
    std::string function = humanJoin(rest);
    std::string behavior = behaviorFromName(t);
    const InputName* simName = nullptr;
    if (text)
        if (const auto it = text->inputs.find(upper(name)); it != text->inputs.end() && !it->second.name.empty()) simName = &it->second;
    std::string note;
    if (logic) {
        note = "Internal logic, not a control";
        behavior = "";
    } else if (visibility) {
        note = "Shows or hides part of the cockpit";
        behavior = "latched";
    } else if (simName) {
        function = humanJoin(tokens(simName->name));
        if (const std::string b = behaviorFromActions(simName->actions); !b.empty() && !lever) behavior = b;
        note = simName->desc.empty() ? "Sim's name: \"" + simName->name + "\"" : "Sim: \"" + simName->desc + "\"";
    } else if (const Tooltip* tip = text ? matchTooltip(text->tips, t) : nullptr) {
        // the tooltip's name, plus what tells twins apart ("Fan Control" for FAN_KNOB_AFT and _FORWARD -> "Fan Control (aft)")
        Tokens extra;
        const std::set<std::string> tw = tip->words;
        for (const auto& x : rest) {
            const char* y = synonym(x);
            if (!tw.count(y ? y : x) && qualifier(x) && !in(x, {"PFD", "MFD", "MIDCOM"})) extra.push_back(x);
        }
        std::string title = tip->title;
        const size_t pct = title.find("%s");
        if (pct != std::string::npos) {
            title.replace(pct, 2, extra.empty() ? "" : humanJoin(extra));
            extra.clear();
            while (!title.empty() && title.front() == ' ') title.erase(0, 1);
        }
        function = extra.empty() ? title : title + " (" + lowerStr(humanJoin(extra)) + ")";
        std::string said = tip->title;
        for (const auto& a : tip->actions) {
            said += (said == tip->title ? ": " : "; ") + a;
            if (std::string b = behaviorFromAction(a); !b.empty() && b != behavior && !lever) behavior = b;
        }
        note = "Tooltip: \"" + said + "\"";
    } else {
        note = "Guessed from the name";
    }
    if (walkaround && behavior.empty() && !logic) behavior = "press";  // a point of the walkaround: clicked, not set
    if (const std::string d = danger(t); !d.empty()) note = d + " " + note;

    std::vector<AutoLabel> out;
    if (behavior == "knob") {
        out.push_back({panel, section, function + ": up", "knob_up", note});
        out.push_back({panel, section, function + ": down", "knob_down", note});
    } else {
        out.push_back({panel, section, function, behavior, note});
    }
    return out;
}

// The aircraft's own prefix ("SF50" in SF50_ENGINE_RUN_KNOB): a first word shared by many names that is not a category.
std::string aircraftPrefix(const std::vector<std::string>& names) {
    std::map<std::string, size_t> first;
    for (const auto& n : names) {
        const Tokens t = tokens(n);
        if (t.size() > 1 && !categoryWord(t[0])) ++first[t[0]];
    }
    std::string best;
    size_t count = 0;
    for (const auto& [w, c] : first)
        if (c > count) { best = w; count = c; }
    return count >= 5 && count * 5 >= names.size() ? best : std::string();
}

}  // namespace

std::vector<std::vector<AutoLabel>> autoLabels(const std::vector<std::string>& names, const PackageText* text) {
    TextIndex index;
    if (text) {
        index.tips = tooltipIndex(text->strings);
        index.inputs = inputIndex(text->strings);
    }
    const std::string prefix = aircraftPrefix(names);
    std::vector<std::vector<AutoLabel>> out;
    out.reserve(names.size());
    for (const auto& n : names) out.push_back(labelOne(n, text ? &index : nullptr, prefix));
    return out;
}

void applyAutoLabels(Catalog& cat, const PackageText* text, const std::string& title) {
    cat.entries.erase(std::remove_if(cat.entries.begin(), cat.entries.end(), [](const CatalogEntry& e) { return e.autoTwin; }), cat.entries.end());
    std::vector<size_t> idx;
    std::vector<std::string> names;
    for (size_t i = 0; i < cat.entries.size(); ++i) {
        const CatalogEntry& e = cat.entries[i];
        if (e.live && e.kind == "input_event") { idx.push_back(i); names.push_back(e.name); }
    }
    const auto labels = autoLabels(names, text);
    std::vector<CatalogEntry> twins;
    for (size_t k = 0; k < idx.size(); ++k) {
        CatalogEntry& e = cat.entries[idx[k]];
        const auto& l = labels[k];
        e.panel = l[0].panel;
        e.section = l[0].section;
        e.function = l[0].function;
        e.behavior = l[0].behavior;
        e.positions = l[0].positions;
        e.note = l[0].note;
        e.automatic = true;
        refreshSearchKey(e);
        for (size_t j = 1; j < l.size(); ++j) {
            CatalogEntry t = e;
            t.function = l[j].function;
            t.behavior = l[j].behavior;
            t.note = l[j].note;
            t.autoTwin = true;
            refreshSearchKey(t);
            twins.push_back(std::move(t));
        }
    }
    for (auto& t : twins) cat.entries.push_back(std::move(t));
    if (!title.empty()) cat.title = title + " (automatic)";
    ++cat.revision;
}

// ---------------------------------------------------------------- guide from the sim's checklist

namespace {

struct XmlNode {
    std::string name;
    std::map<std::string, std::string> attrs;
    std::vector<XmlNode> children;
    std::string text;
    const XmlNode* child(const char* n) const {
        for (const auto& c : children)
            if (c.name == n) return &c;
        return nullptr;
    }
};

std::string decodeEntities(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out += s[i]; continue; }
        const size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { out += s[i]; continue; }
        const std::string ent = s.substr(i + 1, semi - i - 1);
        unsigned long cp = 0;
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            try { cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? std::stoul(ent.substr(2), nullptr, 16) : std::stoul(ent.substr(1)); } catch (...) { cp = '?'; }
            if (cp == 0xa0) cp = ' ';
            if (cp < 0x80) out += static_cast<char>(cp);
            else if (cp < 0x800) { out += static_cast<char>(0xc0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3f)); }
            else { out += static_cast<char>(0xe0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f)); out += static_cast<char>(0x80 | (cp & 0x3f)); }
        } else {
            out += s.substr(i, semi - i + 1);
        }
        i = semi;
    }
    return out;
}

// Enough XML for the checklist files: elements, attributes, text; comments, declarations and CDATA-free.
bool parseXml(const std::string& s, XmlNode& root, std::string& err) {
    std::vector<XmlNode*> stack{&root};
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] != '<') {
            const size_t lt = s.find('<', i);
            stack.back()->text += decodeEntities(s.substr(i, (lt == std::string::npos ? s.size() : lt) - i));
            i = lt == std::string::npos ? s.size() : lt;
            continue;
        }
        if (s.compare(i, 4, "<!--") == 0) {
            const size_t e = s.find("-->", i);
            if (e == std::string::npos) { err = "unterminated comment"; return false; }
            i = e + 3;
            continue;
        }
        if (s.compare(i, 2, "<?") == 0 || s.compare(i, 2, "<!") == 0) {
            const size_t e = s.find('>', i);
            if (e == std::string::npos) { err = "unterminated declaration"; return false; }
            i = e + 1;
            continue;
        }
        const size_t e = s.find('>', i);
        if (e == std::string::npos) { err = "unterminated tag"; return false; }
        std::string tag = s.substr(i + 1, e - i - 1);
        i = e + 1;
        if (!tag.empty() && tag[0] == '/') {
            if (stack.size() <= 1) { err = "unbalanced </" + tag.substr(1) + ">"; return false; }
            stack.pop_back();
            continue;
        }
        const bool selfClosing = !tag.empty() && tag.back() == '/';
        if (selfClosing) tag.pop_back();
        XmlNode n;
        size_t p = 0;
        while (p < tag.size() && !std::isspace(static_cast<unsigned char>(tag[p]))) ++p;
        n.name = tag.substr(0, p);
        while (p < tag.size()) {
            while (p < tag.size() && std::isspace(static_cast<unsigned char>(tag[p]))) ++p;
            const size_t eq = tag.find('=', p);
            if (eq == std::string::npos) break;
            std::string key = tag.substr(p, eq - p);
            while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
            size_t q = eq + 1;
            while (q < tag.size() && std::isspace(static_cast<unsigned char>(tag[q]))) ++q;
            if (q >= tag.size() || (tag[q] != '"' && tag[q] != '\'')) break;
            const size_t close = tag.find(tag[q], q + 1);
            if (close == std::string::npos) break;
            n.attrs[key] = decodeEntities(tag.substr(q + 1, close - q - 1));
            p = close + 1;
        }
        stack.back()->children.push_back(std::move(n));
        if (!selfClosing) stack.push_back(&stack.back()->children.back());
    }
    return true;
}

// Checklist text: trimmed, one line, without the quotes and the "\n" some actions carry ("\"INCREASE TO Vmo/\nMmo\"").
std::string clean(std::string s) {
    for (size_t p; (p = s.find("\\n")) != std::string::npos;) s.replace(p, 2, " ");
    std::string out;
    bool space = false;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) { space = !out.empty(); continue; }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    if (out.size() >= 2 && out.front() == '"' && out.back() == '"') out = out.substr(1, out.size() - 2);
    return out;
}

// "2. AP DISC Button" -> "AP DISC Button", "a. Exit RVSM airspace." -> "Exit RVSM airspace."
std::string stripNumbering(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    if (i == 0 && s.size() > 2 && std::islower(static_cast<unsigned char>(s[0])) && s[1] == '.') i = 1;
    if (i > 0 && i + 1 < s.size() && s[i] == '.' && s[i + 1] == ' ') return s.substr(i + 2);
    return s;
}

std::set<std::string> wordsOf(const std::string& text) {
    std::set<std::string> w;
    for (const auto& t : tokens(text)) {
        if (t.size() < 2 || in(t, {"THE", "AND", "OR", "TO", "OF", "ON", "OFF", "SET", "AS", "IN", "IF", "AT", "BY", "FOR", "CHECK", "ALL"})) continue;
        const char* y = synonym(t);
        w.insert(y ? y : t);
    }
    return w;
}

// The catalog control a checklist line talks about: most of the line's words in the entry's name, the best one only.
const CatalogEntry* matchControl(const Catalog& cat, const std::string& label) {
    const std::set<std::string> want = wordsOf(label);
    if (want.empty()) return nullptr;
    const CatalogEntry* best = nullptr;
    double bestScore = 0;
    for (const auto& e : cat.entries) {
        if (e.kind != "input_event" || e.autoTwin) continue;
        const std::set<std::string> have = wordsOf(e.function + " " + e.section);
        size_t shared = 0;
        for (const auto& w : want) shared += have.count(w);
        if (shared == 0) continue;
        const double score = static_cast<double>(shared) / static_cast<double>(want.size()) + 0.25 * static_cast<double>(shared) / static_cast<double>(have.size());
        if (shared * 3 >= want.size() * 2 && score > bestScore) { best = &e; bestScore = score; }
    }
    return best;
}

}  // namespace

bool buildAutoGuide(const std::string& aircraft, const std::string& title, const PackageText& text, const Catalog& cat, Guide& out, std::string& err) {
    if (text.checklistXml.empty()) { err = "no checklist"; return false; }
    XmlNode doc;
    if (!parseXml(text.checklistXml, doc, err)) return false;
    const XmlNode* root = doc.child("Checklist");
    if (!root) { err = "no <Checklist>"; return false; }
    out = Guide{};
    out.aircraft = aircraft;
    out.title = title;
    out.automatic = true;
    out.notice = "Automatic, from the sim's checklist. Controls not tried.";
    out.sources.push_back(text.checklistSource + " (the sim's own checklist)");
    // the default group (normal procedures) first
    std::vector<const XmlNode*> groups;
    const std::string defaultGroup = root->attrs.count("default-group-name") ? root->attrs.at("default-group-name") : "";
    for (const auto& g : root->children)
        if (g.name == "Group") groups.push_back(&g);
    std::stable_partition(groups.begin(), groups.end(), [&](const XmlNode* g) { return g->attrs.count("name") && g->attrs.at("name") == defaultGroup; });
    int listNo = 0;
    for (const XmlNode* g : groups) {
        const std::string group = g->attrs.count("name") ? clean(g->attrs.at("name")) : "Checklist";
        for (const auto& l : g->children) {
            if (l.name != "List") continue;
            const std::string list = l.attrs.count("name") ? clean(l.attrs.at("name")) : "List";
            const std::string id = "list" + std::to_string(++listNo);
            GuideNode n;
            n.id = id;
            n.type = "steps";
            n.title = list;
            n.source = text.checklistSource + ": " + group + " / " + list;
            n.next = id + "_end";
            std::string pendingWarn;
            for (const auto& item : l.children) {
                if (item.name != "Item") continue;
                const std::string type = item.attrs.count("type") ? item.attrs.at("type") : "";
                if (type == "note" || type == "title") {
                    const XmlNode* t = item.child("Text");
                    const std::string note = t ? clean(t->text) : "";
                    if (note.empty() || note == "Procedure Complete") continue;
                    const std::string up = upper(note.substr(0, 8));
                    if (up.rfind("CAUTION", 0) == 0 || up.rfind("WARNING", 0) == 0) { pendingWarn += (pendingWarn.empty() ? "" : " ") + note; continue; }
                    GuideStep s;
                    s.text = note;
                    s.source = n.source;
                    n.steps.push_back(std::move(s));
                    continue;
                }
                const XmlNode* lt = item.child("LabelText");
                if (!lt) continue;
                const std::string label = stripNumbering(clean(lt->text));
                const XmlNode* at = item.child("ActionText");
                const std::string action = at ? clean(at->text) : "";
                GuideStep s;
                s.text = action.empty() ? label : label + ": " + action;
                s.source = n.source;
                s.warn = pendingWarn;
                pendingWarn.clear();
                if (const CatalogEntry* e = matchControl(cat, label)) s.controls.push_back({e->api, e->function});
                n.steps.push_back(std::move(s));
            }
            if (!pendingWarn.empty()) {
                GuideStep s;
                s.text = pendingWarn;
                s.warn = pendingWarn;
                s.source = n.source;
                n.steps.push_back(std::move(s));
            }
            if (n.steps.empty()) continue;
            GuideNode end;
            end.id = id + "_end";
            end.type = "end";
            end.title = list;
            end.text = "Done";
            out.topics.push_back({id, list, group, id});
            out.nodes[end.id] = std::move(end);
            out.nodes[id] = std::move(n);
        }
    }
    if (out.topics.empty()) { err = "the checklist has no lists"; return false; }
    return true;
}

}  // namespace lr::app
