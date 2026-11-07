// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// Zone behaviours and lever recipes (app/LeverZones.hpp): the reverse thrust sequence is the one checked on the A320neo V2, the
// amount is never sent without the aircraft reporting reverse, recipes apply for either engine and say when the lever was changed,
// and the binding keeps its zone through a save.
#include <cstdio>
#include <filesystem>
#include <string>

#include "app/LeverZones.hpp"

using namespace lr::app;

static int failures = 0;
#define CHECK(cond)                                                                         \
    do {                                                                                    \
        if (!(cond)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } \
    } while (0)

static ReverseZone a320(int engine) {
    ReverseZone r;
    r.engine = engine;
    r.otherEngine = engine == 1 ? 2 : 1;
    r.reversedCode = "(L:INI_AUTOTHRUST_TLA:{n}, Number) -1 <";
    return r;
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";

    // the A320neo sequence, lever 1 (as checked on the aircraft, 2026-11-06)
    {
        const auto in = reverseZoneActions(a320(1), "THROTTLE1_AXIS_SET_EX1", true);
        CHECK(in.size() == 4);
        if (in.size() == 4) {
            CHECK(in[0].kind == ActionKind::LVarSet && in[0].name == "MLC_REV_LEVER_1" && in[0].value == 1);
            CHECK(in[1].kind == ActionKind::CalcCode &&
                  in[1].name == "(A:SIM ON GROUND, Bool) (L:INI_AUTOTHRUST_TLA:1, Number) -1 < ! and if{ 0 (>K:THROTTLE_REVERSE_THRUST_TOGGLE) }");
            CHECK(in[2].kind == ActionKind::Delay && in[2].value == 250);
            CHECK(in[3].name == "(L:INI_AUTOTHRUST_TLA:1, Number) -1 < if{ 16384 (>K:THROTTLE1_AXIS_SET_EX1) }");
        }
        const auto out = reverseZoneActions(a320(1), "THROTTLE1_AXIS_SET_EX1", false);
        CHECK(out.size() == 4);
        if (out.size() == 4) {
            CHECK(out[0].kind == ActionKind::LVarSet && out[0].name == "MLC_REV_LEVER_1" && out[0].value == 0);
            CHECK(out[1].name == "-8192 (>K:THROTTLE1_AXIS_SET_EX1)");
            CHECK(out[2].kind == ActionKind::Delay && out[2].value == 150);
            CHECK(out[3].name == "(L:MLC_REV_LEVER_2, Number) 0 == (L:INI_AUTOTHRUST_TLA:1, Number) -1 < and if{ 0 (>K:THROTTLE_REVERSE_THRUST_TOGGLE) }");
        }
    }
    // lever 2 goes second: both levers pulled together switch reverse once
    {
        const auto in = reverseZoneActions(a320(2), "THROTTLE2_AXIS_SET_EX1", true);
        CHECK(in.size() == 5 && in[1].kind == ActionKind::Delay && in[1].value == 300);
        const auto out = reverseZoneActions(a320(2), "THROTTLE2_AXIS_SET_EX1", false);
        CHECK(out.size() == 4 && out[2].value == 450);
        if (out.size() == 4) CHECK(out[3].name.rfind("(L:MLC_REV_LEVER_1, Number) 0 == ", 0) == 0);
    }
    // the amount is only ever sent behind the "reverse is on" condition, and a zone that is not set up does nothing
    {
        for (int e : {1, 2})
            for (bool entering : {true, false})
                for (const auto& a : reverseZoneActions(a320(e), "THROTTLE1_AXIS_SET_EX1", entering))
                    if (a.name.find("16384") != std::string::npos) CHECK(a.name.find(" if{ 16384") != std::string::npos);
        ReverseZone blank;
        CHECK(reverseZoneActions(blank, "THROTTLE1_AXIS_SET_EX1", true).empty());
        ReverseZone own = a320(1);
        own.otherEngine = 0;
        own.groundOnly = false;
        const auto in = reverseZoneActions(own, "THROTTLE1_AXIS_SET_EX1", true);
        CHECK(in.size() == 3 && in[0].name == "(L:INI_AUTOTHRUST_TLA:1, Number) -1 < ! if{ 0 (>K:THROTTLE_REVERSE_THRUST_TOGGLE) }");
        const auto out = reverseZoneActions(own, "THROTTLE1_AXIS_SET_EX1", false);
        CHECK(out.size() == 3 && out[2].name == "(L:INI_AUTOTHRUST_TLA:1, Number) -1 < if{ 0 (>K:THROTTLE_REVERSE_THRUST_TOGGLE) }");
    }

    // the shipped recipes
    std::vector<LeverRecipe> recipes;
    std::string err;
    CHECK(loadLeverRecipes(root + "/data/levers.json", false, recipes, err));
    const LeverRecipe* airbus = nullptr;
    const LeverRecipe* hornet = nullptr;
    for (const auto& r : recipes) {
        if (r.id == "airbus-inibuilds-reverse") airbus = &r;
        if (r.id == "fa18e") hornet = &r;
        CHECK(!r.title.empty() && !r.checked.empty());
    }
    CHECK(airbus && hornet && recipes.size() >= 4);
    if (airbus) {
        Binding b;
        b.input.kind = lr::input::InputKind::Axis;
        applyLeverRecipe(*airbus, 2, 0.75, b);
        CHECK(b.axis.detents.enabled && b.axis.detents.top == LeverTop::Full && b.axis.detents.stages.size() == 3);
        CHECK(leverAxisEvent(b) == "THROTTLE2_AXIS_SET_EX1" && leverEngine(b) == 2);
        CHECK(b.cutoffMode == ZoneMode::Reverse && b.reverse.engine == 2 && b.reverse.otherEngine == 1 && b.onCutoff.empty());
        CHECK(b.axis.detents.stages[0].name == "CL" && std::abs(b.axis.detents.stages[0].at - 0.75 * 0.55) < 1e-9);
        CHECK(b.recipe == "airbus-inibuilds-reverse");
        CHECK(leverMatchesRecipe(*airbus, 2, 0.75, b));
        Binding changed = b;
        changed.axis.detents.stages[0].at = 0.5;
        CHECK(!leverMatchesRecipe(*airbus, 2, 0.75, changed));
        changed = b;
        changed.reverse.groundOnly = false;
        CHECK(!leverMatchesRecipe(*airbus, 2, 0.75, changed));

        // through a save and a load
        const std::string dir = (std::filesystem::temp_directory_path() / "lr_lever_zones_test").string();
        std::filesystem::create_directories(dir);
        Settings s;
        Profile p;
        p.name = "T";
        p.bindings.push_back(b);
        s.profiles.push_back(p);
        s.activeProfile = "T";
        CHECK(saveSettings(dir + "/config.json", s, err));
        Settings back;
        CHECK(loadSettings(dir + "/config.json", back, err));
        if (!back.profiles.empty() && !back.profiles[0].bindings.empty()) {
            const Binding& bb = back.profiles[0].bindings[0];
            CHECK(bb.cutoffMode == ZoneMode::Reverse && bb.reverse == b.reverse && bb.recipe == b.recipe);
        } else {
            CHECK(false);
        }
        std::filesystem::remove_all(dir);
    }
    // a lever saved as a recipe works for the other engine
    if (hornet) {
        Binding b;
        b.input.kind = lr::input::InputKind::Axis;
        applyLeverRecipe(*hornet, 2, -1, b);
        CHECK(!b.onPress.empty() && b.onPress[0].name == "ENGINE_Throttle_2" && b.onCutoff.size() == 4 && b.onAfterburner.size() == 3);
        const LeverRecipe mine = recipeFromBinding(b, "my-1", "Mine");
        CHECK(mine.userMade && !mine.press.empty() && mine.press[0].name == "ENGINE_Throttle_{n}");
        CHECK(mine.onCutoff.size() == 4 && mine.onCutoff[3].name == "ENGINE_Throttle_{n}_STOP");
        CHECK(mine.onAfterburner[0].name == "XMLVAR_LastAB{n}ThrottleSet");
        Binding again;
        again.input.kind = lr::input::InputKind::Axis;
        applyLeverRecipe(mine, 1, -1, again);
        CHECK(again.onPress[0].name == "ENGINE_Throttle_1" && again.onCutoff[3].name == "ENGINE_Throttle_1_STOP");
    }

    std::printf(failures ? "\n%d check(s) FAILED\n" : "all lever zone checks passed\n", failures);
    return failures ? 1 : 0;
}
