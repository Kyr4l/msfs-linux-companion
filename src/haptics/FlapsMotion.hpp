// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Are the flaps moving? The sim's flaps value jitters a little at rest (the iniBuilds A320neo of MSFS 2024, flaps up: about 0.2 %, wobbling by
// +-0.05 % every frame), so a rate between two samples taken 33 ms apart is noise. The flaps count as moving once they are kStep away from where
// they last stood, and for kHold after each such step.
#include <chrono>
#include <cmath>

namespace lr::haptics {

class FlapsMotion {
public:
    using clock = std::chrono::steady_clock;
    static constexpr double kStep = 0.005;  // 0.5 % of the travel: above the jitter at rest, passed within a fraction of a second by real travel
    static constexpr std::chrono::milliseconds kHold{600};
    static constexpr std::chrono::seconds kFollow{2};  // at rest the reference follows a slow drift, so the drift never adds up to a step

    // value: flaps position 0..1. Returns true while the flaps are moving.
    bool update(double value, clock::time_point now) {
        if (!anchored_ || std::fabs(value - anchor_) >= kStep) {
            if (anchored_) movingUntil_ = now + kHold;
            anchored_ = true;
            anchor_ = value;
            anchorAt_ = now;
        } else if (now - anchorAt_ > kFollow) {
            anchor_ = value;
            anchorAt_ = now;
        }
        return now < movingUntil_;
    }
    void reset() { *this = FlapsMotion(); }

private:
    bool anchored_ = false;
    double anchor_ = 0;
    clock::time_point anchorAt_{}, movingUntil_{};
};

}  // namespace lr::haptics
