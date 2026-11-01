// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// The hand-written catalogs of aircraft whose controls come from the sim (data/asobo_cj4_catalog.json): every event the
// sim reported is covered, behaviours turn into the actions that were checked on the real aircraft, and nothing claims
// more than was tried.
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "app/Catalog.hpp"

using namespace lr::app;

static int failures = 0;
#define CHECK(cond)                                                                         \
    do {                                                                                    \
        if (!(cond)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } \
    } while (0)

static const CatalogEntry* find(const Catalog& c, const std::string& name, const std::string& behavior) {
    for (const auto& e : c.entries)
        if (e.name == name && (behavior.empty() || e.behavior == behavior)) return &e;
    return nullptr;
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    Catalog cj4;
    std::string err;
    if (!cj4.load(root + "/data/asobo_cj4_catalog.json", err)) { std::printf("cannot load the CJ4 catalog: %s\n", err.c_str()); return 1; }
    CHECK(cj4.aircraft == "Asobo_CJ4");

    // every event the sim reported has at least one entry
    {
        std::ifstream in(root + "/tools/aircraft_rules/asobo_cj4_events.json");
        nlohmann::json j;
        in >> j;
        std::set<std::string> have;
        for (const auto& e : cj4.entries) have.insert(e.name);
        size_t missing = 0;
        for (const auto& n : j["input_events"]) if (!have.count(n.get<std::string>())) { std::printf("  no entry for %s\n", n.get<std::string>().c_str()); ++missing; }
        CHECK(missing == 0);
        CHECK(j["input_events"].size() == 309);
    }

    // rotary knobs: one click per event, +1 / -1
    {
        const auto* up = find(cj4, "AUTOPILOT_Heading", "knob_up");
        const auto* down = find(cj4, "AUTOPILOT_Heading", "knob_down");
        CHECK(up && down);
        if (up && down) {
            CHECK(up->toAction().kind == lr::app::ActionKind::InputEvent && up->toAction().value == 1);
            CHECK(down->toAction().value == -1);
            CHECK(up->isIncDec() && down->isIncDec());
            CHECK(up->verified && down->verified);
        }
    }
    // trims: the handler acts on >= 2 (up) and <= 0 (down); 1 does nothing, so the plain +1 / -1 would not work
    for (const char* n : {"HANDLING_ElevatorTrim_YOKE", "HANDLING_AileronsTrim_1", "HANDLING_RudderTrim_1"}) {
        const auto* up = find(cj4, n, "knob_up");
        const auto* down = find(cj4, n, "knob_down");
        CHECK(up && down);
        if (up && down) {
            CHECK(up->toAction().value >= 2);
            CHECK(down->toAction().value <= 0);
            CHECK(up->verified && down->verified);
        }
    }
    // push buttons that toggle on every event: ONE event on press, nothing on release (a release would toggle again)
    for (const char* n : {"AUTOPILOT_AP_1", "AUTOPILOT_YD_Button", "AUTOPILOT_FD_1_Mode", "AUTOPILOT_Heading_Mode", "AUTOPILOT_VS_Mode"}) {
        const auto* e = find(cj4, n, "press");
        CHECK(e != nullptr);
        if (e) {
            const Action a = e->toAction();
            CHECK(a.kind == lr::app::ActionKind::InputEvent && a.value == 1);
            CHECK(e->verified);
        }
    }
    // latched switches flip / cycle by reading the state
    {
        const auto* nav = find(cj4, "LIGHTING_NAV_1", "latched");
        CHECK(nav && nav->toAction().kind == lr::app::ActionKind::InputEventToggle && nav->toAction().value == 2 && nav->verified);
        const auto* av = find(cj4, "ELECTRICAL_Avionics_Bus_1", "latched");
        CHECK(av && av->positions == 3 && av->toAction().value == 3);
    }
    // brightness knobs are levels with a range for axes
    {
        const auto* pot = find(cj4, "LIGHTING_POTENTIOMETER_15", "level");
        CHECK(pot && pot->hasAxisRange && pot->axisMin == 0 && pot->axisMax == 100 && pot->verified);
    }

    // honesty: a verified entry says what was seen; a guess says it is a guess; unknown behaviours explain themselves
    std::map<std::string, int> perPanel;
    for (const auto& e : cj4.entries) {
        ++perPanel[e.panel];
        CHECK(!e.function.empty());
        CHECK(e.kind == "input_event");
        if (e.verified) CHECK(e.note.rfind("Verified on a CJ4", 0) == 0);
        else CHECK(!e.note.empty());
        if (e.behavior.empty()) CHECK(!e.note.empty());
    }
    CHECK(perPanel.size() >= 10);
    // controls that did not behave must not be offered as something they are not
    {
        const auto* thr = find(cj4, "ENGINE_Throttle_1", "");
        const auto* panel = find(cj4, "LIGHTING_PANEL_1", "");
        CHECK(thr && thr->behavior.empty() && !thr->verified && thr->note.find("THROTTLE1_SET") != std::string::npos);
        CHECK(panel && panel->behavior.empty() && !panel->verified);
    }
    // flaps and spoilers are 0..16384 axes: fixed positions are "set to this value", the axis entry carries the range
    {
        auto levels = [&](const char* name) {
            std::map<double, bool> v;
            bool axis = false;
            for (const auto& e : cj4.entries)
                if (e.name == name && e.behavior == "level") {
                    CHECK(e.verified);
                    if (e.hasAxisRange) { axis = e.axisMin == 0 && e.axisMax == 16384; }
                    else v[e.toAction().value] = true;
                }
            return std::make_pair(v, axis);
        };
        const auto flaps = levels("HANDLING_Flaps");
        CHECK(flaps.first.size() == 3 && flaps.first.count(0) && flaps.first.count(8192) && flaps.first.count(16384) && flaps.second);
        const auto spoilers = levels("HANDLING_Spoilers");
        CHECK(spoilers.first.size() == 2 && spoilers.first.count(0) && spoilers.first.count(16384) && spoilers.second);
        const auto brake = levels("LANDING_GEAR_ParkingBrake");
        CHECK(brake.first.size() == 2 && brake.first.count(0) && brake.first.count(100));
    }
    // the electrical switches: verified, set-value (0 = ON), cycled by reading the state
    for (const char* n : {"ELECTRICAL_Battery_1", "ELECTRICAL_Alternator_1", "ELECTRICAL_Alternator_2", "ENGINE_Starter_1", "FUEL_RunStop_2"}) {
        const auto* e = find(cj4, n, "latched");
        CHECK(e && e->verified && e->toAction().kind == lr::app::ActionKind::InputEventToggle);
    }
    {
        const auto* av = find(cj4, "ELECTRICAL_Avionics_Bus_1", "latched");
        CHECK(av && av->verified && av->positions == 3);
        const auto* alt = find(cj4, "ELECTRICAL_Alternator_1", "latched");
        CHECK(alt && alt->note.find("MASTER ALTERNATOR") != std::string::npos);  // the variable that really follows the control
    }

    // ---- the F/A-18E
    {
        Catalog fa;
        if (!fa.load(root + "/data/asobo_fa18e_catalog.json", err)) { std::printf("cannot load the F/A-18E catalog: %s\n", err.c_str()); return 1; }
        CHECK(fa.aircraft == "Asobo_FA18E");
        std::ifstream in(root + "/tools/aircraft_rules/asobo_fa18e_events.json");
        nlohmann::json j;
        in >> j;
        std::set<std::string> have;
        for (const auto& e : fa.entries) have.insert(e.name);
        size_t missing = 0;
        for (const auto& n : j["input_events"]) if (!have.count(n.get<std::string>())) { std::printf("  no entry for %s\n", n.get<std::string>().c_str()); ++missing; }
        CHECK(missing == 0 && j["input_events"].size() == 142);
        for (const auto& e : fa.entries) {
            CHECK(e.kind == "input_event" && !e.function.empty());
            if (e.verified) CHECK(e.note.rfind("Verified on an F/A-18E", 0) == 0);
            else CHECK(!e.note.empty());
        }
        // the crank switch: 0 = left, 1 = centre, 2 = right
        std::set<double> crank;
        for (const auto& e : fa.entries)
            if (e.name == "ENGINE_Starter_Ignition_1" && e.behavior == "level" && e.verified) crank.insert(e.toAction().value);
        CHECK(crank == std::set<double>({0, 1, 2}));
        // the throttle is a fraction, with the idle value that really lights the engine
        const CatalogEntry* idle = nullptr;
        for (const auto& e : fa.entries)
            if (e.name == "ENGINE_Throttle_2" && e.function == "Throttle 2: idle") idle = &e;
        CHECK(idle && idle->verified && idle->toAction().value > 0 && idle->toAction().value < 0.5);  // 0 would change nothing from lever 0
        // the polarity differs from the CJ4: strobes, engine anti-ice and windshield de-ice are inverted, the battery is not
        for (const char* n : {"LIGHTING_STROBE_1", "DEICE_Engine_1", "DEICE_Windshield_1"}) {
            const auto* e = find(fa, n, "latched");
            CHECK(e && e->verified && e->note.find("INVERTED") != std::string::npos);
        }
        const auto* batt = find(fa, "ELECTRICAL_Battery_1", "latched");
        CHECK(batt && batt->verified && batt->note.find("set 1 = ON") != std::string::npos);
        // what could not be understood must not be offered with a behaviour
        // the cut-off and the afterburner were worked out from the sim's throttle template and run on the aircraft: they say what they need
        for (const char* n : {"ENGINE_Throttle_1_STOP", "ENGINE_Throttle_2_STOP"}) {
            const auto* e = find(fa, n, "latched");
            CHECK(e && e->verified && e->note.find("exactly 0") != std::string::npos && e->note.find("FUELSYSTEM VALVE SWITCH") != std::string::npos);
        }
        for (const char* n : {"ENGINE_Throttle_1_Afterburner", "ENGINE_Throttle_2_Afterburner"}) {
            const auto* e = find(fa, n, "latched");
            CHECK(e && e->verified && e->note.find("XMLVAR_LastAB") != std::string::npos);
        }
        for (const char* n : {"LANDING_GEAR_Parking_Brake", "HANDLING_Spoilers", "LANDING_GEAR_Gear"}) {
            const auto* e = find(fa, n, "");
            CHECK(e && !e->verified);
        }
        const auto* brake = find(fa, "LANDING_GEAR_Parking_Brake", "");
        CHECK(brake && brake->behavior.empty());
        // 20 pushbuttons on each of the three display units
        size_t push = 0;
        for (const auto& e : fa.entries) push += e.name.rfind("AS04F_DDI_Push_", 0) == 0;
        CHECK(push == 60);
    }

    // ---- the Spirit of St. Louis: built from the cockpit model and the sim's own checklist, nothing claimed as tried
    {
        Catalog sp;
        if (!sp.load(root + "/data/microsoft_spirit_of_stlouis_catalog.json", err)) { std::printf("cannot load the Spirit catalog: %s\n", err.c_str()); return 1; }
        CHECK(sp.aircraft == "Microsoft_Spirit_of_StLouis" && sp.curated && sp.axisControls);
        std::ifstream in(root + "/tools/aircraft_rules/microsoft_spirit_of_stlouis_events.json");
        nlohmann::json j;
        in >> j;
        std::set<std::string> have;
        for (const auto& e : sp.entries) have.insert(e.name);
        size_t missing = 0;
        for (const auto& n : j["input_events"]) if (!have.count(n.get<std::string>())) { std::printf("  no entry for %s\n", n.get<std::string>().c_str()); ++missing; }
        CHECK(missing == 0 && j["input_events"].size() == 19);
        for (const auto& e : sp.entries) {
            CHECK(!e.function.empty() && !e.note.empty());
            CHECK(!e.verified);  // nothing was tried through the program yet; the notes say where each entry comes from
        }
        // the throttle and mixture the sim's own checklist drives: 0 .. 16383, as levels with a range and as fixed positions
        for (const char* n : {"THROTTLE1_SET", "MIXTURE1_SET"}) {
            bool axis = false, zero = false, full = false;
            for (const auto& e : sp.entries)
                if (e.kind == "sim_event" && e.name == n) {
                    if (e.hasAxisRange) axis = e.axisMin == 0 && e.axisMax == 16383;
                    else if (e.hasValue && e.value == 0) zero = true;
                    else if (e.hasValue && e.value == 16383) full = true;
                }
            CHECK(axis && zero && full);
        }
        // a fuel valve is opened by FUELSYSTEM_VALVE_OPEN with its number as the parameter, six valves, open and close
        for (const char* verb : {"FUELSYSTEM_VALVE_OPEN", "FUELSYSTEM_VALVE_CLOSE"}) {
            std::set<double> valves;
            for (const auto& e : sp.entries)
                if (e.kind == "sim_event" && e.name == verb && e.hasValue) {
                    CHECK(e.toAction().kind == lr::app::ActionKind::SimEvent && e.toAction().value == e.value);
                    valves.insert(e.value);
                }
            CHECK(valves == std::set<double>({1, 2, 3, 4, 5, 6}));
        }
        // the magneto switch cycles its four positions
        const auto* mag = find(sp, "ENGINE_Magneto_1", "latched");
        CHECK(mag && mag->positions == 4 && mag->toAction().value == 4);
        // the other controls of the cockpit exist too: hand prop, primer, periscope lever
        CHECK(find(sp, "TOGGLE_STARTER1", "press") && find(sp, "TOGGLE_PRIMER", "press") && find(sp, "NYP_Periscope_anim_handle_percent", ""));
        // a refresh adds the axis events every aircraft has (brakes, surfaces ...) without the standard events, and keeps the aircraft's own entries
        CatalogSet set;
        Catalog std_;
        std_.aircraft = kStandardCatalogId;
        set.addShipped(std_);
        set.addShipped(sp);
        const size_t before = set.find("Microsoft_Spirit_of_StLouis")->entries.size();
        set.refreshStandard();
        const Catalog* withAxes = set.find("Microsoft_Spirit_of_StLouis");
        CHECK(withAxes->entries.size() > before);
        bool brake = false, rudder = false, std_event = false;
        size_t throttle1 = 0;
        for (const auto& e : withAxes->entries) {
            brake |= e.kind == "sim_event" && e.name == "AXIS_LEFT_BRAKE_SET" && e.hasAxisRange;
            rudder |= e.kind == "sim_event" && e.name == "AXIS_RUDDER_SET" && e.hasAxisRange;
            std_event |= e.standard && !e.hasAxisRange && e.section.rfind("Common: ", 0) != 0;
            throttle1 += e.kind == "sim_event" && e.name == "THROTTLE1_SET";
        }
        CHECK(brake && rudder && !std_event);
        bool baroStd = false;
        for (const auto& e : withAxes->entries) baroStd |= e.name == "BAROMETRIC_STD_PRESSURE";
        CHECK(baroStd);  // the altimeter buttons come with the axis controls
        CHECK(throttle1 == 3);  // the aircraft's own three entries, not duplicated by the axis list
    }

    // ---- the Cirrus Vision Jet G2: the event list is what the running MSFS 2024 reported; labels come from the aircraft's tooltips,
    // its own checklist and the public G3000 sources, nothing claimed as tried
    {
        Catalog sf;
        if (!sf.load(root + "/data/microsoft_sf50_catalog.json", err)) { std::printf("cannot load the SF50 catalog: %s\n", err.c_str()); return 1; }
        CHECK(sf.aircraft == "Microsoft_SF50" && sf.curated && sf.axisControls);
        std::ifstream in(root + "/tools/aircraft_rules/microsoft_sf50_events.json");
        nlohmann::json j;
        in >> j;
        std::set<std::string> have;
        for (const auto& e : sf.entries) have.insert(e.name);
        size_t missing = 0;
        for (const auto& n : j["input_events"]) if (!have.count(n.get<std::string>())) { std::printf("  no entry for %s\n", n.get<std::string>().c_str()); ++missing; }
        CHECK(missing == 0 && j["input_events"].size() == 141);
        for (const auto& e : sf.entries) {
            CHECK(!e.function.empty() && !e.note.empty());
            CHECK(!e.verified);  // nothing was tried through the program yet; the notes say where each entry comes from
        }
        // the switches the sim's checklist names flip between their positions
        for (const char* n : {"SF50_ELECTRICAL_BATTERY_1_SWITCH", "SF50_ELECTRICAL_GEN_2_SWITCH", "SF50_PRESSURIZATION_SWITCH_AIR_BLEED", "SF50_ENGINE_RUN_KNOB"}) {
            const auto* e = find(sf, n, "latched");
            CHECK(e && e->toAction().kind == lr::app::ActionKind::InputEventToggle && e->toAction().value == 2);
        }
        const auto* fuel = find(sf, "SF50_FUEL_CONTROL_KNOB", "latched");
        CHECK(fuel && fuel->positions == 3 && fuel->toAction().value == 3);  // LEFT / AUTO / RIGHT
        const auto* flaps = find(sf, "SF50_HANDLING_FLAP_SELECTOR", "latched");
        CHECK(flaps && flaps->positions == 3);  // UP / 50% / 100%
        // the touch controller knobs of the three GTCs: the sign of the value picks the direction (public G3000 behaviour file)
        for (int n = 1; n <= 3; ++n) {
            const std::string knob = "AS3000_TSC_" + std::to_string(n) + "_FREQUENCY_KNOB_KHZ";
            const auto* up = find(sf, knob, "knob_up");
            const auto* down = find(sf, knob, "knob_down");
            CHECK(up && down && up->toAction().value == 1 && down->toAction().value == -1 && up->isIncDec());
            for (const char* key : {"SOFTKEY_1", "SOFTKEY_2", "SOFTKEY_3", "FREQUENCY_SWAP", "MULTIFUNCTION_KNOB_BUTTON"})
                CHECK(find(sf, "AS3000_TSC_" + std::to_string(n) + "_" + key, "press"));
        }
        // controls that can hurt carry a warning in their note and are not offered as an ordinary press or toggle
        const auto* caps = find(sf, "SF50_CAPS_HANDLE", "");
        CHECK(caps && caps->behavior.empty() && caps->note.find("DANGER") == 0);
        const auto* safeReturn = find(sf, "SF50_SAFE_RETURN_ACTIVATE_BUTTON", "press");
        CHECK(safeReturn && safeReturn->note.find("WARNING") == 0);
        // what the management tablet sets: an L: variable per option, the K: event of the GPU button
        const auto* chocks = find(sf, "SF50_Chocks_Enabled", "");
        CHECK(chocks && chocks->kind == "lvar" && chocks->toAction().kind == lr::app::ActionKind::LVarToggle);
        size_t states = 0;
        for (const auto& e : sf.entries)
            if (e.kind == "lvar" && e.name == "SF50_Management_Status_Type" && e.hasValue) {
                CHECK(e.toAction().kind == lr::app::ActionKind::LVarSet && e.toAction().value == e.value);
                states += e.value >= 1 && e.value <= 3;
            }
        CHECK(states == 3);
        const auto* gpu = find(sf, "TOGGLE_EXTERNAL_POWER", "press");
        CHECK(gpu && gpu->kind == "sim_event" && gpu->toAction().kind == lr::app::ActionKind::SimEvent);
        // a refresh adds the ranged axis events (thrust lever, brakes, surfaces) next to the aircraft's own entries
        CatalogSet set;
        Catalog std_;
        std_.aircraft = kStandardCatalogId;
        set.addShipped(std_);
        set.addShipped(sf);
        const size_t before = set.find("Microsoft_SF50")->entries.size();
        set.refreshStandard();
        const Catalog* withAxes = set.find("Microsoft_SF50");
        CHECK(withAxes->entries.size() > before);
        bool thrustAxis = false;
        for (const auto& e : withAxes->entries) thrustAxis |= e.kind == "sim_event" && e.name == "THROTTLE1_AXIS_SET_EX1" && e.hasAxisRange;
        CHECK(thrustAxis);
    }

    // ---- the Guimbal Cabri G2 (a helicopter): the event list is what the running MSFS 2024 reported; labels come from the aircraft's tooltips and
    // checklist wording and from the Asobo behaviour files of the install, nothing claimed as tried
    {
        Catalog cab;
        if (!cab.load(root + "/data/asobo_cabri_g2_catalog.json", err)) { std::printf("cannot load the Cabri catalog: %s\n", err.c_str()); return 1; }
        CHECK(cab.aircraft == "Asobo_Cabri_G2" && cab.curated && cab.axisControls);
        std::ifstream in(root + "/tools/aircraft_rules/asobo_cabri_g2_events.json");
        nlohmann::json j;
        in >> j;
        std::set<std::string> have;
        for (const auto& e : cab.entries) have.insert(e.name);
        size_t missing = 0;
        for (const auto& n : j["input_events"]) if (!have.count(n.get<std::string>())) { std::printf("  no entry for %s\n", n.get<std::string>().c_str()); ++missing; }
        CHECK(missing == 0 && j["input_events"].size() == 97);
        for (const auto& e : cab.entries) {
            CHECK(!e.function.empty() && !e.note.empty());
            CHECK(!e.verified);
        }
        // the switches the sim's checklist names flip between their positions
        for (const char* n : {"ENGINE_PLASMA", "ENGINE_MAGNETO", "ENGINE_GOVERNOR", "ROTOR_CLUTCH", "ELECTRICAL_ALTERNATOR", "FUEL_VALVE", "BREAKER_BREAKER_PFD"}) {
            const auto* e = find(cab, n, "latched");
            CHECK(e && e->toAction().kind == lr::app::ActionKind::InputEventToggle && e->toAction().value == 2);
        }
        const auto* carb = find(cab, "DEICE_CARB_HEAT", "latched");
        CHECK(carb && carb->positions == 3 && carb->toAction().value == 3);  // COLD / AUTO / HOT
        const auto* xpdr = find(cab, "AS_21_MODE", "latched");
        CHECK(xpdr && xpdr->positions == 5);  // OFF / STBY / GRND / ON / ALT (AS21.xml)
        // the radio knobs: the sign of the value picks the direction (AS650.xml)
        const auto* mhz = find(cab, "AS_650_FREQ_MHZ", "knob_up");
        CHECK(mhz && mhz->toAction().value == 1 && find(cab, "AS_650_FREQ_MHZ", "knob_down")->toAction().value == -1);
        // levers whose range is not known are listed without a behaviour, with a note, so nothing sets a wrong value
        for (const char* n : {"ROTOR_COLLECTIVE", "ROTOR_BRAKE", "ENGINE_THROTTLE"}) {
            const auto* e = find(cab, n, "");
            CHECK(e && e->behavior.empty() && e->note.find("Not tried") != std::string::npos);
        }
        // the sim's own helicopter trim and autostart events
        for (const char* n : {"ROTOR_LATERAL_TRIM_INC", "ROTOR_TRIM_RESET", "ENGINE_AUTO_START"}) {
            const auto* e = find(cab, n, "press");
            CHECK(e && e->kind == "sim_event" && e->toAction().kind == lr::app::ActionKind::SimEvent);
        }
        const auto* cover = find(cab, "XMLVAR_ROTORCRAFT_Clutch_Cover_Opened", "");
        CHECK(cover && cover->kind == "lvar" && cover->toAction().kind == lr::app::ActionKind::LVarToggle);
        // a refresh adds the ranged axis events: the helicopter axes next to the aircraft's own entries
        CatalogSet set;
        Catalog std_;
        std_.aircraft = kStandardCatalogId;
        set.addShipped(std_);
        set.addShipped(cab);
        set.refreshStandard();
        const Catalog* withAxes = set.find("Asobo_Cabri_G2");
        size_t heli = 0;
        for (const auto& e : withAxes->entries)
            if (e.kind == "sim_event" && e.hasAxisRange && e.axisMin == -16383 && e.axisMax == 16383 &&
                (e.name == "AXIS_COLLECTIVE_SET" || e.name == "AXIS_CYCLIC_LONGITUDINAL_SET" || e.name == "AXIS_CYCLIC_LATERAL_SET" || e.name == "AXIS_TAIL_ROTOR_SET"))
                ++heli;
        CHECK(heli == 4);
    }

    std::printf(failures ? "\n%d check(s) FAILED\n" : "all curated catalog checks passed\n", failures);
    return failures ? 1 : 0;
}
