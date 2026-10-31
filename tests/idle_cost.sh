#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
# Usage: idle_cost.sh <binary> <source root>
# The tool runs next to a heavy simulator, so doing nothing must cost next to nothing: wake-ups and CPU of the headless
# program while connected to the mock simulator and while the simulator is gone, and no handshake to services that are
# not the simulator.
set -u
export MSFS_LINUX_COMPANION_ISOLATED=1  # never probe a simulator that happens to be running, never open the real joysticks or Winwing devices
BIN="$1"; ROOT="$2"
TMP="$(mktemp -d)"; PIDS=()
export XDG_STATE_HOME="$TMP/state" XDG_CONFIG_HOME="$TMP/cfg"
cleanup() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done; rm -rf "$TMP"; }
trap cleanup EXIT
fail=0
ok()  { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }

PORT=$((20000 + RANDOM % 20000)); OTHER=$((PORT + 1))
python3 -I -B "$ROOT/tools/mock_simconnect.py" --port "$PORT" --aircraft Asobo_CJ4 >"$TMP/mock.log" 2>&1 &
MOCK=$!; PIDS+=("$MOCK")
python3 -I -B "$ROOT/tools/mock_simconnect.py" --port "$OTHER" --aircraft Asobo_CJ4 >"$TMP/other.log" 2>&1 &
PIDS+=("$!")
for _ in $(seq 1 50); do grep -q listening "$TMP/mock.log" 2>/dev/null && grep -q listening "$TMP/other.log" 2>/dev/null && break; sleep 0.1; done

echo '{"version":3,"host":"127.0.0.1","port":'"$PORT"',"auto_connect":true,"active_profile":"x","profiles":[{"name":"x","aircraft":"Asobo_CJ4","bindings":[]}]}' > "$TMP/config.json"
"$BIN" --headless --config "$TMP/config.json" -q >"$TMP/app.log" 2>&1 &
APP=$!; PIDS+=("$APP")
sleep 6
grep -q "OPEN from 'msfs-linux-companion'" "$TMP/mock.log" && ok "connected to the simulator" || bad "did not connect"

read -r wake cpu < <(python3 -I "$ROOT/tests/sample_threads.py" "$APP" 6)
echo "      connected and idle: $wake wake-ups/s, $cpu % of one core"
awk -v w="$wake" 'BEGIN{exit !(w < 60)}' && ok "connected idle: fewer than 60 wake-ups a second" || bad "connected idle wake-ups: $wake"
awk -v c="$cpu" 'BEGIN{exit !(c < 1.5)}' && ok "connected idle: under 1.5 % of one core" || bad "connected idle cpu: $cpu"

kill "$MOCK" 2>/dev/null; sleep 5      # the simulator goes away
read -r wake cpu < <(python3 -I "$ROOT/tests/sample_threads.py" "$APP" 6)
echo "      simulator gone: $wake wake-ups/s, $cpu % of one core"
awk -v w="$wake" 'BEGIN{exit !(w < 40)}' && ok "simulator gone: fewer than 40 wake-ups a second" || bad "disconnected wake-ups: $wake"
kill "$APP" 2>/dev/null; wait "$APP" 2>/dev/null

# the configured port does not answer and another loopback service (here a second mock) is NOT the simulator's own
# (wineserver) socket: it must never receive a SimConnect handshake
echo '{"version":3,"host":"127.0.0.1","port":1,"auto_connect":true,"active_profile":"x","profiles":[{"name":"x","aircraft":"Asobo_CJ4","bindings":[]}]}' > "$TMP/config2.json"
if pgrep -x wineserver >/dev/null; then
    echo "skip: a wineserver is running (a real simulator?): the search for the simulator's port is not exercised, so it cannot reach it"
else
    env -u MSFS_LINUX_COMPANION_ISOLATED "$BIN" --headless --config "$TMP/config2.json" -q >"$TMP/app2.log" 2>&1 &
    APP2=$!; PIDS+=("$APP2")
    sleep 9
    kill "$APP2" 2>/dev/null; wait "$APP2" 2>/dev/null
    grep -q "OPEN from" "$TMP/other.log" && bad "a service that is not the simulator received a handshake" || ok "no handshake sent to other loopback services"
fi
exit $fail
