// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Zone behaviours of a lever with gates, and lever recipes.
//
// A zone behaviour is something the program does by itself when the lever enters or leaves a gate zone, instead of a list of actions
// the user writes. Reverse thrust in the cut-off zone is the first one: on an aircraft whose reverse is a switch (the iniBuilds A320neo
// V2: K:THROTTLE_REVERSE_THRUST_TOGGLE, one switch for both engines) the lever's axis value means forward thrust until the aircraft
// reports reverse, so the amount must only be sent once it does. The actions built here are the sequence checked on that aircraft
// (2026-11-06): flag, switch on if off (on the ground), wait, amount if reversed; on leaving: flag off, reverse idle, wait, switch off
// if reversed and the other lever is not in its cut-off. The second lever waits a little longer, so both levers pulled together
// cannot switch reverse twice (on and straight off again).
//
// A lever recipe is a saved lever: the range, the stages, what each zone does, and the axis event, with "{n}" for the engine number.
// Shipped ones are in data/levers.json, the user's own in <config>/my_levers.json.

#include <string>
#include <vector>

#include "app/Profile.hpp"

namespace lr::app {

// The L: variable a lever in reverse mode sets while it is in its cut-off ("MLC_REV_LEVER_1"), read by the lever that shares the switch.
std::string reverseFlagName(int engine);

// The actions run when the lever enters (entering = true) or leaves its cut-off zone. `bindingAxisEvent` is the K: event the binding
// sends its value with, used when the zone names none.
std::vector<Action> reverseZoneActions(const ReverseZone& r, const std::string& bindingAxisEvent, bool entering);

// The engine a lever binding moves: the digit of THROTTLE<n>_... in its value event, else its reverse zone's engine, else 1.
int leverEngine(const Binding& b);

// The K: event the binding sends its lever value with (its first sim event), empty when it has none.
std::string leverAxisEvent(const Binding& b);

struct LeverRecipe {
    std::string id, title;
    std::string checked;               // what was tried and where ("Checked on the A320neo V2"), shown on the card
    bool verified = false;             // checked on a real aircraft
    std::vector<std::string> aircraft; // aircraft ids it is suggested for (empty: any)
    std::vector<Action> press;         // what the lever sends its value with, {n} = engine (THROTTLE{n}_AXIS_SET_EX1); empty = keep the binding's
    AxisDetents detents;               // range, values and stages (enabled is set when applied)
    std::vector<double> atGate;        // per stage: > 0 = place the stage at this fraction of the max gate (0 = use `at`)
    bool integer = true;
    ZoneMode cutoffMode = ZoneMode::Actions;
    ReverseZone reverse;               // texts with {n}; engine / otherEngine are filled in when applied
    bool reverseShared = false;        // the other lever shares the reverse switch (engines 1 and 2 swap)
    std::vector<Action> onCutoff, onCutoffLeave, onAfterburner, onAfterburnerLeave;  // with {n}
    bool userMade = false;             // from the user's own file (can be deleted)
};

bool loadLeverRecipes(const std::string& path, bool userMade, std::vector<LeverRecipe>& out, std::string& err);
bool saveUserLeverRecipes(const std::string& path, const std::vector<LeverRecipe>& recipes, std::string& err);

// Fills the lever part of `b` from the recipe for engine `engine`. `gateAt` is where the max gate is on the lever's used range
// (0..1, from the calibration), or a negative number when not known.
void applyLeverRecipe(const LeverRecipe& r, int engine, double gateAt, Binding& b);
// Whether the lever part of `b` is still what the recipe made of it.
bool leverMatchesRecipe(const LeverRecipe& r, int engine, double gateAt, const Binding& b);
// A recipe from the lever part of `b` (engine numbers in event and variable names become {n}).
LeverRecipe recipeFromBinding(const Binding& b, const std::string& id, const std::string& title);

}  // namespace lr::app
