// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The controls of any aircraft an axis can drive through a plain K: sim event (throttle, mixture, propeller, flaps, trim,
// spoilers, brakes, the flight surfaces), with the range of values each event takes. They work on every aircraft whose
// engines and surfaces are the sim's own, which includes the simple ones that have no catalog of their own.
#include <string>
#include <vector>

namespace lr::app {

struct AxisTarget {
    const char* group;  // heading in the pickers
    const char* label;  // what the user sees: "Throttle 1"
    const char* event;  // K: event name, without the prefix
    double lo, hi;      // the parameter range the event takes
    const char* note;   // one line: what it does, where the range comes from
    bool verified;      // set, then read back from the real simulator
};

const std::vector<AxisTarget>& axisTargets();

// Buttons every aircraft can use through plain K: events, listed under names people look for ("baro", "STD").
struct CommonControl {
    const char* section;  // "Altimeter"
    const char* label;    // "Altimeter setting (baro): increase"
    const char* event;    // K: event name, without the prefix (H: event name for hEvent)
    const char* keywords; // extra words the Catalog search should find it by
    const char* note;     // one line: what it does and where that comes from
    bool incDec;          // a stepping control: a button bound to it repeats while held
    bool hEvent = false;  // an H: event (needs the MobiFlight module) instead of a K: event
};
const std::vector<CommonControl>& commonControls();
const AxisTarget* findAxisTarget(const std::string& event);  // by event name (with or without "K:"), or nullptr

}  // namespace lr::app
