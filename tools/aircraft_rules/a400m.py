# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Rules that label the controls of the Airbus A400M Atlas (iniBuilds, MSFS 2024; build_aircraft_catalog.py).

The aircraft has 751 cockpit input events (B:), named AIRLINER_<control>, INSTRUMENT_<control>[_IE_ID|_KNOB|_PULL|_PUSH] and
AIRCRAFT_<control>_IE_ID. What each one is comes from
  * the aircraft's English tooltips (Official2024/Steam/microsoft-aircraft-a400m/minimal.fsarchive, en-us.locpak, stored trimmed in
    a400m_tooltips.json): A400M.TOOLTIPS.<control>.TITLE / .ACTION and the names of the positions (.1 .2 .3 ..., a few .0 .1 .2)
  * the sim's own A400M checklists in the same file (A400.CHECKLISTS.*): the names the procedures give the controls and their positions
  * the aircraft's panel states (Official2024/Steam/microsoft-aircraft-a400m/data/panelstates/*.ini): the L: variables it really has
    (ini_apu_master_switch, ini_autobrake_level, ini_1_altimeter_pull_command ...), the same names as the iniBuilds A320neo V2
  * the aircraft's throttle calibration (WASM/MSFS2024/microsoft-aircraft-a400m/work/ThrottleData.ini): the A320neo V2's regions on four axes
