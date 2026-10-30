#!/usr/bin/env python3
"""Build data/standard_catalog.json: the K: (SimConnect) events that cockpits of MSFS 2020 aircraft actually fire.

Reads an MSFS installation (read-only) and keeps NAMES ONLY, no text or files are copied:
  * every `(>K:NAME)` found in the model behaviour XML and instrument code of the Official packages
  * which of those names the MSFS controls menu also offers (the input profiles in <MSFS>/Input), so the catalog can
    mark the "cockpit only" ones, the controls you cannot bind in the menu

Usage:
  tools/build_standard_catalog.py --packages ".../Packages/Official/Steam" --input ".../MicrosoftFlightSimulator/Input"
Run it with python3 -I. Output defaults to data/standard_catalog.json.
"""
import argparse
import json
import os
import re
import sys

K_REF = re.compile(rb"(?:&gt;|>)K:([A-Za-z0-9_]+)")
NAME_OK = re.compile(r"[A-Z][A-Z0-9_]{3,}")
ACTION = re.compile(rb'ActionName="([A-Za-z0-9_]+)"')

# (category, tokens that put an event into it); the first matching category wins. Tokens match whole words
# (COM1 matches COM), so FLIGHT_DIRECTOR does not land in LIGHTS.
CATEGORIES = [
    ("Autopilot and flight director", {"AP", "AUTOPILOT", "FLIGHT_DIRECTOR", "HEADING", "YAW", "AUTOTHROTTLE", "AUTO", "VS", "ALT", "SPD"}),
    ("Landing gear and brakes", {"GEAR", "BRAKE", "BRAKES", "PARKING", "AUTOBRAKE", "TAILHOOK", "WHEEL", "STEERING", "TAILWHEEL"}),
    ("Flaps and spoilers", {"FLAPS", "FLAP", "SPOILER", "SPOILERS", "AIRBRAKE", "SLATS"}),
    ("Ice, air and hydraulics", {"ANTI", "ICE", "DEICE", "PRESSURIZATION", "BLEED", "HYDRAULIC", "PITOT", "WINDSHIELD", "PROBE", "STRUCTURAL", "PNEUMATIC", "AIR"}),
    ("Engines", {"THROTTLE", "MIXTURE", "PROP", "PROPELLER", "STARTER", "IGNITION", "MAGNETO", "ENGINE", "TURBINE", "COWLFLAP", "ENG", "REVERSE",
                 "AFTERBURNER", "JET", "COLLECTIVE", "ROTOR"}),
    ("Fuel", {"FUEL", "FUELSYSTEM", "CROSSFEED", "PUMP"}),
    ("Electrical", {"ELECTRICAL", "BATTERY", "ALTERNATOR", "GENERATOR", "AVIONICS", "CIRCUIT", "BREAKER", "EXTERNAL", "INVERTER", "MASTER", "APU"}),
    ("Radios and navigation", {"COM", "NAV", "ADF", "DME", "XPNDR", "TRANSPONDER", "RADIO", "VOR", "ILS", "MARKER", "INTERCOM", "AUDIO", "PILOT",
                               "COPILOT", "GPS", "KOHLSMAN", "BAROMETRIC", "ATC", "TACAN", "OBS", "RMI", "SPEAKER", "ICS"}),
    ("Lights", {"LIGHT", "LIGHTS", "LIGHTING", "STROBE", "STROBES", "BEACON", "LANDING", "TAXI", "POTENTIOMETER", "PANEL"}),
    ("Flight controls and trim", {"TRIM", "ELEVATOR", "AILERON", "RUDDER", "ELEV"}),
    ("Doors and cabin", {"DOOR", "EXIT", "SMOKING", "SEATBELTS", "SEAT", "CANOPY", "HOOK", "CARGO", "WINDOW", "RETRACTABLE", "WATER", "WING", "CABIN"}),
]
CATEGORY_ORDER = [c for c, _ in CATEGORIES] + ["Other"]


def categorize(name):
    tokens = [re.sub(r"\d+$", "", t) or t for t in name.split("_")]
    for cat, words in CATEGORIES:
        if any(t in words for t in tokens):
            return cat
    return "Other"


def walk(root, exts):
    for d, _, files in os.walk(root):
        for f in files:
            if f.lower().endswith(exts):
                yield os.path.join(d, f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--packages", required=True, help="the Official/<store> folder with the asobo-*/fs-base-* packages")
    ap.add_argument("--input", help="the MSFS 'Input' folder (controls menu profiles), optional")
    ap.add_argument("-o", "--output", default="data/standard_catalog.json")
    args = ap.parse_args()

    names = {}
    scanned = 0
    for path in walk(args.packages, (".xml", ".js")):
        try:
            data = open(path, "rb").read()
        except OSError:
            continue
        scanned += 1
        for m in K_REF.finditer(data):
            n = m.group(1).decode("ascii")
            names[n] = names.get(n, 0) + 1
    kept = {n: c for n, c in names.items() if NAME_OK.fullmatch(n) and not n.endswith("_") and not n.isdigit()}
    print(f"scanned {scanned} files: {len(names)} names, {len(kept)} after dropping macro fragments", file=sys.stderr)

    menu = set()
    if args.input:
        for path in walk(args.input, (".xml",)):
            for m in ACTION.finditer(open(path, "rb").read()):
                a = m.group(1).decode("ascii")
                menu.add(a[4:] if a.startswith("KEY_") else a)
        print(f"{len(menu)} controls menu actions", file=sys.stderr)

    entries = []
    for name in sorted(kept, key=lambda n: (CATEGORY_ORDER.index(categorize(n)), n)):
        in_menu = name in menu
        cockpit_only = bool(menu) and not in_menu
        entries.append({
            "panel": "Standard events",
            "section": categorize(name),
            "function": name.replace("_", " ").title(),
            "api": name, "name": name, "kind": "sim_event",
            "values": "parameter" if name.endswith(("_SET", "_VAR_SET")) else "",
            "rw": "", "type": "K: event",
            "remark": ("cockpit only: not in the MSFS controls menu" if cockpit_only else ""),
            "standard": True, "cockpit_only": cockpit_only,
        })
    doc = {"aircraft": "STANDARD", "title": "Standard sim events (any aircraft)", "entries": entries}
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")
    only = sum(1 for e in entries if e["cockpit_only"])
    print(f"wrote {len(entries)} events ({only} cockpit-only) to {args.output}", file=sys.stderr)
    by = {}
    for e in entries:
        by[e["section"]] = by.get(e["section"], 0) + 1
    print(by, file=sys.stderr)


if __name__ == "__main__":
    main()
