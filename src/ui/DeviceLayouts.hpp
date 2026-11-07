// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Names of the controls of devices whose layout is known, for the key caps and sections of the Bindings list ("B5", "MODE 3" on the
// "Base" of the throttle). Unknown devices and controls give an empty name: the caller falls back to the plain button number.
//
// WINCTRL URSA MINOR Combat Throttle Metal(.EX) (4098:b970): from WINCTRL's user manuals (U.COMBAT.T.MET.1 key layout, U.COMBAT.T.EX.1
// key layout). The manual's button N is evdev 287 + N up to 16 and 687 + N from 17 (checked: the gate switches 16..23 are the codes the
// calibration learned, 303 and 704..710).
#include <string>

namespace lr::ui {

struct ControlName {
    std::string section;  // "Base", "Grip", "EX panel", "Thrust levers"
    std::string name;     // "B5", "MODE 3", "SW4 up"
};

inline ControlName knownControl(const std::string& deviceKey, bool axis, int code) {
    if (deviceKey.rfind("4098:b970", 0) != 0) return {};
    if (axis) {
        switch (code) {
            case 3: return {"Thrust levers", "Lever 1"};
            case 4: return {"Thrust levers", "Lever 2"};
            case 0: return {"EX panel", "Wheel L"};
            case 1: return {"EX panel", "Wheel R"};
            case 2: return {"Grip", "Trim wheel"};
            case 5: return {"Grip", "Front wheel"};
            case 6: case 7: return {"Grip", "Mini-stick"};
            default: return {};
        }
    }
    const int n = code <= 303 ? code - 287 : code - 687;
    // words, not arrows: the built-in font has no arrow characters
    static const char* const up = "up", *down = "down", *left = "left", *right = "right", *mid = "mid", *minus = "-";
    auto s = [](const char* a, const char* b) { return std::string(a) + " " + b; };
    if (n >= 1 && n <= 8) return {"Base", "B" + std::to_string(n)};
    if (n >= 9 && n <= 11) return {"Base", "MODE " + std::to_string(n - 8)};
    if (n == 12 || n == 13) return {"Base", s("E1", n == 12 ? minus : "+")};
    if (n == 14 || n == 15) return {"Base", s("E2", n == 14 ? minus : "+")};
    if (n >= 16 && n <= 23) {
        static const char* const g[] = {"AB", "MIL", "IDLE", "OFF"};
        return {"Thrust levers", std::string("Gate ") + g[(n - 16) % 4] + " " + (n < 20 ? "1" : "2")};
    }
    if (n == 24 || n == 25) return {"Thrust levers", "Lift " + std::to_string(n - 23)};
    if (n == 27) return {"Grip", "Thumb"};
    if (n == 28 || n == 29) return {"Grip", "Btn " + std::to_string(n)};
    if (n >= 30 && n <= 32) return {"Grip", s("Slide", n == 30 ? "1" : n == 31 ? mid : "2")};
    if (n >= 33 && n <= 35) return {"Grip", s("Toggle", n == 33 ? up : n == 34 ? mid : down)};
    if (n >= 36 && n <= 40) { const char* d[] = {up, right, down, left, "push"}; return {"Grip", s("H3", d[n - 36])}; }
    if (n >= 41 && n <= 45) { const char* d[] = {left, up, right, down, "push"}; return {"Grip", s("H2", d[n - 41])}; }
    if (n >= 46 && n <= 50) { const char* d[] = {left, up, right, down, "push"}; return {"Grip", s("H1", d[n - 46])}; }
    if (n == 51) return {"Grip", "Stick push"};
    if (n >= 52 && n <= 55) { const char* d[] = {up, right, down, left}; return {"Grip", s("Stick", d[n - 52])}; }
    if (n == 56) return {"Grip", "Front wheel"};
    if (n >= 57 && n <= 59) return {"Grip", s("Trim", n == 57 ? mid : n == 58 ? up : down)};
    if (n == 60 || n == 61) return {"Grip", "Lock " + std::to_string(n - 59)};
    if (n == 62) return {"EX panel", "START"};
    if (n >= 63 && n <= 65) return {"EX panel", s("SW1", n == 63 ? up : n == 64 ? mid : down)};
    if (n >= 66 && n <= 68) return {"EX panel", s("SW4", n == 66 ? up : n == 67 ? mid : down)};
    if (n >= 69 && n <= 71) return {"EX panel", s("SW7", n == 69 ? minus : n == 70 ? mid : "+")};
    if (n >= 72 && n <= 74) return {"EX panel", s("SW10", n == 72 ? minus : n == 73 ? mid : "+")};
    if (n >= 75 && n <= 77) return {"EX panel", s("RUD", n == 75 ? "L" : n == 76 ? mid : "R")};
    if (n >= 78 && n <= 81) return {"EX panel", "Wheel " + std::to_string(n - 77)};
    return {};
}

}  // namespace lr::ui
