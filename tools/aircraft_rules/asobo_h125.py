# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Rules that label the controls of the Airbus H125 (Asobo, MSFS 2024; build_aircraft_catalog.py).

The helicopter has 207 cockpit input events (B:). The engine, electrical, rotor and light controls were checked on the running helicopter
on 2026-11-26 (MSFS 2024, parked: the sim's autostart compared before and after, then switch by switch, the engine stopped and everything
put back); those entries are marked verified and say what was read back. The checklist wording is from the aircraft's language file
(minimalcache fs24-asobo-aircraft-h125, minimal.fsarchive, en-us.locpak). The other controls are labelled from their names and say so.
"""
from common import NOT_TRIED, guess, knob, latched, level, press, unknown

AIRCRAFT = "Asobo_H125"
TITLE = "Airbus H125 (Asobo)"
EVENTS_FILE = "asobo_h125_events.json"
AXIS_CONTROLS = True  # collective, cyclic, pedals: the ranged axis events every aircraft has
SOURCES = [
    "Cockpit input events enumerated from MSFS 2024 12.2.282174.999 (msfs-linux-companion --dump-controls), aircraft asobo_h125, 2026-11-26",
    "Checked on the running helicopter, MSFS 2024, 2026-11-26 (parked; read back with the simulation variables named in each note)",
    "MSFS2024/minimalcache/fs24-asobo-aircraft-h125/minimal.fsarchive, en-us.locpak: the aircraft's checklist wording (CLUE.*)",
]

P_ENG = "Engine and rotor"
P_ELEC = "Electrical"
P_FUEL = "Fuel and hydraulics"
P_HAND = "Flight controls"
P_LIGHT = "Lights"
P_RADIO = "Radio and audio"
P_INSTR = "Instruments"
P_BRK = "Circuit breakers"
P_SAFE = "Safety and tests"
P_AUX = "Mission equipment"
P_EXT = "Walkaround"
P_OTHER = "Other"
PANEL_ORDER = [P_ENG, P_ELEC, P_FUEL, P_HAND, P_LIGHT, P_RADIO, P_INSTR, P_SAFE, P_AUX, P_BRK, P_EXT, P_OTHER]

_V = "Verified on the H125 in MSFS 2024 (2026-11-26): "
VERIFIED = {
    "ELECTRICAL_BATTERY": _V + "three positions: 2 = ON, 1 = OFF ((A:CIRCUIT ON:1) follows), 0 springs back to 1. Cold and dark is 1. "
                          "(A:ELECTRICAL MASTER BATTERY:1) stays 1 and the bus voltage variables read 0 in this aircraft.",
    "ELECTRICAL_GENERATOR": _V + "2 = ON, 1 = OFF (the sim's autostart sets 2; cold and dark is 1).",
    "ENGINE_STARTER": _V + "the starting selector: 1 = ON (with its guard up, COMMON_ENGINE_STARTER_COVER = 0) starts the engine (the FADEC runs "
                      "the start); 0 = OFF stops the engine, and the selector goes to OFF only with the guard down (cover 1), as the sim's checklist "
                      "says: \"Set starting selector on Off with switch guard down\".",
    "COMMON_ENGINE_STARTER_COVER": _V + "the guard over the starting selector: 0 = up (the sim's autostart leaves it there), 1 = down. "
                                   "The selector goes to OFF only with the guard down.",
    "ENGINE_THROTTLE_TWIST": _V + "two positions: 0 = IDLE, 1 = FLIGHT (0.5 stays where it was). The FADEC sets the fuel: "
                             "AXIS_HELICOPTER_THROTTLE_SET does nothing here. At IDLE N1 falls to about 50 %, the rotor to about 93 %.",
    "ROTOR_BRAKE": _V + "takes 0 to 1: 1 = brake on ((A:ROTOR BRAKE ACTIVE) = 1), 0 = off. Cold and dark is 1; the autostart releases it.",
    "ROTOR_COLLECTIVE": _V + "takes 0 to 1 (0.2 read back as 3277 = 20 %, (A:COLLECTIVE POSITION)). For a lever use the axis AXIS_COLLECTIVE_SET "
                        "(-16383 to 16383, also checked).",
    "LIGHTING_LANDING_LIGHT": _V + "1 = ON, 0 = OFF, read back with (A:LIGHT LANDING).",
    "LIGHTING_TAXI_LIGHT": _V + "1 = ON, 0 = OFF, read back with (A:LIGHT TAXI).",
    "LIGHTING_ANTI_COLLISION_LIGHT": _V + "1 = ON, 0 = OFF, read back with (A:LIGHT STROBE) ((A:LIGHT BEACON) does not follow).",
    "LIGHTING_SELECTOR": _V + "the VEMD lighting selector: 0 and 1 seen; 1 lit (A:LIGHT NAV) in this test, the autostart sets 2.",
    "PROCEDURE_STANDALONE_AUTO_START_SHUTDOWN": _V + "one event starts the engine by the sim's own procedure (rotor at 100 % in about 65 s). "
                                                "A second event did NOT shut it down: stop the engine with the starting selector.",
}

_SIMPLE = {
    "ELECTRICAL_BATTERY": latched(P_ELEC, "Power", "Battery switch (1 OFF / 2 ON)", positions=3),
    "ELECTRICAL_GENERATOR": latched(P_ELEC, "Power", "Generator switch (1 OFF / 2 ON)", positions=3),
    "ELECTRICAL_AVIONICS_MASTER": latched(P_ELEC, "Power", "Avionics master", note=NOT_TRIED),
    "ENGINE_STARTER": latched(P_ENG, "Start", "Starting selector (OFF / ON)"),
    "COMMON_ENGINE_STARTER_COVER": latched(P_ENG, "Start", "Starting selector guard (0 up / 1 down)"),
    "ENGINE_THROTTLE_TWIST": latched(P_ENG, "Start", "Twist grip (0 IDLE / 1 FLIGHT)"),
    "ROTOR_BRAKE": level(P_ENG, "Rotor", "Rotor brake lever", (0, 1)),
    "ROTOR_COLLECTIVE": level(P_HAND, "Collective", "Collective lever (cockpit event)", (0, 1)),
    "ROTOR_COLLECTIVE_LOCK": latched(P_HAND, "Collective", "Collective lock", note=NOT_TRIED),
    "LIGHTING_LANDING_LIGHT": latched(P_LIGHT, "Exterior", "Landing light"),
    "LIGHTING_TAXI_LIGHT": latched(P_LIGHT, "Exterior", "Taxi light"),
    "LIGHTING_ANTI_COLLISION_LIGHT": latched(P_LIGHT, "Exterior", "Anti-collision light"),
    "LIGHTING_SELECTOR": latched(P_LIGHT, "Interior", "VEMD lighting selector", positions=3),
    "PROCEDURE_STANDALONE_AUTO_START_SHUTDOWN": press(P_ENG, "Start", "Autostart (sim procedure)"),
    "FUEL_SHUTOFF": latched(P_FUEL, "Fuel", "Fuel shut-off lever",
                            note="Cold and dark is 1; the sim's checklist: \"Fuel shut-off lever must be in forward position\". "
                                 "(A:FUELSYSTEM VALVE OPEN:1) seemed to follow (1 = open) but the reading lagged: not confirmed."),
    "FUEL_PUMP": latched(P_FUEL, "Fuel", "Booster pump", note="The sim's checklist: \"Switch booster pump on, blue indicator light must be on\". "
                                                              "A set to 1 was refused with the engine stopped (read back 0). Not confirmed."),
    "HYDRAULIC_HYD": latched(P_FUEL, "Hydraulics", "Hydraulic power", note="Tooltip \"Hydraulic power\"; cold and dark is 1. " + NOT_TRIED),
    "SAFETY_HORN": latched(P_SAFE, "Warnings", "Horn (low rotor speed warning)",
                           note="The sim's checklist: the audio warning must sound for NR at or below 360 RPM; the autostart sets 1. " + NOT_TRIED),
}

_PREFIX = [  # (prefix, panel, section, prefixes dropped from the name)
    ("BREAKER_", P_BRK, "Breakers", ("BREAKER_",)),
    ("G225_", P_RADIO, "G225 radio", ("G225_",)),
    ("AS1000_", P_RADIO, "Radio", ("AS1000_AS1000_",)),
    ("RADIO_", P_RADIO, "Radio", ("RADIO_",)),
    ("AUDIO_", P_RADIO, "Audio", ("AUDIO_",)),
    ("INSTRUMENT_VEMD_", P_INSTR, "VEMD", ("INSTRUMENT_VEMD_",)),
    ("INSTRUMENT_", P_INSTR, "Instruments", ("INSTRUMENT_",)),
    ("HANDLING_", P_HAND, "Controls", ("HANDLING_",)),
    ("PROCEDURE_LATERAL_TRIM", P_HAND, "Trim", ("PROCEDURE_",)),
    ("PROCEDURE_LONGITUDINAL_TRIM", P_HAND, "Trim", ("PROCEDURE_",)),
    ("PROCEDURE_", P_OTHER, "Simulation options", ("PROCEDURE_",)),
    ("LIGHTING_", P_LIGHT, "Lights", ("LIGHTING_",)),
    ("ELECTRICAL_", P_ELEC, "Electrical", ("ELECTRICAL_",)),
    ("ENGINE_", P_ENG, "Engine", ("ENGINE_",)),
    ("DEICE_", P_FUEL, "Heating", ("DEICE_",)),
    ("PNEUMATICS_", P_FUEL, "Cabin air", ("PNEUMATICS_",)),
    ("SAFETY_", P_SAFE, "Safety", ("SAFETY_",)),
    ("AUXILIARY_", P_AUX, "Equipment", ("AUXILIARY_",)),
    ("COMMON_", P_EXT, "Walkaround", ("COMMON_",)),
    ("LANDING_", P_EXT, "Walkaround", ("LANDING_",)),
    ("UNKNOWN_", P_OTHER, "Model", ("UNKNOWN_",)),
]


def classify(name):
    """Rows (one per catalog entry) for a cockpit input event, or None when the event has no rule."""
    if name in _SIMPLE:
        return _SIMPLE[name]
    for prefix, panel, section, drop in _PREFIX:
        if name.startswith(prefix):
            return guess(name, panel, section, drop, P_EXT)
    return None
