#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Probes the cockpit input events (B:) of the aircraft loaded in a running simulator, one control at a time, and records what each one does.

For every event of a `--dump-controls` file the tool sets the event to the opposite of its value, to a very high and to a very low value, reads
the sim's own value back after each, and puts the original value back. What comes back tells the kind of control:

  latched   the value follows what was set and the sim clamps it to 0..N  (a switch with N+1 positions)
  level     the value follows and is clamped to a range other than whole numbers from 0 (a knob, a lever, a brightness)
  press     the value does not follow: a push button that acts on every event and rests at its value
  free      the value follows and is not clamped: the range is not known

Some events are set as a fraction of their range and read back in real units (the altimeter setting: set 0..1, read 955..1060 hPa): a control
that moves but does not take the value it was given is tried again with the fraction that would give the original reading, and is called
"fraction" when that puts it back. Only the sim's own reading of the event is used; whether the aircraft reacts as the name suggests is a separate check. Dangerous controls (engine
start, autostart, magnetos, the G1000 keys that change pages) are skipped unless named by --only.

    tools/probe_controls.py --binary build/msfs-linux-companion --dump dump.json --out probe.json [--only REGEX] [--skip REGEX] [--dry-run]

The aircraft must be on the ground and nobody may touch the cockpit while it runs (a few seconds per control). Every control is restored
right after its probe and once more at the end. Run `msfs-linux-companion --dump-controls dump.json` first.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

HIGH = 100000.0
LOW = -100000.0
# never probed by default: they start the engine, change a display page that cannot be put back, are animation points without a cockpit control,
# or open a door, a window or a hatch (opening the cabin door of a C172 in MSFS 2024 took the pilot out of the cockpit),
# or cannot be undone: the airframe parachute (CAPS) and the emergency autoland (Safe Return) of the Vision Jet; the Cabri's
# PROCEDURE_AUTO_START_SHUTDOWN starts and stops the engine by the sim's procedure
DEFAULT_SKIP = (r"AUTOSTART|AUTO_START|MAGNETO|STARTER|IGNITION|DOOR|WINDOW|HATCH|CANOPY|EXIT|CARGO|STAIR|^UNKNOWN_|CAPS_HANDLE|SAFE_RETURN|"
                r"FIRE_|_AGT|AGENT|_CUTOFF$|ENG_START$|RAPID_DEPRESS|DIRECT_LAW|"
                r"^AS1000_.*(SOFTKEY|PUSH|MENU|FPL|PROC|ENT|CLR|DIRECTTO|RANGE)|_(MFD|PFD|MIDCOM)$")
WAIT_MS = 1200


def fmt(v):
    return ("%.6f" % v).rstrip("0").rstrip(".") if v != int(v) or abs(v) >= 1e15 else str(int(v))


def make_script(events, skip, only):
    lines, probed = [], []
    for e in events:
        n, v0 = e["name"], e.get("value")
        if e.get("text") or v0 is None:
            continue
        if only and not re.search(only, n):
            continue
        if not only and skip and re.search(skip, n):
            continue
        flip = 1.0 - v0 if v0 in (0.0, 1.0) else v0 + 1.0
        probed.append(e)
        lines.append("# %s  (rests at %s)" % (n, fmt(v0)))
        for tag, v in (("flip", flip), ("high", HIGH), ("low", LOW)):
            lines.append("set %s %s" % (n, fmt(v)))
            lines.append("expectb %s == %s %d" % (n, fmt(v), WAIT_MS))
        lines.append("set %s %s" % (n, fmt(v0)))
        lines.append("expectb %s == %s %d" % (n, fmt(v0), WAIT_MS))
    lines.append("# the final restore, in case a control was moved again by something else")
    for e in probed:
        lines.append("set %s %s" % (e["name"], fmt(e["value"])))
    return "\n".join(lines) + "\n", probed


LINE = re.compile(r"^(ok:  |FAIL:) B:(\S+) (==|!=|>=|<=|>|<) (\S+) \((?:is )?(\S+?)\)")


def parse(output, probed):
    """The expectations come back in the order they were written: flip, high, low, restore for each probed event."""
    results = {e["name"]: {"name": e["name"], "initial": e["value"]} for e in probed}
    seen = {}
    for line in output.splitlines():
        m = LINE.match(line)
        if not m:
            continue
        name, requested = m.group(2), float(m.group(4))
        try:
            actual = float(m.group(5).rstrip(")"))
        except ValueError:
            actual = None
        r = results.get(name)
        if r is None:
            continue
        k = seen.get(name, 0)
        seen[name] = k + 1
        tag = ("flip", "high", "low", "restore")[k] if k < 4 else None
        if tag:
            r[tag] = {"requested": requested, "actual": actual}
    for r in results.values():
        r.update(classify(r))
    return list(results.values())


