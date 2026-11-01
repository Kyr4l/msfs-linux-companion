// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The helicopter vibration rules, apart from the engine so that a test can run them without a Winwing device.
#include <algorithm>

#include "sim/SimBridge.hpp"

namespace lr::haptics {

// The steady shake of a turning main rotor, 0..1; 0 when the rotor stands still or the aircraft has none. The rotor speed is 1 at 100 %.
// A little comes from the speed and more from the collective (the lever the pilot pulls to climb).
inline float rotorVibration(const sim::Telemetry& t) {
    const double nr = std::clamp(t.rotorRpmPct, 0.0, 1.2);
    if (nr < 0.3) return 0.f;
    return static_cast<float>(std::clamp(0.06 + 0.10 * std::min(1.0, nr) + 0.14 * std::clamp(t.collective, 0.0, 1.0), 0.0, 1.0));
}

// True in flight when the rotor turns below 85 % of its speed: the lower limit of the Cabri G2's rotor gauge (450 of 530 RPM), where its horn sounds.
inline bool rotorLow(const sim::Telemetry& t) { return t.onGround < 0.5 && t.rotorRpmPct > 0.3 && t.rotorRpmPct < 0.85; }

}  // namespace lr::haptics
