// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// The flaps vibration: no vibration from the jitter of the flaps value at rest, vibration while the flaps really travel.
#include <cstdio>

#include "haptics/FlapsMotion.hpp"

using lr::haptics::FlapsMotion;
using namespace std::chrono_literals;

static int failures = 0;
#define CHECK(cond)                                                                                     \
    do {                                                                                                \
        if (!(cond)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; }      \
    } while (0)

int main() {
    // TRAILING EDGE FLAPS LEFT PERCENT of the MSFS 2024 iniBuilds A320neo, flaps handle 0, read every 250 ms (2026-11-04)
    const double rest[] = {0.00214097, 0.00228887, 0.00220244, 0.00213291, 0.0021083, 0.00219667, 0.0021216, 0.00251643,
                           0.00243838, 0.00247276, 0.00212062, 0.00217323, 0.0021794,  0.00241026, 0.00214067, 0.00230938,
                           0.00240217, 0.00220521, 0.00235273, 0.00308441, 0.00242946, 0.001886,   0.00350668, 0.00202064};
    {
        FlapsMotion f;
        auto t = FlapsMotion::clock::time_point{} + 1h;
        int moving = 0;
        for (int round = 0; round < 10; ++round)  // the sequence again and again, sampled every 33 ms (the vibration loop while flying)
            for (double v : rest) { moving += f.update(v, t); t += 33ms; }
        CHECK(moving == 0);
    }
    // a slow drift at rest (0.1 % per second for a minute) is not travel either
    {
        FlapsMotion f;
        auto t = FlapsMotion::clock::time_point{} + 1h;
        int moving = 0;
        for (int i = 0; i < 60 * 30; ++i) { moving += f.update(0.002 + 0.001 * i / 30.0, t); t += 33ms; }
        CHECK(moving == 0);
    }
    // flaps 0 -> 1 on an airliner: about 20 % of the travel in 6 s; vibrates during the travel, stops within a second after it
    {
        FlapsMotion f;
        auto t = FlapsMotion::clock::time_point{} + 1h;
        for (int i = 0; i < 30; ++i) { f.update(0.002, t); t += 33ms; }
        int moving = 0, samples = 0;
        for (int i = 0; i <= 180; ++i) { moving += f.update(0.002 + 0.2 * i / 180.0, t); ++samples; t += 33ms; }
        CHECK(moving > samples * 9 / 10);
        bool still = true;
        for (int i = 0; i < 30; ++i) { still = f.update(0.202, t); t += 33ms; }
        CHECK(!still);
    }
    std::printf(failures ? "\n%d check(s) FAILED\n" : "all flaps motion checks passed\n", failures);
    return failures ? 1 : 0;
}
