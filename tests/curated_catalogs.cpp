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

    std::printf(failures ? "\n%d check(s) FAILED\n" : "all curated catalog checks passed\n", failures);
    return failures ? 1 : 0;
}