def fraction_script(results):
    """Second pass: a control that moved but did not come back is given the fraction of its range that matches the reading it started at."""
    lines, again = [], []
    for r in results:
        hi, lo = (r.get("high") or {}).get("actual"), (r.get("low") or {}).get("actual")
        if r["restored"] or hi is None or lo is None or abs(hi - lo) < 1e-9 or abs(hi - HIGH) < 1 or abs(lo - LOW) < 1:
            continue
        f = (r["initial"] - lo) / (hi - lo)
        if not 0.0 <= f <= 1.0:
            continue
        again.append((r, f))
        lines.append("set %s %s" % (r["name"], fmt(f)))
        lines.append("expectb %s == %s %d" % (r["name"], fmt(r["initial"]), 2500))
    return "\n".join(lines) + "\n", again


def classify(r):
    flip, high, low, back = r.get("flip"), r.get("high"), r.get("low"), r.get("restore")
    out = {"restored": bool(back and back["actual"] is not None and abs(back["actual"] - r["initial"]) < 1e-6)}
    if not flip or flip["actual"] is None:
        out["kind"] = "unread"
        return out
    follows = abs(flip["actual"] - flip["requested"]) < 1e-6
    if not follows:
        out["kind"] = "press"  # the sim did not take the value: it rests where it was (push button, or a control that is inert here)
        return out
    hi = high["actual"] if high else None
    lo = low["actual"] if low else None
    if hi is None or lo is None or abs(hi - HIGH) < 1 or abs(lo - LOW) < 1:
        out["kind"] = "free"  # not clamped: the range is not known
        out["note_range"] = [lo, hi]
        return out
    out["range"] = [lo, hi]
    whole = abs(lo - round(lo)) < 1e-9 and abs(hi - round(hi)) < 1e-9
    if whole and lo == 0 and 1 <= hi <= 12:
        out["kind"] = "latched"
        out["positions"] = int(hi) + 1
    else:
        out["kind"] = "level"
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", default="build/msfs-linux-companion")
    ap.add_argument("--dump", required=True, help="file written by msfs-linux-companion --dump-controls")
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", help="probe only the events whose name matches this regular expression (also the dangerous ones)")
    ap.add_argument("--skip", default=DEFAULT_SKIP, help="regular expression of events that are not probed")
    ap.add_argument("--dry-run", action="store_true", help="write the script and print it, do not run it")
    ap.add_argument("--config-dir", help="scratch directory for the program's own config and logs")
    ap.add_argument("--binary-args", default="", help="extra arguments for the program, for example \"--port 12345\" (the mock simulator)")
    args = ap.parse_args()

    with open(args.dump) as f:
        dump = json.load(f)
    script, probed = make_script(dump["input_events"], args.skip, args.only)
    print("%s: %d of %d events to probe" % (dump.get("aircraft"), len(probed), len(dump["input_events"])), file=sys.stderr)
    work = args.config_dir or tempfile.mkdtemp(prefix="probe-")
    os.makedirs(os.path.join(work, "state"), exist_ok=True)
    path = os.path.join(work, "probe.script")
    with open(path, "w") as f:
        f.write(script)
    if args.dry_run:
        sys.stdout.write(script)
        return 0
    env = dict(os.environ, XDG_STATE_HOME=os.path.join(work, "state"), XDG_CONFIG_HOME=os.path.join(work, "cfg"))
    cp = subprocess.run([args.binary, "--config", os.path.join(work, "none.json")] + args.binary_args.split() + ["--script", path], env=env, capture_output=True, text=True,
                        timeout=60 + 6 * len(probed))
    with open(os.path.join(work, "probe.out"), "w") as f:  # what the program printed, for checking a result by hand
        f.write(cp.stdout + cp.stderr)
    results = parse(cp.stdout, probed)
    script2, again = fraction_script(results)
    if again:
        path2 = os.path.join(work, "restore.script")
        with open(path2, "w") as f:
            f.write(script2)
        cp2 = subprocess.run([args.binary, "--config", os.path.join(work, "none.json")] + args.binary_args.split() + ["--script", path2], env=env,
                             capture_output=True, text=True, timeout=60 + 6 * len(again))
        with open(os.path.join(work, "restore.out"), "w") as f:
            f.write(cp2.stdout + cp2.stderr)
        for line in cp2.stdout.splitlines():
            m = LINE.match(line)
            if m and m.group(1).startswith("ok"):
                for r, f in again:
                    if r["name"] == m.group(2):
                        r.update({"restored": True, "kind": "fraction", "set_range": [0.0, 1.0], "read_range": [r["low"]["actual"], r["high"]["actual"]]})
    doc = {"aircraft": dump.get("aircraft"), "sim": dump.get("sim"), "results": results}
    with open(args.out, "w") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")
    kinds = {}
    for r in results:
        kinds[r["kind"]] = kinds.get(r["kind"], 0) + 1
    bad = [r["name"] for r in results if not r["restored"]]
    print("probed %d events: %s -> %s" % (len(results), kinds, args.out), file=sys.stderr)
    if bad:
        print("NOT confirmed back at their original value: %s" % ", ".join(bad), file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
