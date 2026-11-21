// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// What the joysticks do on the desktop while the simulator is not running, and how the MCDU behaves there. Saved in the config file.
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "input/Evdev.hpp"

namespace lr::desktop {

enum class OutputKind { Key, MouseButton, MouseMove, Wheel };
enum class Direction { Up = 0, Down = 1, Left = 2, Right = 3 };
// How the travel of an axis is read (antiMicroX's throttle setting). Negative and Positive stretch the whole travel over one side, the half modes fold it.
enum class AxisMode { Normal, Negative, Positive, NegativeHalf, PositiveHalf };
enum class MouseCurve { EnhancedPrecision, Linear, Quadratic, Cubic, QuadraticExtreme, Power };

struct Output {
    OutputKind kind = OutputKind::Key;
    int code = 0;  // Key, MouseButton: the Linux code (KEY_*, BTN_*). MouseMove, Wheel: a Direction.
    bool operator==(const Output&) const = default;
};

// The defaults are antiMicroX's, so that an imported profile feels the same.
inline constexpr double kAxisUnits = 32767.0;
inline constexpr double kDefaultDeadZone = 6000.0 / kAxisUnits;
inline constexpr double kDefaultMaxZone = 32000.0 / kAxisUnits;
inline constexpr int kDefaultMouseSpeed = 50;   // times 20 = pixels a second at full deflection
inline constexpr int kDefaultWheelSpeed = 20;   // notches a second
inline constexpr size_t kMaxBindings = 128;

struct Binding {
    std::string label;
    input::InputId input;
    int side = 0;  // an axis: -1 the lower half of its travel, +1 the upper half
    std::vector<Output> outputs;
    double deadZone = kDefaultDeadZone, maxZone = kDefaultMaxZone;  // of an axis: fractions of the half travel from the centre
    int mouseSpeed = kDefaultMouseSpeed;
    int wheelSpeed = kDefaultWheelSpeed;
    AxisMode mode = AxisMode::Normal;
    MouseCurve curve = MouseCurve::EnhancedPrecision;
    double sensitivity = 1.0;  // the Power curve only
};

struct Config {
    bool enabled = false;       // off until the user turns it on: antiMicroX may still be running next to it
    std::string importedFrom;   // the profile file the bindings came from
    std::vector<Binding> bindings;
    bool isDefault() const { return !enabled && importedFrom.empty() && bindings.empty(); }
};

struct Shortcut {
    std::string label;
    std::vector<int> keys;  // pressed together, in order (KEY_LEFTCTRL, KEY_C)
    bool operator==(const Shortcut&) const = default;
};

inline constexpr int kShortcutSlots = 12;
inline constexpr int kDirectApps = 6;
inline constexpr int kCommsSlots = 1;  // the Comms page: the fourth side key is yours, the others control the sound

inline constexpr int kCurvePoints = 4;
// A brightness curve's level at a room light (0..100 %).
inline int curveAt(const std::array<int, 4>& c, int room) {
    const float p = std::clamp(room, 0, 100) / 100.f * (kCurvePoints - 1);
    const int i = std::min(static_cast<int>(p), kCurvePoints - 2);
    return static_cast<int>(c[i] + (c[i + 1] - c[i]) * (p - i) + 0.5f);
}

struct McduConfig {
    int screenPercent = 80, keysPercent = 60, ledPercent = 80;
    bool autoBrightness = false;  // follow the panel's light sensors
    // Automatic: each light's level at room light 0, 33, 67 and 100 % (dark, dim, lit, bright), straight lines between.
    // Made for an average desk: the screen dims well in the dark, the key labels and lamps stay readable.
    std::array<int, 4> autoScreen{35, 60, 90, 100}, autoKeys{70, 85, 100, 100}, autoLeds{60, 80, 100, 100};
    int sleepMinutes = 30;        // screen and lights off after this long without a key; 0 = never
    std::string keyboardLayout = "qwerty";  // "qwerty" or "azerty": the Type page sends physical keys
    std::vector<Shortcut> shortcuts;        // the Keys page; empty = the built-in set
    std::vector<Shortcut> shortcuts2;       // the Keys 2 page (SEC F-PLN); empty = the built-in set
    std::vector<Shortcut> commsShortcuts;   // the Comms page (ATC COMM): the shortcut of its fourth side key
    std::string commsApp = "goofcord";      // the voice program the Comms page mutes and deafens
    std::vector<std::string> directApps;    // the Direct To page (DIR): what its left side keys open; empty = the built-in set
    std::string homeAirport;                // the Weather page (AIRPORT): four letters, empty = none yet
    bool netBits = false;                   // the Comms page shows Mbit/s instead of MB/s
    std::string systemLook = "list";        // the System page: "list" or "bars"
    std::string gpuView = "both";           // "both" on the page, "switch" with a side key, "page" of their own
    std::string diskView = "switch";        // "switch" with a side key, "pick" from a list, "page" of their own
    std::string diskMount;                  // the disk shown on the System page; empty = the first
    int gpuIndex = 0;                       // the GPU shown when only one is
    int refreshSeconds = 2;                 // 1, 2 or 5
    bool clockUtc = false, clock12h = false;
    std::string font = "built-in";          // the screen's font: "built-in" (the unit's own), "b612" or "aircraft" (sent to the unit)
    bool isDefault() const {
        const McduConfig d;
        return screenPercent == d.screenPercent && keysPercent == d.keysPercent && ledPercent == d.ledPercent && autoBrightness == d.autoBrightness && autoScreen == d.autoScreen && autoKeys == d.autoKeys && autoLeds == d.autoLeds &&
               sleepMinutes == d.sleepMinutes && keyboardLayout == d.keyboardLayout && shortcuts.empty() && shortcuts2.empty() && commsShortcuts.empty() && directApps.empty() && commsApp == d.commsApp && homeAirport.empty() && !netBits && systemLook == d.systemLook && gpuView == d.gpuView &&
               diskView == d.diskView && diskMount.empty() && gpuIndex == 0 && refreshSeconds == d.refreshSeconds && !clockUtc && !clock12h && font == d.font;
    }
};

}  // namespace lr::desktop
