// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/AxisTargets.hpp"

namespace lr::app {

// Sources: the MSFS SDK pages "Aircraft Engine Events" and "Aircraft Flight Control Events" (docs.flightsimulator.com,
// Programming Tools > Event IDs) for the ranges they state, the sim's own aircraft checklists (Checklist/*.xml: the copilot
// fires an event with a parameter, then tests the lever) and its controls menu (Input/*.xml) for which events it binds to its
// axes. Evidence for the ranges: THROTTLE1_SET 0..16383 and MIXTURE1_SET 0 lean / 16383 rich (the Spirit of St. Louis checklist tests them as 0 and
// 100 % of the lever), AXIS_PROPELLER_SET -16383..16383 (the Pitts checklist). The SDK does not state the range of the AXIS_MIXTURE_SET and
// PROP_PITCH_AXIS_SET_EX1 events; THROTTLE_AXIS_SET_EX1 is -16384..16383 on a throttle with reverse.
const std::vector<AxisTarget>& axisTargets() {
    // "In the sim's own checklists": the copilot of the aircraft's checklist (Checklist/*.xml, EventID / EventParam) fires the event with that
    // parameter and then tests the lever's position, so the range is what the sim itself uses.
    static const std::vector<AxisTarget> t = {
        {"Engines", "Throttle (all engines)", "THROTTLE_SET", 0, 16383, "0 = idle, 16383 = full", false},
        {"Engines", "Throttle 1", "THROTTLE1_SET", 0, 16383,
         "0 = idle, 16383 = full", false},
        {"Engines", "Throttle 2", "THROTTLE2_SET", 0, 16383, "0 = idle, 16383 = full", false},
        {"Engines", "Throttle 3", "THROTTLE3_SET", 0, 16383, "0 = idle, 16383 = full", false},
        {"Engines", "Throttle 4", "THROTTLE4_SET", 0, 16383, "0 = idle, 16383 = full", false},
        {"Engines", "Mixture (all engines)", "MIXTURE_SET", 0, 16383, "0 = lean, 16383 = rich", false},
        {"Engines", "Mixture 1", "MIXTURE1_SET", 0, 16383,
         "0 = lean, 16383 = rich", false},
        {"Engines", "Mixture 2", "MIXTURE2_SET", 0, 16383, "0 = lean, 16383 = rich", false},
        {"Engines", "Mixture 3", "MIXTURE3_SET", 0, 16383, "0 = lean, 16383 = rich", false},
        {"Engines", "Mixture 4", "MIXTURE4_SET", 0, 16383, "0 = lean, 16383 = rich", false},
        {"Engines", "Propeller (all engines)", "AXIS_PROPELLER_SET", -16383, 16383,
         "-16383 to 16383", false},
        {"Engines", "Propeller 1", "AXIS_PROPELLER1_SET", -16383, 16383, "-16383 to 16383", false},
        {"Engines", "Propeller 2", "AXIS_PROPELLER2_SET", -16383, 16383, "-16383 to 16383", false},
        {"Engines", "Propeller 3", "AXIS_PROPELLER3_SET", -16383, 16383, "-16383 to 16383", false},
        {"Engines", "Propeller 4", "AXIS_PROPELLER4_SET", -16383, 16383, "-16383 to 16383", false},

        {"Flight controls", "Elevator", "AXIS_ELEVATOR_SET", -16383, 16383, "Pitch", false},
        {"Flight controls", "Ailerons", "AXIS_AILERONS_SET", -16383, 16383, "Roll", false},
        {"Flight controls", "Rudder", "AXIS_RUDDER_SET", -16383, 16383, "Yaw and steering", false},
        {"Flight controls", "Elevator trim", "AXIS_ELEV_TRIM_SET", -16383, 16383, "Trim wheel", false},
        {"Flight controls", "Flaps", "AXIS_FLAPS_SET", -16383, 16383, "Flap handle", false},
        {"Flight controls", "Spoilers", "SPOILERS_SET", 0, 16383, "Spoiler handle", false},

        {"Brakes", "Left brake", "AXIS_LEFT_BRAKE_SET", -16383, 16383, "-16383 = released, 16383 = full", false},
        {"Brakes", "Right brake", "AXIS_RIGHT_BRAKE_SET", -16383, 16383, "-16383 = released, 16383 = full", false},

        // the events the sim's own controls menu binds to its axes (Input/*.xml): same levers, other events
        {"MSFS controls menu axes", "Throttle (all engines), with reverse", "THROTTLE_AXIS_SET_EX1", 0, 16383,
         "Same axis as the MSFS controls menu", false},
        {"MSFS controls menu axes", "Throttle 1, with reverse", "THROTTLE1_AXIS_SET_EX1", 0, 16383, "Same axis as the MSFS controls menu", false},
        {"MSFS controls menu axes", "Throttle 2, with reverse", "THROTTLE2_AXIS_SET_EX1", 0, 16383, "Same axis as the MSFS controls menu", false},
        {"MSFS controls menu axes", "Throttle 3, with reverse", "THROTTLE3_AXIS_SET_EX1", 0, 16383, "Same axis as the MSFS controls menu", false},
        {"MSFS controls menu axes", "Throttle 4, with reverse", "THROTTLE4_AXIS_SET_EX1", 0, 16383, "Same axis as the MSFS controls menu", false},
        {"MSFS controls menu axes", "Mixture (all engines)", "AXIS_MIXTURE_SET", -16383, 16383, "Same axis as the MSFS controls menu", false},
        {"MSFS controls menu axes", "Mixture 1", "AXIS_MIXTURE1_SET", -16383, 16383, "Same axis as the MSFS controls menu", false},
        {"MSFS controls menu axes", "Mixture 2", "AXIS_MIXTURE2_SET", -16383, 16383, "Same axis as the MSFS controls menu", false},
        {"MSFS controls menu axes", "Mixture 3", "AXIS_MIXTURE3_SET", -16383, 16383, "Same axis as the MSFS controls menu", false},
        {"MSFS controls menu axes", "Mixture 4", "AXIS_MIXTURE4_SET", -16383, 16383, "Same axis as the MSFS controls menu", false},
        {"MSFS controls menu axes", "Propeller pitch (all engines)", "PROP_PITCH_AXIS_SET_EX1", 0, 16383,
         "Same axis as the MSFS controls menu", false},
        {"MSFS controls menu axes", "Propeller pitch 1", "PROP_PITCH1_AXIS_SET_EX1", 0, 16383, "Same axis as the MSFS controls menu", false},
        {"MSFS controls menu axes", "Propeller pitch 2", "PROP_PITCH2_AXIS_SET_EX1", 0, 16383, "Same axis as the MSFS controls menu", false},
        {"MSFS controls menu axes", "Propeller pitch 3", "PROP_PITCH3_AXIS_SET_EX1", 0, 16383, "Same axis as the MSFS controls menu", false},
        {"MSFS controls menu axes", "Propeller pitch 4", "PROP_PITCH4_AXIS_SET_EX1", 0, 16383, "Same axis as the MSFS controls menu", false},
        {"MSFS controls menu axes", "Spoilers (controls menu)", "AXIS_SPOILER_SET", -16383, 16383, "Same axis as the MSFS controls menu", false},
    };
    return t;
}

// Sources: the MSFS controls menu binds KOHLSMAN_INC, KOHLSMAN_DEC and BAROMETRIC (Input profiles of the MSFS 2024 install). The Garmin G1000 NXi (the
// C172's avionics) intercepts BAROMETRIC and BAROMETRIC_STD_PRESSURE (Shared/Input/AltimeterBaroKeyEventHandler.ts in
// github.com/microsoft/msfs-avionics-mirror, folder src/workingtitle-instruments-g1000/html_ui) and reads what they do in PFD/Components/FlightInstruments/Altimeter.tsx:
//  * BAROMETRIC_STD_PRESSURE publishes "STD on": it only switches STD baro ON, never off (a second press while on STD overwrites the remembered
//    local setting with 29.92). It is not a toggle.
//  * BAROMETRIC (the 'B' key) acts by the indicated altitude: above 18000 ft it switches STD on, at or below it switches STD off (without restoring
//    the local setting it remembered: the setting stays 29.92 inHg until it is set again).
//  * The knob steps AS1000_PFD_BARO_INC / _DEC (H: events) leave STD at once and put back the local setting that was remembered, then step.
//  * KOHLSMAN_INC / _DEC are not intercepted: they move the sim's setting, which the display follows, but not out of STD.
// None of this has been tried on a real cockpit yet.
const std::vector<CommonControl>& commonControls() {
    static const std::vector<CommonControl> t = {
        {"Altimeter", "Baro up", "KOHLSMAN_INC", "baro barometric pressure qnh kohlsman altimeter up higher setting",
         "One step up. Repeats while held.", true},
        {"Altimeter", "Baro down", "KOHLSMAN_DEC", "baro barometric pressure qnh kohlsman altimeter down lower setting",
         "One step down. Repeats while held.", true},
        {"Altimeter", "STD baro", "BAROMETRIC_STD_PRESSURE", "baro std standard pressure 1013 29.92 qnh altimeter toggle",
         "Sets STD. On the G1000 it does not switch back: use G1000 baro up/down.", false},
        {"Altimeter", "Baro: local", "BAROMETRIC", "baro barometric pressure qnh local sync altimeter",
         "Local pressure. On the G1000: STD on above 18000 ft, off below.", false},
        {"Altimeter", "G1000 baro up", "AS1000_PFD_BARO_INC", "baro g1000 as1000 knob std standard qnh altimeter up higher leave",
         "G1000 baro knob up. Leaves STD. Needs MobiFlight.", true, true},
        {"Altimeter", "G1000 baro down", "AS1000_PFD_BARO_DEC", "baro g1000 as1000 knob std standard qnh altimeter down lower leave",
         "G1000 baro knob down. Leaves STD. Needs MobiFlight.", true, true},
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
