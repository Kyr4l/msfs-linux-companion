# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Rules that label the controls of the Cirrus Vision Jet G2 (Microsoft SF50, MSFS 2024; build_aircraft_catalog.py).

The aircraft has 141 cockpit input events (B:). They are named SF50_<group>_<control>; what each one is comes from
  * the aircraft's English tooltips (Official2024/Steam/microsoft-aircraft-sf50/minimal.fsarchive, en-us.locpak): title and action text of the
    controls (Engine Knob "Toggle engine RUN/STOP mode", Fuel Tank Selector "Select fuel tank", Safe Return "Activate Safe Return" ...)
  * the sim's own checklist (html_ui.fsarchive, airplanes/microsoft_sf50/checklist/checklist.xml): the names the pilot's procedures give the
    switches (BAT 1, GEN 1, Engine Knob, Fuel Control Switch, WING/STAB IPS, BLEED, CAPS Pin ...) and what position each is set to
  * the management tablet (html_ui.fsarchive, airplanes/microsoft_sf50/tablet/tablet.js): the L: variables and the one K: event it sets
  * the public G3000 behaviour file (microsoft/msfs-avionics-mirror, src/workingtitle-instruments-g3000/ModelBehaviorDefs/BehaviorsV1/GTC.xml):
    the knobs and softkeys of the touch controllers (GTC), where the sign of an event's value picks increase or decrease
