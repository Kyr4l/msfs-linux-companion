# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Rules that label the controls of the Cessna 400 Corvalis TT (Microsoft / Carenado, MSFS 2024; build_aircraft_catalog.py).

The aircraft has 302 cockpit input events (B:). What each one is comes from
  * the event names themselves (the sim's standard families: ELECTRICAL_, ENGINE_, LIGHTING_, AS1000_ for the G1000 ...)
  * the aircraft's English tooltips (Official2024/Steam/microsoft-aircraft-c400-corvalis/minimal.fsarchive, en-us.locpak): Primer
    "Hold between 3 - 5 seconds", Bus Tie, Test Annunciator Lights, Yaw/pitch trim, Alternate Air, the G1000 control pad keys, the buttons
    marked "Inoperative" ...
  * the G1000 NXi's own event names (microsoft/msfs-avionics-mirror, src/workingtitle-instruments-g1000/html_ui/Pages/VCockpit/Instruments/
    NavSystems/WTG1000/PFD/PFDViewService.ts and MFD/MFDViewService.ts)
The plain archives of the package hold no checklist (it is in the encrypted model archive, not read), so the procedures come from the
locpak's checklist texts (GAME.CHECKLIST_*, CLUE.*). Nothing here was tried through this program on a running aircraft yet, so no entry
is marked verified; each note says where the entry comes from.
"""
import re

from common import NOT_TRIED, knob, latched, level, press, unknown

AIRCRAFT = "Microsoft_C400_Corvalis"
TITLE = "Cessna 400 Corvalis TT (Microsoft C400)"
EVENTS_FILE = "microsoft_c400_corvalis_events.json"
AXIS_CONTROLS = True     # throttle, propeller, mixture, brakes, flaps and trim also appear as the ranged axis events every aircraft has
STANDARD_EVENTS = True   # and the standard K: events (parking brake, flaps, magnetos ...)
SOURCES = [
    "Cockpit input events enumerated from MSFS 2024 (SimConnect EnumerateInputEvents), from the companion's live cache of the aircraft",
    "Official2024/Steam/microsoft-aircraft-c400-corvalis/minimal.fsarchive, en-us.locpak: the aircraft's English tooltips and checklist texts (unencrypted archive)",
    "microsoft/msfs-avionics-mirror, workingtitle-instruments-g1000: PFDViewService.ts and MFDViewService.ts (the G1000 NXi's H: events and knobs)",
]

P_AP = "Autopilot"
P_ENG = "Engine"
P_FUEL = "Fuel"
P_ELEC = "Electrical"
P_LIGHT = "Lights"
P_ICE = "Ice protection"
P_HAND = "Flight controls"
P_ENV = "Cabin climate"
P_PFD = "G1000 PFD"
P_MFD = "G1000 MFD"
P_PAD = "G1000 control pad"
P_AUDIO = "Audio panel"
P_INST = "Instruments"
P_CB = "Circuit breakers"
P_SAFE = "Safety"
P_EXT = "Walkaround"
P_OTHER = "Other"
PANEL_ORDER = [P_AP, P_ENG, P_FUEL, P_ELEC, P_LIGHT, P_ICE, P_HAND, P_ENV, P_PFD, P_MFD, P_PAD, P_AUDIO, P_INST, P_CB, P_SAFE, P_EXT, P_OTHER]

VERIFIED = {}

_TIP = "Tooltip in the aircraft: \"%s\". " + NOT_TRIED
_G1000 = "G1000 NXi control (named as in the Working Title source). " + NOT_TRIED
_G1000_KNOB = "G1000 NXi knob (named as in the Working Title source); which way is which is taken from the name. Not tried."
_INOP = "The aircraft's tooltip marks this control \"Inoperative\": the model has it, the systems do not. Not tried."
_DOOR = ("WARNING: a door or seat event of MSFS 2024 can move the pilot out of the cockpit (it happened on a C172); never sent "
         "by the probe. Not tried.")
_WALK = "A point of the interactive preflight walkaround (MSFS 2024), not a cockpit control. Not tried."
_CB = "Circuit breaker (pulled / pushed in), named from the event. Not tried."

_SIMPLE = {
    # --- autopilot (on the yoke and glareshield; the AP panel itself is the G1000's, below) ------------------------------------------------
    "AUTOPILOT_GO_AROUND": press(P_AP, "Yoke and throttle", "Go-around (TO/GA)", note=_TIP % "Abort Landing"),
    "HANDLING_DISCONNECT": press(P_AP, "Yoke and throttle", "Autopilot disconnect", note=_TIP % "Autopilot disconnect button"),
    # --- engine ------------------------------------------------------------------------------------------------------------------------------
    "ENGINE_THROTTLE_LEFT": level(P_ENG, "Levers", "Throttle", note=NOT_TRIED),
    "ENGINE_PROPELLER_LEFT": level(P_ENG, "Levers", "Propeller", note=NOT_TRIED),
    "ENGINE_MIXTURE_LEFT": level(P_ENG, "Levers", "Mixture", note="Full lean = IDLE CUTOFF (the sim's checklist text). " + NOT_TRIED),
    "ENGINE_MAGNETO": latched(P_ENG, "Start", "Ignition key (OFF, R, L, BOTH, START)", positions=5,
                              note="Five positions as on the real aircraft's key switch; the order in the sim is not known. Not tried."),
    "ENGINE_PRIMER": press(P_ENG, "Start", "Primer", note=_TIP % "Primer control button. Hold between 3 - 5 seconds"),
    "INSTRUMENT_INOP_SWITCH_VAPOR_SUPPRESS": unknown(P_ENG, "Start", "Vapor suppression (inoperative)", _INOP),
    # --- fuel --------------------------------------------------------------------------------------------------------------------------------
    "FUEL_SELECTOR": latched(P_FUEL, "Fuel", "Fuel selector (LEFT, RIGHT, OFF)", positions=3,
                             note=_TIP % "Fuel Selector" + " Three positions as on the real aircraft; the order in the sim is not known."),
    "FUEL_PUMP_LEFT": latched(P_FUEL, "Fuel", "Backup fuel pump 1", note="The sim's checklist sets the backup fuel pumps to ARMED. " + NOT_TRIED),
    "FUEL_PUMP_RIGHT": latched(P_FUEL, "Fuel", "Backup fuel pump 2", note="The sim's checklist sets the backup fuel pumps to ARMED. " + NOT_TRIED),
    # --- electrical --------------------------------------------------------------------------------------------------------------------------
    "ELECTRICAL_BATTERY_LEFT": latched(P_ELEC, "Power", "Battery 1", note=NOT_TRIED),
    "ELECTRICAL_BATTERY_RIGHT": latched(P_ELEC, "Power", "Battery 2", note=NOT_TRIED),
    "ELECTRICAL_ALTERNATOR_LEFT": latched(P_ELEC, "Power", "Alternator 1", note=NOT_TRIED),
    "ELECTRICAL_ALTERNATOR_RIGHT": latched(P_ELEC, "Power", "Alternator 2", note=NOT_TRIED),
    "ELECTRICAL_AVIONICS": latched(P_ELEC, "Power", "Avionics master", note=NOT_TRIED),
    "ELECTRICAL_BUS_TIE": latched(P_ELEC, "Power", "Bus tie", note=_TIP % "Bus Tie"),
    "ELECTRICAL_TEST": press(P_ELEC, "Annunciators", "Annunciator test", note=_TIP % "Test Annunciator Lights"),
    # --- lights ------------------------------------------------------------------------------------------------------------------------------
    "LIGHTING_LANDING_LIGHT": latched(P_LIGHT, "Exterior", "Landing light", note=NOT_TRIED),
    "LIGHTING_TAXI_LIGHT": latched(P_LIGHT, "Exterior", "Taxi light", note=NOT_TRIED),
    "LIGHTING_NAVIGATION_LIGHT": latched(P_LIGHT, "Exterior", "Navigation lights", note=NOT_TRIED),
    "LIGHTING_STROBE_LIGHT": latched(P_LIGHT, "Exterior", "Strobe lights", note=NOT_TRIED),
    "LIGHTING_CABIN_LIGHT": latched(P_LIGHT, "Cabin", "Cabin light", note=NOT_TRIED),
    "LIGHTING_CABIN_LIGHT_BACK_LEFT": latched(P_LIGHT, "Cabin", "Rear cabin light, left", note=NOT_TRIED),
    "LIGHTING_CABIN_LIGHT_BACK_RIGHT": latched(P_LIGHT, "Cabin", "Rear cabin light, right", note=NOT_TRIED),
    "LIGHTING_CABIN_LIGHT_INTENSITY": knob(P_LIGHT, "Cabin", "Cabin light dimmer", note=NOT_TRIED),
    "LIGHTING_PANEL_LIGHT_INTENSITY": knob(P_LIGHT, "Panel", "Panel lights dimmer", note=NOT_TRIED),
    "LIGHTING_SUB_PANEL_LIGHT_INTENSITY": knob(P_LIGHT, "Panel", "Subpanel lights dimmer", note=_TIP % "Subpanel lighting"),
    "LIGHTING_GLARESHIELD_LIGHT_INTENSITY": knob(P_LIGHT, "Panel", "Glareshield lights dimmer", note=NOT_TRIED),
    # --- ice protection ----------------------------------------------------------------------------------------------------------------------
    "DEICE_AIRFRAME": latched(P_ICE, "Ice protection", "Airframe de-ice", note=_TIP % "Airframe de-ice"),
    "DEICE_PROP_HEAT": latched(P_ICE, "Ice protection", "Propeller de-ice", note=_TIP % "Propeller de-ice"),
    "DEICE_WINDSHIELD": latched(P_ICE, "Ice protection", "Windshield de-ice", note=_TIP % "Windshield de-ice"),
    "DEICE_PITOT_HEAT": latched(P_ICE, "Ice protection", "Pitot heat", note=NOT_TRIED),
    # --- flight controls ---------------------------------------------------------------------------------------------------------------------
    "HANDLING_FLAPS": latched(P_HAND, "Flaps", "Flaps (UP, takeoff, landing)", positions=3,
                              note="Three positions as on the real aircraft; the sim's checklist retracts them at 400 ft AGL and 95 KIAS or more. Not tried."),
    "HANDLING_ELEVATOR_TRIM": knob(P_HAND, "Trim", "Elevator trim", "nose up", "nose down", note=NOT_TRIED),
    "HANDLING_YAWPITCH": knob(P_HAND, "Trim", "Yaw/pitch trim (coolie hat)", "up", "down", note=_TIP % "Yaw/pitch trim"),
    "HANDLING_SPOILERS": unknown(P_HAND, "Other", "Spoilers", "The real aircraft has no spoilers; the sim lists the event. Not tried."),
    "LANDING_PARKING_BRAKE": latched(P_HAND, "Brakes", "Parking brake", note=NOT_TRIED),
    "INSTRUMENT_INOP_BUTTON_RUDDER_HOLD": unknown(P_HAND, "Other", "Rudder hold (inoperative)", _INOP),
    # --- cabin climate -----------------------------------------------------------------------------------------------------------------------
    "INSTRUMENT_AIR_TEMP_ON": press(P_ENV, "Climate", "Climate ON", note=NOT_TRIED),
    "INSTRUMENT_AIR_TEMP_OFF": press(P_ENV, "Climate", "Climate OFF", note=NOT_TRIED),
    "INSTRUMENT_AIR_TEMP_AUTO": press(P_ENV, "Climate", "Climate AUTO", note=NOT_TRIED),
    "INSTRUMENT_AIR_TEMP_MORE_RED": press(P_ENV, "Climate", "Warmer", note=NOT_TRIED),
    "INSTRUMENT_AIR_TEMP_LESS_BLUE": press(P_ENV, "Climate", "Cooler", note=NOT_TRIED),
    "PNEUMATICS_CABIN_HEAT": latched(P_ENV, "Climate", "Cabin heat", note=NOT_TRIED),
    "INSTRUMENT_INOP_C400_BTN_AIR_TEMP_COLD": unknown(P_ENV, "Climate", "Cold (inoperative)", _INOP),
    "INSTRUMENT_INOP_C400_BTN_AIR_TEMP_LESS": unknown(P_ENV, "Climate", "Less (inoperative)", _INOP),
    "INSTRUMENT_INOP_C400_BTN_AIR_TEMP_OAT": unknown(P_ENV, "Climate", "OAT (inoperative)", _INOP),
    "INSTRUMENT_INOP_C400_BTN_AIR_TEMP_PLUSS": unknown(P_ENV, "Climate", "More (inoperative)", _INOP),
    # --- standby instruments -----------------------------------------------------------------------------------------------------------------
    "INSTRUMENT_ALTIMETER_2": knob(P_INST, "Standby", "Standby altimeter baro", note=NOT_TRIED),
    "INSTRUMENT_ATTITUDE_INDICATOR_CAGE": press(P_INST, "Standby", "Standby attitude cage", note=NOT_TRIED),
    # --- safety ------------------------------------------------------------------------------------------------------------------------------
    "SAFETY_ALTERNATE_AIR": latched(P_SAFE, "Engine", "Alternate air", note=_TIP % "Alternate Air"),
    "SAFETY_ELT": latched(P_SAFE, "ELT", "ELT", note="Arming or testing the ELT; leave it alone in flight. Not tried."),
    # --- cabin, doors, walkaround ------------------------------------------------------------------------------------------------------------
    "AIRFRAME_SUN_VISOR_LEFT": latched(P_OTHER, "Cabin", "Sun visor, left", note=NOT_TRIED),
    "AIRFRAME_SUN_VISOR_RIGHT": latched(P_OTHER, "Cabin", "Sun visor, right", note=NOT_TRIED),
    "COMMON_DOOR": unknown(P_EXT, "Doors", "Door", _DOOR),
    "COMMON_DOOR_LEFT": unknown(P_EXT, "Doors", "Left door", _DOOR),
    "COMMON_BAGGAGE_DOOR": unknown(P_EXT, "Doors", "Baggage door", _DOOR),
    "VARIABLE_FORCE_CLOSE_BAGGAGE_DOOR": unknown(P_EXT, "Doors", "Baggage door, close (logic)", "Internal logic of the aircraft, not a cockpit control. Not tried."),
    "PROCEDURE_AUTOSTART": press(P_OTHER, "Procedures", "Autostart", note="The sim's own engine autostart (MSFS 2024). Not tried; do not press it in flight."),
    "PROCEDURE_ELEVATOR_TRIM": unknown(P_OTHER, "Procedures", "Elevator trim (procedure)", "A step of the sim's procedures, not a cockpit control. Not tried."),
}

_WALKAROUND = {
    "CENTER_TIRE": "Nose tire", "CHOCKS": "Wheel chocks", "ELEVATOR": "Elevator", "ENGINE_COVER_1": "Engine cover 1", "ENGINE_COVER_2": "Engine cover 2",
    "FRONT_LEG_1": "Nose gear 1", "FRONT_LEG_2": "Nose gear 2", "FRONT_LEG_3": "Nose gear 3", "FUEL_CAP_2": "Fuel cap", "LEFT_AILERON": "Left aileron",
    "LEFT_FLAP": "Left flap", "LEFT_TIRE": "Left tire", "OIL_CAP_1": "Oil cap", "OIL_GAUGE": "Oil dipstick", "PITOT_COVER": "Pitot cover",
    "RIGHT_AILERON": "Right aileron", "RIGHT_FLAP": "Right flap", "RIGHT_TIRE": "Right tire", "RUDDER": "Rudder",
}

_BREAKERS = {
    "ADC": "Air data computer", "AFCS": "Autopilot (AFCS)", "AHRS": "AHRS", "AUDIO": "Audio panel", "AUX_PUMPS": "Backup fuel pumps",
    "AVCONT": "Avionics control", "AVIONIC_BUS": "Avionics bus", "COM_1": "COM 1", "COM_2": "COM 2", "DEICE": "De-ice", "DME": "DME",
    "ENGINST": "Engine instruments", "FLAPS": "Flaps", "GDU_FAN": "Display fan", "GEAR": "Gear", "GEAR_WARNING": "Gear warning",
    "GPS_NAV_1": "GPS/NAV 1", "GPS_NAV_2": "GPS/NAV 2", "LANDING_LIGHT": "Landing light", "LH_ALT": "Alternator 1", "LH_BATT": "Battery 1",
    "LH_ECUA": "Engine control 1 A", "LH_ECUB": "Engine control 1 B", "LH_ECUBUS": "Engine control bus 1", "LH_FUEL_A": "Fuel 1 A",
    "LH_FUEL_B": "Fuel 1 B", "MFD": "MFD", "NAVFLOCO": "Nav, flood and courtesy lights", "NSTLT": "Nav, strobe and taxi lights", "PFD": "PFD",
    "PITOT": "Pitot heat", "RH_ALT": "Alternator 2", "RH_BATT": "Battery 2", "RH_ECUA": "Engine control 2 A", "RH_ECUB": "Engine control 2 B",
    "RH_ECUBUS": "Engine control bus 2", "RH_FUEL_A": "Fuel 2 A", "RH_FUEL_B": "Fuel 2 B", "SAM": "Stall warning module", "STALL_WARNING": "Stall warning",
    "STATIC": "Alternate static", "TAS": "Traffic (TAS)", "TAXI_MAP": "Taxi and map lights", "WX_RADAR": "Weather radar", "XPDR": "Transponder",
}

# the G1000's bezel and AP keys, per display
_G1000_PRESS = {
    "DIRECTTO": "Direct-to", "FPL_PUSH": "FPL", "PROC_PUSH": "PROC", "MENU_PUSH": "MENU", "CLR": "CLR", "ENT_PUSH": "ENT",
    "FMS_UPPER_PUSH": "FMS knob push (cursor)", "RANGE_PUSH": "Range knob push (pan)", "NAV_ID_PUSH": "NAV ID push", "NAV_PUSH": "NAV knob push (NAV 1/2)",
    "NAV_SWITCH": "NAV frequency transfer", "COM_PUSH": "COM knob push (COM 1/2)", "COM_SWITCH": "COM frequency transfer", "COM_SQUELCH_PUSH": "COM squelch push",
    "COURSE_PUSH": "CRS push (center)", "HEADING_SYNC": "HDG push (sync)",
    "AUTOPILOT_AP": "AP", "AUTOPILOT_FD": "FD", "AUTOPILOT_YD": "YD", "AUTOPILOT_HEADING": "HDG", "AUTOPILOT_NAVIGATION": "NAV",
    "AUTOPILOT_APPROACH": "APR", "AUTOPILOT_ALTITUDE": "ALT", "AUTOPILOT_VERTICALSPEED": "VS", "AUTOPILOT_FLIGHTLEVELCHANGE": "FLC",
    "AUTOPILOT_VERTICAL_NAVIGATION": "VNV", "AUTOPILOT_VERTICALSPEED_UP": "NOSE UP (VS / FLC)", "AUTOPILOT_VERTICALSPEED_DOWN": "NOSE DN (VS / FLC)",
}
_G1000_KNOBS = {
    "FMS_UPPER": ("FMS knob, outer", "right", "left"), "FMS_LOWER": ("FMS knob, inner", "right", "left"),
    "RANGE": ("Range knob", "zoom out", "zoom in"), "RANGE_ZOOM": ("Range zoom", "out", "in"),
    "COM_LARGE": ("COM knob, outer (MHz)", "up", "down"), "COM_SMALL": ("COM knob, inner (kHz)", "up", "down"),
    "NAV_LARGE": ("NAV knob, outer (MHz)", "up", "down"), "NAV_SMALL": ("NAV knob, inner (kHz)", "up", "down"),
    "COM_VOLUME": ("COM volume", "up", "down"), "NAV_VOLUME": ("NAV volume", "up", "down"),
    "COURSE": ("CRS knob", "right", "left"), "HEADING": ("HDG knob", "right", "left"), "BARO": ("BARO knob", "up", "down"),
    "ALTITUDE_OUTER": ("ALT knob, outer (1000 ft)", "up", "down"), "ALTITUDE_INNER": ("ALT knob, inner (100 ft)", "up", "down"),
}
_AUDIO = {
    "COM_1_MIC": "COM 1 MIC", "COM_1": "COM 1", "COM_2_MIC": "COM 2 MIC", "COM_2": "COM 2", "COM_3_MIC": "COM 3 MIC", "COM_3": "COM 3",
    "COM_SWAP": "COM 1/2 swap", "COM_TEL": "TEL", "PA": "PA", "SPEAKER": "SPKR", "NAV_1": "NAV 1", "NAV_2": "NAV 2", "ADF": "ADF",
    "DME": "DME", "AUX": "AUX", "MARKERMUTE": "Marker mute", "MARKERSENSITIVITYHIGH": "Marker sensitivity HI", "MAN_SQ": "Manual squelch",
    "PLAY": "PLAY", "ISOLATEPILOT": "Pilot isolate", "ISOLATECOPILOT": "Copilot isolate", "AUDIOPANEL_PASS_COPILOT": "Pass to copilot",
    "DISPLAYBACKUP": "Display backup (red button)",
}
_PAD_NAMES = {
    "BKSP": "BKSP", "COM": "COM", "DOT": ".", "MFD": "MFD", "NAV": "NAV", "PFD": "PFD", "PLUSMINUS": "+/-", "PUSH_CLEAR": "CLR",
    "PUSH_DIRECTTO": "Direct-to", "PUSH_ENTER": "ENT", "PUSH_FLIGHTPLAN": "FPL", "PUSH_MENU": "MENU", "PUSH_PROCEDURE": "PROC",
    "SPC": "SPC", "SWAP": "Frequency swap", "FREQ_INNER_PUSH": "FMS NAV/COM knob push",
}


def classify(name):
    """Rows (one per catalog entry) for a cockpit input event, or None when the event has no rule."""
    if name in _SIMPLE:
        return _SIMPLE[name]
    m = re.fullmatch(r"UNKNOWN_(\w+)", name)
    if m and m.group(1) in _WALKAROUND:
        return unknown(P_EXT, "Walkaround", _WALKAROUND[m.group(1)], _WALK)
    m = re.fullmatch(r"BREAKER_(\w+)", name)
    if m and m.group(1) in _BREAKERS:
        return latched(P_CB, "Circuit breakers", _BREAKERS[m.group(1)], note=_CB)
    m = re.fullmatch(r"AS1000_SOFTKEY_(\d+)_(PFD|MFD)", name)
    if m:
        return press(P_PFD if m.group(2) == "PFD" else P_MFD, "Softkeys", "Softkey %s" % m.group(1), note=_G1000)
    m = re.fullmatch(r"AS1000_(\w+?)_(PFD|MFD)", name)
    if m:
        panel = P_PFD if m.group(2) == "PFD" else P_MFD
        key = m.group(1)
        section = "Autopilot" if key.startswith("AUTOPILOT_") else "Radios" if key.startswith(("COM", "NAV")) else "Bezel"
        if key in _G1000_PRESS:
            return press(panel, section, _G1000_PRESS[key], note=_G1000)
        if key in _G1000_KNOBS:
            fn, up, down = _G1000_KNOBS[key]
            return knob(panel, section, fn, up, down, note=_G1000_KNOB)
        return None
    m = re.fullmatch(r"AS1000_(\w+)_MIDCOM", name)
    if m:
        if m.group(1) == "AUDIOPANELVOLUME":
            return knob(P_AUDIO, "Audio panel", "Audio panel volume", note=NOT_TRIED)
        if m.group(1) in _AUDIO:
            return press(P_AUDIO, "Audio panel", _AUDIO[m.group(1)], note=NOT_TRIED)
        return None
    m = re.fullmatch(r"INSTRUMENT_AS1000_CONTROLPAD_(\w+)", name)
    if m:
        k = m.group(1)
        if k in ("FREQ_OUTER", "FREQ_INNER"):
            return knob(P_PAD, "Knobs", "FMS NAV/COM knob, " + ("outer" if k == "FREQ_OUTER" else "inner"), "right", "left",
                        note=_TIP % "FMS NAV/COM")
        if k == "RANGE":
            return knob(P_PAD, "Knobs", "Range knob", "zoom out", "zoom in", note=NOT_TRIED)
        if len(k) == 1:
            return press(P_PAD, "Letters and digits", k, note="Key of the G1000 control pad. " + NOT_TRIED)
        if k in _PAD_NAMES:
            return press(P_PAD, "Keys", _PAD_NAMES[k], note="Key of the G1000 control pad. " + NOT_TRIED)
    return None
