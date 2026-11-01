#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""tools/probe_controls.py: how the readings of a probe turn into the kind of a control, and which controls are left alone."""
import importlib.util
import os
import sys

root = sys.argv[1] if len(sys.argv) > 1 else "."
spec = importlib.util.spec_from_file_location("probe_controls", os.path.join(root, "tools", "probe_controls.py"))
pc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pc)

fails = 0


def check(cond, what):
    global fails
    if not cond:
        print("FAIL:", what)
        fails += 1


def result(initial, flip, high, low, back=None):
    r = {"initial": initial, "flip": {"requested": 1 - initial if initial in (0, 1) else initial + 1, "actual": flip},
         "high": {"requested": pc.HIGH, "actual": high}, "low": {"requested": pc.LOW, "actual": low},
         "restore": {"requested": initial, "actual": initial if back is None else back}}
    r.update(pc.classify(r))
    return r


# a two-position switch: follows, clamps to 0..1
r = result(0.0, 1.0, 1.0, 0.0)
check(r["kind"] == "latched" and r["positions"] == 2 and r["restored"], "two-position switch")
# a four-position switch clamps to 0..3
r = result(0.0, 1.0, 3.0, 0.0)
check(r["kind"] == "latched" and r["positions"] == 4, "four-position switch")
# a brightness knob: 0..100
r = result(10.0, 11.0, 100.0, 0.0)
check(r["kind"] == "level" and r["range"] == [0.0, 100.0], "level with a range")
# a push button that rests where it was
r = result(0.0, 0.0, 0.0, 0.0)
check(r["kind"] == "press", "push button")
# the sim keeps whatever it is given: the range is not known
r = result(0.0, 1.0, pc.HIGH, pc.LOW)
check(r["kind"] == "free", "not clamped")
# a control that did not come back to its value is reported
r = result(1.0, 0.0, 1.0, 0.0, back=0.0)
check(not r["restored"], "not restored is noticed")

# which controls are probed: the dangerous ones and the display keys are not
events = [{"name": n, "value": 0.0} for n in ("LIGHTING_LANDING_1", "PROCEDURE_AUTOSTART", "ENGINE_MAGNETO_1", "AS1000_SOFTKEY_1_PFD", "UNKNOWN_CHOCKS", "HANDLING_FLAPS")]
script, probed = pc.make_script(events, pc.DEFAULT_SKIP, None)
check([e["name"] for e in probed] == ["LIGHTING_LANDING_1", "HANDLING_FLAPS"], "default skip list: %s" % [e["name"] for e in probed])
# doors and windows are left alone: opening one took the pilot out of the cockpit in MSFS 2024
doors = [{"name": n, "value": 0.0} for n in ("INSTRUMENT_DOOR_LEFT", "AIRCRAFT_DOOR_WINDOW_PILOT", "UNKNOWN_DOOR_HANDLE_1", "CANOPY_1", "LIGHTING_NAV_0")]
check([e["name"] for e in pc.make_script(doors, pc.DEFAULT_SKIP, None)[1]] == ["LIGHTING_NAV_0"], "doors, windows and canopies are not probed")
# the parachute handle and the emergency autoland of the Vision Jet cannot be put back
risky = [{"name": n, "value": 0.0} for n in ("SF50_CAPS_HANDLE", "SF50_SAFE_RETURN_ACTIVATE_BUTTON", "SF50_SAFE_RETURN_AUTOPILOT_INTERCEPTS", "SF50_CAPS_SAFETY_PIN")]
check([e["name"] for e in pc.make_script(risky, pc.DEFAULT_SKIP, None)[1]] == ["SF50_CAPS_SAFETY_PIN"], "CAPS handle and Safe Return are not probed")
# a control that moves but does not take the value (altimeter: set 0..1, read 955..1060 hPa) gets its fraction
alt = {"name": "INSTRUMENT_ALTIMETER_CALIBRATION_0", "initial": 1013.25, "restored": False, "high": {"requested": pc.HIGH, "actual": 1060.0},
       "low": {"requested": pc.LOW, "actual": 955.0}}
sc, again = pc.fraction_script([alt])
check(len(again) == 1 and abs(again[0][1] - 0.5547619) < 1e-5 and "set INSTRUMENT_ALTIMETER_CALIBRATION_0 0.554762" in sc, "fraction restore: %s" % sc)
check(pc.fraction_script([dict(alt, restored=True)])[1] == [], "a restored control needs no second pass")
script, probed = pc.make_script(events, pc.DEFAULT_SKIP, "MAGNETO")
check([e["name"] for e in probed] == ["ENGINE_MAGNETO_1"], "--only names a dangerous control on purpose")
check("set LIGHTING_LANDING_1" not in script, "only the named control is in the script")
# every probe is followed by the restore of the original value
script, probed = pc.make_script([{"name": "FUEL_PUMP_1", "value": 1.0}], pc.DEFAULT_SKIP, None)
lines = [l for l in script.splitlines() if l.startswith("set ")]
check(lines[0] == "set FUEL_PUMP_1 0" and lines[-1] == "set FUEL_PUMP_1 1" and lines[-2] == "set FUEL_PUMP_1 1", "flip first, restore after the probe and at the end: %s" % lines)

# the output of the script is read back in the order it was written
out = "\n".join(["ok:   B:FUEL_PUMP_1 == 0 (is 0)", "FAIL: B:FUEL_PUMP_1 == 100000 (is 1)  [line 5]", "FAIL: B:FUEL_PUMP_1 == -100000 (is 0)  [line 7]", "ok:   B:FUEL_PUMP_1 == 1 (is 1)"])
res = pc.parse(out, probed)
check(res[0]["kind"] == "latched" and res[0]["restored"], "parsed script output: %s" % res)

print("%d check(s) failed" % fails if fails else "all probe checks passed")
sys.exit(1 if fails else 0)
