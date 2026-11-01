# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Rules that label the controls of the Guimbal Cabri G2 (Asobo, MSFS 2024; build_aircraft_catalog.py).

The helicopter has 97 cockpit input events (B:). What each one is comes from files of the install that are readable offline:
  * the aircraft's English tooltips and checklist clues (MSFS2024/minimalcache/fs24-asobo-aircraft-cabri-g2/minimal.fsarchive, en-us.locpak):
    control titles ("Master", "Plasma ignition", "Cold air control" ...) and the sim's own checklist wording (CHECKLIST.CLUE.*, GAME.CHECKLIST_*)
  * the Asobo behaviour files of the shared package Packages/fs-base-aircraft-common/ModelBehaviorDefs/Asobo: AircraftTypes/Rotorcrafts.xml (collective,
    governor switch, clutch, rotor brake), Common/Inputs/Engine_Inputs.xml (what the engine switches send: PLASMA_SET, ROTOR_CLUTCH_SWITCH_SET ...),
    Transponder/AS21.xml, GlassCockpit/AS650.xml and Misc/AS_EPM.xml (radio, transponder, engine monitor)
  * the engine monitor's script (Packages/asobo-vcockpits-instruments-helicopters, AS_EPM.js): the simulation variables the aircraft's own gauges read
  * the sim's helicopter control profiles (Packages/asobo-input-profiles-pc/InputProfiles/Categories/Helicopter): the K: events it binds