Both archives are the plain, unencrypted ones of the package; the aircraft's model and system files are not readable offline.
Nothing here was tried through this program on a running aircraft yet, so no entry is marked verified; each note says where the entry
comes from.
"""
from common import NOT_TRIED, knob, latched, press, unknown

AIRCRAFT = "Microsoft_SF50"
TITLE = "Cirrus Vision Jet G2 (Microsoft SF50)"
EVENTS_FILE = "microsoft_sf50_events.json"
AXIS_CONTROLS = True  # the thrust lever, brakes, flaps and trim also appear as the ranged axis events every aircraft has
SOURCES = [
    "Cockpit input events enumerated from MSFS 2024 12.2.282174.999 (SimConnect EnumerateInputEvents), preset sf50_executive",
    "Official2024/Steam/microsoft-aircraft-sf50/minimal.fsarchive, en-us.locpak: the aircraft's English tooltips (unencrypted archive)",
    "Official2024/Steam/microsoft-aircraft-sf50/html_ui/html_ui.fsarchive: airplanes/microsoft_sf50/checklist/checklist.xml (the sim's own checklist) and tablet/tablet.js (the management tablet) (unencrypted archive)",
    "microsoft/msfs-avionics-mirror, src/workingtitle-instruments-g3000/ModelBehaviorDefs/BehaviorsV1/GTC.xml and html_ui/GTC/GtcService/GtcInteractionEvent.ts (the G3000 touch controller)",
]

P_AP = "Autopilot"
P_ENG = "Engine and thrust"
P_FUEL = "Fuel"
P_ELEC = "Electrical"
P_LIGHT = "Lights"
P_ICE = "Ice protection"
P_HAND = "Gear, flaps, brakes and trim"
P_CABIN = "Pressurization and cabin"
P_SAFE = "Safety"
P_GTC = "G3000 touch controllers"
P_EXT = "Walkaround and doors"
P_OTHER = "Other"
PANEL_ORDER = [P_AP, P_ENG, P_FUEL, P_ELEC, P_LIGHT, P_ICE, P_HAND, P_CABIN, P_SAFE, P_GTC, P_EXT, P_OTHER]

VERIFIED = {}

_TIP = "Tooltip in the aircraft: "
_CHK = "Name in the sim's checklist: "

_GTC = ("From the G3000 touch controller (GTC.xml); the aircraft's own event name differs from that file's, so which hardware control it is "
        "is read from the name. Not tried.")
_GTC_KNOB = ("From the G3000 touch controller (GTC.xml): the sign of the value picks the direction (above 0 increases, 0 or less decreases). "
             "The aircraft's own event name differs from that file's. Not tried.")
_INTERCEPT = ("The name says the aircraft intercepts %s; what the event does is not documented. Not tried.")
_LOGIC = "Internal logic of the aircraft, not a cockpit control; no tooltip. Not tried."


def _tsc(n):
    p, s = P_GTC, "GTC %s" % n
    return {
        "AS3000_TSC_%s_FREQUENCY_KNOB_MHZ" % n: knob(p, s, "Frequency knob, outer", "right", "left", note=_GTC_KNOB),
        "AS3000_TSC_%s_FREQUENCY_KNOB_KHZ" % n: knob(p, s, "Frequency knob, inner", "right", "left", note=_GTC_KNOB),
        "AS3000_TSC_%s_FREQUENCY_SWAP" % n: press(p, s, "Frequency swap (inner knob push)", note=_GTC),
        "AS3000_TSC_%s_MULTIFUNCTION_KNOB" % n: knob(p, s, "Lower knob (map)", "right", "left", note=_GTC_KNOB),
        "AS3000_TSC_%s_MULTIFUNCTION_KNOB_BUTTON" % n: press(p, s, "Lower knob push", note=_GTC),
        "AS3000_TSC_%s_SOFTKEY_1" % n: press(p, s, "Softkey PFD", note=_GTC + " The GTC's tooltip names the three softkeys PFD, MFD and NAV/COM."),
        "AS3000_TSC_%s_SOFTKEY_2" % n: press(p, s, "Softkey MFD", note=_GTC + " The GTC's tooltip names the three softkeys PFD, MFD and NAV/COM."),
        "AS3000_TSC_%s_SOFTKEY_3" % n: press(p, s, "Softkey NAV/COM", note=_GTC + " The GTC's tooltip names the three softkeys PFD, MFD and NAV/COM."),
    }


def _walk(function, item):
    return unknown(P_EXT, "Walkaround", function,
                   "A point of the interactive preflight walkaround; the sim's checklist item is \"%s\". Not tried." % item)


_SIMPLE = {
    # --- autopilot ---------------------------------------------------------------------------------------------------------------------
    "AUTOPILOT_AP_1": press(P_AP, "Master", "AP", note="Standard autopilot event name; the CJ4's toggles on every event whatever its value. Not tried."),
    "AUTOPILOT_FD_1_MODE": press(P_AP, "Master", "FD", note="Standard flight director event name. Not tried."),
    "AUTOPILOT_BARO_1": knob(P_AP, "Baro", "Baro knob", "up", "down", note=NOT_TRIED),
    "AUTOPILOT_BARO_1_SYNC": press(P_AP, "Baro", "Baro knob push", note=NOT_TRIED),
    "SF50_AUTOPILOT_HEADING": knob(P_AP, "Heading", "HDG knob", "up", "down", note=NOT_TRIED),
    "SF50_AUTOPILOT_HEADING_SYNC": press(P_AP, "Heading", "HDG sync", note=NOT_TRIED),
    "SF50_AUTOPILOT_HEADING_MODE": press(P_AP, "Lateral", "HDG mode", note=NOT_TRIED),
    "SF50_AUTOPILOT_NAV_MODE": press(P_AP, "Lateral", "NAV mode", note=NOT_TRIED),
    "SF50_AUTOPILOT_APPROACH_MODE": press(P_AP, "Lateral", "APR mode", note=NOT_TRIED),
    "SF50_AUTOPILOT_LEVEL": press(P_AP, "Lateral", "LVL (wings level)", note=NOT_TRIED),
    "SF50_AUTOPILOT_ALTITUDE": knob(P_AP, "Altitude", "ALT knob", "up", "down", note=NOT_TRIED),
    "SF50_AUTOPILOT_ALTITUDE_SYNC": press(P_AP, "Altitude", "ALT sync", note=NOT_TRIED),
    "SF50_AUTOPILOT_ALTITUDE_MODE": press(P_AP, "Altitude", "ALT mode", note=NOT_TRIED),
    "SF50_AUTOPILOT_VERTICALSPEED": knob(P_AP, "Vertical", "VS wheel", "up", "down", note=NOT_TRIED),
    "SF50_AUTOPILOT_VS_MODE": press(P_AP, "Vertical", "VS mode", note=NOT_TRIED),
    "SF50_AUTOPILOT_FLC_MODE": press(P_AP, "Vertical", "FLC mode", note=NOT_TRIED),
    "SF50_AUTOPILOT_VNAV_MODE": press(P_AP, "Vertical", "VNAV mode", note=NOT_TRIED),
    "SF50_AUTOPILOT_SPEED": knob(P_AP, "Speed", "SPD knob", "up", "down", note=NOT_TRIED),
    "SF50_AUTOPILOT_FMS_SPEED_MODE": press(P_AP, "Speed", "Speed reference: FMS", note=_TIP + "\"Set airspeed reference mode to FMS\". Not tried."),
    "SF50_AUTOPILOT_MAN_SPEED_MODE": press(P_AP, "Speed", "Speed reference: manual", note=_TIP + "\"Set airspeed reference mode to manual\". Not tried."),
    "SF50_AUTOPILOT_AUTOTHROTTLE_ARM": press(P_AP, "Autothrottle", "Autothrottle arm", note=NOT_TRIED),
    "SF50_AUTOPILOT_AUTOTHROTTLE_DISCONNECT": press(P_AP, "Autothrottle", "Autothrottle disconnect", note=NOT_TRIED),
    # --- engine and thrust -------------------------------------------------------------------------------------------------------------
    "SF50_ENGINE_RUN_KNOB": latched(P_ENG, "Start", "Engine knob (RUN / OFF)",
                                    note=_TIP + "\"Toggle engine RUN/STOP mode\"; " + _CHK + "Engine Knob, RUN or OFF. Which value is RUN is not confirmed."),
    "SF50_ENGINE_STARTER": press(P_ENG, "Start", "Engine button",
                                 note=_TIP + "\"Start engine\", \"Stop engine\", \"Abort engine start\" and \"Press and hold to dry motor engine\" (one button; the Engine Knob decides). Not tried: whether a hold needs a release event is not known."),
    "SF50_ENGINE_THROTTLE": unknown(P_ENG, "Thrust lever", "Thrust lever (cockpit event)",
                                    "Not tried; the range this event takes is not known. For an axis use the thrust lever of the Control list (THROTTLE1_AXIS_SET_EX1, THROTTLE1_SET)."),
    "SF50_ENGINE_MCT_SNAP": press(P_ENG, "Thrust lever", "Thrust lever to MCT", note=_TIP + "\"MCT Snap: Snaps throttle to MCT position\". Not tried."),
    "SF50_ENGINE_TOGA_PRIMARY": press(P_ENG, "Thrust lever", "TO/GA button (primary)", note="Not tried; the aircraft has a primary and a secondary TO/GA event, which is which is not known."),
    "SF50_ENGINE_TOGA_SECONDARY": press(P_ENG, "Thrust lever", "TO/GA button (secondary)", note="Not tried; the aircraft has a primary and a secondary TO/GA event, which is which is not known."),
    # --- fuel --------------------------------------------------------------------------------------------------------------------------
    "SF50_FUEL_CONTROL_KNOB": latched(P_FUEL, "Tanks", "Fuel control switch (LEFT / AUTO / RIGHT)", positions=3,
                                      note=_TIP + "\"Select fuel tank\"; " + _CHK + "Fuel Control Switch, LEFT, AUTO or RIGHT. Which value is which position is not confirmed."),
    # --- electrical --------------------------------------------------------------------------------------------------------------------
    "SF50_ELECTRICAL_BATTERY_1_SWITCH": latched(P_ELEC, "Batteries", "BAT 1 switch", note=_CHK + "BAT 1 Switch, ON or OFF. Polarity not confirmed."),
    "SF50_ELECTRICAL_BATTERY_2_SWITCH": latched(P_ELEC, "Batteries", "BAT 2 switch", note=_CHK + "BAT 2 Switch, ON or OFF. Polarity not confirmed."),
    "SF50_ELECTRICAL_GEN_1_SWITCH": latched(P_ELEC, "Generators", "GEN 1 switch", note=_CHK + "GEN 1, ON or OFF. Polarity not confirmed."),
    "SF50_ELECTRICAL_GEN_2_SWITCH": latched(P_ELEC, "Generators", "GEN 2 switch", note=_CHK + "GEN 2, ON or OFF. Polarity not confirmed."),
    "SF50_ELECTRICAL_BATTERY_GENERATOR_INTERCEPT": unknown(P_ELEC, "Logic", "Battery / generator (intercept)", _INTERCEPT % "the battery and generator switches"),
    # --- lights ------------------------------------------------------------------------------------------------------------------------
    "LIGHTING_STROBE_1": latched(P_LIGHT, "Exterior", "Strobe lights", note=_CHK + "Strobe Lights, ON. Standard event name; the CJ4's takes set 1 / set 0. Not tried."),
    "LIGHTING_WING_1": latched(P_LIGHT, "Exterior", "Wing light (ICE light)",
                               note="Standard event name. The checklist's ICE Light is the aircraft's wing inspection light and the only wing light in the list, so this is most likely it. Not tried."),
    "SF50_LIGHTING_LANDING_LIGHTS": latched(P_LIGHT, "Exterior", "Landing lights", note=_CHK + "Landing Lights, A/R. How many positions the switch has is not known. Not tried."),
    "SF50_LIGHTING_NAV_LIGHTS_INTERCEPT": unknown(P_LIGHT, "Exterior", "Navigation lights (intercept)", _INTERCEPT % "the navigation lights"),
    "SF50_LIGHTING_RECOGNITION_LIGHTS_INTERCEPT": unknown(P_LIGHT, "Exterior", "Recognition lights (intercept)", _INTERCEPT % "the recognition lights"),
    "SF50_LIGHTING_INSTRUMENT_LIGHTS": unknown(P_LIGHT, "Interior", "Instrument lights", _TIP + "\"Panel Lighting: Set the brightness of the panel lighting\". Not tried: a dimmer, its range is not known."),
    "SF50_LIGHTING_CONVENIENCE_SWITCH": latched(P_LIGHT, "Interior", "Convenience lights", note=_TIP + "\"Convenience Lights\". Not tried."),
    "SF50_LIGHTING_PILOT_TASK_LIGHT": unknown(P_LIGHT, "Interior", "Pilot task light", "Not tried; a switch or a dimmer, which is not known."),
    "SF50_LIGHTING_COPILOT_TASK_LIGHT": unknown(P_LIGHT, "Interior", "Copilot task light", "Not tried; a switch or a dimmer, which is not known."),
    "SF50_LIGHTING_CABIN_TASK_LIGHT_FORWARD_L": unknown(P_LIGHT, "Cabin", "Cabin light, forward left", _TIP + "\"Cabin Lighting\". Not tried; a switch or a dimmer, which is not known."),
    "SF50_LIGHTING_CABIN_TASK_LIGHT_FORWARD_C": unknown(P_LIGHT, "Cabin", "Cabin light, forward centre", _TIP + "\"Cabin Lighting\". Not tried; a switch or a dimmer, which is not known."),
    "SF50_LIGHTING_CABIN_TASK_LIGHT_FORWARD_R": unknown(P_LIGHT, "Cabin", "Cabin light, forward right", _TIP + "\"Cabin Lighting\". Not tried; a switch or a dimmer, which is not known."),
    "SF50_LIGHTING_CABIN_TASK_LIGHT_AFT_L": unknown(P_LIGHT, "Cabin", "Cabin light, aft left", _TIP + "\"Cabin Lighting\". Not tried; a switch or a dimmer, which is not known."),
    "SF50_LIGHTING_CABIN_TASK_LIGHT_AFT_R": unknown(P_LIGHT, "Cabin", "Cabin light, aft right", _TIP + "\"Cabin Lighting\". Not tried; a switch or a dimmer, which is not known."),
    # --- ice protection ----------------------------------------------------------------------------------------------------------------
    "DEICE_PITOT_1": latched(P_ICE, "Probe heat", "Probe heat switch", note=_CHK + "PROBE HEAT Switch, ON or OFF. Standard event name; the CJ4's takes set 1 / set 0. Not tried."),
    "SF50_DEICE_ENGINE": latched(P_ICE, "IPS", "Engine IPS switch", note=_TIP + "\"Toggle engine ice protection\"; " + _CHK + "ENGINE IPS Switch. Not tried."),
    "SF50_DEICE_ENGINE_DEICE_LOGIC": unknown(P_ICE, "IPS", "Engine IPS (logic)", _LOGIC),
    "SF50_DEICE_WINGSTAB": latched(P_ICE, "IPS", "Wing / stabilizer IPS switch", note=_TIP + "\"Toggle wing/stabilizer ice protection\"; " + _CHK + "WING/STAB IPS Switch. Not tried."),
    "SF50_DEICE_WINGSTAB_DEICE_LOGIC": unknown(P_ICE, "IPS", "Wing / stabilizer IPS (logic)", _LOGIC),
    "SF50_DEICE_WINDSHIELD": latched(P_ICE, "IPS", "Windshield IPS switch", note=_TIP + "\"Toggle windshield ice protection\"; " + _CHK + "WINDSHLD IPS Switch. Not tried."),
    "SF50_DEICE_WINDSHIELD_HIGH": latched(P_ICE, "IPS", "Windshield IPS, high", note=_TIP + "\"Toggle windshield ice protection high mode\". The checklist's MAX switch (press and hold until fluid exits the spray bar) is not in the event list as such. Not tried."),
    # --- gear, flaps, brakes and trim --------------------------------------------------------------------------------------------------
    "SF50_HANDLING_LANDING_GEAR_LEVER": latched(P_HAND, "Landing gear", "Landing gear lever", note=_CHK + "Landing Gear, UP or DOWN. Which value is which position is not confirmed."),
    "SF50_HANDLING_FLAP_SELECTOR": latched(P_HAND, "Flaps", "Flap selector (UP / 50% / 100%)", positions=3, note=_CHK + "Flaps, UP, 50% or 100%. Which value is which position is not confirmed."),
    "SF50_HANDLING_PARKING_BRAKE": latched(P_HAND, "Brakes", "Parking brake", note=_CHK + "Parking Brake, set or release. Not tried."),
    "HANDLING_ELEVATORTRIM_1": unknown(P_HAND, "Trim", "Pitch trim", "Standard event name. On the CJ4 the trims want a value of 2 or more (up) or 0 or less (down); not tried here."),
    "HANDLING_RUDDERTRIM_1": unknown(P_HAND, "Trim", "Rudder trim", "Standard event name; the checklist calls the trim switch \"Electric Roll Trim\", so which trim this moves is not known. Not tried."),
    "HANDLING_TRIM_DISCONNECT_1": press(P_HAND, "Trim", "Trim disconnect 1", note="Standard event name. Not tried."),
    "HANDLING_TRIM_DISCONNECT_2": press(P_HAND, "Trim", "Trim disconnect 2", note="Standard event name. Not tried."),
    "HANDLING_YOKE_HIDER_1": latched(P_HAND, "Side stick", "Hide pilot side stick", note="Cosmetic: hides the stick. Not tried."),
    "HANDLING_YOKE_HIDER_2": latched(P_HAND, "Side stick", "Hide copilot side stick", note="Cosmetic: hides the stick. Not tried."),
    "ESP_PITCH_CONTROL_AXIS": unknown(P_HAND, "Stability", "ESP pitch control axis", "An axis of the electronic stability and protection system; its range is not known and it was not tried. Do not bind the pitch axis here: use the elevator axis of the Control list."),
    "ESP_ROLL_CONTROL_AXIS": unknown(P_HAND, "Stability", "ESP roll control axis", "An axis of the electronic stability and protection system; its range is not known and it was not tried. Do not bind the roll axis here: use the aileron axis of the Control list."),
    # --- pressurization and cabin ------------------------------------------------------------------------------------------------------
    "SF50_PRESSURIZATION_SWITCH_AIR_BLEED": latched(P_CABIN, "Air supply", "BLEED / FRESH switch",
                                                    note=_TIP + "\"Toggle air supply source\" (Bleed Air, Fresh Air); " + _CHK + "BLEED Switch, BLEED or FRESH. Which value is which is not confirmed."),
    "SF50_PRESSURIZATION_OXYGEN_MASTER": latched(P_CABIN, "Oxygen", "OXYGEN MASTER switch", note=_TIP + "\"Toggle oxygen availability\"; " + _CHK + "OXYGEN MASTER Switch, ON or OFF. Polarity not confirmed."),
    "SF50_CABIN_ECS_TEMP_BACKUP": latched(P_CABIN, "ECS", "Temperature backup", note=_TIP + "\"Toggle temperature backup mode\"; the checklist's TEMP BACKUP. Not tried."),
    "SF50_CABIN_ECS_DISABLE": latched(P_CABIN, "ECS", "ECS disable", note=_TIP + "\"Toggle ECS disable\". Not tried."),
    "SF50_CABIN_ECS_DEFOG": latched(P_CABIN, "ECS", "Windshield defog", note=_TIP + "\"Toggle windshield defog\". Not tried."),
    "SF50_CABIN_ECS_AFT_CONTROL": latched(P_CABIN, "ECS", "Aft temperature control", note=_TIP + "\"Toggle aft temperature control\". Not tried."),
    "SF50_CABIN_ECS_TEMP_KNOB_FORWARD": unknown(P_CABIN, "ECS", "Cabin temperature knob, forward", _TIP + "\"Adjust set temperature\" (\"Adjust temperature control valve position\" in backup mode). Not tried; its range is not known."),
    "SF50_CABIN_ECS_TEMP_KNOB_AFT": unknown(P_CABIN, "ECS", "Cabin temperature knob, aft", _TIP + "\"Adjust set temperature\". Not tried; its range is not known."),
    "SF50_CABIN_ECS_FAN_KNOB_FORWARD": unknown(P_CABIN, "ECS", "Fan knob, forward", _TIP + "\"Adjust fan speed\". Not tried; its range is not known."),
    "SF50_CABIN_ECS_FAN_KNOB_AFT": unknown(P_CABIN, "ECS", "Fan knob, aft", _TIP + "\"Adjust fan speed\". Not tried; its range is not known."),
    # --- safety ------------------------------------------------------------------------------------------------------------------------
    "SF50_CAPS_HANDLE": unknown(P_SAFE, "CAPS", "CAPS handle (parachute)",
                                "DANGER: " + _TIP + "\"Pull handle down to activate parachute\". It fires the airframe parachute; do not bind it to a button you can press by accident. Never tried."),
    "SF50_CAPS_SAFETY_PIN": latched(P_SAFE, "CAPS", "CAPS pin (remove / replace)", note=_TIP + "\"Remove/replace safety pin\"; " + _CHK + "CAPS Pin, REMOVE AND STOW before takeoff, REPLACE at shutdown. Not tried."),
    "SF50_SAFETY_ELT_REMOTE_SWITCH": unknown(P_SAFE, "ELT", "ELT remote switch", _TIP + "\"Set emergency locator transmitter mode\"; " + _CHK + "ELT, ARMED. Not tried: how many positions it has is not known."),
    "SF50_SAFE_RETURN_ACTIVATE_BUTTON": press(P_SAFE, "Safe Return", "Safe Return button (emergency autoland)",
                                              note="WARNING: " + _TIP + "\"Activate Safe Return\" starts the emergency autoland and flies the aircraft to a runway. Never tried."),
    "SF50_SAFE_RETURN_AUTOPILOT_INTERCEPTS": unknown(P_SAFE, "Safe Return", "Safe Return (autopilot intercept)", _INTERCEPT % "the autopilot while Safe Return runs"),
    # --- checklist wheel ---------------------------------------------------------------------------------------------------------------
    "AS3000_CHECKLIST_WHEEL": knob(P_GTC, "Checklist wheel", "Checklist wheel", "up", "down", note=_TIP + "\"Checklist Thumbwheel: Scroll selection up/down\". Which sign scrolls which way is not known. Not tried."),
    "AS3000_CHECKLIST_WHEEL_PUSH": press(P_GTC, "Checklist wheel", "Checklist wheel push", note=_TIP + "\"Toggle item completed\". Not tried."),
    # --- walkaround and doors ----------------------------------------------------------------------------------------------------------
    "SF50_EXT_CABIN_DOOR": press(P_EXT, "Doors", "Cabin door, outside", note=_TIP + "\"Open/Close Door\". Opening it from the cockpit can take you out of the aircraft. Not tried."),
    "SF50_EXT_PILOT_SIT": press(P_EXT, "Doors", "Enter the cockpit", note=_TIP + "\"Cabin Door Entrance: Enter cockpit and sit down\". Not tried."),
    "SF50_INT_CABIN_DOOR": press(P_EXT, "Doors", "Cabin door, inside", note=_TIP + "\"Open/Close Door\". Not tried."),
    "SF50_INT_PILOT_EXIT": press(P_EXT, "Doors", "Leave the cockpit", note=_TIP + "\"Cabin Door Exit: Exit aircraft\". It takes the pilot out of the aircraft. Not tried."),
    "SF50_INT_EMERGENCY_EXIT_SAFETY_PIN": latched(P_EXT, "Doors", "Egress door pin", note=_TIP + "\"Emergency Exit Safety Pin\"; " + _CHK + "Egress Door Pin, REPLACE at shutdown. Not tried."),
    "SF50_EXT_BAGGAGE_DOOR": press(P_EXT, "Doors", "Baggage door", note="Checklist: Baggage Door, SECURE. Not tried."),
    "SF50_EXT_EXTERNAL_POWER_SERVICE_DOOR": press(P_EXT, "Doors", "External power door", note=_TIP + "\"External Power Service Door\". Not tried."),
    "SF50_EXT_OIL_CHECK_PANEL_DOOR": press(P_EXT, "Doors", "Oil check door", note=_TIP + "\"Oil Check Panel Door\". Not tried."),
    "SF50_EXT_FUEL_CAP_LEFT": press(P_EXT, "Covers and caps", "Fuel cap, left", note=_TIP + "\"Open/close fuel cap\". Not tried."),
    "SF50_EXT_FUEL_CAP_RIGHT": press(P_EXT, "Covers and caps", "Fuel cap, right", note=_TIP + "\"Open/close fuel cap\". Not tried."),
    "SF50_EXT_ENGINE_COVERS": press(P_EXT, "Covers and caps", "Engine covers", note=_TIP + "\"Remove/replace cover\". Not tried."),
    "SF50_EXT_PITOT_COVERS": press(P_EXT, "Covers and caps", "Pitot covers", note=_TIP + "\"Remove/replace cover\". Not tried."),
    "SF50_EXT_WHEEL_CHOCKS": press(P_EXT, "Covers and caps", "Wheel chocks", note=_TIP + "\"Remove/replace chocks\". Not tried."),
    "SF50_EXT_LEFT_AILERON_CHECK": _walk("Check left aileron", "LH Aileron, Roll Trim Tab: CONDITION, MOTION"),
    "SF50_EXT_RIGHT_AILERON_CHECK": _walk("Check right aileron", "RH Aileron, Roll Servo Tab: CONDITION, MOVEMENT"),
    "SF50_EXT_LEFT_FLAP_CHECK": _walk("Check left flap", "LH Flap: CONDITION"),
    "SF50_EXT_RIGHT_FLAP_CHECK": _walk("Check right flap", "RH Flap: CONDITION"),
    "SF50_EXT_LEFT_MAIN_GEAR_BRAKES_CHECK": _walk("Check left brakes", "LH MLG Brakes, Wheel and Tire: CONDITION"),
    "SF50_EXT_LEFT_MAIN_GEAR_STRUT_CHECK": _walk("Check left main gear strut", "LH MLG: CONDITION"),
    "SF50_EXT_LEFT_MAIN_GEAR_TIRE_CHECK": _walk("Check left main gear tire", "LH MLG Brakes, Wheel and Tire: CONDITION"),
    "SF50_EXT_RIGHT_MAIN_GEAR_BRAKES_CHECK": _walk("Check right brakes", "RH MLG Brakes, Wheel and Tire: CONDITION"),
    "SF50_EXT_RIGHT_MAIN_GEAR_STRUT_CHECK": _walk("Check right main gear strut", "RH MLG: CONDITION"),
    "SF50_EXT_RIGHT_MAIN_GEAR_TIRE_CHECK": _walk("Check right main gear tire", "RH MLG Brakes, Wheel and Tire: CONDITION"),
    "SF50_EXT_NOSE_GEAR_STRUT_CHECK": _walk("Check nose gear strut", "NLG: CONDITION"),
    "SF50_EXT_NOSE_GEAR_TIRE_CHECK": _walk("Check nose gear tire", "NLG Wheel, Tire: CONDITION"),
    "SF50_EXT_RUDDERVATOR_CHECK": _walk("Check ruddervators", "Ruddervators, Pitch Servo Tabs: CONDITION, MOTION"),
    "SF50_ACCESS_CABIN_DOOR_AUTO_CLOSE_LOGIC": unknown(P_EXT, "Logic", "Cabin door auto close (logic)", _LOGIC),
    "SF50_ACCESS_CABIN_DOOR_OPEN_LOGIC": unknown(P_EXT, "Logic", "Cabin door open (logic)", _LOGIC),
    "SF50_ACCESS_CABIN_DOOR_SOUND_LOGIC": unknown(P_EXT, "Logic", "Cabin door sound (logic)", _LOGIC),
    # --- other -------------------------------------------------------------------------------------------------------------------------
    "PROCEDURE_AUTOSTART": press(P_OTHER, "Procedures", "Autostart", note="The sim's own engine autostart (MSFS 2024). Not tried; do not press it in flight."),
    "UNKNOWN_TABLET_BACKDROP": unknown(P_OTHER, "Management tablet", "Tablet backdrop", "Part of the management tablet's model, not a cockpit control. Not tried."),
    "UNKNOWN_BUTTONS_COLLISION_EFB_VISIBILITY_HIDE": unknown(P_OTHER, "Management tablet", "Tablet buttons (hide)", "Part of the management tablet's model, not a cockpit control. Not tried."),
    "UNKNOWN_MOUNT_ARM_EFB_VISIBILITY_HIDE": unknown(P_OTHER, "Management tablet", "Tablet arm (hide)", "Part of the management tablet's model, not a cockpit control. Not tried."),
    "UNKNOWN_MOUNT_SCREW_EFB_VISIBILITY_HIDE": unknown(P_OTHER, "Management tablet", "Tablet mount screw (hide)", "Part of the management tablet's model, not a cockpit control. Not tried."),
}
for _n in (1, 2, 3):
    _SIMPLE.update(_tsc(_n))


def classify(name):
    """Rows (one per catalog entry) for a cockpit input event, or None when the event has no rule."""
    return _SIMPLE.get(name)


def _sim(panel, section, function, api, behavior, note, **kw):
    e = {"panel": panel, "section": section, "function": function, "api": api, "name": api, "kind": "sim_event",
         "type": "K: event", "rw": "", "behavior": behavior, "note": note}
    e.update(kw)
    return e


def _lvar(panel, section, function, api, note, **kw):
    e = {"panel": panel, "section": section, "function": function, "api": api, "name": api, "kind": "lvar",
         "type": "LVAR", "rw": "R/W", "behavior": "", "note": note}
    e.update(kw)
    return e


_TAB = "From the management tablet (tablet.js): "


def _extra():
    out = [
        _sim(P_EXT, "Management tablet", "External power: toggle", "TOGGLE_EXTERNAL_POWER", "press",
             _TAB + "its GPU button sends (>K:TOGGLE_EXTERNAL_POWER) and reads (A:EXTERNAL POWER ON). Not tried through this program.", value=1),
        _lvar(P_EXT, "Management tablet", "Wheel chocks (tablet)", "SF50_Chocks_Enabled", _TAB + "the chocks button flips this L: variable. Not tried.", values="0|1"),
        _lvar(P_EXT, "Management tablet", "Storage cover (tablet)", "SF50_Storage_Cover_Enabled", _TAB + "the storage cover button flips this L: variable. Not tried.", values="0|1"),
        _lvar(P_EXT, "Management tablet", "Covers (tablet)", "SF50_Covers_Enabled", _TAB + "the covers button flips this L: variable. Not tried.", values="0|1"),
        _lvar(P_EXT, "Management tablet", "Luggage door (tablet)", "SF50_Luggage_Door_Open", _TAB + "the luggage door button flips this L: variable. Not tried.", values="0|1"),
        _lvar(P_EXT, "Management tablet", "Registration shadow (tablet)", "SF50_Registration_Shadow_Enabled", _TAB + "the registration shadow button flips this L: variable (cosmetic). Not tried.", values="0|1"),
        _lvar(P_CABIN, "Management tablet", "Noise cancelling headset (tablet)", "SF50_ANR_On", _TAB + "the ANR button flips this L: variable. Not tried.", values="0|1"),
        _lvar(P_EXT, "Management tablet", "Tablet open", "SF50_Tablet_Open", _TAB + "the tablet updates its buttons only while this L: variable is not 0. Not tried.", values="0|1"),
    ]
    for label, v, what in (("none", 0, "no luggage"), ("family", 1, "family luggage"), ("golf", 2, "golf bags"), ("snowboard", 3, "a snowboard")):
        out.append(_lvar(P_EXT, "Management tablet", "Luggage: " + label, "SF50_Management_Luggage_Type",
                         _TAB + "the luggage page sets this L: variable to %d for %s (the buttons are none, family, golf, snowboard in that order). Not tried." % (v, what),
                         values=str(v), value=v))
    for label, v in (("cold and dark", 1), ("taxi", 2), ("takeoff", 3)):
        out.append(_lvar(P_EXT, "Management tablet", "Aircraft state: " + label, "SF50_Management_Status_Type",
                         _TAB + "the status page sets this L: variable to %d for \"%s\". What the aircraft does with it (a state change of the whole cockpit?) is not known. Not tried." % (v, label),
                         values=str(v), value=v))
    for label, v in (("executive", 0), ("complete", 1), ("family", 2)):
        out.append(_lvar(P_EXT, "Management tablet", "Seating: " + label, "SF50_Management_Config_Type",
                         _TAB + "the configuration page sets this L: variable to %d for %s seating (the buttons are executive, complete, family in that order). Not tried." % (v, label),
                         values=str(v), value=v))
    return out


EXTRA_ENTRIES = _extra()
