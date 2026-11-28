# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Row builders shared by the aircraft rules modules (build_aircraft_catalog.py)."""

NOT_TRIED = "Behaviour taken from the control's name, not tried on a real aircraft."


def row(panel, section, function, behavior="", **kw):
    r = {"panel": panel, "section": section, "function": function, "behavior": behavior}
    r.update(kw)
    return r


def knob(panel, section, function, up="increase", down="decrease", **kw):
    return [row(panel, section, "%s: %s" % (function, up), "knob_up", **kw),
            row(panel, section, "%s: %s" % (function, down), "knob_down", **kw)]


def press(panel, section, function, **kw):
    return [row(panel, section, function, "press", **kw)]


def latched(panel, section, function, positions=2, **kw):
    return [row(panel, section, function, "latched", positions=positions, **kw)]


def level(panel, section, function, rng=(0, 100), **kw):
    return [row(panel, section, function, "level", axis_range=list(rng), **kw)]


def unknown(panel, section, function, note):
    return [row(panel, section, function, "", note=note)]


def readable(name, drop=()):
    """A function name from an event name: the words in sentence case, without the prefixes in drop."""
    for p in drop:
        if name.startswith(p):
            name = name[len(p):]
            break
    words = name.replace("__", "_").strip("_").split("_")
    text = " ".join(words).lower()
    return text[:1].upper() + text[1:]


_KNOB_WORDS = ("KNOB", "VOLUME", "FREQUENCY", "KHZ", "MHZ", "COURSE", "HEADING", "ALTIMETER", "CALIBRATION", "DECISION_HEIGHT",
               "HORIZON_BAR", "GYRO_DRIFT", "TUNE", "GAIN", "LOOP", "INSTLIGHT", "SYSTLIGHT", "CURSOR")
_WALK_WORDS = ("DOOR", "WINDOW", "COWLING", "SOCKS", "SKID", "PITOT_COVER", "STATIC_PORT_COVER", "COVER_ENGINE", "TAIL_ROTOR_LOCK", "OIL_LEVEL")


def guess(name, panel, section, drop=(), walk_panel=None):
    """Rows for an event known only by its name (a rules module's fallback): knobs, walkaround items and switches told apart by words in the
    name; the note says it was not tried. Unclear names get no behaviour, so nothing sends a value that might be wrong."""
    fn = readable(name, drop)
    if any(w in name for w in _WALK_WORDS):
        return press(walk_panel or panel, "Walkaround", fn, note="Opens, closes or removes a part of the aircraft (taken from the name). "
                     "Doors and hatches of MSFS 2024 can move the pilot out of the cockpit. Not tried.")
    if name.endswith("_COVER"):
        return latched(panel, section, fn, note="A switch guard (taken from the name). " + NOT_TRIED)
    if any(w in name for w in _KNOB_WORDS):
        return knob(panel, section, fn, note=NOT_TRIED)
    if "SWITCH" in name or name.startswith(("BREAKER_", "LIGHTING_", "ELECTRICAL_", "FUEL_", "DEICE_", "PNEUMATICS_", "HYDRAULIC_")):
        return latched(panel, section, fn, note=NOT_TRIED)
    return unknown(panel, section, fn, "Taken from the name; what it does and the values it takes are not known. Not tried.")