The aircraft's model and system files are encrypted and not read. Nothing here was tried through this program on a running helicopter yet, so no
entry is marked verified; each note says where the entry comes from.
"""
from common import NOT_TRIED, knob, latched, press, unknown

AIRCRAFT = "Asobo_Cabri_G2"
TITLE = "Guimbal Cabri G2 (Asobo)"
EVENTS_FILE = "asobo_cabri_g2_events.json"
AXIS_CONTROLS = True  # collective, cyclic, pedals and the twist grip also appear as the ranged axis events every aircraft has
SOURCES = [
    "Cockpit input events enumerated from MSFS 2024 12.2.282174.999 (SimConnect EnumerateInputEvents), preset cabri_g2_flightseeing",
    "MSFS2024/minimalcache/fs24-asobo-aircraft-cabri-g2/minimal.fsarchive, en-us.locpak: the aircraft's English tooltips and checklist clues (unencrypted archive)",
    "MSFS2024/Packages/fs-base-aircraft-common/ModelBehaviorDefs/Asobo: AircraftTypes/Rotorcrafts.xml, Common/Inputs/Engine_Inputs.xml, Transponder/AS21.xml, GlassCockpit/AS650.xml, Misc/AS_EPM.xml",
    "MSFS2024/Packages/asobo-vcockpits-instruments-helicopters/html_ui/Pages/VCockpit/Instruments/Helicopters/AS_EPM/dist/AS_EPM.js: the simulation variables the engine monitor reads",
    "MSFS2024/Packages/asobo-input-profiles-pc/InputProfiles/Categories/Helicopter: the K: events the sim binds for helicopters",
]

P_ENG = "Engine and rotor"
P_ELEC = "Electrical"
P_BRK = "Circuit breakers"
P_FUEL = "Fuel and heat"
P_LIGHT = "Lights"
P_HAND = "Flight controls"
P_RADIO = "Radio and transponder"
P_INSTR = "Instruments"
P_SAFE = "Safety"
P_EXT = "Walkaround"
P_OTHER = "Other"
PANEL_ORDER = [P_ENG, P_ELEC, P_FUEL, P_HAND, P_LIGHT, P_RADIO, P_INSTR, P_BRK, P_SAFE, P_EXT, P_OTHER]

VERIFIED = {}

_TIP = "Tooltip in the aircraft: "
_CLUE = "The sim's checklist says: "
_WALKI = "Walkaround item of the sim's checklist: "
_ENGINE_T = "Asobo engine template (Engine_Inputs.xml): "
_ROTOR_T = "Asobo rotorcraft template (Rotorcrafts.xml): "
_POL = " Which value is ON is not confirmed."

_SIMPLE = {}


def _breaker(event, name, note=""):
    _SIMPLE[event] = latched(P_BRK, "Breakers", name + " breaker",
                             note="Circuit breaker; the aircraft's input list has ON, OFF and TOGGLE for it. %sNot tried.%s" % (note, _POL))


for _ev, _nm, _nt in (
        ("BREAKER_BREAKER_AC", "AC", ""),
        ("BREAKER_BREAKER_ADI_MAPLIGHT", "ADI / map light", ""),
        ("BREAKER_BREAKER_ALTERNATOR", "Alternator", ""),
        ("BREAKER_BREAKER_AVIONICS_2", "Avionics 2", ""),
        ("BREAKER_BREAKER_AVIONICS_STARTER", "Avionics starter", "The input list names the starter breaker \"STARTER\". "),
        ("BREAKER_BREAKER_BARC", "BARC (barometric altimeter correction)", ""),
        ("BREAKER_BREAKER_EPM", "EPM (engine parameter monitor)", ""),
        ("BREAKER_BREAKER_GOVERNOR", "Governor", ""),
        ("BREAKER_BREAKER_LANDING_LIGHT", "Landing light", ""),
        ("BREAKER_BREAKER_NAV_LIGHT", "Navigation light", ""),
        ("BREAKER_BREAKER_PFD", "PFD", ""),
        ("BREAKER_BREAKER_PLASMA", "Plasma ignition", ""),
        ("BREAKER_BREAKER_PUMP_CLUTCH", "Pump clutch", ""),
        ("BREAKER_BREAKER_STROBE", "Strobe light", ""),
        ("BREAKER_BREAKER_WARNING_LIGHTS", "Warning lights", "")):
    _breaker(_ev, _nm, _nt)


def _walk(function, note):
    return press(P_EXT, "Walkaround", function, note=note + " Not tried.")


_SIMPLE.update({
    # --- engine and rotor ------------------------------------------------------------------------------------------------------------------
    "ENGINE_MASTER": latched(P_ENG, "Start", "Master switch",
                             note=_TIP + "\"Master\". Which circuit it switches is not documented." + _POL + " Not tried."),
    "ENGINE_MAGNETO": latched(P_ENG, "Start", "Magneto switch",
                              note=_CLUE + "\"Switch plasma and magneto switches off\" at shutdown. " + _ENGINE_T + "a magneto of type ON/OFF sends MAGNETO1_SET 0 or 3." + _POL + " Not tried."),
    "ENGINE_PLASMA": latched(P_ENG, "Start", "Plasma ignition switch",
                             note=_CLUE + "\"Switch plasma ignition on and check beeper\". " + _ENGINE_T + "sends PLASMA_SET 0 or 1 and reads (A:PLASMA ON:1, Bool)." + _POL + " Not tried."),
    "ENGINE_STARTER": press(P_ENG, "Start", "Starter button",
                            note=_CLUE + "\"Press starter button, STARTER light must light on, then back off\". " + _ENGINE_T + "a starter sends TOGGLE_STARTER and reads (A:GENERAL ENG STARTER:1, Bool). Not tried: whether it needs the button held is not known."),
    "ENGINE_GOVERNOR": latched(P_ENG, "Start", "Governor switch (GOV)",
                               note=_TIP + "\"Toggle governor switch\". " + _ROTOR_T + "ON sends ROTOR_GOV_SWITCH_ON, OFF sends ROTOR_GOV_SWITCH_OFF, state (A:ROTOR GOV SWITCH POS, Bool); the checklist's GOV OFF light is lit when it is off." + _POL + " Not tried."),
    "ENGINE_THROTTLE": unknown(P_ENG, "Throttle", "Throttle twist grip (cockpit event)",
                               _CLUE + "\"Turn throttle fully right\", \"Set throttle lever to get NR around 530 RPM\". " + _ENGINE_T + "the throttle set takes the position on a 16k scale (THROTTLE1_SET, (A:GENERAL ENG THROTTLE LEVER POSITION:1)). Not tried; the range this event takes is not known. For an axis use the Helicopter throttle of the Control list."),
    "ENGINE_MIXTURE": unknown(P_ENG, "Throttle", "Mixture (cockpit event)",
                              "The sim's checklist for this aircraft never names a mixture control. Not tried; the range is not known."),
    "ROTOR_CLUTCH": latched(P_ENG, "Rotor", "Clutch switch",
                            note=_CLUE + "\"Engage clutch and lock the switch, CLUTCH light must light on\"; \"Switch clutch off\" at shutdown. " + _ROTOR_T + "sends ROTOR_CLUTCH_SWITCH_SET, state (A:ROTOR CLUTCH SWITCH POS, Bool)." + _POL + " Not tried."),
    "COMMON_ROTOR_CLUTCH_COVER": latched(P_ENG, "Rotor", "Clutch switch cover",
                                         note=_ROTOR_T + "the cover over the clutch switch; its state is the L: variable XMLVAR_ROTORCRAFT_Clutch_Cover_Opened, and the switch only works while the cover is open." + _POL + " Not tried."),
    "ROTOR_BRAKE": unknown(P_ENG, "Rotor", "Rotor brake lever",
                           _CLUE + "\"Pull rotor brake lever fully backward, check that indicator is On, then lock rotor brake lever fully forward\"; the rotor can be braked below 150 RPM. " + _ROTOR_T + "the lever sets ROTOR_BRAKE_SET and reads (A:ROTOR BRAKE HANDLE POS, Percent). Not tried; the range is not known."),
    "ROTOR_COLLECTIVE": unknown(P_HAND, "Collective", "Collective lever (cockpit event)",
                                _ROTOR_T + "the lever sets (A:COLLECTIVE HANDLE POSITION, Percent over 100). Not tried; the range this event takes is not known. For an axis use the Collective of the Control list (AXIS_COLLECTIVE_SET)."),
    "DEICE_CARB_HEAT": latched(P_FUEL, "Carburetor", "Carburetor heat switch (COLD / AUTO / HOT)", positions=3,
                               note=_CLUE + "\"Set carburetor heat switch on COLD\" and \"on HOT\" (the NR drops when it is set to HOT). The input list has COLD, AUTO and HOT. Which value is which is not confirmed. Not tried."),
    "PROCEDURE_AUTO_START_SHUTDOWN": press(P_ENG, "Start", "Autostart / shutdown (sim procedure)",
                                           note="The sim's own engine autostart and shutdown (MSFS 2024). Not tried; do not press it in flight."),
    # --- electrical ------------------------------------------------------------------------------------------------------------------------
    "ELECTRICAL_ALTERNATOR": latched(P_ELEC, "Alternator", "Alternator switch (ALT)",
                                     note=_TIP + "\"Alternator\". " + _CLUE + "\"ALT light must be Off\"." + _POL + " Not tried."),
    # --- fuel ------------------------------------------------------------------------------------------------------------------------------
    "FUEL_PUMP": latched(P_FUEL, "Fuel", "Fuel pump switch", note="Fuel pump; the aircraft's input list has ON, OFF and TOGGLE." + _POL + " Not tried."),
    "FUEL_VALVE": latched(P_FUEL, "Fuel", "Fuel valve (OPEN / CLOSED)", note="Fuel valve; the aircraft's input list has OPEN, CLOSED and TOGGLE. Which value is OPEN is not confirmed. Not tried."),
    "PNEUMATICS_COLD_AIR_CONTROL": unknown(P_FUEL, "Cabin air", "Cold air control", _TIP + "\"Cold air control\". Not tried; the range is not known."),
    "PNEUMATICS_WARM_AIR_CONTROL": unknown(P_FUEL, "Cabin air", "Warm air control", _TIP + "\"Warm air control\". Not tried; the range is not known."),
    # --- cyclic trim -----------------------------------------------------------------------------------------------------------------------
    "HANDLING_TRIM_PILOT": unknown(P_HAND, "Cyclic trim", "Cyclic trim, pilot", _TIP + "\"Trim Lateral/Longitudinal\" (the dual-axis electric trim on top of the cyclic). Not tried; use the two trim events below."),
    "HANDLING_TRIM_COPILOT": unknown(P_HAND, "Cyclic trim", "Cyclic trim, copilot", _TIP + "\"Trim Lateral/Longitudinal\". Not tried; use the two trim events below."),
    "PROCEDURE_LATERAL_TRIM": knob(P_HAND, "Cyclic trim", "Trim left / right", "right", "left",
                                   note="The trim switch's lateral axis; the aircraft's input list has INCREMENT, DECREMENT, SET and NEUTRAL. Which sign moves it which way is not known. Not tried."),
    "PROCEDURE_LONGITUDINAL_TRIM": knob(P_HAND, "Cyclic trim", "Trim forward / aft", "forward", "aft",
                                        note="The trim switch's longitudinal axis; the aircraft's input list has INCREMENT, DECREMENT, SET and NEUTRAL. Which sign moves it which way is not known. Not tried."),
    # --- lights ----------------------------------------------------------------------------------------------------------------------------
    "LIGHTING_LANDING_LIGHT": latched(P_LIGHT, "Exterior", "Landing light", note=_CLUE + "\"Switch landing and navigation lights off\" at shutdown." + _POL + " Not tried."),
    "LIGHTING_NAVIGATION_LIGHT": latched(P_LIGHT, "Exterior", "Navigation light", note=_CLUE + "\"Switch landing and navigation lights off\" at shutdown." + _POL + " Not tried."),
    "LIGHTING_STROBE_LIGHT": latched(P_LIGHT, "Exterior", "Strobe light", note="Strobe light; the aircraft's input list has ON, OFF and TOGGLE." + _POL + " Not tried."),
    "LIGHTING_MAP_LIGHT": latched(P_LIGHT, "Interior", "Map / backup light (OFF / LEFT / RIGHT)", positions=3,
                                  note=_TIP + "\"Map/Backup light\". The input list has OFF, LEFT and RIGHT. Which value is which is not confirmed. Not tried."),
    # --- radio, intercom and transponder ---------------------------------------------------------------------------------------------------
    "AS_650_FREQ": press(P_RADIO, "AS 650", "Frequency knob push (COM / NAV)",
                         note=_TIP + "\"Toggle between COM and NAV\". AS650.xml: a short push flips the knob between COM and NAV (L:AS650_NAV_Freq_Modify), a long push swaps the standby and active frequency. Not tried: a long push needs a held button."),
    "AS_650_FREQ_MHZ": knob(P_RADIO, "AS 650", "Frequency, MHz", "right", "left",
                            note="AS650.xml: the sign of the value picks the direction (above 0 = WHOLE_INC, 0 or less = WHOLE_DEC) of the COM or NAV radio, whichever the knob is set to. Not tried."),
    "AS_650_FREQ_KHZ": knob(P_RADIO, "AS 650", "Frequency, kHz", "right", "left",
                            note="AS650.xml: the sign of the value picks the direction (above 0 = FRACT_INC, 0 or less = FRACT_DEC) of the COM or NAV radio, whichever the knob is set to. Not tried."),
    "AS_650_FUNC": press(P_RADIO, "AS 650", "Direct to button", note=_TIP + "\"Direct to\". AS650.xml sends H:AS650_BTN_Direct_To_Press. Not tried."),
    "AS_650_HOME": press(P_RADIO, "AS 650", "Home button", note=_TIP + "\"Home\". AS650.xml sends H:AS650_BTN_Home_Press (a long press, H:AS650_BTN_MODE_LongPress). Not tried."),
    "AS_650_VOLUME": knob(P_RADIO, "AS 650", "Volume", "up", "down", note=_TIP + "\"Volume\". Not tried."),
    "AS_21_MODE": latched(P_RADIO, "Transponder (AS 21)", "Mode knob (OFF / STBY / GRND / ON / ALT)", positions=5,
                          note="AS21.xml: the mode knob is of type OFF_STBY_GRND_ON_ALT, five positions. Which value is which is not confirmed. Not tried."),
    "AS_21_CODE": knob(P_RADIO, "Transponder (AS 21)", "Code knob", "right", "left",
                       note="AS21.xml: the sign of the value picks the direction (above 0 = H:Transponder_Code_INC, 0 or less = H:Transponder_Code_DEC). Not tried."),
    "AS_21_IDENT": press(P_RADIO, "Transponder (AS 21)", "IDENT button", note="AS21.xml sends K:XPNDR_IDENT_ON. Not tried."),
    "AS_21_VFR": press(P_RADIO, "Transponder (AS 21)", "VFR button", note="AS21.xml sends H:Transponder_VFR. Not tried."),
    "AS_21_FUNC": press(P_RADIO, "Transponder (AS 21)", "FUNC button", note="AS21.xml sends H:Transponder_FUNC. Not tried."),
    "AS_21_ENTER": press(P_RADIO, "Transponder (AS 21)", "ENTER button", note="AS21.xml sends H:Transponder_ENTER. Not tried."),
    "RADIO_COM_NAV_SWAP": press(P_RADIO, "Radio", "COM / NAV swap", note="Taken from the name; the AS 650 does the COM / NAV change with its own knob push. Not tried."),
    "RADIO_FREQUENCY_MHZ": knob(P_RADIO, "Radio", "Frequency MHz (event)", "up", "down", note=NOT_TRIED),
    "RADIO_FREQUENCY_KHZ": knob(P_RADIO, "Radio", "Frequency kHz (event)", "up", "down", note=NOT_TRIED),
    "RADIO_FREQUENCY_SWAP_PILOT": press(P_RADIO, "Radio", "Frequency swap, pilot", note=NOT_TRIED),
    "RADIO_FREQUENCY_SWAP_COPILOT": press(P_RADIO, "Radio", "Frequency swap, copilot", note=NOT_TRIED),
    "RADIO_PUSH_TO_TALK_PILOT": press(P_RADIO, "Intercom", "Push to talk, pilot", note="Taken from the name. A push to talk is held while speaking; not tried, so whether it needs a release is not known."),
    "RADIO_PUSH_TO_TALK_COPILOT": press(P_RADIO, "Intercom", "Push to talk, copilot", note="Taken from the name. A push to talk is held while speaking; not tried, so whether it needs a release is not known."),
    "AUDIO_INTERCOM": latched(P_RADIO, "Intercom", "Intercom (ALL / CREW / ISOLATE)", positions=3,
                              note="The aircraft's input list has INTERCOM ALL, CREW and ISOLATE. Which value is which is not confirmed. Not tried."),
    "AUDIO_VOLUME_PILOT": knob(P_RADIO, "Intercom", "Volume, pilot", "up", "down", note=NOT_TRIED),
    "AUDIO_VOLUME_COPILOT": knob(P_RADIO, "Intercom", "Volume, copilot", "up", "down", note=NOT_TRIED),
    "AUDIO_SQUELCH_PILOT": knob(P_RADIO, "Intercom", "Squelch, pilot", "up", "down", note=NOT_TRIED),
    "AUDIO_SQUELCH_COPILOT": knob(P_RADIO, "Intercom", "Squelch, copilot", "up", "down", note=NOT_TRIED),
    # --- instruments -----------------------------------------------------------------------------------------------------------------------
    "EPM_SOFTKEY_1": press(P_INSTR, "EPM", "Softkey 1", note="Engine parameter monitor; AS_EPM.xml sends H:AS_EPM_Softkey_1_Press. Not tried."),
    "EPM_SOFTKEY_2": press(P_INSTR, "EPM", "Softkey 2", note="Engine parameter monitor; AS_EPM.xml sends H:AS_EPM_Softkey_2_Press. Not tried."),
    "EPM_SOFTKEY_3": press(P_INSTR, "EPM", "Softkey 3", note="Engine parameter monitor; AS_EPM.xml: a short press sends H:AS_EPM_Softkey_3_Press, a long press H:AS_EPM_Softkey_3_LongPress. Not tried: a long press needs a held button."),
    "EPM_SOFTKEY_4": press(P_INSTR, "EPM", "Softkey 4", note="Engine parameter monitor; AS_EPM.xml sends H:AS_EPM_Softkey_4_Press. Not tried."),
    "EPM_SOFTKEY_5": press(P_INSTR, "EPM", "Softkey 5", note="Engine parameter monitor; AS_EPM.xml sends H:AS_EPM_Softkey_5_Press. Not tried."),
    "INSTRUMENT_NR": unknown(P_INSTR, "NR indicator", "NR indicator (main / backup / mute)",
                             _TIP + "\"NR\". The aircraft's input list has MAIN, BACKUP and MUTE (the low rotor speed horn). Not tried; what each value does is not known."),
    "INSTRUMENT_ALTIMETER_CALIBRATION_0": knob(P_INSTR, "Altimeter", "Altimeter setting", "up", "down", note=NOT_TRIED),
    "INSTRUMENT_HEADING_1": knob(P_INSTR, "Heading", "Heading bug", "up", "down", note=NOT_TRIED),
    "INSTRUMENT_CDI_OBS_1": knob(P_INSTR, "Navigation", "CDI OBS", "up", "down", note=NOT_TRIED),
    "INSTRUMENT_ATTITUDE_CAGE": press(P_INSTR, "Attitude", "Attitude indicator cage", note="Taken from the name. Not tried."),
    "INSTRUMENT_HORIZON_BAR": knob(P_INSTR, "Attitude", "Horizon bar", "up", "down", note=NOT_TRIED),
    "INSTRUMENT_DIRECTTO": press(P_INSTR, "Navigation", "Direct to (instrument)", note="Taken from the name. Not tried."),
    "INSTRUMENT_HOME": press(P_INSTR, "Navigation", "Home (instrument)", note="Taken from the name. Not tried."),
    "INSTRUMENT_VOLUME_SQUELCH": unknown(P_INSTR, "Radio", "Volume / squelch (instrument)", "Taken from the name; a knob with a push. Not tried."),
    # --- safety ----------------------------------------------------------------------------------------------------------------------------
    "SAFETY_ELT": unknown(P_SAFE, "ELT", "ELT switch (ARMED / ON / TEST)",
                          "The aircraft's input list has ARMED, ON and TEST for the emergency locator transmitter. Not tried: what each value does is not known; do not press it while flying."),
    # --- walkaround and covers -------------------------------------------------------------------------------------------------------------
    "COMMON_BLADE_SOCKS_1": _walk("Blade sock 1", "A main rotor blade cover (remove it before flight); the sim's solo lesson says: don't forget to remove covers."),
    "COMMON_BLADE_SOCKS_2": _walk("Blade sock 2", "A main rotor blade cover (remove it before flight); the sim's solo lesson says: don't forget to remove covers."),
    "COMMON_BLADE_SOCKS_3": _walk("Blade sock 3", "A main rotor blade cover (remove it before flight); the sim's solo lesson says: don't forget to remove covers."),
    "COMMON_PITOT_AND_STATIC_PORT_COVERS_1": _walk("Pitot and static port cover 1", _WALKI + "\"Pitot tube and static pressure port\"; remove the cover."),
    "COMMON_PITOT_AND_STATIC_PORT_COVERS_2": _walk("Pitot and static port cover 2", _WALKI + "\"Pitot tube and static pressure port\"; remove the cover."),
    "COMMON_ENGINE_COWLING_LATCH_LEFT": _walk("Left engine cowling latch", _WALKI + "\"Behind left engine cowling\", \"Close and lock both latches\"."),
    "COMMON_ENGINE_COWLINGS_LATCH_CENTER": _walk("Centre engine cowling latch", _WALKI + "\"Close and lock front latch\"."),
    "COMMON_ENGINE_COWLING_RIGHT": _walk("Right engine cowling", _WALKI + "\"Behind right engine cowling\", \"Close and lock both latches\"."),
    "COMMON_OIL_GAUGE": _walk("Oil dipstick", _CLUE + "\"Pull dipstick out, oil level must be between 4 and 6 Qt, then tighten dipstick back in place\"."),
    "UNKNOWN_INTERNAL_OIL_CAP": unknown(P_EXT, "Walkaround", "Oil cap", _CLUE + "\"Oil level must be at mid-sight gauge minimum\". An animation point of the model, not a cockpit control. Not tried."),
    "COMMON_DOOR_HANDLE_RIGHT": _walk("Right door handle", "Opens or closes the right door. Opening it from the cockpit can take you out of the aircraft."),
    "UNKNOWN_DOOR_RIGHT": unknown(P_EXT, "Doors", "Right door", "Opens or closes the right door; an animation point of the model. Opening it from the cockpit can take you out of the aircraft. Not tried."),
    "LANDING_SKID_LEFT": _walk("Left landing skid", _WALKI + "\"Landing gear pants and skid\"."),
    "LANDING_SKID_RIGHT": _walk("Right landing skid", _WALKI + "\"Landing gear pants and skid\"."),
    # --- other -----------------------------------------------------------------------------------------------------------------------------
    "AUXILIARY_BRACKET": latched(P_OTHER, "Cabin", "Auxiliary bracket (tablet mount)", note="The aircraft's input list has ON, OFF and TOGGLE; cosmetic. Not tried."),
    "UNKNOWN_TABLET_BACKDROP": unknown(P_OTHER, "Management tablet", "Tablet backdrop", "Part of the management tablet's model, not a cockpit control. Not tried."),
    "UNKNOWN_BUTTONS_COLLISION2_EFB_VISIBILITY_HIDE": unknown(P_OTHER, "Management tablet", "Tablet buttons (hide)", "Part of the management tablet's model, not a cockpit control. Not tried."),
})


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


_HEL = "One of the K: events the sim binds in its helicopter control profiles. Not tried on the Cabri."


def _extra():
    return [
        _sim(P_HAND, "Cyclic trim", "Trim right (K: event)", "ROTOR_LATERAL_TRIM_INC", "press", _HEL),
        _sim(P_HAND, "Cyclic trim", "Trim left (K: event)", "ROTOR_LATERAL_TRIM_DEC", "press", _HEL),
        _sim(P_HAND, "Cyclic trim", "Trim forward (K: event)", "ROTOR_LONGITUDINAL_TRIM_INC", "press", _HEL + " Which of INC and DEC is forward is not known."),
        _sim(P_HAND, "Cyclic trim", "Trim aft (K: event)", "ROTOR_LONGITUDINAL_TRIM_DEC", "press", _HEL + " Which of INC and DEC is forward is not known."),
        _sim(P_HAND, "Cyclic trim", "Trim reset (K: event)", "ROTOR_TRIM_RESET", "press", _HEL),
        _sim(P_ENG, "Rotor", "Governor switch on (K: event)", "ROTOR_GOV_SWITCH_ON", "press",
             _ROTOR_T + "what the governor switch sends when it is set to ON. Not tried."),
        _sim(P_ENG, "Rotor", "Governor switch off (K: event)", "ROTOR_GOV_SWITCH_OFF", "press",
             _ROTOR_T + "what the governor switch sends when it is set to OFF. Not tried."),
        _sim(P_ENG, "Rotor", "Clutch switch toggle (K: event)", "ROTOR_CLUTCH_SWITCH_TOGGLE", "press",
             _ROTOR_T + "what the covered clutch switch sends. Not tried."),
        _sim(P_ENG, "Start", "Autostart (K: event)", "ENGINE_AUTO_START", "press", _HEL + " Starts the engine by the sim's own procedure; do not press it in flight."),
        _sim(P_ENG, "Start", "Autoshutdown (K: event)", "ENGINE_AUTO_SHUTDOWN", "press", _HEL + " Stops the engine by the sim's own procedure; do not press it in flight."),
        _lvar(P_ENG, "Rotor", "Clutch cover opened (L:)", "XMLVAR_ROTORCRAFT_Clutch_Cover_Opened",
              _ROTOR_T + "the state of the cover over the clutch switch (the switch works only while it is 1). Not tried.", values="0|1"),
        _lvar(P_RADIO, "AS 650", "Knob mode: NAV (1) or COM (0)", "AS650_NAV_Freq_Modify",
              "AS650.xml: the L: variable the frequency knob push flips; the knobs tune the NAV radio when it is 1 and the COM radio when it is 0. Not tried.", values="0|1"),
    ]


EXTRA_ENTRIES = _extra()
