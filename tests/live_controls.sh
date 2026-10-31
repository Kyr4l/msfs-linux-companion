#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
# Usage: live_controls.sh <binary> <repo root>
# Runs the real binary against tools/mock_simconnect.py posing as the CJ4 (whose cockpit is "still loading" for the first
# enumeration): lists the cockpit controls, sets, toggles and cycles input events, and reads them back.
set -u
export MSFS_LINUX_COMPANION_ISOLATED=1  # never probe a simulator that happens to be running, never open the real joysticks or Winwing devices
BIN="$1"; ROOT="$2"
TMP="$(mktemp -d)"; MOCKPID=""
export XDG_STATE_HOME="$TMP/state"  # session logs of the test runs stay out of the real state directory
cleanup() { [ -n "$MOCKPID" ] && kill "$MOCKPID" 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
fail=0
ok()   { echo "ok:   $1"; }
bad()  { echo "FAIL: $1"; fail=1; }

PORT=$((20000 + RANDOM % 20000))
python3 -I -B "$ROOT/tools/mock_simconnect.py" --port "$PORT" --aircraft Asobo_CJ4 --late 1 >"$TMP/mock.log" 2>&1 &
MOCKPID=$!
for _ in $(seq 1 50); do grep -q listening "$TMP/mock.log" 2>/dev/null && break; sleep 0.1; done
grep -q listening "$TMP/mock.log" || { echo "FAIL: mock did not start"; cat "$TMP/mock.log"; exit 1; }

run() { timeout 90 "$BIN" --config "$TMP/none.json" --port "$PORT" -q "$@" 2>&1; }

out="$(run --list-controls)"
echo "$out" | grep -q '^aircraft: Asobo_CJ4 (Asobo CJ4)'      && ok "aircraft identified"            || bad "aircraft id: $out"
echo "$out" | grep -q 'cockpit input events (B:): 8'          && ok "8 input events after the retry"  || bad "event count: $out"
echo "$out" | grep -q '  B:LIGHTING_LANDING_1'                && ok "event names listed"              || bad "names"
echo "$out" | grep -q 'L: variables: 5'                       && ok "L: variables listed"             || bad "lvars: $out"

[ "$(run --read-input LIGHTING_LANDING_1 | tail -1)" = "B:LIGHTING_LANDING_1 = 0" ] && ok "initial value read" || bad "initial read"
[ "$(run --fire input_event:LIGHTING_LANDING_1=1 --read-input LIGHTING_LANDING_1 | tail -1)" = "B:LIGHTING_LANDING_1 = 1" ] && ok "set"      || bad "set"
# MSFS 2024 spells the input events in upper case where 2020 mixes the case: a name written either way finds the control
[ "$(run --fire input_event:Lighting_Landing_1=0 --read-input lighting_landing_1 | tail -1)" = "B:lighting_landing_1 = 0" ] && ok "event names are found ignoring the case" || bad "case-insensitive names"
[ "$(run --fire input_event:lighting_landing_1=1 --read-input LIGHTING_LANDING_1 | tail -1)" = "B:LIGHTING_LANDING_1 = 1" ] && ok "a lower-case name sets the control" || bad "lower-case set"
[ "$(run --fire input_event_toggle:LIGHTING_LANDING_1 --read-input LIGHTING_LANDING_1 | tail -1)" = "B:LIGHTING_LANDING_1 = 0" ] && ok "toggle 1 -> 0" || bad "toggle off"
[ "$(run --fire input_event_toggle:LIGHTING_LANDING_1 --read-input LIGHTING_LANDING_1 | tail -1)" = "B:LIGHTING_LANDING_1 = 1" ] && ok "toggle 0 -> 1" || bad "toggle on"
[ "$(run --fire input_event:ELECTRICAL_BATTERY_1=2 --fire input_event_toggle:ELECTRICAL_BATTERY_1=3 --read-input ELECTRICAL_BATTERY_1 | tail -1)" = "B:ELECTRICAL_BATTERY_1 = 0" ] && ok "3-position cycle wraps 2 -> 0" || bad "cycle"
# two toggles of the same event in one session must both take effect (the second waits for the first answer): 0 -> 1 -> 0
[ "$(run --fire input_event_toggle:FUEL_BOOST_PUMP_1 --fire input_event_toggle:FUEL_BOOST_PUMP_1 --read-input FUEL_BOOST_PUMP_1 | tail -1)" = "B:FUEL_BOOST_PUMP_1 = 0" ] && ok "back-to-back toggles do not read a stale value" || bad "back-to-back toggle"
[ "$(run --fire input_event_toggle:FUEL_BOOST_PUMP_1 --fire input_event_toggle:FUEL_BOOST_PUMP_1 --fire input_event_toggle:FUEL_BOOST_PUMP_1 --read-input FUEL_BOOST_PUMP_1 | tail -1)" = "B:FUEL_BOOST_PUMP_1 = 1" ] && ok "three in a row: 0 -> 1 -> 0 -> 1" || bad "three toggles"
[ "$(run --fire input_event:TRIM_AILERON=0.25 --read-input TRIM_AILERON | tail -1)" = "B:TRIM_AILERON = 0.25" ] && ok "fractional value" || bad "fractional"
timeout 90 "$BIN" --config "$TMP/none.json" --port "$PORT" --fire input_event:NOT_AN_EVENT=1 2>&1 | grep -q 'is not an input event of the loaded aircraft' && ok "unknown event refused with a message" || bad "unknown event"
# Watched values while the set changes under a running connection (what a guide does on every page). Every step must deliver
# all its values, each value must be the one of ITS code (never the old content of the slot, which the mock leaves in
# place until a moment after the Add), and a code that comes back keeps its value.
cat > "$TMP/rewatch.script" <<'EOF'
rewatch (A:SIM ON GROUND, Bool)
rewatch (A:GENERAL ENG COMBUSTION:1, Bool) | (A:SIM ON GROUND, Bool)
rewatch (A:BRAKE PARKING POSITION, Bool) | (A:SIM ON GROUND, Bool) | (L:SOME_VAR, number) | (A:ELECTRICAL MASTER BATTERY, Bool)
rewatch (A:ELECTRICAL MASTER BATTERY, Bool) | (A:GENERAL ENG COMBUSTION:1, Bool)
rewatch (A:SIM ON GROUND, Bool) | (A:BRAKE PARKING POSITION, Bool)
EOF
rw="$(run --script "$TMP/rewatch.script")"
[ "$(echo "$rw" | grep -c '^rewatch: \([0-9]*\) of \1 value')" = "5" ] && ok "every watch change delivered all its values" || bad "rewatch: $rw"
python3 -I - "$rw" <<'PY' && ok "each value belongs to its own code (no stale slot content)" || bad "stale or wrong values"
import re, sys, zlib
seen, wrong = 0, 0
for line in sys.argv[1].splitlines():
    m = re.match(r"^  (\(.*\)) = (\S+)$", line)
    if m:
        seen += 1
        if float(m.group(2)) != float(zlib.crc32(m.group(1).encode()) % 97 + 1):
            wrong += 1
            print("wrong:", line)
sys.exit(1 if wrong or seen != 11 else 0)
PY
# Watching the cockpit: the trace subscribes to the input events and reads every value back; a control changed by somebody else (here: the
# script writes it, the mock tells subscribers, like the sim does for a click in the cockpit) is recorded once, whichever way it was found.
cat > "$TMP/trace.script" <<'EOF'
trace on
sleep 1500
set TRIM_AILERON 0.75
expecttrace TRIM_AILERON 3000
trace off
EOF
tr="$(run --script "$TMP/trace.script")"
echo "$tr" | grep -q '^ok:   trace saw B:TRIM_AILERON change 0.25 -> 0.75' && ok "a change of a cockpit control is seen by the trace" || bad "trace: $tr"
echo "$tr" | grep -q '[1-9][0-9]* announced' && ok "the sim's announcement of the change arrived" || bad "no announcement: $tr"
grep -q 'INPUT EVENT SUBSCRIBE all' "$TMP/mock.log" && ok "the trace subscribed to all input events" || bad "no subscription in the mock log"
grep -q 'INPUT EVENT SET LIGHTING_LANDING_1=1' "$TMP/mock.log" && ok "the simulator received the writes" || bad "mock log"
exit $fail
