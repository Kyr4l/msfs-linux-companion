# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Rules that label the controls of the Bell UH-1H (Microsoft, MSFS 2024; build_aircraft_catalog.py).

The helicopter has 83 cockpit input events (B:). The engine, throttle, rotor and battery controls were checked on the running helicopter on
2026-11-26 (MSFS 2024, parked: the sim's autostart compared before and after, a manual start and stop, everything put back); those entries
are marked verified and say what was read back. The other controls are labelled from their names and say so.
"""
from common import NOT_TRIED, guess, latched, level, press, unknown

AIRCRAFT = "Microsoft_UH1H"
TITLE = "Bell UH-1H (Microsoft)"
EVENTS_FILE = "microsoft_uh1h_events.json"
AXIS_CONTROLS = True
SOURCES = [
    "Cockpit input events enumerated from MSFS 2024 12.2.282174.999 (msfs-linux-companion --dump-controls), aircraft microsoft_uh1h, 2026-11-26",
    "Checked on the running helicopter, MSFS 2024, 2026-11-26 (parked; read back with the simulation variables named in each note)",
]

P_ENG = "Engine and rotor"
P_ELEC = "Electrical"
P_FUEL = "Fuel"
P_HAND = "Flight controls"
P_LIGHT = "Lights"
P_RADIO = "Radio"
P_INSTR = "Instruments"
P_EXT = "Walkaround"
P_OTHER = "Other"
PANEL_ORDER = [P_ENG, P_ELEC, P_FUEL, P_HAND, P_LIGHT, P_RADIO, P_INSTR, P_EXT, P_OTHER]

_V = "Verified on the UH-1H in MSFS 2024 (2026-11-26): "
VERIFIED = {
    "ELECTRICAL_BATTERY_1": _V + "1 = ON, 0 = OFF (the autostart sets 1; (A:ELECTRICAL MASTER BATTERY:1) and (A:CIRCUIT ON:1) follow).",
    "HELICOPTER_ENGINE_THROTTLE_1": _V + "takes 0 to 1 (read back 0 to 100). Without the idle release a value below 0.25 stops at 0.25, the idle "
                                    "stop (N1 about 65 %); with HELICOPTER_ENGINE_IDLE_RELEASE_1 = 1, 0 cuts the engine. Full is 1 (the governor then "
                                    "holds the rotor at 100 %). AXIS_HELICOPTER_THROTTLE_SET does nothing here.",
    "HELICOPTER_ENGINE_IDLE_RELEASE_1": _V + "1 lets the throttle go below the idle stop (0 = cut-off). With the engine stopped it could not be set "
                                        "back to 0 (it stayed 1).",
    "HELICOPTER_ENGINE_GOV": _V + "0 = governor on ((A:ROTOR GOV ACTIVE:1) = 1); cold and dark is 0.",
    "MB_ROTOR_BRAKE": _V + "1 applies the rotor brake ((A:ROTOR BRAKE ACTIVE) = 1, the handle moves to 100 %), 0 releases it.",
    "PROCEDURE_AUTOSTART": _V + "one event starts the engine by the sim's own procedure (battery on, throttle full; rotor at 100 % in about 50 s).",
}

_SIMPLE = {
    "ELECTRICAL_BATTERY_1": latched(P_ELEC, "Power", "Battery switch (0 OFF / 1 ON)"),
    "HELICOPTER_ENGINE_THROTTLE_1": level(P_ENG, "Throttle", "Throttle (0.25 idle stop, 1 full)", (0, 1)),
    "HELICOPTER_ENGINE_IDLE_RELEASE_1": latched(P_ENG, "Throttle", "Idle release"),
    "HELICOPTER_ENGINE_GOV": latched(P_ENG, "Governor", "Governor switch (0 AUTO)"),
    "MB_ROTOR_BRAKE": latched(P_ENG, "Rotor", "Rotor brake"),
    "PROCEDURE_AUTOSTART": press(P_ENG, "Start", "Autostart (sim procedure)"),
    "ENGINE_COLLECTIVE_LEVER": unknown(P_HAND, "Collective", "Collective lever (cockpit event)",
                                       "Reads 0 to 100 ((A:COLLECTIVE POSITION) 25 % read back as 25); the range it takes was not tried. "
                                       "For a lever use the axis AXIS_COLLECTIVE_SET (-16383 to 16383, checked)."),
    "FUEL_FUEL_MASTER_SWITCH": latched(P_FUEL, "Fuel", "Fuel master switch",
                                       note="0 in cold and dark and with the engine running after the sim's autostart, so 0 is likely ON. Not confirmed."),
}

_PREFIX = [
    ("210_IE_BASE_", P_RADIO, "COM / NAV", ("210_IE_BASE_",)),
    ("SS_ANARC27_", P_RADIO, "AN/ARC-27", ("SS_ANARC27_",)),
    ("SS_IE_BASE_ADF_", P_RADIO, "ADF", ("SS_IE_BASE_ADF_",)),
    ("NAVCOM_", P_RADIO, "Radio", ("NAVCOM_",)),
    ("SS_IE_BASE_", P_LIGHT, "Search light", ("SS_IE_BASE_",)),
    ("TAOG_LIGHT_", P_LIGHT, "Search light", ("TAOG_",)),
    ("LIGHTING_", P_LIGHT, "Lights", ("LIGHTING_",)),
    ("INSTRUMENT_", P_INSTR, "Instruments", ("INSTRUMENT_",)),
    ("HANDLING_", P_HAND, "Controls", ("HANDLING_",)),
    ("SAFETY_", P_OTHER, "Wipers", ("SAFETY_",)),
    ("DEICE_", P_OTHER, "Heating", ("DEICE_",)),
    ("FUEL_", P_FUEL, "Fuel", ("FUEL_",)),
    ("COMMON_", P_EXT, "Walkaround", ("COMMON_",)),
    ("AIRFRAME_", P_EXT, "Walkaround", ("AIRFRAME_",)),
]


def classify(name):
    """Rows (one per catalog entry) for a cockpit input event, or None when the event has no rule."""
    if name in _SIMPLE:
        return _SIMPLE[name]
    for prefix, panel, section, drop in _PREFIX:
        if name.startswith(prefix):
            return guess(name, panel, section, drop, P_EXT)
    return None
