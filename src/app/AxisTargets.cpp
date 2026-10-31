// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/AxisTargets.hpp"

namespace lr::app {

// Sources: the MSFS SDK pages "Aircraft Engine Events" and "Aircraft Flight Control Events" (docs.flightsimulator.com,
// Programming Tools > Event IDs) for the ranges they state, and the sim's own controls menu (Input/*.xml in the install)
// for which events it binds to its throttle, mixture and propeller axes. A note says when a range is not stated anywhere.
const std::vector<AxisTarget>& axisTargets() {
    static const std::vector<AxisTarget> t = {
        {"Engines", "Throttle (all engines)", "THROTTLE_AXIS_SET_EX1", 0, 16383,
         "The event the sim's own controls menu binds to the throttle axis. 0 is idle, 16383 full power; with a throttle that has reverse thrust the range is -16384 to 16383.", false},
        {"Engines", "Throttle 1", "THROTTLE1_AXIS_SET_EX1", 0, 16383, "Throttle lever of engine 1 only: 0 is idle, 16383 full power.", false},
        {"Engines", "Throttle 2", "THROTTLE2_AXIS_SET_EX1", 0, 16383, "Throttle lever of engine 2 only: 0 is idle, 16383 full power.", false},
        {"Engines", "Throttle 3", "THROTTLE3_AXIS_SET_EX1", 0, 16383, "Throttle lever of engine 3 only: 0 is idle, 16383 full power.", false},
        {"Engines", "Throttle 4", "THROTTLE4_AXIS_SET_EX1", 0, 16383, "Throttle lever of engine 4 only: 0 is idle, 16383 full power.", false},
        {"Engines", "Mixture (all engines)", "AXIS_MIXTURE_SET", -16383, 16383, "Mixture lever. The SDK does not state the range of this event.", false},
        {"Engines", "Mixture 1", "AXIS_MIXTURE1_SET", -16383, 16383, "Mixture lever of engine 1.", false},
        {"Engines", "Mixture 2", "AXIS_MIXTURE2_SET", -16383, 16383, "Mixture lever of engine 2.", false},
        {"Engines", "Mixture 3", "AXIS_MIXTURE3_SET", -16383, 16383, "Mixture lever of engine 3.", false},
        {"Engines", "Mixture 4", "AXIS_MIXTURE4_SET", -16383, 16383, "Mixture lever of engine 4.", false},
        {"Engines", "Propeller (all engines)", "PROP_PITCH_AXIS_SET_EX1", 0, 16383,
         "Propeller pitch lever, the event the sim's own controls menu binds to the propeller axis. Does nothing on a fixed-pitch propeller.", false},
        {"Engines", "Propeller 1", "PROP_PITCH1_AXIS_SET_EX1", 0, 16383, "Propeller pitch lever of engine 1.", false},
        {"Engines", "Propeller 2", "PROP_PITCH2_AXIS_SET_EX1", 0, 16383, "Propeller pitch lever of engine 2.", false},
        {"Engines", "Propeller 3", "PROP_PITCH3_AXIS_SET_EX1", 0, 16383, "Propeller pitch lever of engine 3.", false},
        {"Engines", "Propeller 4", "PROP_PITCH4_AXIS_SET_EX1", 0, 16383, "Propeller pitch lever of engine 4.", false},

        {"Flight controls", "Elevator", "AXIS_ELEVATOR_SET", -16383, 16383, "Pitch. The SDK states -16383 to 16384.", false},
        {"Flight controls", "Ailerons", "AXIS_AILERONS_SET", -16383, 16383, "Roll. The SDK states -16383 to 16384.", false},
        {"Flight controls", "Rudder", "AXIS_RUDDER_SET", -16383, 16383, "Yaw, and the steering of the nose or tail wheel. The SDK states -16383 to 16383.", false},
        {"Flight controls", "Elevator trim", "AXIS_ELEV_TRIM_SET", -16383, 16383, "The trim wheel. The SDK states -16383 to 16384.", false},
        {"Flight controls", "Flaps", "AXIS_FLAPS_SET", -16383, 16383, "The flap handle moves to the nearest notch. The SDK states -16383 to 16383.", false},
        {"Flight controls", "Spoilers", "AXIS_SPOILER_SET", -16383, 16383, "Spoiler or airbrake handle.", false},

        {"Brakes", "Left brake", "AXIS_LEFT_BRAKE_SET", -16383, 16383, "Toe brake, -16383 is released and 16383 full braking.", false},
        {"Brakes", "Right brake", "AXIS_RIGHT_BRAKE_SET", -16383, 16383, "Toe brake, -16383 is released and 16383 full braking.", false},

        {"Engines (alternative events)", "Throttle (all engines), exact", "THROTTLE_SET", 0, 16383,
         "Set all the throttles exactly (0 to 16383, the SDK). Same effect as the first entry; try it if that one does not move the lever of an aircraft.", false},
        {"Engines (alternative events)", "Throttle 1, exact", "THROTTLE1_SET", 0, 16383, "Set throttle 1 exactly (0 to 16383, the SDK).", false},
        {"Engines (alternative events)", "Throttle 2, exact", "THROTTLE2_SET", 0, 16383, "Set throttle 2 exactly (0 to 16383, the SDK).", false},
        {"Engines (alternative events)", "Throttle 3, exact", "THROTTLE3_SET", 0, 16383, "Set throttle 3 exactly (0 to 16383, the SDK).", false},
        {"Engines (alternative events)", "Throttle 4, exact", "THROTTLE4_SET", 0, 16383, "Set throttle 4 exactly (0 to 16383, the SDK).", false},
        {"Engines (alternative events)", "Spoilers, exact", "SPOILERS_SET", 0, 16383, "Set the spoiler handle (0 to 16383, the SDK).", false},
    };
    return t;
}

const AxisTarget* findAxisTarget(const std::string& event) {
    const std::string name = event.rfind("K:", 0) == 0 ? event.substr(2) : event;
    for (const auto& t : axisTargets())
        if (name == t.event) return &t;
    return nullptr;
}

}  // namespace lr::app
