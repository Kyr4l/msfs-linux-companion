// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// The speed brake vibration: only on an aircraft that has spoilers, in flight, with the handle out.
// The engine rumble on the ground: not for a helicopter with its rotor turning.
#include <cstdio>

#include "haptics/RotorRules.hpp"
#include "haptics/SpeedbrakeRule.hpp"

using lr::haptics::groundEngineLevel;
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

    // a jet at takeoff thrust on the runway rumbles; at idle it does not
    lr::sim::Telemetry e;
    e.onGround = 1;
    e.engineType = 1;
    e.n1Eng1 = e.n1Eng2 = 95;
    CHECK(groundEngineLevel(e) > 0.25);
    e.n1Eng1 = e.n1Eng2 = 25;
    CHECK(groundEngineLevel(e) == 0.0);
    // the H125 on the ground: torque ~90 % while the rotor spins up (seen live 2026-11-26), no engine rumble
    e.engineType = 3;
    e.torqueEng1 = 90;
    e.rotorRpmPct = 0.3;
    CHECK(groundEngineLevel(e) == 0.0);
    // the Cabri G2 (piston) governed at 100 % RPM on the ground with its rotor at 100 %
    e.engineType = 0;
    e.rpmPctEng1 = 100;
    e.rotorRpmPct = 1.0;
    CHECK(groundEngineLevel(e) == 0.0);
    // the same piston engine in an aeroplane (no rotor) at full RPM rumbles
    e.rotorRpmPct = 0;
    CHECK(groundEngineLevel(e) > 0.3);

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("haptics_rules: ok\n");
    return 0;
}
