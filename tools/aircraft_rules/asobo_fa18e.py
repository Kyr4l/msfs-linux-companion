# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Rules that label the cockpit input events of the Asobo Boeing F/A-18E Super Hornet (build_aircraft_catalog.py).

Names come from the event names and from the sim's own checklist (Official/Steam/asobo-aircraft-fa18e/.../Checklist/
FA18E_Checklist.xml). Behaviours come from trying the controls on a running F/A-18E with `--script` (set the event,
read the simulator's own A: variable, compare): only those carry a VERIFIED note. Everything else says it was not tried.
"""
import re

from common import NOT_TRIED, knob, latched, level, press, row, unknown

AIRCRAFT = "Asobo_FA18E"
TITLE = "Boeing F/A-18E Super Hornet (Asobo)"
EVENTS_FILE = "asobo_fa18e_events.json"
SOURCES = [
    "Cockpit input events enumerated from MSFS 11.0.282174.999 (SimConnect EnumerateInputEvents)",
    "Asobo_FA18E/Checklist/FA18E_Checklist.xml shipped with the aircraft",
]

P_ENG = "Engines and throttles"
P_ELEC = "Electrical and APU"
P_LIGHT = "Lights"
P_GEAR = "Gear, brakes and hook"
P_FLT = "Flaps, wings and trim"
P_ICE = "Anti-ice and bleed air"
P_COMM = "UFCD, radios and TACAN"
P_HUD = "HUD and helmet display"
P_DDI = "Display units"
P_EFD = "Engine and fuel display (EFD)"
P_OTHER = "Other"
PANEL_ORDER = [P_ENG, P_ELEC, P_LIGHT, P_GEAR, P_FLT, P_ICE, P_COMM, P_HUD, P_DDI, P_EFD, P_OTHER]

SEEN = "Seen on a running F/A-18E:"
VERIFIED = {
    "ELECTRICAL_Battery_1": "Verified on an F/A-18E: set 1 = ON, set 0 = OFF (the opposite of the CJ4), a second set 1 changes nothing; read back with (A:ELECTRICAL MASTER BATTERY).",
    "ELECTRICAL_Alternator_1": "Verified on an F/A-18E: set 0 = OFF, set 1 = ON, read back with (A:GENERAL ENG MASTER ALTERNATOR:1).",
    "ELECTRICAL_Alternator_2": "Verified on an F/A-18E: set 0 = OFF, set 1 = ON, read back with (A:GENERAL ENG MASTER ALTERNATOR:2).",
    "ELECTRICAL_APU_Starter": "Verified on an F/A-18E: set 1 starts the APU ((A:APU SWITCH) = 1, (A:APU PCT RPM) reached 99.7 %), set 0 switches it off.",
    "ELECTRICAL_External_Power": "Verified on an F/A-18E: a three-position switch: set 0 puts external power on, set 2 takes it off ((A:EXTERNAL POWER ON:1) 1 / 0), set 1 is the neutral centre and changes nothing.",
    "ENGINE_Starter_Ignition_1": "Verified on an F/A-18E (APU running): the engine crank switch: set 0 cranks engine 1 (left), set 2 cranks engine 2 (right) ((A:GENERAL ENG STARTER:n) = 1, N2 rises about 4 % a second), set 1 is the centre (off). Both engines were started with it.",
    "ENGINE_Throttle_1": "Verified on an F/A-18E: the value is the lever position as a fraction: 0.5 puts the lever at 50 %, 1 or more at 80 % (the military-power stop; afterburner is its own event), 0 from a higher position or 0.1 from the stop at 0 puts it at the idle stop (10 %). A set 0 while the lever already is at 0 changes nothing. Light-off at 12 % N2 needs the lever at idle ((A:GENERAL ENG THROTTLE LEVER POSITION:1)).",
    "ENGINE_Throttle_2": "Verified on an F/A-18E: the value is the lever position as a fraction: 0.5 puts the lever at 50 %, 1 or more at 80 % (the military-power stop; afterburner is its own event), 0 from a higher position or 0.1 from the stop at 0 puts it at the idle stop (10 %). A set 0 while the lever already is at 0 changes nothing. Light-off at 12 % N2 needs the lever at idle ((A:GENERAL ENG THROTTLE LEVER POSITION:2)).",
    "HANDLING_Flaps": "Verified on an F/A-18E: an axis from 0 to 16384; 0 = flap position 0, 8192 = position 1, 16384 = position 2 ((A:FLAPS HANDLE INDEX) 0 / 1 / 2, (A:FLAPS HANDLE PERCENT) 0 / 50 / 100). Values 0.5, 1 and 2 do nothing.",
    "HANDLING_Anti_Skid": "Verified on an F/A-18E: set 1 / set 0 switches the anti-skid, read back with (A:ANTISKID BRAKES ACTIVE).",
    "HANDLING_Lever_WingFold": "Verified on an F/A-18E (engines running): set 1 folds the wings, set 0 spreads them ((A:FOLDING WING LEFT PERCENT) and RIGHT moved to 47 % / 64 % within 4 s and back to 0 within 5 s).",
    "HANDLING_Lever_TailHook": "Verified on an F/A-18E: set 1 lowers the hook ((A:TAILHOOK POSITION) 97.9 %, (A:TAILHOOK HANDLE) = 1), set 0 raises it.",
    "INSTRUMENT_Altimeter_1": "Verified on an F/A-18E: the value is the altimeter setting in hPa ((A:KOHLSMAN SETTING MB:1) followed set 1000 and set 1013.25).",
    "LIGHTING_LANDING_1": "Verified on an F/A-18E: set 1 / set 0 switches the landing light, read back with (A:LIGHT LANDING).",
    "LIGHTING_CABIN_1": "Verified on an F/A-18E: set 1 / set 0 switches cabin light 1, read back with (A:LIGHT CABIN:1).",
    "LIGHTING_STROBE_1": "Verified on an F/A-18E, INVERTED: set 0 switches the strobes ON, set 1 switches them OFF, read back with (A:LIGHT STROBE).",
    "DEICE_Engine_1": "Verified on an F/A-18E, INVERTED: set 0 = engine anti-ice ON, set 1 = OFF, read back with (A:ENG ANTI ICE:1).",
    "DEICE_Windshield_1": "Verified on an F/A-18E, INVERTED: set 0 = windshield de-ice ON, set 1 = OFF, read back with (A:WINDSHIELD DEICE SWITCH).",
    "DEICE_Pitot_1": "Verified on an F/A-18E: set 1 = pitot heat ON, set 0 = OFF, read back with (A:PITOT HEAT SWITCH:1).",
}


def _ddi(name):
    m = re.match(r"AS04F_DDI_(Push|Knob_Brightness)_?(\d+)?_?MFD_(\d)$", name)
    if not m:
        return None
    kind, n, unit = m.groups()
    sec = "Display unit %s" % unit
    note = "Display unit %s of the cockpit; which of the three screens it is is not known. Not tried." % unit
    if kind == "Push":
        return press(P_DDI, sec, "Button %s (around the screen)" % n, note="Pushbutton %s around display unit %s; its effect depends on the page shown. Not tried." % (n, unit))
    return level(P_DDI, sec, "Brightness knob", note="Rests at 50; range assumed 0..100. " + note)


def _simple(name):
    t = {
        "AS04F_AS04F_HSI_Switch_CRS": unknown(P_OTHER, "HSI", "HSI switch (CRS)", "Reads 1 at rest; positions not known, not tried."),
        "AS04F_EFD_Knob_Bingo": unknown(P_EFD, "EFD", "Bingo fuel knob", "Reads 0 at rest; not tried."),
        "AS04F_EFD_Knob_Bingo_Pull": press(P_EFD, "EFD", "Bingo fuel knob: pull", note=NOT_TRIED),
        "AS04F_EFD_Knob_Brightness": level(P_EFD, "EFD", "Brightness knob", note="Rests at 50; range assumed 0..100, not tried."),
        "AS04F_EFD_Push_Mode": press(P_EFD, "EFD", "MODE button", note=NOT_TRIED),
        "AS04F_EFD_Switch_HDG_TK": unknown(P_EFD, "EFD", "HDG / TK switch", "Reads 1 at rest; positions not known, not tried."),
        "AS04F_Fire_Test": unknown(P_OTHER, "Warnings", "Fire test switch", "Reads 1 at rest; deliberately not tried."),
        "AS04F_HMD_Knob_Brightness": level(P_HUD, "Helmet display", "HMD brightness knob", note="Rests at 0; range assumed 0..100, not tried."),
        "AS04F_HUD_Knob_AOA_Indexer_1": level(P_HUD, "HUD", "AOA indexer brightness knob", note="Rests at 50; range assumed 0..100, not tried."),
        "AS04F_HUD_Knob_Brightness_1": level(P_HUD, "HUD", "HUD brightness knob", note="Rests at 80; range assumed 0..100, not tried."),
        "AS04F_HUD_Push_VelocityVector_Cage": press(P_HUD, "HUD", "Velocity vector cage button", note=NOT_TRIED),
        "AS04F_HUD_Switch_ATT_Selector_1": unknown(P_HUD, "HUD", "ATT selector switch", "Reads 0 at rest; positions not known, not tried."),
        "AS04F_HUD_Switch_Alt_Selector_1": unknown(P_HUD, "HUD", "ALT selector switch", "Reads 0 at rest; positions not known, not tried."),
        "AS04F_HUD_Switch_DayNight_1": unknown(P_HUD, "HUD", "Day / night switch", "Reads 0 at rest; positions not known, not tried."),
        "AS04F_HUD_Switch_Symbology_Reject_1": unknown(P_HUD, "HUD", "Symbology reject switch", "Reads 0 at rest; positions not known, not tried."),
        "AS04F_HUD_Switch_Video_Control_1": unknown(P_HUD, "HUD", "Video control switch", "Reads 0 at rest; positions not known, not tried."),
        "AS04F_KNOB_Light_IDENT": unknown(P_LIGHT, "Interior", "IDENT light knob", "Reads 0 at rest; not tried."),
        "AS04F_Push_NWS": press(P_GEAR, "Brakes", "Nosewheel steering button", note="The sim's checklist switches nosewheel steering on before taxi; the button itself was not tried."),
        "AS04F_Switch_Hook_Bypass": unknown(P_GEAR, "Hook", "Hook bypass switch", "Reads 1 at rest; positions not known, not tried."),
        "AS04F_UFCD_Knob_Brightness_1": level(P_COMM, "UFCD", "UFCD brightness knob", note="Rests at 50; range assumed 0..100, not tried."),
        "AS04F_UFCD_Knob_COM1_1": unknown(P_COMM, "UFCD", "COM 1 knob", "Reads 0 at rest; not tried."),
        "AS04F_UFCD_Knob_COM1_Volume_1": level(P_COMM, "UFCD", "COM 1 volume knob", note="Rests at 100; range assumed 0..100, not tried."),
        "AS04F_UFCD_Knob_COM2_1": unknown(P_COMM, "UFCD", "COM 2 knob", "Reads 0 at rest; not tried."),
        "AS04F_UFCD_Knob_COM2_Volume_1": level(P_COMM, "UFCD", "COM 2 volume knob", note="Rests at 100; range assumed 0..100, not tried."),
        "AS04F_UFCD_Knob_SYM_1": unknown(P_COMM, "UFCD", "SYM knob", "Reads 0 at rest; not tried."),
        "AS04F_UFCD_Push_EMCON_1": press(P_COMM, "UFCD", "EMCON button", note=NOT_TRIED),
        "AS04F_UFCD_Push_ID_1": press(P_COMM, "UFCD", "ID button", note=NOT_TRIED),
        "NAVCOM_TACAN1_Volume": level(P_COMM, "TACAN", "TACAN volume", note="Rests at 100; range assumed 0..100, not tried."),
        "AUTOPILOT_Speed_Mode": press(P_OTHER, "Autopilot", "Speed mode button", note=NOT_TRIED),
        "DEICE_Engine_1": latched(P_ICE, "Anti-ice", "Engine anti-ice"),
        "DEICE_Pitot_1": latched(P_ICE, "Anti-ice", "Pitot heat"),
        "DEICE_Windshield_1": latched(P_ICE, "Anti-ice", "Windshield de-ice"),
        "ELECTRICAL_APU_Starter": latched(P_ELEC, "APU", "APU start switch"),
        "ELECTRICAL_Alternator_1": latched(P_ELEC, "Generators", "Generator 1 (left)"),
        "ELECTRICAL_Alternator_2": latched(P_ELEC, "Generators", "Generator 2 (right)"),
        "ELECTRICAL_Battery_1": latched(P_ELEC, "Battery", "Battery switch"),
        "ELECTRICAL_External_Power": [row(P_ELEC, "External power", "External power: on", "level", value=0),
                                      row(P_ELEC, "External power", "External power: neutral", "level", value=1),
                                      row(P_ELEC, "External power", "External power: off", "level", value=2)],
        "ENGINE_Starter_Ignition_1": [row(P_ENG, "Engine crank switch", "Crank: left engine", "level", value=0),
                                      row(P_ENG, "Engine crank switch", "Crank: off (centre)", "level", value=1),
                                      row(P_ENG, "Engine crank switch", "Crank: right engine", "level", value=2)],
        "ENGINE_Throttle_1_Afterburner": unknown(P_ENG, "Throttle 1 (left)", "Afterburner", "Reads 0 at rest; not tried."),
        "ENGINE_Throttle_2_Afterburner": unknown(P_ENG, "Throttle 2 (right)", "Afterburner", "Reads 0 at rest; not tried."),
        "ENGINE_Throttle_1_STOP": unknown(P_ENG, "Throttle 1 (left)", "STOP", "The sim's checklist toggles it to move the lever past the idle stop, but the value always read 0 whatever was sent and the engine kept running: the engine cut-off was not found."),
        "ENGINE_Throttle_2_STOP": unknown(P_ENG, "Throttle 2 (right)", "STOP", "The sim's checklist toggles it to move the lever past the idle stop, but the value always read 0 whatever was sent and the engine kept running: the engine cut-off was not found."),
        "HANDLING_AILERON_ELEVATOR_Trim": unknown(P_FLT, "Trim", "Aileron / elevator trim switch", "Reads 4 at rest; positions not known, not tried."),
        "HANDLING_AILERON_Trim": unknown(P_FLT, "Trim", "Aileron trim", "Reads 1 at rest; not tried."),
        "HANDLING_ELEVATOR_Trim": unknown(P_FLT, "Trim", "Elevator trim", "Reads 1 at rest; not tried."),
        "HANDLING_Anti_Skid": latched(P_GEAR, "Brakes", "Anti-skid switch"),
        "HANDLING_Flaps": [row(P_FLT, "Flaps", "Flaps: position 0 (up)", "level", value=0),
                           row(P_FLT, "Flaps", "Flaps: position 1", "level", value=8192),
                           row(P_FLT, "Flaps", "Flaps: position 2", "level", value=16384),
                           row(P_FLT, "Flaps", "Flaps: axis (0 to 16384)", "level", axis_range=[0, 16384])],
        "HANDLING_GLimiter": unknown(P_FLT, "Flight controls", "G limiter", "Reads 0 at rest; not tried."),
        "HANDLING_HANDLING_Trim_Reset": press(P_FLT, "Trim", "Trim reset button", note=NOT_TRIED),
        "HANDLING_Lever_LaunchBar": unknown(P_GEAR, "Launch bar", "Launch bar lever", "Reads 0 at rest; the variable to read back was not found, not tried."),
        "HANDLING_Lever_TailHook": latched(P_GEAR, "Hook", "Tail hook lever"),
        "HANDLING_Lever_WingFold": latched(P_FLT, "Wings", "Wing fold lever"),
        "HANDLING_RudderTrim_1": unknown(P_FLT, "Trim", "Rudder trim", "Reads 50 at rest; the CJ4's trims act on clicks, this one was not tried."),
        "HANDLING_Spoilers": unknown(P_FLT, "Flight controls", "Spoilers", "Reads 1 at rest; set 0 and set 1 left (A:SPOILERS HANDLE POSITION) at 0, so the control was not understood."),
        "HANDLING_Yoke_Hider_1": latched(P_OTHER, "View", "Hide the stick", note="Cosmetic; not tried."),
        "INSTRUMENT_Altimeter_1": [row(P_OTHER, "Altimeter", "Altimeter: 1013.25 hPa (standard)", "level", value=1013.25),
                                   row(P_OTHER, "Altimeter", "Altimeter: setting axis (950 to 1050 hPa)", "level", axis_range=[950, 1050])],
        "LANDING_GEAR_Gear": latched(P_GEAR, "Gear", "Landing gear lever", note="Reads 1 (down) on the ground; deliberately not tried."),
        "LANDING_GEAR_Parking_Brake": unknown(P_GEAR, "Brakes", "Parking brake", "Reads 1 (set); the values 0, 0.5, 2, -1 and 100 all left (A:BRAKE PARKING POSITION) at 1 with the engines running, so releasing it was not found."),
        "LANDING_GEAR_Parking_Brake_Emergency": unknown(P_GEAR, "Brakes", "Emergency parking brake", "Reads 0 at rest; not tried."),
        "LIGHTING_CABIN_1": latched(P_LIGHT, "Interior", "Cabin light 1"),
        "LIGHTING_CABIN_2": latched(P_LIGHT, "Interior", "Cabin light 2", note=NOT_TRIED),
        "LIGHTING_EXT_Lights_Mode": latched(P_LIGHT, "Exterior", "Exterior lights mode", 3, note="Seen on a running F/A-18E: the value follows set 0 / 1 / 2, but (A:LIGHT NAV), BEACON, STROBE and RECOGNITION did not change with any of them, so what it controls was not read."),
        "LIGHTING_INT_Lights_Mode": latched(P_LIGHT, "Interior", "Interior lights mode", 3, note="Reads 2 at rest; not tried."),
        "LIGHTING_LANDING_1": latched(P_LIGHT, "Exterior", "Landing light"),
        "LIGHTING_Navigation_Light_Knob": unknown(P_LIGHT, "Exterior", "Navigation lights knob", "Reads 100 at rest; set 0 and set 100 left (A:LIGHT NAV) at 0, so its effect was not read."),
        "LIGHTING_Recognition_Light_Knob": unknown(P_LIGHT, "Exterior", "Recognition lights knob", "Reads 100 at rest; not tried."),
        "LIGHTING_STROBE_1": latched(P_LIGHT, "Exterior", "Strobes"),
        "LIGHTING_Test_Lights": press(P_LIGHT, "Interior", "Test lights button", note=NOT_TRIED),
        "PRESSURIZATION_Bleed_Air_Source": latched(P_ICE, "Bleed air", "Bleed air source", 3, note="Reads 2 at rest; the sim's checklist sets it to NORM for the engine start. Not tried."),
        "PRESSURIZATION_Bleed_Air_Source_APU": latched(P_ICE, "Bleed air", "APU bleed air", note=NOT_TRIED),
        "PRESSURIZATION_Bleed_Sync": latched(P_ICE, "Bleed air", "Bleed sync", note=NOT_TRIED),
        "PRESSURIZATION_Dump": latched(P_ICE, "Cabin", "Cabin pressure dump", note=NOT_TRIED),
        "PROCEDURE_GearControlledCircuit": unknown(P_OTHER, "Procedure helpers", "Gear controlled circuit (procedure helper)", "Reads 0 at rest; not tried."),
        "SAFETY_Caution_1": press(P_OTHER, "Warnings", "Master caution", note=NOT_TRIED),
    }
    return t.get(name)


def classify(name):
    """Rows (one per catalog entry) for a cockpit input event, or None when the event has no rule."""
    if name.startswith("AS04F_DDI_"):
        return _ddi(name)
    if name in ("ENGINE_Throttle_1", "ENGINE_Throttle_2"):
        n = name[-1]
        sec = "Throttle %s (%s)" % (n, "left" if n == "1" else "right")
        return [row(P_ENG, sec, "Throttle %s: idle" % n, "level", value=0.1),
                row(P_ENG, sec, "Throttle %s: 50 %%" % n, "level", value=0.5),
                row(P_ENG, sec, "Throttle %s: military power (80 %%)" % n, "level", value=1),
                row(P_ENG, sec, "Throttle %s: axis (0 to 1)" % n, "level", axis_range=[0, 1])]
    m = re.match(r"LIGHTING_POTENTIOMETER_(\d+)$", name)
    if m:
        return level(P_LIGHT, "Brightness knobs", "Brightness knob %s (light potentiometer %s)" % (m.group(1), m.group(1)),
                     note="Not tried; which panel it dims is not known.")
    return _simple(name)
