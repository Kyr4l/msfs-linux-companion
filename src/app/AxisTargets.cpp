// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/AxisTargets.hpp"

namespace lr::app {

// Sources: the MSFS SDK pages "Aircraft Engine Events" and "Aircraft Flight Control Events" (docs.flightsimulator.com,
// Programming Tools > Event IDs) for the ranges they state, the sim's own aircraft checklists (Checklist/*.xml: the copilot
// fires an event with a parameter, then tests the lever) and its controls menu (Input/*.xml) for which events it binds to its
// axes. A note says when a range is not stated anywhere.
const std::vector<AxisTarget>& axisTargets() {
    // "In the sim's own checklists": the copilot of the aircraft's checklist (Checklist/*.xml, EventID / EventParam) fires the event with that
    // parameter and then tests the lever's position, so the range is what the sim itself uses.
    static const std::vector<AxisTarget> t = {
        {"Engines", "Throttle (all engines)", "THROTTLE_SET", 0, 16383, "Set the throttles exactly: 0 is idle, 16383 full power (SDK: 0 to 16383).", false},
        {"Engines", "Throttle 1", "THROTTLE1_SET", 0, 16383,
         "Throttle lever of engine 1: 0 is closed, 16383 full. In the sim's own checklists (the Spirit of St. Louis, 0 and 16383 test as 0 % and 100 % of the lever).", false},
        {"Engines", "Throttle 2", "THROTTLE2_SET", 0, 16383, "Throttle lever of engine 2: 0 is closed, 16383 full.", false},
        {"Engines", "Throttle 3", "THROTTLE3_SET", 0, 16383, "Throttle lever of engine 3: 0 is closed, 16383 full.", false},
        {"Engines", "Throttle 4", "THROTTLE4_SET", 0, 16383, "Throttle lever of engine 4: 0 is closed, 16383 full.", false},
        {"Engines", "Mixture (all engines)", "MIXTURE_SET", 0, 16383, "Mixture lever of every engine. The range is the one of MIXTURE1_SET; the SDK states none.", false},
        {"Engines", "Mixture 1", "MIXTURE1_SET", 0, 16383,
         "Mixture lever of engine 1: 0 is lean (cut off), 16383 rich. In the sim's own checklists (the Spirit of St. Louis, 16383 tests as 100 % of the lever).", false},
        {"Engines", "Mixture 2", "MIXTURE2_SET", 0, 16383, "Mixture lever of engine 2: 0 is lean, 16383 rich.", false},
        {"Engines", "Mixture 3", "MIXTURE3_SET", 0, 16383, "Mixture lever of engine 3: 0 is lean, 16383 rich.", false},
        {"Engines", "Mixture 4", "MIXTURE4_SET", 0, 16383, "Mixture lever of engine 4: 0 is lean, 16383 rich.", false},
        {"Engines", "Propeller (all engines)", "AXIS_PROPELLER_SET", -16383, 16383,
         "Propeller lever of every engine (SDK: -16383 to +16383). In the sim's own checklists (the Pitts) 16383 tests as 100 % of the lever and -16383 as 0 %. Does nothing on a fixed-pitch propeller.", false},
        {"Engines", "Propeller 1", "AXIS_PROPELLER1_SET", -16383, 16383, "Propeller lever of engine 1 (SDK: -16383 to +16383).", false},
        {"Engines", "Propeller 2", "AXIS_PROPELLER2_SET", -16383, 16383, "Propeller lever of engine 2 (SDK: -16383 to +16383).", false},
        {"Engines", "Propeller 3", "AXIS_PROPELLER3_SET", -16383, 16383, "Propeller lever of engine 3 (SDK: -16383 to +16383).", false},
        {"Engines", "Propeller 4", "AXIS_PROPELLER4_SET", -16383, 16383, "Propeller lever of engine 4 (SDK: -16383 to +16383).", false},

        {"Flight controls", "Elevator", "AXIS_ELEVATOR_SET", -16383, 16383, "Pitch. The SDK states -16383 to 16384.", false},
        {"Flight controls", "Ailerons", "AXIS_AILERONS_SET", -16383, 16383, "Roll. The SDK states -16383 to 16384.", false},
        {"Flight controls", "Rudder", "AXIS_RUDDER_SET", -16383, 16383, "Yaw, and the steering of the nose or tail wheel. The SDK states -16383 to 16383.", false},
        {"Flight controls", "Elevator trim", "AXIS_ELEV_TRIM_SET", -16383, 16383, "The trim wheel. The SDK states -16383 to 16384.", false},
        {"Flight controls", "Flaps", "AXIS_FLAPS_SET", -16383, 16383, "The flap handle moves to the nearest notch. The SDK states -16383 to 16383.", false},
        {"Flight controls", "Spoilers", "SPOILERS_SET", 0, 16383, "Spoiler or airbrake handle (SDK: 0 to 16383).", false},

        {"Brakes", "Left brake", "AXIS_LEFT_BRAKE_SET", -16383, 16383, "Toe brake, -16383 is released and 16383 full braking.", false},
        {"Brakes", "Right brake", "AXIS_RIGHT_BRAKE_SET", -16383, 16383, "Toe brake, -16383 is released and 16383 full braking.", false},

        // the events the sim's own controls menu binds to its axes (Input/*.xml): same levers, other events
        {"The sim's own axis events", "Throttle (all engines), with reverse", "THROTTLE_AXIS_SET_EX1", 0, 16383,
         "The event the sim's controls menu binds to the throttle axis. 0 is idle, 16383 full power; with a throttle that has reverse thrust the range is -16384 to 16383.", false},
        {"The sim's own axis events", "Throttle 1, with reverse", "THROTTLE1_AXIS_SET_EX1", 0, 16383, "As above, engine 1 only.", false},
        {"The sim's own axis events", "Throttle 2, with reverse", "THROTTLE2_AXIS_SET_EX1", 0, 16383, "As above, engine 2 only.", false},
        {"The sim's own axis events", "Throttle 3, with reverse", "THROTTLE3_AXIS_SET_EX1", 0, 16383, "As above, engine 3 only.", false},
        {"The sim's own axis events", "Throttle 4, with reverse", "THROTTLE4_AXIS_SET_EX1", 0, 16383, "As above, engine 4 only.", false},
        {"The sim's own axis events", "Mixture (all engines)", "AXIS_MIXTURE_SET", -16383, 16383, "The mixture axis of the controls menu. The SDK does not state the range.", false},
        {"The sim's own axis events", "Mixture 1", "AXIS_MIXTURE1_SET", -16383, 16383, "As above, engine 1 only.", false},
        {"The sim's own axis events", "Mixture 2", "AXIS_MIXTURE2_SET", -16383, 16383, "As above, engine 2 only.", false},
        {"The sim's own axis events", "Mixture 3", "AXIS_MIXTURE3_SET", -16383, 16383, "As above, engine 3 only.", false},
        {"The sim's own axis events", "Mixture 4", "AXIS_MIXTURE4_SET", -16383, 16383, "As above, engine 4 only.", false},
        {"The sim's own axis events", "Propeller pitch (all engines)", "PROP_PITCH_AXIS_SET_EX1", 0, 16383,
         "The propeller axis of the controls menu. The SDK does not state the range; PROP_PITCH_SET takes 0 to 16383.", false},
        {"The sim's own axis events", "Propeller pitch 1", "PROP_PITCH1_AXIS_SET_EX1", 0, 16383, "As above, engine 1 only.", false},
        {"The sim's own axis events", "Propeller pitch 2", "PROP_PITCH2_AXIS_SET_EX1", 0, 16383, "As above, engine 2 only.", false},
        {"The sim's own axis events", "Propeller pitch 3", "PROP_PITCH3_AXIS_SET_EX1", 0, 16383, "As above, engine 3 only.", false},
        {"The sim's own axis events", "Propeller pitch 4", "PROP_PITCH4_AXIS_SET_EX1", 0, 16383, "As above, engine 4 only.", false},
        {"The sim's own axis events", "Spoilers (controls menu)", "AXIS_SPOILER_SET", -16383, 16383, "The spoiler axis of the controls menu. The SDK does not state the range.", false},
    };
    return t;
}

// Sources: the MSFS controls menu binds KOHLSMAN_INC, KOHLSMAN_DEC and BAROMETRIC (Input profiles of the MSFS 2024 install); the Garmin G1000 NXi (the
// C172's avionics) intercepts BAROMETRIC (sets the baro to the local value) and BAROMETRIC_STD_PRESSURE (switches STD baro on and off) in
// src/workingtitle-instruments-g1000/html_ui/Shared/Input/AltimeterBaroKeyEventHandler.ts of github.com/microsoft/msfs-avionics-mirror.
const std::vector<CommonControl>& commonControls() {
    static const std::vector<CommonControl> t = {
        {"Altimeter", "Baro setting up", "KOHLSMAN_INC", "baro barometric pressure qnh kohlsman altimeter up higher",
         "One step up per press; a button repeats while held. The sim's controls menu binds this event.", true},
        {"Altimeter", "Baro setting down", "KOHLSMAN_DEC", "baro barometric pressure qnh kohlsman altimeter down lower",
         "One step down per press; a button repeats while held. The sim's controls menu binds this event.", true},
        {"Altimeter", "STD baro toggle", "BAROMETRIC_STD_PRESSURE", "baro std standard pressure 1013 29.92 qnh altimeter toggle",
         "Switches STD baro on and off on the Garmin G1000 NXi (source: its key event handler). Not in the sim's controls menu.", false},
        {"Altimeter", "Baro: local pressure", "BAROMETRIC", "baro barometric pressure qnh local sync altimeter",
         "Sets the altimeter to the local pressure (the SDK: sea level pressure; the G1000 NXi sets its baro). The sim's controls menu binds this event.", false},
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
