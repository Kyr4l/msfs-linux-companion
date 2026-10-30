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