Which value a position takes is not written anywhere. The rule used here was checked on the iniBuilds A320neo V2, whose tooltips follow
the same scheme: the position named .1 is value 0, .2 is 1, .3 is 2 (LAND L: .1 ON .2 OFF .3 RETRACTED = 0 / 1 / 2, read back from the sim);
where the tooltip counts from .0 the value is the number itself. The A400M's panel states agree: its switch L: variables (ini_seatbelts_switch,
ini_beacon_light_switch, ini_landing_light_switch ...) take exactly those values in cold and dark and in ready for takeoff. None of it was
tried on the A400M, so no entry is marked verified.
"""
import json
import os
import re

from common import knob, latched, press, row, unknown

AIRCRAFT = "a400m"
TITLE = "Airbus A400M Atlas (iniBuilds, MSFS 2024)"
EVENTS_FILE = "a400m_events.json"
AXIS_CONTROLS = True     # four thrust levers, flaps, brakes, trims: the ranged axis events every aircraft has
STANDARD_EVENTS = True   # and the standard K: events (GEAR_UP, PARKING_BRAKES, SPOILERS_ARM_TOGGLE ...)
SOURCES = [
    "Cockpit input events enumerated from MSFS 2024 12.2.282174.999 (SimConnect EnumerateInputEvents), preset a400m_cargo",
    "Official2024/Steam/microsoft-aircraft-a400m/minimal.fsarchive, en-us.locpak: the aircraft's English tooltips (A400M.TOOLTIPS.*) and checklists (A400.CHECKLISTS.*) (unencrypted archive)",
    "Official2024/Steam/microsoft-aircraft-a400m/data/panelstates/*.ini: the aircraft's L: variables (cold and dark, on GPU, APU running, ready for takeoff)",
    "WASM/MSFS2024/microsoft-aircraft-a400m/work/ThrottleData.ini: the thrust lever regions (reverse -1, idle -0.5, CL 0, FLX/MCT 0.5, TOGA 1, on four axes)",
    "The iniBuilds A320neo V2 catalog (data/microsoft_a320neo_catalog.json): the same L: variables and the tooltip numbering, checked there",
]

P_GLARE = "Glareshield"
P_MAIN = "Main panel"
P_OVH = "Overhead"
P_PED = "Pedestal"
P_AUDIO = "Audio and radio"
P_SIDE = "Side consoles"
P_CABIN = "Cabin and airframe"
PANEL_ORDER = [P_GLARE, P_MAIN, P_OVH, P_PED, P_AUDIO, P_SIDE, P_CABIN]

_SEEN = "Checked on the aircraft (2026-11-06, MSFS 2024, parked, engines running): "
VERIFIED = {
    "INSTRUMENT_OVH_LDG_LIGHT_IE_ID": _SEEN + "1 turned the landing lights on and 0 off; L:INI_LANDING_LIGHT_SWITCH followed.",
}
# read back from the running aircraft without being set (2026-11-06, engines running): the position numbering holds for these
_READ = ("Read on the aircraft (2026-11-06, engines running): ENG START read 1 (NORM) and ENG 1 MASTER 0 (RUN), as this numbering says; "
         "not set by this program yet. ")

with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "a400m_tooltips.json")) as _f:
    TIPS = json.load(_f)["tooltips"]

_TIP = "Tooltip in the aircraft: "
_CHK = "The sim's A400M checklist: "
_POS = ("Value = tooltip position - 1 (.1 = 0, .2 = 1 ...): the numbering checked on the iniBuilds A320neo V2, and the A400M's own panel states "
        "agree (seat belts, beacon, landing, turn-off and wing lights: the switch L: variables go from the OFF value to the ON value of this "
        "numbering between cold and dark and ready for takeoff). Not tried on the A400M.")
_TOGGLE = "Switch with two positions; which value is on is not confirmed. Not tried."
_PRESS = "Push button (the tooltip says PRESS in both states). Not tried."
_KNOB = "Knob; the sign of the value is taken as the direction (the convention of the sim's templates). Not tried."
_INTERNAL = "Animation helper of the aircraft's model, not a cockpit control. Not tried."
_DANGER = "DANGER: "

# --------------------------------------------------------------------------------------------------------------------------------------
# panel and section from the event name


def _side(stem):
    if stem.startswith("CPT_"):
        return "captain"
    if stem.startswith("FO_"):
        return "first officer"
    if stem.startswith("VHF3_"):
        return "third crew"
    return ""


def _place(stem):
    s = stem
    side = _side(s)
    core = re.sub(r"^(CPT|FO|VHF3)_", "", s)
    if re.match(r"(COMM_|INT_TOGGLE)", core) or re.search(r"_VOL_(KNOB|BUTTON|SEL2?)$", core) or core in ("OVRD_PUSH",):
        return P_AUDIO, ("Radio panel " + side).strip() if core.startswith("COMM_") else ("Audio " + side).strip()
    if core.startswith("EFIS_") or core.startswith("BARO_") or core in ("XCHECK",):
        return P_GLARE, ("EFIS " + side).strip()
    if core in ("MASTER_WARN", "MASTER_CAUTION"):
        return P_GLARE, "Warnings"
    if core.startswith("HUD_"):
        return P_GLARE, ("HUD " + side).strip()
    if core.startswith("FCU_"):
        return P_GLARE, "FCU"
    if core.startswith(("INNER_DU_", "OUTER_DU_", "LT_KNOB")):
        return P_MAIN, ("Display brightness " + side).strip()
    if core.startswith(("SIDESTICK", "TILLER", "ARMREST", "SEAT", "TRAY", "CONSOLE_LT", "PILLAR_LIGHT", "MAP_LIGHT")) or s.startswith("SEAT_"):
        return P_SIDE, ("Console " + side).strip()
    if core.startswith(("DU_", "STBY_HORI", "MCU_", "MAIN_PNL", "AUTO_BRK", "ANTISKID", "ACCU_PRESS", "CAB_ALT", "CTR_SHADE", "GEAR")):
        sec = {"DU_": "Displays", "STBY_HORI": "Standby instrument", "MCU_": "Lights", "MAIN_PNL": "Lights", "AUTO_BRK": "Autobrake",
               "ANTISKID": "Brakes", "ACCU_PRESS": "Brakes", "CAB_ALT": "Cabin", "CTR_SHADE": "Sun shade", "GEAR": "Landing gear"}
        for k, v in sec.items():
            if core.startswith(k):
                return P_MAIN, v
    if re.match(r"ENG\d_CUTOFF$", core) or core.startswith(("ENG_MODE_", "ENG_START")):
        return P_PED, "Engines"
    if core.startswith(("FMGS", "FMS_MODE")):
        return P_PED, "FMS"
    if core.startswith("ECAM_") or core.startswith("ECAM"):
        return P_PED, "ECAM control panel"
    if core.startswith(("WXR_", "WER_")):
        return P_PED, "Weather radar"
    if core.startswith(("TCAS_", "SURV_", "TAWS", "IFF_")):
        return P_PED, "Surveillance"
    if core.startswith(("REVTHRUST", "PITCHTRIM", "ROLLTRIM", "RUDDER_TRIM", "GND_SPOILER", "PED", "FORMATION", "U_TURN", "VS_TOGGLE",
                        "THROTTLE")):
        return P_PED, "Flight controls"
    if core.startswith("CARGO_DOOR") or core.startswith(("COCKPIT_DOOR", "REAR_READING", "PEDESTAL_FLOOD")):
        return P_PED, "Doors and lights"
    # overhead
    groups = [
        ("ELEC_", "Electrical"), ("EXT_POWER", "Electrical"), ("EMER_GEN", "Electrical"), ("ACC_ESS", "Electrical"), ("OVH_BAT_CHECK", "Electrical"),
        ("OVH_LOADSHED", "Electrical"), ("FUEL_", "Fuel"), ("FIRE_", "Fire"), ("HYD_", "Hydraulics"), ("GND_HYD_", "Hydraulics"),
        ("APU_", "APU"), ("OVH_AIR_", "Air"), ("AIR_COND", "Air"), ("RAM_AIR", "Air"), ("RAPID_DEPRESS", "Pressurization"),
        ("CAB_PRESS", "Pressurization"), ("LDG_ELEVATION", "Pressurization"), ("VENT_", "Ventilation"), ("RECYC_FANS", "Ventilation"),
        ("AVNCS_BLOW", "Ventilation"), ("OVH_HOT_AIR", "Air"), ("SMOKE_", "Smoke"), ("FCTL_", "Flight controls"), ("DIRECT_LAW", "Flight controls"),
        ("PITCHTRIM_RESET", "Flight controls"), ("ROLLTRIM_RESET", "Flight controls"), ("GADIRS", "ADIRS"), ("OVH_ADR", "ADIRS"),
        ("OVH_IR", "ADIRS"), ("AIR_DATA", "ADIRS"), ("ATT_HDG", "ADIRS"), ("OVH_ANTI_ICE", "Anti-ice"), ("OVH_ICE_IND", "Anti-ice"),
        ("OVH_PROBE", "Anti-ice"), ("OVH_CPT_WIPER", "Wipers"), ("OVH_FO_WIPER", "Wipers"), ("OVH_ENG", "Engines"), ("OVH_FADEC", "Engines"),
        ("OVH_BEACON", "Exterior lights"), ("OVH_STROBE", "Exterior lights"), ("OVH_NAV", "Exterior lights"), ("OVH_LDG", "Exterior lights"),
        ("OVH_TAXI", "Exterior lights"), ("OVH_RWY", "Exterior lights"), ("OVH_WING", "Exterior lights"), ("OVH_NVG", "Exterior lights"),
        ("OVH_DOME", "Interior lights"), ("OVH_ANN_LT", "Interior lights"), ("OVH_ELMF", "Interior lights"), ("OVH_SEATBELT", "Signs"),
        ("OVH_EMER_EXIT", "Signs"), ("AUDIO_MODE", "Audio"),
    ]
    for k, v in groups:
        if core.startswith(k) or s.startswith(k):
            return P_OVH, v
    return P_CABIN, "Other"


# --------------------------------------------------------------------------------------------------------------------------------------
# tooltip lookup and the generic rule


def _stem_candidates(name):
    b = re.sub(r"^(AIRLINER_|INSTRUMENT_|AIRCRAFT_)", "", name)
    out = [b, re.sub(r"_IE_ID$", "", b)]
    for suf in ("_KNOB", "_PULL", "_PUSH", "_BUTTON"):
        if b.endswith(suf):
            out += [b[: -len(suf)], b[: -len(suf)] + "_PUSH"]
    return out


def _tip(name):
    for c in _stem_candidates(name):
        if c in TIPS:
            return c, TIPS[c]
    return None, None


_SHORT = [(r"\s+(SWITCH|SELECTOR|PUSHBUTTON|PUSH BUTTON|BUTTON)$", ""), (r"\s+SLECTOR", " SELECTOR"), (r"\s{2,}", " ")]


def _title(t, stem):
    if not t.get("TITLE") and stem.endswith("_PUSH") and TIPS.get(stem[:-5], {}).get("TITLE"):
        t = TIPS[stem[:-5]]  # FCU_ROTARY_ALT_PUSH has only the push / pull texts; the knob's tooltip names it
    title = (t.get("TITLE") or t.get("ACTION") or stem.replace("_", " ")).strip()
    for a, b in _SHORT:
        title = re.sub(a, b, title)
    return title


def _positions(t):
    nums = sorted((int(k), v) for k, v in t.items() if k.isdigit() and v.strip())
    if not nums:
        return []
    zero = any(k == 0 for k, _ in nums)
    return [(k if zero else k - 1, v.strip()) for k, v in nums]


def _base(name):
    return re.sub(r"^(AIRLINER_|INSTRUMENT_|AIRCRAFT_)", "", name)


def _raw_title(name, stem, t):
    title = _title(t, stem)
    side = {"captain": "CPT", "first officer": "FO", "third crew": "3RD"}.get(_side(_base(name)), "")
    if side and not re.search(r"\b(CPT|CAPT|CAPTAIN|FO|F/O|3RD)\b", title):
        title = "%s %s" % (side, title)
    return title


_NOISE = {"CPT", "FO", "VHF3", "OVH", "VOL", "BUTTON", "KNOB", "IE", "ID", "PUSH", "PULL", "SEL"}


def _distinct(name, title):
    """The part of the event name that tells it apart from the other events with the same tooltip title."""
    parts = re.split(r"[ /]", title)
    words = set(parts) | {a + b for a, b in zip(parts, parts[1:])}  # PITCH TRIM also covers PITCHTRIM
    toks = [w for w in _base(name).split("_") if w not in _NOISE and w not in words]
    return " ".join(toks)


def _shared_titles():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), EVENTS_FILE)) as f:
        names = json.load(f)["input_events"]
    seen = {}
    for n in names:
        if n.endswith(("_PULL", "_PUSH")):  # those get ": pull" / ": push" anyway
            continue
        stem, t = _tip(n)
        if t is not None:
            seen.setdefault(_raw_title(n, stem, t), set()).add(n)
    return {k for k, v in seen.items() if len(v) > 1}


def _generic(name):
    stem, t = _tip(name)
    if t is None:
        return None
    panel, section = _place(_base(name))
    if section == "Other":
        panel, section = _place(stem)
    title = _raw_title(name, stem, t)
    if title in SHARED and _distinct(name, title):
        title = "%s %s" % (title, _distinct(name, title))
    action = t.get("ACTION", "")
    note_tip = _TIP + "%s%s." % (t.get("TITLE", stem), (" \"" + action.lower() + "\"") if action else "")
    if name.endswith("_PULL") or name.endswith("_PUSH"):
        verb = "pull" if name.endswith("_PULL") else "push"
        extra = t.get("STATE_" + verb.upper(), "")
        return press(panel, section, "%s: %s" % (title, verb), note=note_tip + (" %s: \"%s\"." % (verb.upper(), extra) if extra else "") + " Not tried.")
    if name.endswith("_KNOB") and not _positions(t):
        return knob(panel, section, title, "up", "down", note=note_tip + " " + _KNOB)
    pos = _positions(t)
    if set(pos and [v for _, v in pos]) <= {"SET"} and pos:  # EFIS knobs: every position is called SET
        return knob(panel, section, title, "right", "left", note=note_tip + " " + _KNOB)
    if pos:
        rows = latched(panel, section, title + ": next position", positions=len(pos), note=note_tip + " " + _POS)
        labels = [label for _, label in pos]
        for v, label in pos:
            if labels.count(label) > 1:  # the tooltip names two positions alike (FO MAP MODE: ENG twice)
                label = "%s (%d)" % (label, v)
            rows.append(row(panel, section, "%s: %s" % (title, label), "level", value=v, note=note_tip + " " + _POS))
        return rows
    on, off = t.get("ON", ""), t.get("OFF", "")
    if on == "PRESS" and off == "PRESS" or action.startswith(("MUTE", "PERFORM", "FMGS")):
        return press(panel, section, title, note=note_tip + " " + _PRESS)
    if on or off:
        return latched(panel, section, title, note=note_tip + " " + _TOGGLE)
    if t.get("STATE_PULL") or re.search(r"(_KNOB|_BIG|_SMALL|_SEL2?)$", name) or action.startswith(("ADJUST", "SET VOLUME")):
        return knob(panel, section, title, "up", "down", note=note_tip + " " + _KNOB)
    return press(panel, section, title, note=note_tip + " Not tried.")


# --------------------------------------------------------------------------------------------------------------------------------------
# hand-written rows: the controls the sim's checklists name, the dangerous ones, and the events without a tooltip


def _eng_cutoff(n):
    note = (_TIP + "ENGINE %d CUTOFF SWITCH, RUN / FTHR / OFF. " % n + _CHK + "ENGINE MASTER %d to the middle position (FTHR) to start, "
            "then RUN once all four engines run (START_PROCEDURE). " % n + _READ + _POS)
    sec = "Engine masters"
    return [
        row(P_PED, sec, "ENG %d MASTER: RUN" % n, "level", value=0, note=note),
        row(P_PED, sec, "ENG %d MASTER: FTHR (start)" % n, "level", value=1, note=note),
        row(P_PED, sec, "ENG %d MASTER: OFF" % n, "level", value=2, note=_DANGER + "stops engine %d. " % n + note),
        row(P_PED, sec, "ENG %d MASTER: next position" % n, "latched", positions=3, note=_DANGER + "may stop engine %d. " % n + note),
    ]


def _fire(n):
    return latched(P_OVH, "Fire", "ENG %d FIRE handle" % n, note=_DANGER + "the fire handle shuts engine %d down. Not tried." % n)


def _agent(n, k):
    return press(P_OVH, "Fire", "ENG %d AGENT %d" % (n, k), note=_DANGER + "discharges a fire bottle. Not tried.")


def _walk(function):
    return unknown(P_CABIN, "Walkaround", function, "A point of the sim's interactive walkaround or a model helper, not a cockpit control. Not tried.")


_HAND = {
    "ENGINE_THROTTLE_%d" % n: unknown(P_PED, "Thrust levers", "THROTTLE %d (cockpit lever event)" % n,
                                      "The tooltip is \"MOVE THROTTLE %d\"; the range this event takes is not known. For a lever use the axis of the "
                                      "Control list (THROTTLE%d_AXIS_SET_EX1): the aircraft's ThrottleData.ini has the A320neo V2's regions "
                                      "(reverse -1, idle -0.5, CL 0, FLX/MCT 0.5, TOGA 1) on four axes." % (n, n))
    for n in range(1, 5)
}
_HAND.update({"AIRLINER_ENG%d_CUTOFF" % n: _eng_cutoff(n) for n in range(1, 5)})
_HAND.update({"AIRLINER_FIRE_ENG%d" % n: _fire(n) for n in range(1, 5)})
_HAND.update({"AIRLINER_FIRE_ENG%d_AGT%d" % (n, k): _agent(n, k) for n in range(1, 5) for k in (1, 2)})
_HAND.update({
    "AIRLINER_FIRE_APU": latched(P_OVH, "Fire", "APU FIRE handle", note=_DANGER + "shuts the APU down. Not tried."),
    "AIRLINER_FIRE_APU_AGENT": press(P_OVH, "Fire", "APU AGENT", note=_DANGER + "discharges the APU fire bottle. Not tried."),
    "AIRLINER_ENG_START": [
        row(P_PED, "Engines", "ENG START: CRANK", "level", value=0, note=_TIP + "ENGINE START MODE SELECTOR, CRANK / NORM / IGN START. " + _POS),
        row(P_PED, "Engines", "ENG START: NORM", "level", value=1, note=_TIP + "ENGINE START MODE SELECTOR. " + _CHK + "NORMAL after the start. " + _READ + _POS),
        row(P_PED, "Engines", "ENG START: IGN START", "level", value=2, note=_TIP + "ENGINE START MODE SELECTOR. " + _CHK + "IGN/START for the start and the takeoff. " + _POS),
        row(P_PED, "Engines", "ENG START: next position", "latched", positions=3, note=_TIP + "ENGINE START MODE SELECTOR. " + _POS),
    ],
    "AIRLINER_APU_MASTER": latched(P_OVH, "APU", "APU MASTER", note=_TIP + "APU MASTER SWITCH. " + _CHK + "APU MASTER, then APU START 3 s later. "
                                   "The aircraft also has L:INI_APU_MASTER_SWITCH (its panel states), the A320neo V2's variable. " + _TOGGLE),
    "AIRLINER_APU_START": latched(P_OVH, "APU", "APU START", note=_TIP + "APU START. " + _CHK + "at least 3 s after APU MASTER. " + _TOGGLE),
    "AIRLINER_OVH_AIR_APU_BLEED": latched(P_OVH, "Air", "APU BLEED", note=_TIP + "APU BLEED. " + _CHK + "ON after power-up, OFF after the engine start. " + _TOGGLE),
    "AIRLINER_GND_SPOILER": press(P_PED, "Flight controls", "GND SPOILERS (cockpit event, does nothing)",
                                  note=_TIP + "GROUND SPOILERS, \"arm ground spoilers\". Tried on the aircraft (2026-11-06, engines running): the value is "
                                  "back to 0 at once and nothing arms; the cockpit button writes L:A400M_GND_SPOILER instead (use the GND SPOILERS ARM "
                                  "entries). K:SPOILERS_ARM_ON does nothing either."),
    "AIRLINER_CARGO_DOOR_TOGGLE": [
        row(P_PED, "Doors and lights", "CARGO DOOR & RAMP: DOWN", "level", value=0, note=_DANGER + "opens the ramp. " + _POS),
        row(P_PED, "Doors and lights", "CARGO DOOR & RAMP: OFF", "level", value=1, note=_POS),
        row(P_PED, "Doors and lights", "CARGO DOOR & RAMP: UP", "level", value=2, note=_POS),
    ],
    "AIRLINER_RAPID_DEPRESS": latched(P_OVH, "Pressurization", "RAPID DEPRESS", note=_DANGER + "depressurizes the cabin. Not tried."),
    "AIRLINER_DIRECT_LAW": latched(P_OVH, "Flight controls", "DIRECT LAW", note=_DANGER + "switches the flight controls to direct law. Not tried."),
    "LANDING_GEAR_GEAR": latched(P_MAIN, "Landing gear", "Gear lever", note=_TIP + "LANDING GEAR LEVER. Which value is down is not confirmed; "
                                 "the standard GEAR_UP / GEAR_DOWN events are in the list too. Not tried."),
    "LANDING_GEAR_PARKINGBRAKE": latched(P_PED, "Brakes", "PARKING BRAKE", note="Standard event name. Not tried; PARKING_BRAKES (K:) is in the list too."),
    "HANDLING_FLAPS": unknown(P_PED, "Flaps", "Flaps lever (cockpit event)",
                              _CHK + "flaps 1, 2, gear down, then 3, 4 and FULL. The range of this event is not known; use FLAPS_INCR / FLAPS_DECR or the "
                              "flaps axis of the Control list."),
    "HANDLING_X_DIR": unknown(P_SIDE, "Sidestick", "Sidestick X", "No tooltip; the range is not known. Use the aileron axis."),
    "HANDLING_Y_DIR": unknown(P_SIDE, "Sidestick", "Sidestick Y", "No tooltip; the range is not known. Use the elevator axis."),
    "HANDLING_X_DIR_Y_DIR": unknown(P_SIDE, "Sidestick", "Sidestick X and Y", "No tooltip; the range is not known."),
    "AIRLINER_CPT_LS_VOL_BUTTON": press(P_AUDIO, "Audio captain", "CPT MUTE/UNMUTE LS",
                                        note="No tooltip of its own (the captain's LS volume knob has one). " + _PRESS),
    "AIRLINER_AIR_COND_1": latched(P_OVH, "Air", "AIR COND 1", note="No tooltip. " + _TOGGLE),
    "AIRLINER_AIR_COND_2": latched(P_OVH, "Air", "AIR COND 2", note="No tooltip. " + _TOGGLE),
    "AIRLINER_CPT_PILLAR_LIGHT_PAN": knob(P_SIDE, "Console captain", "Pillar light pan", "right", "left", note="No tooltip. " + _KNOB),
    "AIRLINER_CPT_PILLAR_LIGHT_TILT": knob(P_SIDE, "Console captain", "Pillar light tilt", "up", "down", note="No tooltip. " + _KNOB),
    "AIRLINER_CPT_SEAT_TOGGLE_BKREST": latched(P_SIDE, "Console captain", "Seat backrest", note="No tooltip. " + _TOGGLE),
    "AIRLINER_FO_ARMREST_LS": latched(P_SIDE, "Console first officer", "Armrest", note="No tooltip. " + _TOGGLE),
    "AIRLINER_FO_SEAT_ARMREST_R_1": latched(P_SIDE, "Console first officer", "Right armrest", note="No tooltip. " + _TOGGLE),
    "AIRLINER_IFF_MODE": latched(P_PED, "Surveillance", "IFF MODE", note="No tooltip. " + _TOGGLE),
    "DOOR_LOCK_LEFT": latched(P_CABIN, "Doors", "Left door lock", note="No tooltip. " + _TOGGLE),
    "UNKNOWN_CHOCKS": latched(P_CABIN, "Ground equipment", "Wheel chocks", note=_CHK + "PARKING BRAKE/CHOCKS: either the brake set or the chocks. Not tried."),
    "UNKNOWN_DOOR": latched(P_CABIN, "Doors", "Door", note="No tooltip. Not tried."),
})
for _n in ("ELEVATOR_LEFT", "ELEVATOR_RIGHT", "GEAR_SHOCK_FRONT", "GEAR_WHEELS_FRONT", "RUDDER", "WHEEL_L1_HELPER", "WHEEL_L2_HELPER",
           "WHEEL_L3_HELPER", "WHEEL_R1_HELPER", "WHEEL_R2_HELPER", "WHEEL_R3_HELPER", "WING_AILERON_RIGHT", "WING_FLAPS_INNER_LEFT",
           "WING_FLAPS_INNER_RIGHT", "WING_FLAPS_OUTER_LEFT", "WING_FLAPS_OUTER_RIGHT", "WING_SLATS_LEFT", "WING_SLATS_RIGHT",
           "X0_WING_LEFT_CHECKBOX_AILERON_OUTER"):
    _HAND["UNKNOWN_" + _n] = _walk(_n.replace("_", " ").capitalize())


SHARED = _shared_titles()


def classify(name):
    if name in _HAND:
        return _HAND[name]
    return _generic(name)


# --------------------------------------------------------------------------------------------------------------------------------------
# L: variables and calculator code: the iniBuilds variables the A400M shares with the A320neo V2 (all in the A400M's panel states)

_LV = "L: variable of the aircraft (in its panel states), the A320neo V2's name. "


def _lvar(panel, section, function, name, value=None, values="", note=""):
    e = {"panel": panel, "section": section, "function": function, "api": name, "name": name, "kind": "lvar", "type": "LVAR", "rw": "R/W",
         "behavior": "", "note": note}
    if value is not None:
        e["value"] = value
    if values:
        e["values"] = values
    return e


def _calc(panel, section, function, code, note):
    return {"panel": panel, "section": section, "function": function, "api": code, "name": code, "kind": "calc_code", "type": "CALCULATOR CODE",
            "rw": "", "behavior": "", "note": note}


def _baro(side, up):
    s = str(side)
    inc = "++" if up else "--"
    k = "KOHLSMAN_INC" if up else "KOHLSMAN_DEC"
    return ("(L:XMLVAR_Baro%s_Mode) 2 != (L:XMLVAR_Baro%s_Mode) 3 != and if{ (L:XMLVAR_Baro_Selector_HPA_%s) if{ %s (A:KOHLSMAN SETTING MB:%s, mbars) %s 16 * "
            "(>K:2:KOHLSMAN_SET) } els{ %s (>K:%s) } }" % (s, s, s, s, s, inc, s, k))


_BARO_NOTE = ("The A320neo V2's recipe, checked there (+-1 hPa, KOHLSMAN_INC/DEC in inHg, nothing on STD); the A400M has the same L: variables "
              "(xmlvar_baro1_mode, xmlvar_baro_selector_hpa_1 ...). Not tried on the A400M.")

_GS = ("The cockpit's GND SPOILERS button writes this L: variable (seen 2026-11-06: a click set it to 1, the cockpit event and "
       "A:SPOILERS ARMED stayed 0). ")
EXTRA_ENTRIES = [
    dict(_lvar(P_PED, "Flight controls", "GND SPOILERS ARM: toggle", "A400M_GND_SPOILER", values="0|1",
               note=_SEEN + "toggling L:A400M_GND_SPOILER disarmed and then armed the ground spoilers, the cockpit showed both. " + _GS), verified=True),
    _lvar(P_PED, "Flight controls", "GND SPOILERS ARM: on", "A400M_GND_SPOILER", value=1, note=_GS + "Writing it works (the toggle was checked); 1 = armed."),
    _lvar(P_PED, "Flight controls", "GND SPOILERS ARM: off", "A400M_GND_SPOILER", value=0, note=_GS + "Writing it works (the toggle was checked); 0 = disarmed."),
    _lvar(P_OVH, "APU", "APU MASTER: toggle (L:)", "INI_APU_MASTER_SWITCH", values="0|1", note=_LV + "Switch state (1 = on). Not tried."),
    _lvar(P_OVH, "APU", "APU START: toggle (L:)", "INI_APU_START_BUTTON", values="0|1", note=_LV + "Not tried."),
    _lvar(P_OVH, "Air", "APU BLEED: toggle (L:)", "INI_APU_BLEED_BUTTON", values="0|1", note=_LV + "On the A320neo V2 it drives A:BLEED AIR APU. Not tried here."),
    _lvar(P_MAIN, "Autobrake", "AUTO BRK: LO (L:)", "INI_AUTOBRAKE_LEVEL", value=1, values="0..3", note=_LV + "The aircraft also has ini_autobrake_low/med/hi_decel. Not tried."),
    _lvar(P_MAIN, "Autobrake", "AUTO BRK: MED (L:)", "INI_AUTOBRAKE_LEVEL", value=2, values="0..3", note=_LV + "Not tried."),
    _lvar(P_MAIN, "Autobrake", "AUTO BRK: HI/RTO (L:)", "INI_AUTOBRAKE_LEVEL", value=3, values="0..3", note=_LV + "The sim's checklist calls it HI/RTO. Not tried."),
    _lvar(P_GLARE, "Warnings", "MASTER WARNING: push (L:)", "INI_MASTER_WARNING_COMMAND", value=1, note=_LV + "Not tried."),
    _lvar(P_GLARE, "Warnings", "MASTER CAUTION: push (L:)", "INI_MASTER_CAUTION_COMMAND", value=1, note=_LV + "Not tried."),
    _lvar(P_GLARE, "FCU", "SPD knob: up (L:)", "INI_AIRSPEED_DIAL_UP_COMMAND", value=1, note=_LV + "Not tried."),
    _lvar(P_GLARE, "FCU", "SPD knob: down (L:)", "INI_AIRSPEED_DIAL_DN_COMMAND", value=1, note=_LV + "Not tried."),
    _lvar(P_GLARE, "FCU", "A/THR: push (L:)", "AP8_BUTTON", value=1, note=_LV + "Not tried."),
    _lvar(P_GLARE, "EFIS captain", "Baro: pull (STD) (L:)", "INI_1_ALTIMETER_PULL_COMMAND", value=1, note=_LV + "STD on the A320neo V2. Not tried here."),
    _lvar(P_GLARE, "EFIS captain", "Baro: push (QNH) (L:)", "INI_1_ALTIMETER_PUSH_COMMAND", value=1, note=_LV + "QNH on the A320neo V2. Not tried here."),
    _lvar(P_GLARE, "EFIS first officer", "Baro: pull (STD) (L:)", "INI_2_ALTIMETER_PULL_COMMAND", value=1, note=_LV + "STD on the A320neo V2. Not tried here."),
    _lvar(P_GLARE, "EFIS first officer", "Baro: push (QNH) (L:)", "INI_2_ALTIMETER_PUSH_COMMAND", value=1, note=_LV + "QNH on the A320neo V2. Not tried here."),
    _calc(P_GLARE, "EFIS captain", "Baro: up", _baro(1, True), _BARO_NOTE),
    _calc(P_GLARE, "EFIS captain", "Baro: down", _baro(1, False), _BARO_NOTE),
    _calc(P_GLARE, "EFIS first officer", "Baro: up", _baro(2, True), _BARO_NOTE),
    _calc(P_GLARE, "EFIS first officer", "Baro: down", _baro(2, False), _BARO_NOTE),
    _calc(P_GLARE, "EFIS captain", "Baro: STD / QNH toggle",
          "(L:XMLVAR_Baro1_Mode) 3 == if{ 1 (>L:INI_1_ALTIMETER_PUSH_COMMAND) } els{ 1 (>L:INI_1_ALTIMETER_PULL_COMMAND) }",
          "The A320neo V2's recipe, checked there (XMLVAR_Baro1_Mode 3 = STD). Not tried on the A400M."),
    _calc(P_GLARE, "EFIS first officer", "Baro: STD / QNH toggle",
          "(L:XMLVAR_Baro2_Mode) 3 == if{ 1 (>L:INI_2_ALTIMETER_PUSH_COMMAND) } els{ 1 (>L:INI_2_ALTIMETER_PULL_COMMAND) }",
          "The A320neo V2's recipe, checked there (XMLVAR_Baro2_Mode 3 = STD). Not tried on the A400M."),
    _calc(P_GLARE, "FCU", "ALT 100/1000: toggle (L:)", "1 (>L:INI_FCU_ALTITUDE_MODE_COMMAND)",
          "The A320neo V2's command, checked there (flips L:INI_ALTITUDE_STATE); the A400M has both variables. Not tried here."),
]
