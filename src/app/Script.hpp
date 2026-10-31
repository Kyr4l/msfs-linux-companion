// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// A tiny scripting mode for checking controls against a real (or mock) simulator:
// set a cockpit control, then read the simulator's own state and compare. One command per line, '#' starts a comment.
//
//   set NAME VALUE              cockpit input event (B:) to a value
//   toggle NAME [N]             toggle / cycle through N positions (default 2)
//   press NAME                  one event with value 1: a push button (the sim acts on every event, so no release)
//   event NAME [VALUE]          a sim event (K:), for example THROTTLE1_SET 8192
//   get NAME                    print the current value of an input event
//   code EXPR                   print the value of calculator code, for example (A:LIGHT NAV, Bool)
//   expect EXPR OP VALUE [MS]   wait up to MS (default 1500) until the code satisfies OP (== != > < >= <=); prints ok or FAIL
//   expectb NAME OP VALUE [MS]  the same for an input event value
//   rewatch CODE [| CODE ...]   replace the whole set of watched codes while connected (the way the guides change pages), report what arrives
//   sleep MS
#include <string>
#include <vector>

#include "sim/SimBridge.hpp"

namespace lr::app {

struct ScriptNeeds {
    bool ok = false;
    std::string error;
    bool calculatorCode = false;          // uses code / expect (needs the MobiFlight module)
    std::vector<std::string> codes;       // calculator code to keep watching
    std::vector<std::string> inputEvents; // input events to keep watching
};

ScriptNeeds scanScript(const std::string& path);
// Runs the script; returns the number of failed expectations (or -1 when the script cannot be read / is malformed).
int runScript(sim::SimBridge& bridge, const std::string& path);

}  // namespace lr::app
