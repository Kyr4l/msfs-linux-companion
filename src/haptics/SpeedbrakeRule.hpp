// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The speed brake vibration, apart from the engine so that a test can run it without a Winwing device.
#include <algorithm>

#include "sim/SimBridge.hpp"

namespace lr::haptics {

// Spoilers out in flight, 0..1 (0 = no vibration): more with the handle and the airspeed.
// The handle variable alone is not enough: a speed brake lever bound in the sim's own controls moves it on every aircraft, also on one
// without spoilers (seen on the MSFS 2024 DHC-6 Twin Otter), so the aircraft must also say it has spoilers.
inline double speedbrakeLevel(const sim::Telemetry& t) {
    if (t.onGround > 0.5 || t.spoilerAvailable < 0.5 || t.spoilersHandle <= 0.05) return 0.0;
    return 0.08 + 0.35 * std::min(1.0, t.spoilersHandle) * std::clamp(t.iasKt / 250.0, 0.0, 1.0);
}

}  // namespace lr::haptics
