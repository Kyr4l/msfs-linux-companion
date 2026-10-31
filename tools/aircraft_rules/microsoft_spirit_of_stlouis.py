# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Rules that label the controls of the Microsoft Spirit of St. Louis (build_aircraft_catalog.py).

The aircraft has 19 cockpit input events (B:) and a handful of K: events and L: variables that its model and its checklist use:
  * names and where each control sits: the cockpit model (Official/Steam/microsoft-aircraft-spirit-of-st-louis/SimObjects/Airplanes/
    Microsoft_Spirit_of_StLouis/model.1928/Interior_SpiritOfStLouis_1928.xml) and the English tooltips of the package (en-US.locPak)
  * what the sim itself sends to operate them: the aircraft's checklist (Checklist/Spirit_Of_St_Louis_Checklist.xml and NYP_Lib.xml), whose
    copilot fires an event with a parameter and then tests the lever or switch
Nothing here was tried through this program on a running aircraft yet, so no entry is marked verified; each note says where the entry
comes from.
"""
from common import NOT_TRIED, latched, level, press, row, unknown

AIRCRAFT = "Microsoft_Spirit_of_StLouis"
TITLE = "Ryan NYP Spirit of St. Louis (Microsoft)"
EVENTS_FILE = "microsoft_spirit_of_stlouis_events.json"
AXIS_CONTROLS = True  # the throttle, mixture, brakes and surfaces also appear as the ranged axis events every aircraft has
SOURCES = [
    "Cockpit input events enumerated from MSFS 11.0.282174.999 (SimConnect EnumerateInputEvents)",
    "Microsoft_Spirit_of_StLouis/Checklist/Spirit_Of_St_Louis_Checklist.xml and NYP_Lib.xml shipped with the aircraft (the sim's interactive checklist: events with parameters, tested variables)",
    "Microsoft_Spirit_of_StLouis/model.1928/Interior_SpiritOfStLouis_1928.xml (the cockpit model: which control is which, the K: events and L: variables behind them)",
    "Official/Steam/microsoft-aircraft-spirit-of-st-louis/en-US.locPak (the aircraft's English tooltips) and Official/Steam/fs-base/en-US.locPak (the checklist texts)",
]

P_ENG = "Engine controls"
P_FUEL = "Fuel system"
P_HAND = "Handling"
P_INST = "Instruments and lights"
P_OTHER = "Cockpit extras"
PANEL_ORDER = [P_ENG, P_FUEL, P_HAND, P_INST, P_OTHER]

VERIFIED = {}

_CHECK = "The sim's own checklist uses it: "
_MODEL = "From the cockpit model: "

# the six fuel valves of the model (ASOBO_FUEL_Switch_Valve_Template, ID 1 to 6); the checklist opens them with FUELSYSTEM_VALVE_OPEN n
# short names: they must fit the small cards of the Guides tab and the Catalog tab
_VALVES = {
    "1": ("Left tank valve 1", "its tooltip is \"Left wing fuel tank valve\""),
    "2": ("Centre tank valve 2", "its tooltip is \"Center Wing Fuel tank valve\""),
    "3": ("Right tank valve 3", "its tooltip is \"Right Wing Fuel tank valve\""),
    "4": ("Valve 4", "the cockpit gives it only the generic tooltip \"fuel switch open / closed\""),
    "5": ("Valve 5", "the cockpit gives it only the generic tooltip \"fuel switch open / closed\""),
    "6": ("Inlet valve 6", "its tooltip is \"Engine inlet valve\" and the checklist calls it the global valve"),
}

_SIMPLE = {
    "ENGINE_Throttle_1": unknown(P_ENG, "Throttle", "Throttle lever (cockpit event)",
                                 "Not tried; the range this event takes is not known (a fraction 0 to 1 on the F/A-18E). The sim's own checklist uses the K: event THROTTLE1_SET, 0 to 16383, listed in this panel."),
    "FUEL_Mixture_1": unknown(P_ENG, "Mixture", "Mixture lever (cockpit event)",
                              "Not tried; the range this event takes is not known. The sim's own checklist uses the K: event MIXTURE1_SET, 0 to 16383, listed in this panel."),
    "ENGINE_Magneto_1": latched(P_ENG, "Magneto", "Magneto switch", positions=4,
                                note=_MODEL + "the switch template numbers the positions 0 BOTH, 1 LEFT, 2 RIGHT, 3 OFF. Not tried: which of them this event takes is not confirmed."),
    "ENGINE_Primer_1_Lever": unknown(P_ENG, "Primer", "Primer lever", _MODEL + "the primer lever (ASOBO_ENGINE_Lever_Primer_Template). Not tried: how the event moves it is not known; the checklist strokes it with the K: event TOGGLE_PRIMER."),
    "ENGINE_Primer_1_Lever_Lock": unknown(P_ENG, "Primer", "Primer lever lock", _MODEL + "the lock of the primer lever. Not tried."),
    "DEICE_Engine_1": unknown(P_ENG, "Carburettor", "Carburettor / ram-air lever", _MODEL + "the lever DEICE_Lever_CarburatorRamAir (ASOBO_DEICE_Lever_Engine_Template). Not tried."),
    "FUEL_Manual_Pump_2": unknown(P_FUEL, "Pump", "Wobble pump", _MODEL + "the wobble pump (ASOBO_FUEL_Lever_Pump_Template, ID 2). Not tried; the checklist has its manual pump step commented out."),
    "HANDLING_ElevatorTrim_1": unknown(P_HAND, "Trim", "Elevator trim wheel", _MODEL + "the trim wheel (ASOBO_HANDLING_Wheel_ElevatorTrim_Template). Not tried."),
    "LANDING_GEAR_ParkingBrake": latched(P_HAND, "Brakes", "Parking brake lever", note=_MODEL + "the parking brake lever NYP_Parking_Brake. Not tried; the checklist sets it with the K: event PARKING_BRAKE_SET."),
    "INSTRUMENT_Altimeter_1": unknown(P_INST, "Altimeter", "Altimeter setting knob", _MODEL + "the altimeter knob; the strip reads 29.0 to 32.5 inHg and the knob 29.5 to 32.0. Not tried: the unit the event takes (hPa on the F/A-18E) is not known."),
    "INSTRUMENT_Clock_Time": unknown(P_INST, "Clock", "Clock", "The clock of the panel (ASOBO_INSTRUMENT_Needle_Current_Time_Clock_Template). Not tried."),
    "INSTRUMENT_Clock_Ajustment_Target": unknown(P_INST, "Clock", "Clock adjustment", "The clock setting knob. Not tried."),
    "LIGHTING_PANEL_1": unknown(P_INST, "Lights", "Panel light dimmer", _MODEL + "the panel light dimmer (ASOBO_LIGHTING_Knob_Panel_Template, potentiometer 3). Not tried; on the CJ4 the panel light event misbehaves."),
}


def classify(name):
    """Rows (one per catalog entry) for a cockpit input event, or None when the event has no rule."""
    if name.startswith("FUEL_Valve_"):
        n = name[len("FUEL_Valve_"):]
        if n in _VALVES:
            label, why = _VALVES[n]
            return latched(P_FUEL, "Fuel valves", label,
                           note=_MODEL + "valve %s, %s. Not tried: the checklist opens it with the K: event FUELSYSTEM_VALVE_OPEN %s." % (n, why, n))
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


def _extra():
    out = [
        _sim(P_ENG, "Throttle", "Throttle lever (0 to 16383)", "THROTTLE1_SET", "level",
             _CHECK + "THROTTLE1_SET 0 closes the throttle and 16383 opens it fully (the checklist tests GENERAL ENG THROTTLE LEVER POSITION at 0 and 100 %). Not tried through this program.",
             values="0..16383", axis_range=[0, 16383]),
        _sim(P_ENG, "Mixture", "Mixture lever (0 to 16383)", "MIXTURE1_SET", "level",
             _CHECK + "MIXTURE1_SET 16383 is rich (the checklist tests GENERAL ENG MIXTURE LEVER POSITION at 100 %) and 0 is lean (cut off). Not tried through this program.",
             values="0..16383", axis_range=[0, 16383]),
        _sim(P_ENG, "Mixture", "Mixture: rich", "MIXTURE1_SET", "level", _CHECK + "16383 is rich. Not tried through this program.", values="16383", value=16383),
        _sim(P_ENG, "Mixture", "Mixture: lean (cut off)", "MIXTURE1_SET", "level", _CHECK + "0 is lean. Not tried through this program.", values="0", value=0),
        _sim(P_ENG, "Throttle", "Throttle: closed", "THROTTLE1_SET", "level", _CHECK + "0 closes the throttle. Not tried through this program.", values="0", value=0),
        _sim(P_ENG, "Throttle", "Throttle: full", "THROTTLE1_SET", "level", _CHECK + "16383 opens the throttle fully. Not tried through this program.", values="16383", value=16383),
        _sim(P_ENG, "Magneto", "Magnetos: both", "MAGNETO_BOTH", "press", _CHECK + "MAGNETO_BOTH, then it tests RECIP ENG LEFT MAGNETO:1 and RIGHT MAGNETO:1. Not tried through this program."),
        _sim(P_ENG, "Magneto", "Magnetos: off", "MAGNETO1_OFF", "press", _CHECK + "MAGNETO1_OFF at shutdown. Not tried through this program."),
        _sim(P_ENG, "Magneto", "Magneto: left", "MAGNETO1_LEFT", "press", "A standard event; the checklist only uses BOTH and OFF. Not tried."),
        _sim(P_ENG, "Magneto", "Magneto: right", "MAGNETO1_RIGHT", "press", "A standard event; the checklist only uses BOTH and OFF. Not tried."),
        _sim(P_ENG, "Starter", "Hand prop the airplane", "TOGGLE_STARTER1", "press",
             _MODEL + "the hand prop (NYP_Paper_Start, tooltip \"Hand prop the airplane\") sends (>K:TOGGLE_STARTER1), and the checklist sends the same event. Not tried through this program."),
        _sim(P_ENG, "Primer", "Primer: one stroke", "TOGGLE_PRIMER", "press",
             _CHECK + "TOGGLE_PRIMER, repeated until three strokes are counted (RECIP ENG PRIMER:1). Not tried through this program."),
        _sim(P_HAND, "Brakes", "Parking brake", "PARKING_BRAKES", "press", "A standard event that toggles the parking brake (the checklist uses PARKING_BRAKE_SET). Not tried."),
        _sim(P_HAND, "Doors", "Door handle", "TOGGLE_AIRCRAFT_EXIT", "press", _MODEL + "the door handle sends 0 (>K:TOGGLE_AIRCRAFT_EXIT), only below 10 knots. Not tried.", value=0),
        _sim(P_INST, "Compass", "Inductor compass reference: increase", "INDUCTOR_COMPASS_REF_INC", "knob_up",
             _MODEL + "the inductor compass knob (INDUCTOR_COMPASS_REF_INC clockwise). Not tried."),
        _sim(P_INST, "Compass", "Inductor compass reference: decrease", "INDUCTOR_COMPASS_REF_DEC", "knob_down",
             _MODEL + "the inductor compass knob (INDUCTOR_COMPASS_REF_DEC anticlockwise). Not tried."),
        _lvar(P_OTHER, "Periscope", "Periscope lever (0 to 100 %)", "NYP_Periscope_anim_handle_percent",
              _MODEL + "the periscope lever drags the L: variable NYP_Periscope_anim_handle_percent in percent (0 to 100). Not tried.", values="0..100"),
        _lvar(P_OTHER, "View", "Hide / show the control stick", "XMLVAR_YokeHidden1",
              _MODEL + "the stick's hit box flips the L: variable XMLVAR_YokeHidden1 (cosmetic). Not tried.", values="0|1"),
    ]
    for n, (label, why) in sorted(_VALVES.items()):
        for verb, word in (("OPEN", "Open"), ("CLOSE", "Close")):
            out.append(_sim(P_FUEL, "Fuel valves", "%s: %s" % (label, word.lower()), "FUELSYSTEM_VALVE_" + verb, "press",
                            _CHECK + "FUELSYSTEM_VALVE_%s %s, then it tests FUELSYSTEM VALVE SWITCH:%s. %s. Not tried through this program." % (verb, n, n, why[0].upper() + why[1:]),
                            values=n, value=int(n)))
    return out


EXTRA_ENTRIES = _extra()
