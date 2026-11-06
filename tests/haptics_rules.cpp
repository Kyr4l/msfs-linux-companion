// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// The speed brake vibration: only on an aircraft that has spoilers, in flight, with the handle out.
#include <cstdio>

#include "haptics/SpeedbrakeRule.hpp"

using lr::haptics::speedbrakeLevel;

static int failures = 0;
#define CHECK(cond)                                                                                     \
    do {                                                                                                \
        if (!(cond)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; }      \
    } while (0)

int main() {
    lr::sim::Telemetry t;
    t.onGround = 0;
    t.iasKt = 80;
    t.spoilersHandle = 0.4;

    // a DHC-6 Twin Otter: no spoilers, but the handle variable follows a speed brake lever bound in the sim
    t.spoilerAvailable = 0;
    CHECK(speedbrakeLevel(t) == 0.0);

    // an airliner with spoilers out in flight
    t.spoilerAvailable = 1;
    CHECK(speedbrakeLevel(t) > 0.08);
    t.iasKt = 250;
    const double fast = speedbrakeLevel(t);
    t.iasKt = 120;
    CHECK(fast > speedbrakeLevel(t));
    CHECK(fast <= 0.43 + 1e-9);

    // handle stowed, on the ground
    t.spoilersHandle = 0.02;
    CHECK(speedbrakeLevel(t) == 0.0);
    t.spoilersHandle = 1.0;
    t.onGround = 1;
    CHECK(speedbrakeLevel(t) == 0.0);

    // a negative airspeed (backwards on the ground, sensor noise) does not lower the level below the base
    t.onGround = 0;
    t.iasKt = -5;
    CHECK(speedbrakeLevel(t) == 0.08);

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("haptics_rules: ok\n");
    return 0;
}
