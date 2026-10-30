# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Rules that label the cockpit input events of the Asobo Cessna Citation CJ4 (build_aircraft_catalog.py).

Panel / function names come from the event names and from the sim's own CJ4 checklist
(Official/Steam/asobo-aircraft-cj4/.../Checklist/CJ4_Checklist.xml). Behaviours come from trying the controls on a
running CJ4 with `--script` (set the event, read the simulator's own A: variable, compare): only those carry a
VERIFIED note. Everything else is inferred from the name and says so.
"""
import re

AIRCRAFT = "Asobo_CJ4"
TITLE = "Cessna Citation CJ4 (Asobo)"
EVENTS_FILE = "asobo_cj4_events.json"
SOURCES = [
    "Cockpit input events enumerated from MSFS 11.0.282174.999 (SimConnect EnumerateInputEvents)",
    "Asobo_CJ4/Checklist/CJ4_Checklist.xml shipped with the aircraft",
]

P_AP = "Autopilot and flight instruments"
P_ENG = "Engines"
P_FUEL = "Fuel"
P_ELEC = "Electrical"
P_LIGHT = "Lights"
P_ICE = "Ice protection"
P_GEAR = "Landing gear and brakes"
P_TRIM = "Flaps, spoilers and trim"
P_CABIN = "Pressurization and cabin"
P_RADIO = "Audio and volumes"
P_SAFE = "Warnings and signs"
P_CDU1 = "FMS CDU 1 (pilot)"
P_CDU2 = "FMS CDU 2 (copilot)"
P_AS3000 = "AS3000 display keys"
P_OTHER = "Other"
PANEL_ORDER = [P_AP, P_ENG, P_FUEL, P_ELEC, P_LIGHT, P_ICE, P_GEAR, P_TRIM, P_CABIN, P_RADIO, P_SAFE, P_CDU1, P_CDU2, P_AS3000, P_OTHER]

# --- what was actually observed on a running CJ4 (set the event, read the simulator's own state) ---------------
_LIGHT = "Verified on a CJ4: set 1 / set 0 switches %s, read back with (A:%s)."
VERIFIED = {
    "LIGHTING_NAV_1": _LIGHT % ("the navigation lights", "LIGHT NAV"),
    "LIGHTING_BEACON_1": _LIGHT % ("the beacon", "LIGHT BEACON"),
    "LIGHTING_LANDING_1": _LIGHT % ("the landing lights", "LIGHT LANDING"),
    "LIGHTING_TAXI_1": _LIGHT % ("the taxi light", "LIGHT TAXI"),
    "LIGHTING_STROBE_1": _LIGHT % ("the strobes", "LIGHT STROBE"),
    "LIGHTING_WING_1": _LIGHT % ("the wing lights", "LIGHT WING"),
    "LIGHTING_LOGO_1": _LIGHT % ("the logo lights", "LIGHT LOGO"),
    "LIGHTING_RECOGNITION_1": _LIGHT % ("the recognition lights", "LIGHT RECOGNITION"),
    "LIGHTING_CABIN_1": "Verified on a CJ4: set 1 / set 0 switches cabin light 1, read back with (A:LIGHT CABIN:1).",
    "LIGHTING_CABIN_2": "Verified on a CJ4: set 1 / set 0 switches cabin light 2, read back with (A:LIGHT CABIN:2).",
    "LIGHTING_CABIN_3": "Verified on a CJ4: set 1 / set 0 switches cabin light 3, read back with (A:LIGHT CABIN:3).",
    "DEICE_Pitot_1": "Verified on a CJ4: set 1 / set 0 switches pitot heat 1, read back with (A:PITOT HEAT SWITCH:1).",
    "DEICE_Airframe_1": "Verified on a CJ4: set 1 / set 0 switches airframe de-ice, read back with (A:STRUCTURAL DEICE SWITCH).",
    "DEICE_Engine_1": "Verified on a CJ4: set 1 / set 0 switches engine anti-ice 1, read back with (A:ENG ANTI ICE:1).",
    "AUTOPILOT_Heading": "Verified on a CJ4: an event of +1 moves the heading bug up one degree, -1 moves it down one degree; 5 and -5 also move it only one degree.",
    "AUTOPILOT_Altitude": "Verified on a CJ4: an event of +1 raises the selected altitude by 100 ft, -1 lowers it by 100 ft.",
    "AUTOPILOT_Speed": "Verified on a CJ4: an event of +1 raises the selected speed by 1 kt, -1 lowers it by 1 kt.",
    "AUTOPILOT_Baro_1": "Verified on a CJ4: an event of +1 raises the baro setting by about 0.3 hPa (1013.25 to 1013.56), -1 lowers it again.",
    "AUTOPILOT_Course_Dyn_1": "Verified on a CJ4: an event of +1 raises the course (OBS 1) by 1 degree, -1 lowers it by 1 degree.",
    "AUTOPILOT_AP_1": "Verified on a CJ4: every event toggles the autopilot, whatever its value (sending 0 while it is off turns it on), so a binding sends it once, with nothing on release.",
    "AUTOPILOT_YD_Button": "Verified on a CJ4: every event toggles the yaw damper, whatever its value, so a binding sends it once. Engaging the autopilot also engages the yaw damper.",
    "AUTOPILOT_FD_1_Mode": "Verified on a CJ4: every event toggles the flight director, whatever its value; the value read back is the state (1 = on).",
    "AUTOPILOT_Heading_Mode": "Verified on a CJ4 with the flight director on: every event toggles HDG, whatever its value.",
    "AUTOPILOT_Altitude_Button": "Verified on a CJ4 with the flight director on: every event toggles ALT hold, whatever its value.",
    "AUTOPILOT_VS_Mode": "Verified on a CJ4 with the flight director on: every event toggles VS, whatever its value.",
    "AUTOPILOT_FLC_Button": "Verified on a CJ4 with the flight director on: every event toggles FLC, whatever its value.",
    "HANDLING_ElevatorTrim_YOKE": "Verified on a CJ4: an event with a value of 2 or more trims one step up, 0 or less one step down (values between 0 and 2 do nothing).",
    "HANDLING_AileronsTrim_1": "Verified on a CJ4: an event with a value of 2 or more trims one step up, 0 or less one step down (values between 0 and 2 do nothing).",
    "HANDLING_RudderTrim_1": "Verified on a CJ4: an event with a value of 2 or more trims one step up, 0 or less one step down (values between 0 and 2 do nothing).",
}
for _n in (15, 16, 17, 18):
    VERIFIED["LIGHTING_POTENTIOMETER_%d" % _n] = "Verified on a CJ4: the value 0..100 is the brightness, read back with (A:LIGHT POTENTIOMETER:%d)." % _n

NOT_TRIED = "Behaviour taken from the control's name, not tried on a real aircraft."


# --- row builders ------------------------------------------------------------------------------------------------
def row(panel, section, function, behavior="", **kw):
    r = {"panel": panel, "section": section, "function": function, "behavior": behavior}
    r.update(kw)
    return r


def knob(panel, section, function, up="increase", down="decrease", **kw):
    return [row(panel, section, "%s: %s" % (function, up), "knob_up", **kw),
            row(panel, section, "%s: %s" % (function, down), "knob_down", **kw)]


def trim(panel, section, function, **kw):
    # the trim handlers act on a value >= 2 (up) or <= 0 (down); 1 does nothing
    return [row(panel, section, "%s: trim up" % function, "knob_up", value=10, **kw),
            row(panel, section, "%s: trim down" % function, "knob_down", value=0, **kw)]


def press(panel, section, function, **kw):
    return [row(panel, section, function, "press", **kw)]


def latched(panel, section, function, positions=2, **kw):
    return [row(panel, section, function, "latched", positions=positions, **kw)]


def level(panel, section, function, rng=(0, 100), **kw):
    return [row(panel, section, function, "level", axis_range=list(rng), **kw)]


def unknown(panel, section, function, note):
    return [row(panel, section, function, "", note=note)]


# --- the table ---------------------------------------------------------------------------------------------------
def _ap(name):
    s = name[len("AUTOPILOT_"):]
    A = P_AP
    table = {
        "AP_1": press(A, "Autopilot", "AP engage / disengage"),
        "AP_Disconnect_1": press(A, "Autopilot", "AP disconnect (yoke button)"),
        "YD_Button": press(A, "Autopilot", "YD (yaw damper)"),
        "FD_1_Mode": press(A, "Flight director", "FD 1 on / off"),
        "FD_2_Mode": press(A, "Flight director", "FD 2 on / off"),
        "Heading": knob(A, "Heading", "HDG knob", "right (+1 deg)", "left (-1 deg)"),
        "Heading_Mode": press(A, "Heading", "HDG mode"),
        "Heading_Sync": press(A, "Heading", "HDG SYNC (heading bug to the current heading)"),
        "Altitude": knob(A, "Altitude", "ALT selector", "up (+100 ft)", "down (-100 ft)"),
        "Altitude_Button": press(A, "Altitude", "ALT hold"),
        "Altitude_Alert_Cancel": press(A, "Altitude", "Altitude alert cancel"),
        "Speed": knob(A, "Speed", "SPD knob", "up (+1 kt)", "down (-1 kt)"),
        "SpeedMach_Mode": press(A, "Speed", "SPD / MACH switch"),
        "FLC_Button": press(A, "Vertical", "FLC (flight level change)"),
        "VNAV_Mode": press(A, "Vertical", "VNAV"),
        "VS_Mode": press(A, "Vertical", "VS mode"),
        "VerticalSpeed": knob(A, "Vertical", "VS wheel", "up", "down",
                              note="Did not change the selected vertical speed while VS mode was off; not tried with VS engaged."),
        "NAV_Mode": press(A, "Lateral", "NAV mode",
                          note="Pressed with the flight director on and no navigation source: nothing engaged, so not confirmed."),
        "Approach_Button": press(A, "Lateral", "APR (approach)",
                                 note="Pressed with the flight director on and no navigation source: nothing engaged, so not confirmed."),
        "Backcourse_Button": press(A, "Lateral", "BC (back course)", note=NOT_TRIED),
        "Bank_Button": press(A, "Lateral", "BANK (low bank)", note=NOT_TRIED),
        "Transfer_Mode": press(A, "Flight director", "XFR (transfer to the other side)", note=NOT_TRIED),
        "Course_Dyn_1": knob(A, "Course", "CRS 1 knob", "up (+1 deg)", "down (-1 deg)"),
        "Course_Dyn_1_Sync": press(A, "Course", "CRS 1 SYNC", note=NOT_TRIED),
        "Course_Dyn_2": knob(A, "Course", "CRS 2 knob", "up (+1 deg)", "down (-1 deg)", note=NOT_TRIED),
        "Course_Dyn_2_Sync": press(A, "Course", "CRS 2 SYNC", note=NOT_TRIED),
        "Baro_1": knob(A, "Baro", "BARO 1 knob", "up", "down"),
        "Baro_1_STD": press(A, "Baro", "BARO 1 STD", note=NOT_TRIED),
        "Baro_2": knob(A, "Baro", "BARO 2 knob", "up", "down", note=NOT_TRIED),
        "Baro_2_STD": press(A, "Baro", "BARO 2 STD", note=NOT_TRIED),
        "Baro_3": knob(A, "Baro", "Standby baro knob", "up", "down", note=NOT_TRIED),
    }
    return table.get(s)


def _as3000(name):
    m = re.match(r"AS3000_(Lower|Upper)_(\d)_(.+)$", name)
    if not m:
        return None
    unit, n, key = m.groups()
    section = "%s unit %s" % (unit.lower(), n)
    pretty = key.replace("_", " ")
    note = "Key of the AS3000 %s unit %s; not tried. The CJ4's own displays are Pro Line 21 type, so it may not be wired to anything." % (unit.lower(), n)
    if key in ("DATA", "ZOOM", "RANGE", "TILT"):
        r = knob(P_AS3000, section, pretty + " knob", "right", "left", note=note)
    elif key.startswith("Navigation_"):
        return unknown(P_AS3000, section, "Joystick " + key[len("Navigation_"):], "Joystick axis of the AS3000 unit; its value range is not known, not tried.")
    else:
        r = press(P_AS3000, section, pretty.replace(" Button", " (push)"), note=note)
    for x in r:
        x["section"] = section
    return r


def _fmc(name):
    m = re.match(r"FMC_CJ4_(\d)_(Button|Keyboard|Softkey)_(.+)$", name)
    if not m:
        return None
    n, kind, key = m.groups()
    panel = P_CDU1 if n == "1" else P_CDU2
    if kind == "Keyboard":
        return press(panel, "Keyboard", "Key " + key)
    if kind == "Softkey":
        side, num = key.split("_")
        return press(panel, "Line select keys", "LSK %s%s" % (side, num))
    labels = {
        "BRT_DIM": ("Brightness", "BRT / DIM"), "CLR": ("Keys", "CLR"), "DEPARR": ("Page keys", "DEP / ARR"),
        "DIR": ("Page keys", "DIR"), "DIV": ("Keys", "/ (slash)"), "DOT": ("Keys", ". (dot)"),
        "DSPL_MENU": ("Page keys", "DSPL MENU"), "EXEC": ("Keys", "EXEC"), "FPLN": ("Page keys", "FPLN"),
        "IDX": ("Page keys", "IDX"), "LEGS": ("Page keys", "LEGS"), "MFD_ADV": ("Page keys", "MFD ADV"),
        "MFD_DATA": ("Page keys", "MFD DATA"), "MSG": ("Keys", "MSG"), "NEXTPAGE": ("Page keys", "NEXT page"),
        "PERF": ("Page keys", "PERF"), "PLUSMINUS": ("Keys", "+/-"), "PREVPAGE": ("Page keys", "PREV page"),
        "SP": ("Keys", "SP (space)"), "TUN": ("Page keys", "TUN"),
    }
    sec, fn = labels[key]
    if key == "BRT_DIM":
        return latched(panel, sec, fn, note="Reads 1 at rest; behaviour not tried.")
    return press(panel, sec, fn)


def _simple(name):
    E, F, L = P_ELEC, P_FUEL, P_LIGHT
    table = {
        "AUTO_Exclusive_Lights": unknown(L, "Exterior", "Exclusive lights (automatic)", "Purpose not known; reads 0 at rest, not tried."),
        "ELECTRICAL_Avionics_Bus_1": latched(E, "Avionics", "Avionics bus switch", 3,
                                             note="Three positions. The sim's own CJ4 checklist sets 2 (DISPATCH) before the engine start and 0 (ON) after it; 1 was read in a cold cockpit. Not tried here."),
        "ELECTRICAL_Battery_1": latched(E, "Batteries", "Battery switch", note=NOT_TRIED),
        "ELECTRICAL_Alternator_1": latched(E, "Generators", "Generator 1 (left)", note=NOT_TRIED),
        "ELECTRICAL_Alternator_2": latched(E, "Generators", "Generator 2 (right)", note=NOT_TRIED),
        "ELECTRICAL_Circuit_49": unknown(E, "Circuits", "Circuit 49", "Purpose not known, not tried."),
        "ELECTRICAL_EMERLIGHTS": latched(L, "Interior", "Emergency lights switch", note=NOT_TRIED),
        "ENGINE_Ignition_1": latched(P_ENG, "Ignition", "Ignition 1 (left)", note=NOT_TRIED),
        "ENGINE_Ignition_2": latched(P_ENG, "Ignition", "Ignition 2 (right)", note=NOT_TRIED),
        "ENGINE_Starter_1": latched(P_ENG, "Starters", "Starter 1 (left)", note=NOT_TRIED),
        "ENGINE_Starter_2": latched(P_ENG, "Starters", "Starter 2 (right)", note=NOT_TRIED),
        "ENGINE_Starter_Disengage": press(P_ENG, "Starters", "Starter disengage", note=NOT_TRIED),
        "ENGINE_TOGA_PUSH": press(P_ENG, "Thrust levers", "TOGA button", note=NOT_TRIED),
        "ENGINE_Throttle_1": level(P_ENG, "Thrust levers", "Thrust lever 1 (left)", note="Reads 0 at idle; scale assumed 0..100, not tried."),
        "ENGINE_Throttle_2": level(P_ENG, "Thrust levers", "Thrust lever 2 (right)", note="Reads 0 at idle; scale assumed 0..100, not tried."),
        "FUEL_Crossfeed_1": latched(F, "Crossfeed", "Crossfeed switch", 3, note="Reads 1 at rest, so probably three positions; not tried."),
        "FUEL_Pump_1": latched(F, "Pumps", "Boost pump 1 (left)", note=NOT_TRIED),
        "FUEL_Pump_2": latched(F, "Pumps", "Boost pump 2 (right)", note=NOT_TRIED),
        "FUEL_RunStop_1": latched(F, "Engine run / stop", "RUN / STOP button 1 (left)", note="The sim's CJ4 checklist presses the RUN button when N2 reaches 20 %; behaviour not tried."),
        "FUEL_RunStop_2": latched(F, "Engine run / stop", "RUN / STOP button 2 (right)", note="The sim's CJ4 checklist presses the RUN button when N2 reaches 20 %; behaviour not tried."),
        "FUEL_FUEL_Push_RunStop_1_Cover": latched(F, "Engine run / stop", "RUN / STOP cover 1 (left)", note=NOT_TRIED),
        "FUEL_FUEL_Push_RunStop_2_Cover": latched(F, "Engine run / stop", "RUN / STOP cover 2 (right)", note=NOT_TRIED),
        "HANDLING_Flaps": unknown(P_TRIM, "Flaps", "Flap handle",
                                  "Did not move the flaps with the values 0, 1, 2 or -1 (on the ground). Use the standard FLAPS_INCR / FLAPS_DECR sim events instead."),
        "HANDLING_Spoilers": unknown(P_TRIM, "Spoilers", "Spoiler handle",
                                     "Moved the handle only 0.3 % per event with 1, 50 or 100; its real behaviour is not known."),
        "HANDLING_ElevatorTrim_YOKE": trim(P_TRIM, "Trim", "Elevator trim (yoke switch)"),
        "HANDLING_ElevatorTrim_PEDESTRAL": trim(P_TRIM, "Trim", "Elevator trim (pedestal wheel)", note="Same handler as the yoke switch is assumed; not tried."),
        "HANDLING_AileronsTrim_1": trim(P_TRIM, "Trim", "Aileron trim"),
        "HANDLING_RudderTrim_1": trim(P_TRIM, "Trim", "Rudder trim"),
        "HANDLING_HANDLING_Push_ElevatorTrim_Secondary_Cover": latched(P_TRIM, "Trim", "Secondary elevator trim cover", note=NOT_TRIED),
        "HANDLING_Secondary_ElevatorTrim_Available": unknown(P_TRIM, "Trim", "Secondary elevator trim available", "Status value, not tried."),
        "HANDLING_Yoke_Hider_1": latched(P_OTHER, "View", "Hide the left yoke", note="Cosmetic (hides the yoke model); not tried."),
        "HANDLING_Yoke_Hider_2": latched(P_OTHER, "View", "Hide the right yoke", note="Cosmetic (hides the yoke model); not tried."),
        "LANDING_GEAR_Gear": latched(P_GEAR, "Gear", "Landing gear lever", note="Reads 1 (down) on the ground; deliberately not tried."),
        "LANDING_GEAR_ParkingBrake": unknown(P_GEAR, "Brakes", "Parking brake",
                                             "Reads 100 when set; deliberately not tried (the brake holds the aircraft while the engines run). Use the standard PARKING_BRAKES sim event."),
        "LIGHTING_PANEL_1": unknown(L, "Interior", "Panel lights",
                                    "Odd on a real CJ4: setting any value switched the panel light on, the value read back moved between 5 and 100, and (A:LIGHT PANEL) only went back to 0 with K:PANEL_LIGHTS_SET 0. Use the standard panel-light events instead."),
        "PASSENGER_Climate_Control": latched(P_CABIN, "Cabin", "Cabin climate control", note=NOT_TRIED),
        "PRESSURIZATION_Dump": latched(P_CABIN, "Pressurization", "Cabin dump", note=NOT_TRIED),
        "PRESSURIZATION_Dump_Cover": latched(P_CABIN, "Pressurization", "Cabin dump cover", note=NOT_TRIED),
        "PRESSURIZATION_Pressource_1": latched(P_CABIN, "Pressurization", "Pressure source", 3, note="Reads 2 at rest, so probably three positions; not tried."),
        "PROCEDURE_ToggleAllLights": press(L, "Exterior", "Toggle all lights (procedure helper)", note=NOT_TRIED),
        "SAFETY_Caution_1": press(P_SAFE, "Master warning", "Master caution 1", note=NOT_TRIED),
        "SAFETY_Caution_2": press(P_SAFE, "Master warning", "Master caution 2", note=NOT_TRIED),
        "SAFETY_Warning_1": press(P_SAFE, "Master warning", "Master warning 1", note=NOT_TRIED),
        "SAFETY_Warning_2": press(P_SAFE, "Master warning", "Master warning 2", note=NOT_TRIED),
        "SAFETY_PASSENGER_SAFETY_PUSH": latched(P_SAFE, "Signs", "Passenger safety sign", note=NOT_TRIED),
        "SAFETY_SEATBELTS_PUSH": latched(P_SAFE, "Signs", "Seat belt sign", note=NOT_TRIED),
        "NAVCOM_AUDIO_ADF1_Sound": latched(P_RADIO, "Audio panel", "ADF 1 audio", note=NOT_TRIED),
        "NAVCOM_AUDIO_COM1_Transmit": latched(P_RADIO, "Audio panel", "COM 1 transmit", note=NOT_TRIED),
        "NAVCOM_AUDIO_COM2_Transmit": latched(P_RADIO, "Audio panel", "COM 2 transmit", note=NOT_TRIED),
        "NAVCOM_AUDIO_DME1_Sound": latched(P_RADIO, "Audio panel", "DME 1 audio", note=NOT_TRIED),
        "NAVCOM_AUDIO_DME2_Sound": latched(P_RADIO, "Audio panel", "DME 2 audio", note=NOT_TRIED),
        "NAVCOM_AUDIO_MARKER1_Sound": latched(P_RADIO, "Audio panel", "Marker beacon audio", note=NOT_TRIED),
        "NAVCOM_AUDIO_Marker_Beacon_Mute": latched(P_RADIO, "Audio panel", "Marker beacon mute", note=NOT_TRIED),
        "NAVCOM_AUDIO_Speaker": latched(P_RADIO, "Audio panel", "Speaker", note=NOT_TRIED),
    }
    return table.get(name)


def classify(name):
    """Rows (one per catalog entry) for a cockpit input event, or None when the event has no rule."""
    if name.startswith("AUTOPILOT_"):
        return _ap(name)
    if name.startswith("AS3000_"):
        return _as3000(name)
    if name.startswith("FMC_CJ4_"):
        return _fmc(name)
    r = _simple(name)
    if r:
        return r
    m = re.match(r"DEICE_(Pitot|Airframe|Engine)_(\d)$", name)
    if m:
        what, n = m.groups()
        fn = {"Pitot": "Pitot / static heat", "Airframe": "Airframe de-ice", "Engine": "Engine anti-ice"}[what] + " " + n
        return latched(P_ICE, what, fn, note=None if name in VERIFIED else NOT_TRIED)
    m = re.match(r"LIGHTING_(NAV|BEACON|LANDING|TAXI|STROBE|WING|LOGO|RECOGNITION)_1$", name)
    if m:
        fn = {"NAV": "Navigation lights", "BEACON": "Beacon", "LANDING": "Landing lights", "TAXI": "Taxi light",
              "STROBE": "Strobes", "WING": "Wing lights", "LOGO": "Logo lights", "RECOGNITION": "Recognition lights"}[m.group(1)]
        return latched(P_LIGHT, "Exterior", fn)
    m = re.match(r"LIGHTING_CABIN_(\d)$", name)
    if m:
        return latched(P_LIGHT, "Interior", "Cabin light " + m.group(1))
    m = re.match(r"LIGHTING_POTENTIOMETER_(\d+)$", name)
    if m:
        return level(P_LIGHT, "Brightness knobs", "Brightness knob %s (light potentiometer %s)" % (m.group(1), m.group(1)),
                     remark="Which display or panel this knob dims is not known.")
    m = re.match(r"NAVCOM_(COM|NAV)([12])_Volume$", name)
    if m:
        return level(P_RADIO, "Volumes", "%s %s volume" % (m.group(1), m.group(2)), note="Value 0..100 assumed from the values read at rest; not tried.")
    m = re.match(r"PASSENGER_Passenger_(Air|Heat)_(\d)$", name)
    if m:
        return level(P_CABIN, "Cabin", "Passenger %s %s" % (m.group(1).lower(), m.group(2)), note="Rests at 50; range assumed 0..100, not tried.")
    return None
