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
        CHECK(av && av->positions == 3 && av->toAction().value == 3 && !av->verified);
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
    // the two controls that did not behave must not be offered as something they are not
    {
        const auto* flaps = find(cj4, "HANDLING_Flaps", "");
        const auto* panel = find(cj4, "LIGHTING_PANEL_1", "");
        CHECK(flaps && flaps->behavior.empty() && !flaps->verified);
        CHECK(panel && panel->behavior.empty() && !panel->verified);
    }

    std::printf(failures ? "\n%d check(s) FAILED\n" : "all curated catalog checks passed\n", failures);
    return failures ? 1 : 0;
}
