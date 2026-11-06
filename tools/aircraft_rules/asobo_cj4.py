# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Rules that label the cockpit input events of the Asobo Cessna Citation CJ4 (build_aircraft_catalog.py).

Panel / function names come from the event names and from the sim's own CJ4 checklist
(Official/Steam/asobo-aircraft-cj4/.../Checklist/CJ4_Checklist.xml). Behaviours come from trying the controls on a
running CJ4 with `--script` (set the event, read the simulator's own A: variable, compare): only those carry a
VERIFIED note. Everything else is inferred from the name and says so.

One catalog serves both sims: MSFS 2024 reports the same aircraft id (asobo_cj4), 303 of its 425 events have a 2020 name
(spelled in upper case, matched without regard to case), 122 are new (the rules in _cj4_2024) and six 2020 events are gone
(ONLY_2020: their entries say what replaces them). The 2024 names, their English tooltips and the 2024 checks of
2026-11-06 (engines running, parking brake set) are in the notes.
"""
import re

AIRCRAFT = "Asobo_CJ4"
TITLE = "Cessna Citation CJ4 (Asobo)"
EVENTS_FILE = "asobo_cj4_events.json"
SOURCES = [
    "Cockpit input events enumerated from MSFS 11.0.282174.999 (SimConnect EnumerateInputEvents)",
    "Asobo_CJ4/Checklist/CJ4_Checklist.xml shipped with the aircraft",
    "Cockpit input events enumerated from MSFS 2024 12.2.282174.999 (SimConnect EnumerateInputEvents), aircraft asobo_cj4, 2026-11-06",
    "Official2024/Steam/asobo-aircraft-cj4/minimal.fsarchive, en-us.locpak: the aircraft's English tooltips (CJ4.*_TITLE / _ACTION) (unencrypted archive)",
    "Working Title CJ4 source, microsoft/msfs-avionics-mirror (commit c817299): src/workingtitle-aircraft-cj4/src/Plugins/Shared/Fadec/CJ4Fadec.ts "
    "(throttle detents) and src/sdk/fadec/ThrottleLeverManager.ts (which events move the lever); "
    "src/workingtitle-instruments-wt21/.../PFD/Menus/Components/RadioboxSTD.tsx (the STD flag L:XMLVAR_Baro1_ForcedToSTD)",
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
P_DISP = "Displays and reversion"
P_FIRE = "Fire protection"
P_EXT = "Exterior and walkaround"
P_OTHER = "Other"
PANEL_ORDER = [P_AP, P_ENG, P_FUEL, P_ELEC, P_LIGHT, P_ICE, P_GEAR, P_TRIM, P_CABIN, P_RADIO, P_SAFE, P_FIRE, P_DISP, P_CDU1, P_CDU2, P_AS3000,
               P_EXT, P_OTHER]

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
VERIFIED.update({
    "ELECTRICAL_Battery_1": "Verified on a CJ4: set 1 = OFF, set 0 = ON (a second set 0 changes nothing), read back with (A:ELECTRICAL MASTER BATTERY).",
    "ELECTRICAL_Alternator_1": "Verified on a CJ4: set 1 = OFF, set 0 = ON, read back with (A:GENERAL ENG MASTER ALTERNATOR:1). (A:GENERAL ENG GENERATOR SWITCH:1) does not follow it.",
    "ELECTRICAL_Alternator_2": "Verified on a CJ4: set 1 = OFF, set 0 = ON, read back with (A:GENERAL ENG MASTER ALTERNATOR:2). (A:GENERAL ENG GENERATOR SWITCH:2) does not follow it.",
    "ELECTRICAL_Avionics_Bus_1": "Verified on a CJ4: three positions by set: 1 = OFF (avionics circuit off), 0 = ON, 2 = DISPATCH (both power the avionics); read back with (A:CIRCUIT AVIONICS ON).",
    "ENGINE_Starter_1": "Verified on a CJ4: set 1 engages the starter ((A:GENERAL ENG STARTER:1) = 1, N2 rises about 3 % every 0.7 s); it drops out by itself at about 50 % N2 and the value returns to 0.",
    "ENGINE_Starter_2": "Verified on a CJ4: set 1 engages the starter ((A:GENERAL ENG STARTER:2) = 1, N2 rises about 3 % every 0.7 s); it drops out by itself at about 50 % N2 and the value returns to 0.",
    "FUEL_RunStop_1": "Verified on a CJ4: set 1 with N2 at 20 % or more lights the engine ((A:GENERAL ENG COMBUSTION:1) = 1); the value reads 1 while it runs.",
    "FUEL_RunStop_2": "Verified on a CJ4: set 1 with N2 at 20 % or more lights the engine ((A:GENERAL ENG COMBUSTION:2) = 1), set 0 stops it; the value reads 1 while it runs.",
    "FUEL_Pump_1": "Verified on a CJ4: toggles the boost pump, read back with (A:GENERAL ENG FUEL PUMP SWITCH:1).",
    "FUEL_Pump_2": "Verified on a CJ4: toggles the boost pump, read back with (A:GENERAL ENG FUEL PUMP SWITCH:2).",
    "DEICE_Pitot_2": "Verified on a CJ4: set 1 / set 0 switches pitot heat 2, read back with (A:PITOT HEAT SWITCH:2).",
    "DEICE_Engine_2": "Verified on a CJ4: set 1 / set 0 switches engine anti-ice 2, read back with (A:ENG ANTI ICE:2).",
    "SAFETY_SEATBELTS_PUSH": "Verified on a CJ4: toggles the seat belt sign, read back with (A:CABIN SEATBELTS ALERT SWITCH); the value is the state.",
    "SAFETY_PASSENGER_SAFETY_PUSH": "Verified on a CJ4: toggles the passenger safety / no smoking sign, read back with (A:CABIN NO SMOKING ALERT SWITCH); the value is the state.",
    "LANDING_GEAR_ParkingBrake": "Verified on a CJ4: 100 sets the parking brake and 0 releases it, read back with (A:BRAKE PARKING POSITION); the aircraft did not move during a one-second release at idle.",
    "HANDLING_Flaps": "Verified on a CJ4 (powered, engines running): an axis from 0 to 16384; 0 = flap position 0, 8192 = position 1, 16384 = position 2 ((A:FLAPS HANDLE INDEX) 0 / 1 / 2, (A:FLAPS HANDLE PERCENT) 0 / 50 / 100).",
    "HANDLING_Spoilers": "Verified on a CJ4: an axis from 0 to 16384; 8192 gave (A:SPOILERS HANDLE POSITION) = 50 %, 16384 gave 100 %, 0 gave 0.",
    "AUTOPILOT_FD_2_Mode": "Verified on a CJ4: every event toggles the flight director, whatever its value; FD 1 and FD 2 share one state ((A:AUTOPILOT FLIGHT DIRECTOR ACTIVE) and :2 both follow).",
    "AUTOPILOT_VerticalSpeed": "Verified on a CJ4 with VS engaged: +1 raises the selected vertical speed by 100 fpm, -1 lowers it by 100 fpm (nothing happens outside VS mode).",
})

# --- MSFS 2024 (checked on 2026-11-06 with both engines running and the parking brake set; each control put back) --------
_V24 = " MSFS 2024: "
for _k, _t in {
    "LIGHTING_TAXI_1": "the same (A:LIGHT TAXI).",
    "DEICE_Pitot_1": "the same.",
    "DEICE_Pitot_2": "the same.",
    "DEICE_Engine_1": "the same; this is the ENG ONLY button ((A:STRUCTURAL DEICE SWITCH) stays 0).",
    "DEICE_Airframe_1": "set 1 also turns engine anti-ice 1 on ((A:ENG ANTI ICE:1) = 1): this is the left WING/ENG button.",
    "AUTOPILOT_Speed": "the same, one knot per event.",
    "AUTOPILOT_Baro_1": "the same (1013.25 to 1013.56 and back).",
    "AUTOPILOT_FD_1_Mode": "the same.",
    "AUTOPILOT_Altitude_Button": "the same.",
    "AUTOPILOT_AP_1": "the same; K:AUTOPILOT_OFF and AUTOPILOT_AP_Disconnect_1 disengage it.",
}.items():
    VERIFIED[_k] += _V24 + _t
VERIFIED.update({
    "AUTOPILOT_AP_Disconnect_1": "Verified on a CJ4 in MSFS 2024: one event disengages the autopilot ((A:AUTOPILOT MASTER) 1 to 0).",
    "AUTOPILOT_Baro_1_STD": "Verified on a CJ4 in MSFS 2024: every event toggles STD, read back with (L:XMLVAR_Baro1_ForcedToSTD) (the WT21 flag; "
                            "(A:KOHLSMAN SETTING STD:1) does not follow it). MSFS 2020: no variable showing the STD state was found.",
    "ENGINE_Ignition_1": "Verified on a CJ4 in MSFS 2024: two positions, 0 = NORM ((A:TURB ENG IGNITION SWITCH EX1:1) = 1, auto) and 1 = ON (= 2); "
                         "2 reads back as 1.",
    "LIGHTING_LANDING_0": "Verified on a CJ4 in MSFS 2024: set 1 / set 0 switches the landing lights, read back with (A:LIGHT LANDING).",
    "ELECTRICAL_SWITCH_AVIONICS_BUS": "Verified on a CJ4 in MSFS 2024: three positions by set, 0 = ON ((A:CIRCUIT AVIONICS ON) = 1); 1 and 2 both read "
                                      "(A:CIRCUIT AVIONICS ON) = 0 (1 = OFF, 2 = DISPATCH as on the 2020 CJ4, which says what DISPATCH powers there). "
                                      "Turning the avionics off reboots the displays.",
})
# 2020 events MSFS 2024 no longer reports, and what replaces them
ONLY_2020 = {
    "ELECTRICAL_Battery_1": "ELECTRICAL_SWITCH_BATTERY_MASTER",
    "ELECTRICAL_Avionics_Bus_1": "ELECTRICAL_SWITCH_AVIONICS_BUS",
    "LIGHTING_LANDING_1": "LIGHTING_LANDING_0",
    "FUEL_Crossfeed_1": "FUEL_TRANSFER_SELECTOR",
    "ELECTRICAL_Circuit_49": None,
    "FMC_CJ4_2_Button_BRT_DIM": "FMC_BRT_DIM_2",
}

for _n in (15, 16, 17, 18):
    VERIFIED["LIGHTING_POTENTIOMETER_%d" % _n] = "Verified on a CJ4: the value 0..100 is the brightness, read back with (A:LIGHT POTENTIOMETER:%d)." % _n

# --- row builders (shared ones live in common.py) ---------------------------------------------------------------
from common import NOT_TRIED, knob, latched, level, press, row, unknown  # noqa: E402


def trim(panel, section, function, **kw):
    # the trim handlers act on a value >= 2 (up) or <= 0 (down); 1 does nothing
    return [row(panel, section, "%s: trim up" % function, "knob_up", value=10, **kw),
            row(panel, section, "%s: trim down" % function, "knob_down", value=0, **kw)]


# the Working Title FADEC of the CJ4 (CJ4Fadec.ts, ThrottleLeverManager.ts in the sources)
_THR = ("For an axis use THROTTLE1_AXIS_SET_EX1 / THROTTLE2_AXIS_SET_EX1 (-16384 = idle, 16384 = full; THROTTLE1_SET takes 0..16384): "
        "the Working Title FADEC turns the value into a lever position 0..1 ((value + 16384) / 32768) and picks the thrust mode from it: "
        "CRU from idle up to 0.776, CLB from 0.776 (axis value about 9044), TO from 0.958 (about 15008). There is no reverse. Read from the source, "
        "not moved on the aircraft.")


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
        "VerticalSpeed": knob(A, "Vertical", "VS wheel", "up (+100 fpm)", "down (-100 fpm)"),
        "NAV_Mode": press(A, "Lateral", "NAV mode",
                          note="Pressed with the flight director on and no navigation source: nothing engaged, so not confirmed."),
        "Approach_Button": press(A, "Lateral", "APR (approach)",
                                 note="Pressed with the flight director on and no navigation source: nothing engaged, so not confirmed."),
        "Backcourse_Button": press(A, "Lateral", "BC (back course)", note=NOT_TRIED),
        "Bank_Button": press(A, "Lateral", "BANK (low bank)", note=NOT_TRIED),
        "Transfer_Mode": press(A, "Flight director", "XFR (transfer to the other side)", note=NOT_TRIED),
        "Course_Dyn_1": knob(A, "Course", "CRS 1 knob", "up (+1 deg)", "down (-1 deg)"),
        "Course_Dyn_1_Sync": press(A, "Course", "CRS 1 SYNC", note=NOT_TRIED),
        "Course_Dyn_2": knob(A, "Course", "CRS 2 knob", "up (+1 deg)", "down (-1 deg)", note="Clicked on a CJ4: (A:NAV OBS:2) did not change, so what this knob moves is not known."),
        "Course_Dyn_2_Sync": press(A, "Course", "CRS 2 SYNC", note=NOT_TRIED),
        "Baro_1": knob(A, "Baro", "BARO 1 knob", "up", "down"),
        "Baro_1_STD": press(A, "Baro", "BARO 1 STD", note="Pressed on a CJ4: (A:KOHLSMAN SETTING MB:1) went from 1013.25 to 1013.24 and back with the second press; no variable showing the STD state was found."),
        "Baro_2": knob(A, "Baro", "BARO 2 knob", "up", "down", note="Clicked on a CJ4: (A:KOHLSMAN SETTING MB:2) did not change. MSFS 2024: +1 moved the PILOT's "
                      "setting ((A:KOHLSMAN SETTING MB:1) 1013.25 to 1013.56), so bind BARO 1 or BARO 2, not both (both would step twice)."),
        "Baro_2_STD": press(A, "Baro", "BARO 2 STD", note="Pressed on a CJ4 in MSFS 2024: it toggles the pilot's STD flag (L:XMLVAR_Baro1_ForcedToSTD), "
                          "as BARO 1 STD does; (L:XMLVAR_Baro2_ForcedToSTD) stays 0. Bind one of the two."),
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
        "ELECTRICAL_Avionics_Bus_1": latched(E, "Avionics", "Avionics bus switch", 3),
        "ELECTRICAL_Battery_1": latched(E, "Batteries", "Battery switch"),
        "ELECTRICAL_Alternator_1": latched(E, "Generators", "Generator 1 (left)"),
        "ELECTRICAL_Alternator_2": latched(E, "Generators", "Generator 2 (right)"),
        "ELECTRICAL_Circuit_49": unknown(E, "Circuits", "Circuit 49", "Purpose not known, not tried."),
        "ELECTRICAL_EMERLIGHTS": latched(L, "Interior", "Emergency lights switch", note=NOT_TRIED),
        "ENGINE_Ignition_1": latched(P_ENG, "Ignition", "Ignition 1 (left)", note="The value follows set 0 / 1 (2 reads back as 1); none of the variables tried ((A:TURB ENG IGNITION SWITCH EX1:1), (A:TURB ENG IS IGNITING:1)) followed it, so its effect was not read."),
        "ENGINE_Ignition_2": latched(P_ENG, "Ignition", "Ignition 2 (right)", note=NOT_TRIED),
        "ENGINE_Starter_1": latched(P_ENG, "Starters", "Starter 1 (left)"),
        "ENGINE_Starter_2": latched(P_ENG, "Starters", "Starter 2 (right)"),
        "ENGINE_Starter_Disengage": press(P_ENG, "Starters", "Starter disengage", note=NOT_TRIED),
        "ENGINE_TOGA_PUSH": press(P_ENG, "Thrust levers", "TOGA button", note=NOT_TRIED),
        "ENGINE_Throttle_1": unknown(P_ENG, "Thrust levers", "Thrust lever 1 (left)",
                                     "Seen on a CJ4 with the brake set: values up to 10 leave the lever at idle and 11 or more send it to 100 % (not a usable axis). " + _THR),
        "ENGINE_Throttle_2": unknown(P_ENG, "Thrust levers", "Thrust lever 2 (right)",
                                     "Same event as lever 1; on lever 1 values up to 10 leave the lever at idle and 11 or more send it to 100 % (not a usable axis). " + _THR),
        "FUEL_Crossfeed_1": latched(F, "Crossfeed", "Crossfeed switch", 3, note="Seen on a CJ4: the value follows set 0 / 1 / 2 (a three-position, set-value switch, resting at 1); the effect on the fuel system was not read."),
        "FUEL_Pump_1": latched(F, "Pumps", "Boost pump 1 (left)"),
        "FUEL_Pump_2": latched(F, "Pumps", "Boost pump 2 (right)"),
        "FUEL_RunStop_1": latched(F, "Engine run / stop", "RUN / STOP button 1 (left)"),
        "FUEL_RunStop_2": latched(F, "Engine run / stop", "RUN / STOP button 2 (right)"),
        "FUEL_FUEL_Push_RunStop_1_Cover": latched(F, "Engine run / stop", "RUN / STOP cover 1 (left)", note=NOT_TRIED),
        "FUEL_FUEL_Push_RunStop_2_Cover": latched(F, "Engine run / stop", "RUN / STOP cover 2 (right)", note=NOT_TRIED),
        "HANDLING_Flaps": [row(P_TRIM, "Flaps", "Flaps: position 0 (up)", "level", value=0),
                           row(P_TRIM, "Flaps", "Flaps: position 1", "level", value=8192),
                           row(P_TRIM, "Flaps", "Flaps: position 2", "level", value=16384),
                           row(P_TRIM, "Flaps", "Flaps: axis (0 to 16384)", "level", axis_range=[0, 16384])],
        "HANDLING_Spoilers": [row(P_TRIM, "Spoilers", "Spoilers: retract", "level", value=0),
                              row(P_TRIM, "Spoilers", "Spoilers: extend fully", "level", value=16384),
                              row(P_TRIM, "Spoilers", "Spoilers: axis (0 to 16384)", "level", axis_range=[0, 16384])],
        "HANDLING_ElevatorTrim_YOKE": trim(P_TRIM, "Trim", "Elevator trim (yoke switch)"),
        "HANDLING_ElevatorTrim_PEDESTRAL": trim(P_TRIM, "Trim", "Elevator trim (pedestal wheel)", note="Same handler as the yoke switch is assumed; not tried."),
        "HANDLING_AileronsTrim_1": trim(P_TRIM, "Trim", "Aileron trim"),
        "HANDLING_RudderTrim_1": trim(P_TRIM, "Trim", "Rudder trim"),
        "HANDLING_HANDLING_Push_ElevatorTrim_Secondary_Cover": latched(P_TRIM, "Trim", "Secondary elevator trim cover", note=NOT_TRIED),
        "HANDLING_Secondary_ElevatorTrim_Available": unknown(P_TRIM, "Trim", "Secondary elevator trim available", "Status value, not tried."),
        "HANDLING_Yoke_Hider_1": latched(P_OTHER, "View", "Hide the left yoke", note="Cosmetic (hides the yoke model); not tried."),
        "HANDLING_Yoke_Hider_2": latched(P_OTHER, "View", "Hide the right yoke", note="Cosmetic (hides the yoke model); not tried."),
        "LANDING_GEAR_Gear": latched(P_GEAR, "Gear", "Landing gear lever", note="Reads 1 (down) on the ground; deliberately not tried."),
        "LANDING_GEAR_ParkingBrake": [row(P_GEAR, "Brakes", "Parking brake: set", "level", value=100),
                                      row(P_GEAR, "Brakes", "Parking brake: release", "level", value=0)],
        "LIGHTING_PANEL_1": unknown(L, "Interior", "Panel lights",
                                    "Odd on a real CJ4: setting any value switched the panel light on, the value read back moved between 5 and 100, and (A:LIGHT PANEL) only went back to 0 with K:PANEL_LIGHTS_SET 0. Use the standard panel-light events instead."),
        "PASSENGER_Climate_Control": latched(P_CABIN, "Cabin", "Cabin climate control", note=NOT_TRIED),
        "PRESSURIZATION_Dump": latched(P_CABIN, "Pressurization", "Cabin dump", note=NOT_TRIED),
        "PRESSURIZATION_Dump_Cover": latched(P_CABIN, "Pressurization", "Cabin dump cover", note=NOT_TRIED),
        "PRESSURIZATION_Pressource_1": latched(P_CABIN, "Pressurization", "Pressure source", 3, note="Seen on a CJ4: the value follows set 0 / 1 / 2 (-1 reads back as 0), a three-position, set-value switch resting at 2; its effect was not read."),
        "PROCEDURE_ToggleAllLights": press(L, "Exterior", "Toggle all lights (procedure helper)", note=NOT_TRIED),
        "SAFETY_Caution_1": press(P_SAFE, "Master warning", "Master caution 1", note=NOT_TRIED),
        "SAFETY_Caution_2": press(P_SAFE, "Master warning", "Master caution 2", note=NOT_TRIED),
        "SAFETY_Warning_1": press(P_SAFE, "Master warning", "Master warning 1", note=NOT_TRIED),
        "SAFETY_Warning_2": press(P_SAFE, "Master warning", "Master warning 2", note=NOT_TRIED),
        "SAFETY_PASSENGER_SAFETY_PUSH": latched(P_SAFE, "Signs", "Passenger safety sign"),
        "SAFETY_SEATBELTS_PUSH": latched(P_SAFE, "Signs", "Seat belt sign"),
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


# --- events only MSFS 2024 reports (upper case); labels from the names and the aircraft's English tooltips --------
_TT = "Label from the MSFS 2024 tooltip %s; not tried."
_DOOR = ("WARNING: a door, hatch or seat event of MSFS 2024 can move the pilot out of the cockpit (it happened on a C172); never sent "
         "by tools/probe_controls.py. Not tried.")
_SIDE = {"PILOT": "pilot", "COPILOT": "copilot"}


def _cj4_2024(name):
    E, L = P_ELEC, P_LIGHT
    table = {
        "ELECTRICAL_SWITCH_BATTERY_MASTER": latched(E, "Batteries", "Battery switch (BATT / OFF / EMER)", 3,
                                                    note="MSFS 2024 name of ELECTRICAL_Battery_1. Seen on a CJ4 in MSFS 2024 (engines running): the "
                                                         "value follows set 0 / 1 / 2 and rests at 0 = BATT (ON, as on the 2020 CJ4, where 1 = OFF); "
                                                         "(A:ELECTRICAL MASTER BATTERY) stayed 1 in every position with the generators on, so OFF "
                                                         "and EMER were not confirmed."),
        "ELECTRICAL_SWITCH_AVIONICS_BUS": latched(E, "Avionics", "Avionics switch (ON / OFF / DISPATCH)", 3),
        "ELECTRICAL_BATTERY_DISCONNECT": latched(E, "Batteries", "Battery disconnect", note=_TT % "CJ4.BATTERY_DISCONNECT"),
        "ELECTICAL_INTERIOR_MASTER": latched(E, "Cabin power", "Interior master (cabin electrical power)",
                                             note=_TT % "CJ4.INTERIOR_MASTER (the event name is spelled ELECTICAL by the sim)"),
        "LIGHTING_LANDING_0": latched(L, "Exterior", "Landing lights"),
        "FMC_BRT_DIM_1": level(P_CDU1, "Brightness", "CDU 1 brightness", (0, 1), note="Rests at 0.5; range 0..1 assumed, not tried."),
        "FMC_BRT_DIM_2": level(P_CDU2, "Brightness", "CDU 2 brightness", (0, 1), note="Rests at 0.5; range 0..1 assumed, not tried."),
        "FUEL_TRANSFER_SELECTOR": latched(P_FUEL, "Crossfeed", "FUEL TRANSFER selector", 3,
                                          note="MSFS 2024 name of FUEL_Crossfeed_1 (which rested at 1 of 0 / 1 / 2 too); rests at 1, not tried."),
        "HYD_VALVE_1": latched(P_FIRE, "Hydraulic shutoff", "Hydraulic shutoff valve, left",
                               note="WARNING: closes the left hydraulic supply. " + _TT % "CJ4.HYD_SHUTOFF_ACTION"),
        "HYD_VALVE_2": latched(P_FIRE, "Hydraulic shutoff", "Hydraulic shutoff valve, right",
                               note="WARNING: closes the right hydraulic supply. " + _TT % "CJ4.HYD_SHUTOFF_ACTION"),
        "COMMON_LOCK_PILOT_HYDSHUTOFF_L_COVER": latched(P_FIRE, "Hydraulic shutoff", "Hydraulic shutoff cover, left", note=NOT_TRIED),
        "COMMON_LOCK_PILOT_HYDSHUTOFF_R_COVER": latched(P_FIRE, "Hydraulic shutoff", "Hydraulic shutoff cover, right", note=NOT_TRIED),
        "COMMON_LOCK_PILOT_LENGINEFIRELIGHT_COVER": latched(P_FIRE, "Engine fire", "ENG FIRE cover, left", note=NOT_TRIED),
        "COMMON_LOCK_COPILOT_R_ENGINEFIRELIGHT_COVER": latched(P_FIRE, "Engine fire", "ENG FIRE cover, right", note=NOT_TRIED),
        "COMMON_LOCK_BATTERY_DISCHARGE_COVER": latched(E, "Batteries", "Battery cover (discharge)", note=NOT_TRIED),
        "COMMON_LOCK_BATTERY_INT_COVER": latched(E, "Batteries", "Battery cover (interior)", note=NOT_TRIED),
        "CJ4_AVIONICS_PUSH_PILOT_LENGINEFIRELIGHT": press(P_FIRE, "Engine fire", "ENG FIRE push light, left",
                                                          note="DANGER: on the real aircraft this shuts off fuel and hydraulics to the engine and "
                                                               "arms the fire bottle. " + _TT % "CJ4.ENG_FIRE_WARNING"),
        "CJ4_AVIONICS_PUSH_COPILOT_RENGINEFIRELIGHT": press(P_FIRE, "Engine fire", "ENG FIRE push light, right",
                                                            note="DANGER: on the real aircraft this shuts off fuel and hydraulics to the engine and "
                                                                 "arms the fire bottle. " + _TT % "CJ4.ENG_FIRE_WARNING"),
        "CJ4_AVIONICS_PUSH_PILOT_LFIREBOTTLEARMEDLIGHT": press(P_FIRE, "Engine fire", "BOTTLE ARMED push light, left",
                                                              note="DANGER: discharges the fire bottle. " + _TT % "CJ4.FIRE_BOTTLE_ARMED_LIGHT"),
        "CJ4_AVIONICS_PUSH_COPILOT_RFIREBOTTLEARMEDLIGHT": press(P_FIRE, "Engine fire", "BOTTLE ARMED push light, right",
                                                                note="DANGER: discharges the fire bottle. " + _TT % "CJ4.FIRE_BOTTLE_ARMED_LIGHT"),
        "CJ4_AVIONICS_PUSH_PILOT_FADECRESET_L": press(P_ENG, "FADEC", "FADEC RESET, left", note=_TT % "CJ4.FADEC_RESET"),
        "CJ4_AVIONICS_PUSH_PILOT_FADECRESET_R": press(P_ENG, "FADEC", "FADEC RESET, right", note=_TT % "CJ4.FADEC_RESET"),
        "PROCEDURE_AUTOSTART": press(P_ENG, "Starters", "Automatic engine start (the sim's procedure)",
                                     note="WARNING: runs the sim's whole engine start (or shutdown); never sent by tools/probe_controls.py. Not tried."),
        "CJ4_AVIONICS_DEICE_PUSH_TAIL_1": latched(P_ICE, "Tail", "TAIL DEICE button, left", note=_TT % "CJ4.TAIL_DEICE"),
        "CJ4_AVIONICS_DEICE_PUSH_TAIL_2": latched(P_ICE, "Tail", "TAIL DEICE button, right", note=_TT % "CJ4.TAIL_DEICE"),
        "CJ4_AVIONICS_DEICE_PUSH_XFLOW": latched(P_ICE, "Anti-ice crossflow", "DEICE XFLOW (crossflow)", note=_TT % "CJ4.DEICE_XFLOW"),
        "CJ4_AVIONICS_LIGHTING_PUSH_LIGHT_PULSE_TCAS": latched(L, "Exterior", "TCAS PULSE lights", note=_TT % "CJ4.TCAS_PULSE_LIGHTS"),
        "LIGHTING_CABIN_4": latched(L, "Interior", "Cabin light 4", note="Rests at 1; not tried."),
        "LIGHTING_CABIN_5": latched(L, "Interior", "Cabin light 5", note="Rests at 1; not tried."),
        "LIGHTING_CABIN_6": latched(L, "Interior", "Cabin light 6", note="Rests at 1; not tried."),
        "CJ4_AVIONICS_PRESSURIZATION_PUSH_CONTROL": latched(P_CABIN, "Pressurization", "Standby pressure control",
                                                            note=_TT % "CJ4.STBY_PRESSURIZATION_CONTROL"),
        "CJ4_AVIONICS_LEVER_PILOT_OXYGENSUPPLY": latched(P_CABIN, "Oxygen", "Oxygen supply lever", note=_TT % "CJ4.OXYGEN_SUPPLY"),
        "PASSENGER_OXYGEN_CONTROL": latched(P_CABIN, "Oxygen", "OXYGEN CONTROL selector (CREW ONLY / DROP MASK)",
                                            note="Rests at 1; positions from the tooltips CJ4.OXYGEN_CONTROL_*_VALUE, values not tried."),
        "CJ4_AVIONICS_PUSH_OXYGENMASK": latched(P_CABIN, "Oxygen", "Oxygen mask", note=NOT_TRIED),
        "CJ4_AVIONICS_PUSH_PILOT_OXYGENMASK_MIC": latched(P_RADIO, "Microphones", "Oxygen mask microphone", note=_TT % "CJ4.OXYGEN_MASK_MIC"),
        "CJ4_AVIONICS_PUSH_COPILOTMIC_PIT": latched(P_RADIO, "Microphones", "Copilot microphone", note=_TT % "CJ4.COPILOT_MIC_PIT"),
        "CJ4_AVIONICS_PUSH_CKPT_SPKR_MUTE": press(P_RADIO, "Audio panel", "Mute cockpit speaker", note=_TT % "CJ4.MUTE_COCKPIT_SPEAKER"),
        "CJ4_AVIONICS_PUSH_COM1TUNE": press(P_RADIO, "Audio panel", "COM 1 tune to 121.5 (emergency)", note=_TT % "CJ4.COMM_1_TUNE"),
        "CJ4_AVIONICS_PUSH_EVENTMARKER": press(P_RADIO, "Audio panel", "Event marker (voice recorder)", note=_TT % "CJ4.EVENT_MARKER"),
        "CJ4_AVIONICS_PUSH_TAWS_GSCANCEL": press(P_SAFE, "TAWS", "TAWS glideslope cancel", note=_TT % "CJ4.TAWS_GS_CANCEL"),
        "SAFETY_ELT_1": latched(P_SAFE, "ELT", "ELT switch", note="Emergency locator transmitter; not tried."),
        "STBY_FLT_DISPLAY_SWITCH": latched(P_DISP, "Standby display", "STBY FLT DISPLAY switch",
                                           note="Rests at 0; the number of positions is not known. " + _TT % "CJ4.STBY_FLT_DISPLAY"),
        "HANDLING_EFB_HIDE": latched(P_OTHER, "View", "Hide the EFB", note="Cosmetic; not tried."),
        "TOGGLE_VISOR_L_VISOR_L": latched(P_OTHER, "View", "Sun visor, left", note="Cosmetic; not tried."),
        "TOGGLE_VISOR_R_VISOR_R": latched(P_OTHER, "View", "Sun visor, right", note="Cosmetic; not tried."),
        "UNKNOWN_BUTTONS_COLLISION_EFB_VISIBILITY_HIDE": unknown(P_OTHER, "View", "EFB visibility (sim helper)", "Sim helper for the tablet; not tried."),
        "UNKNOWN_TABLET_BACKDROP": unknown(P_OTHER, "View", "Tablet backdrop (sim helper)", "Sim helper for the tablet; not tried."),
        "OPEN_DOOR_BATHROOM_BATHROOM_DOOR": latched(P_CABIN, "Cabin", "Lavatory door", note=_DOOR),
        "CJ4_INT_PILOT_EXIT": press(P_EXT, "Pilot", "Pilot: leave the cockpit", note=_DOOR),
        "CJ4_EXT_PILOT_SIT": press(P_EXT, "Pilot", "Pilot: sit in the cockpit", note=_DOOR),
    }
    if name in table:
        return table[name]
    m = re.match(r"AS3000_LOWER_(\d)_MEM(\d)$", name)
    if m:
        return press(P_DISP, "CCP %s" % m.group(1), "MEM %s (MFD format %s)" % (m.group(2), m.group(2)), note=_TT % ("CJ4.CCP_PUSH_MEM_%s" % m.group(2)))
    m = re.match(r"CJ4_AVIONICS_CCP_PUSH_(\w+)_(\d)$", name)
    if m:
        key, n = m.groups()
        fn = {"CHART": "CHART (chart data)", "CKLST": "CKLST (MFD checklists)", "CURSR": "CURSOR", "DATABASE": "DATABASE",
              "PASSBRIEF": "PASS BRIEF (passenger briefing)", "UNKNOWN": "unlabelled key"}[key]
        return press(P_DISP, "CCP %s" % n, fn, note=NOT_TRIED)
    m = re.match(r"CJ4_AVIONICS_PUSH_(PILOT|COPILOT)_(?:APP_)?(ADC|AHRS|CDU|MFD|PFD|MAN|SLEW_LR)$", name)
    if m:
        side, key = m.groups()
        if key == "SLEW_LR":
            return unknown(P_DISP, "Reversion (%s)" % _SIDE[side], "AHRS slave slew (%s)" % _SIDE[side],
                           "A left / right slew switch; its values are not known. " + _TT % "CJ4.AHRS_SLAVE_SLEW")
        fn = {"ADC": "ADC reversion", "AHRS": "AHRS reversion", "CDU": "CDU reversion", "MFD": "MFD reversion", "PFD": "PFD reversion",
              "MAN": "AHRS slave: manual"}[key]
        return press(P_DISP, "Reversion (%s)" % _SIDE[side], "%s (%s)" % (fn, _SIDE[side]), note=NOT_TRIED)
    m = re.match(r"CJ4_AVIONICS_PUSH_(PILOT|COPILOT)_DCP_(CCP|RADAR|TAWS)MENU$", name)
    if m:
        side, key = m.groups()
        return press(P_DISP, "DCP (%s)" % _SIDE[side], "%s MENU (%s)" % (key, _SIDE[side]), note=NOT_TRIED)
    m = re.match(r"CJ4_EXT_(.+)$", name)
    if m:
        what = m.group(1)
        pretty = what.replace("_", " ").lower().replace("enginesafetycover", "engine cover")
        if what.endswith("_CHECK"):
            return press(P_EXT, "Walkaround checks", pretty[:-len(" check")].capitalize() + ": inspect",
                         note="A walkaround inspection point; not tried.")
        if "DOOR" in what:
            return latched(P_EXT, "Doors", pretty.capitalize(), note=_DOOR)
        return latched(P_EXT, "Covers, chocks and caps", pretty.capitalize(), note="Ground equipment; not tried.")
    return None


def classify(name):
    """Rows (one per catalog entry) for a cockpit input event, or None when the event has no rule."""
    rows = _classify(name)
    if rows and name in ONLY_2020:
        repl = ONLY_2020[name]
        for r in rows:
            r["remark"] = "MSFS 2020 only" + ("; in MSFS 2024 use %s." % repl if repl else "; MSFS 2024 does not report it.")
    return rows


def _classify(name):
    r = _cj4_2024(name)
    if r:
        return r
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
