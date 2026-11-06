#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Builds a curated catalog (data/<id>_catalog.json) for an aircraft whose controls come from the simulator itself.

The cockpit input events (B:) of an aircraft cannot be documented offline: the list is whatever the sim reports
(`msfs-linux-companion --dump-controls FILE` writes it). This tool turns such a dump into a hand-quality catalog:
every event is given a panel, a section, a readable function name and a behaviour by the rules module of the aircraft
(tools/aircraft_rules/<module>.py). It refuses to produce a catalog when an event of the dump has no rule, so a new
aircraft version that adds controls shows up as an error instead of as an unlabeled entry.

    tools/build_aircraft_catalog.py asobo_cj4 [--dump FILE] [--out FILE]
"""
import argparse
import importlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, "aircraft_rules"))

BEHAVIORS = {"latched", "knob_up", "knob_down", "press", "level", ""}


def build(rules, names):
    entries, unmatched = [], []
    for name in names:
        rows = rules.classify(name)
        if not rows:
            unmatched.append(name)
            continue
        for row in rows:
            e = {
                "panel": row["panel"], "section": row["section"], "function": row["function"],
                "api": name, "name": name, "kind": "input_event",
                "type": "COCKPIT INPUT EVENT", "rw": "R/W",
                "behavior": row.get("behavior", ""),
            }
            assert e["behavior"] in BEHAVIORS, (name, e["behavior"])
            if e["behavior"] == "latched" and row.get("positions", 2) != 2:
                e["positions"] = row["positions"]
            if "value" in row:
                e["value"] = row["value"]
            if "axis_range" in row:
                e["axis_range"] = row["axis_range"]
            if row.get("remark"):
                e["remark"] = row["remark"]
            ev = rules.VERIFIED.get(name)
            if ev and row.get("verifiable", True):
                e["verified"] = True
                e["note"] = ev
            elif row.get("note"):
                e["note"] = row["note"]
            elif e["behavior"]:
                e["note"] = "Behaviour taken from the control's name, not tried on a real aircraft."
            entries.append(e)
    return entries, unmatched


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("module", help="rules module in tools/aircraft_rules, e.g. asobo_cj4")
    ap.add_argument("--dump", help="events file (default: the one stored next to the rules)")
    ap.add_argument("--out", help="output file (default: data/<module>_catalog.json)")
    args = ap.parse_args()

    rules = importlib.import_module(args.module)
    dump_path = args.dump or os.path.join(HERE, "aircraft_rules", rules.EVENTS_FILE)
    with open(dump_path) as f:
        dump = json.load(f)
    names = sorted(dump["input_events"])
    entries, unmatched = build(rules, names)
    if unmatched:
        sys.exit("no rule for %d event(s): %s" % (len(unmatched), ", ".join(unmatched)))

    # entries that are not cockpit input events: K: events and L: variables the aircraft's model or checklist uses (rules.EXTRA_ENTRIES)
    for e in getattr(rules, "EXTRA_ENTRIES", []):
        assert e["behavior"] in BEHAVIORS and e["kind"] in ("sim_event", "lvar", "calc_code"), e
        entries.append(e)

    order = {p: i for i, p in enumerate(rules.PANEL_ORDER)}
    entries.sort(key=lambda e: (order.get(e["panel"], 99), e["section"], e["function"], e["behavior"] != "knob_up"))
    out = {"aircraft": rules.AIRCRAFT, "title": rules.TITLE, "sources": rules.SOURCES, "entries": entries}
    if getattr(rules, "AXIS_CONTROLS", False):
        out["axis_controls"] = True  # the program adds the ranged axis events every aircraft has (throttle, mixture, brakes, surfaces ...)
    if getattr(rules, "STANDARD_EVENTS", False):
        out["standard_events"] = True  # and every standard event (gear, flaps, parking brake ...), as for the A320neo V2
    out_path = args.out or os.path.join(ROOT, "data", "%s_catalog.json" % args.module)
    with open(out_path, "w") as f:
        json.dump(out, f, indent=1)
        f.write("\n")
    verified = sum(1 for e in entries if e.get("verified"))
    print("%s: %d events -> %d entries (%d verified on the real sim) -> %s" % (rules.AIRCRAFT, len(names), len(entries), verified, out_path))


if __name__ == "__main__":
    main()
