#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Builds data/a32nx_keypads.json: where each key of the FlyByWire A32NX MCDU sits on the panel.

The positions are not drawn by hand: they come from the 3D model the simulator loads, read-only, from the installed
aircraft package (`flybywire-aircraft-a320-neo`):

* SimObjects/AirPlanes/FlyByWire_A320_NEO/model/A320_NEO_INTERIOR_LOD00.gltf: the nodes `PUSH_MCDUL_<key>` hold the position
  of every key of the captain's MCDU and their meshes the size of the key cap.
* ModelBehaviorDefs/A32NX/generated/A32NX_Interior_MCDU.xml: the list of keys, in groups, and the H event each one sends
  (`A320_Neo_CDU_1_BTN_<key>`; 2 for the first officer's unit).

Positions are projected onto the plane of the keys (the panel is tilted), oriented so that the line select keys L1..L6
run down the left side, and written in millimetres from the top left corner of the key area. The printed text of a key is
not in the model: `LABELS` gives the wording the guides use.

    tools/build_keypad_layout.py PACKAGE_DIR [--out data/a32nx_keypads.json] [--preview FILE.png]
"""
import argparse
import json
import math
import re
import sys
from pathlib import Path

MODEL = "SimObjects/AirPlanes/FlyByWire_A320_NEO/model/A320_NEO_INTERIOR_LOD00.gltf"
BEHAVIOR = "ModelBehaviorDefs/A32NX/generated/A32NX_Interior_MCDU.xml"

# Wording of the keys in the guides (FlyByWire documentation); everything else prints its own id.
LABELS = {
    "FPLN": "F-PLN", "RAD": "RAD NAV", "FUEL": "FUEL PRED", "SEC": "SEC F-PLN", "ATC": "ATC COMM", "MENU": "MCDU MENU",
    "DOT": ".", "PLUSMINUS": "+/-", "SLASH": "/", "UARROW": "UP", "DARROW": "DOWN", "LARROW": "LEFT", "RARROW": "RIGHT",
}
for _n in range(1, 7):
    LABELS[f"L{_n}"] = f"{_n}L"
    LABELS[f"R{_n}"] = f"{_n}R"

GROUPS = {
    "FBW_MCDU_LINE_SELECT_KEYS_Template": "line select",
    "FBW_MCDU_PAGE_KEYS_Template": "page",
    "FBW_MCDU_SLEW_KEYS_Template": "slew",
    "FBW_MCDU_NUMPAD_KEYS_Template": "digit",
    "FBW_MCDU_ALPHABET_KEYS_Template": "alpha",
}


def key_groups(package: Path) -> dict:
    """key id -> group name, from the behaviour templates (the keys the simulator actually wires up)."""
    xml = (package / BEHAVIOR).read_text(encoding="utf-8")
    out = {}
    for m in re.finditer(r'<Template Name="(FBW_MCDU_[A-Z_]+_KEYS_Template)">(.*?)</Template>', xml, re.S):
        group = GROUPS.get(m.group(1))
        if group:
            for name in re.findall(r"<BASE_NAME>([^<]*)</BASE_NAME>", m.group(2)):
                out[name] = group
    return out


def key_nodes(package: Path, wanted: dict) -> dict:
    g = json.loads((package / MODEL).read_text(encoding="utf-8"))
    meshes, acc = g["meshes"], g["accessors"]
    found = {}
    for n in g["nodes"]:
        m = re.fullmatch(r"PUSH_MCDUL_(.+)", n.get("name", ""))
        if not m or m.group(1) not in wanted or "mesh" not in n:
            continue
        lo, hi = [1e9] * 3, [-1e9] * 3
        for p in meshes[n["mesh"]]["primitives"]:
            a = acc[p["attributes"]["POSITION"]]
            for k in range(3):
                lo[k], hi[k] = min(lo[k], a["min"][k]), max(hi[k], a["max"][k])
        found[m.group(1)] = (n["translation"], [hi[k] - lo[k] for k in range(3)])
    return found


def project(found: dict) -> dict:
    """3D key positions -> (u, v, w, h) in metres on the key plane, u to the right, v downward."""
    pts = {k: v[0] for k, v in found.items()}
    my = sum(p[1] for p in pts.values()) / len(pts)
    mz = sum(p[2] for p in pts.values()) / len(pts)
    syy = sum((p[1] - my) ** 2 for p in pts.values())
    szz = sum((p[2] - mz) ** 2 for p in pts.values())
    syz = sum((p[1] - my) * (p[2] - mz) for p in pts.values())
    a = 0.5 * math.atan2(2 * syz, syy - szz)  # direction of the panel in the (y, z) plane
    dy, dz = math.cos(a), math.sin(a)
    uv = {k: (p[0], p[1] * dy + p[2] * dz) for k, p in pts.items()}
    flip_u = uv["L1"][0] > uv["R1"][0]  # the line select keys L* are on the left of the screen
    flip_v = uv["L1"][1] > uv["L6"][1]
    return {k: ((-u if flip_u else u), (-v if flip_v else v), found[k][1][0], found[k][1][2]) for k, (u, v) in uv.items()}


def build(package: Path) -> dict:
    groups = key_groups(package)
    found = key_nodes(package, groups)
    missing = sorted(set(groups) - set(found))
    if missing:
        sys.exit(f"keys of the behaviour file without a node in the model: {missing}")
    pos = project(found)
    left = min(u - w / 2 for u, v, w, h in pos.values())
    top = min(v - h / 2 for u, v, w, h in pos.values())
    keys = []
    for k in sorted(pos, key=lambda k: (pos[k][1], pos[k][0])):
        u, v, w, h = pos[k]
        keys.append({"id": k, "label": LABELS.get(k, k), "group": groups[k],
                     "x": round((u - w / 2 - left) * 1000, 1), "y": round((v - h / 2 - top) * 1000, 1),
                     "w": round(w * 1000, 1), "h": round(h * 1000, 1)})
    width = max(k["x"] + k["w"] for k in keys)
    height = max(k["y"] + k["h"] for k in keys)
    return {
        "pads": [{
            "id": "a32nx_mcdu", "aircraft": "A32NX", "title": "MCDU keys (A32NX)", "unit": "mm",
            "width": round(width, 1), "height": round(height, 1),
            "h_event_prefix": "A320_Neo_CDU_1_BTN_",
            "sources": [
                "flybywire-aircraft-a320-neo/" + MODEL + ": position and size of the nodes PUSH_MCDUL_<key>",
                "flybywire-aircraft-a320-neo/" + BEHAVIOR + ": the key list and the H events (A320_Neo_CDU_1_BTN_<key>)",
                "tools/build_keypad_layout.py: projection onto the plane of the keys"],
            "keys": keys,
        }]
    }


def preview(pad: dict, path: str) -> None:
    from PIL import Image, ImageDraw
    s = 6
    img = Image.new("RGB", (int(pad["width"] * s) + 20, int(pad["height"] * s) + 20), (20, 20, 24))
    d = ImageDraw.Draw(img)
    colors = {"line select": (70, 110, 170), "page": (90, 140, 90), "slew": (170, 130, 60), "digit": (120, 120, 130), "alpha": (80, 80, 90)}
    for k in pad["keys"]:
        x, y = 10 + k["x"] * s, 10 + k["y"] * s
        d.rectangle([x, y, x + k["w"] * s, y + k["h"] * s], fill=colors[k["group"]], outline=(220, 220, 220))
        d.text((x + 2, y + 2), k["label"][:6], fill=(255, 255, 255))
    img.save(path)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("package", type=Path, help="installed flybywire-aircraft-a320-neo package folder")
    ap.add_argument("--out", type=Path, default=Path(__file__).resolve().parent.parent / "data" / "a32nx_keypads.json")
    ap.add_argument("--preview", help="also write a PNG of the layout (needs Pillow)")
    args = ap.parse_args()
    doc = build(args.package)
    args.out.write_text(json.dumps(doc, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    pad = doc["pads"][0]
    print(f"{args.out}: {len(pad['keys'])} keys, {pad['width']} x {pad['height']} mm")
    if args.preview:
        preview(pad, args.preview)


if __name__ == "__main__":
    main()
