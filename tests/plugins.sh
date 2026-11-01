#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
# Usage: plugins.sh <binary> <source root> <hello.so> <bad_api.so> <bad_load.so>
# The plugins are found, loaded and used by the real binary: the example plugin (examples/plugins/hello) registers its command, a binding
# runs it, the data plugin (examples/plugins/my-aircraft) brings a catalog, and two broken plugins are refused without taking the program down.
set -u
export MSFS_LINUX_COMPANION_ISOLATED=1  # never probe a simulator that happens to be running, never open the real joysticks or Winwing devices
BIN="$1"; ROOT="$2"; HELLO="$3"; BAD_API="$4"; BAD_LOAD="$5"
TMP="$(mktemp -d)"; PIDS=()
export XDG_STATE_HOME="$TMP/state" XDG_CONFIG_HOME="$TMP/cfg"
unset MSFS_LINUX_COMPANION_PLUGIN_PATH
cleanup() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done; rm -rf "$TMP"; }
trap cleanup EXIT
fail=0
ok()  { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }
has() { grep -q -- "$2" "$1"; }

PORT=$((20000 + RANDOM % 20000))
python3 -I -B "$ROOT/tools/mock_simconnect.py" --port "$PORT" --aircraft Asobo_CJ4 >"$TMP/mock.log" 2>&1 &
PIDS+=("$!")
for _ in $(seq 1 50); do grep -q listening "$TMP/mock.log" 2>/dev/null && break; sleep 0.1; done

# a plugin folder the way a user installs it: the library and nothing else
mkdir -p "$TMP/plugins/hello"; cp "$HELLO" "$TMP/plugins/hello/"
CFG="$TMP/config.json"
echo '{"version":4,"host":"127.0.0.1","port":'"$PORT"',"active_profile":"x","profiles":[{"name":"x","aircraft":"Asobo_CJ4","bindings":[]}]}' > "$CFG"

# 1. --fire runs a plugin command the way a binding does; the data plugin's catalog is read; the shutdown hook runs
timeout 30 "$BIN" --config "$CFG" --plugin "$TMP/plugins/hello" --plugin "$ROOT/examples/plugins/my-aircraft" --fire plugin:hello.count=3 >"$TMP/run1.log" 2>&1
rc=$?
[ $rc -eq 0 ] && ok "--fire with a plugin command exits 0" || bad "--fire with a plugin command: exit $rc"
has "$TMP/run1.log" "Plugin hello 1.0 loaded from" && ok "the example plugin is loaded" || bad "the example plugin was not loaded"
has "$TMP/run1.log" "hello: count 3 (now 3)" && ok "its command ran with the value of the action" || bad "the plugin command did not run"
has "$TMP/run1.log" "Plugin my-aircraft 1.0: data from" && ok "the data plugin is found" || bad "the data plugin was not found"
has "$TMP/run1.log" "Catalog My_Aircraft: 6 entries from .*my-aircraft/data/my_aircraft_catalog.json" && ok "its catalog is read" || bad "the data plugin's catalog was not read"
has "$TMP/run1.log" "hello: started" && ok "the start hook ran" || bad "the start hook did not run"
has "$TMP/run1.log" "hello: stopped, count 3" && ok "the shutdown hook ran" || bad "the shutdown hook did not run"

# 2. an unknown command is reported, not a crash
timeout 30 "$BIN" --config "$CFG" --plugin "$TMP/plugins/hello" --fire plugin:hello.nothing >"$TMP/run2.log" 2>&1
has "$TMP/run2.log" "No plugin has the command hello.nothing" && ok "an unknown command is reported" || bad "an unknown command was not reported"

# 3. broken plugins are refused, the program goes on (and the example next to them still loads)
timeout 30 "$BIN" --config "$CFG" --plugin "$BAD_API" --plugin "$BAD_LOAD" --plugin "$TMP/plugins/hello" --fire plugin:bad_load.x --fire plugin:hello.count >"$TMP/run3.log" 2>&1
rc=$?
[ $rc -eq 0 ] && ok "broken plugins do not stop the program" || bad "with broken plugins: exit $rc"
has "$TMP/run3.log" "Plugin bad_api .*built for plugin API 10[0-9], this program has" && ok "a plugin for another API is refused" || bad "a plugin for another API was not refused"
has "$TMP/run3.log" "Plugin bad_load .*companion_plugin_load() threw: no device here" && ok "a plugin whose load throws is reported" || bad "a throwing plugin was not reported"
has "$TMP/run3.log" "No plugin has the command bad_load.x" && ok "what a failed plugin registered is dropped" || bad "a failed plugin's command was kept"
has "$TMP/run3.log" "hello: count 1 (now 1)" && ok "the good plugin next to them works" || bad "the good plugin did not work next to the broken ones"

# 4. the user's plugin folder is read, a disabled plugin is not loaded, and a test run (isolated) skips the folder unless asked
mkdir -p "$TMP/cfg/msfs-linux-companion/plugins"; cp -r "$TMP/plugins/hello" "$TMP/cfg/msfs-linux-companion/plugins/"
timeout 3 "$BIN" --headless --config "$CFG" --port 1 >"$TMP/run4.log" 2>&1
has "$TMP/run4.log" "Plugin hello" && bad "an isolated run loaded the user's plugins" || ok "an isolated run leaves the user's plugin folder alone"
MSFS_LINUX_COMPANION_PLUGIN_PATH="$TMP/cfg/msfs-linux-companion/plugins" timeout 3 "$BIN" --headless --config "$CFG" --port 1 >"$TMP/run5.log" 2>&1
has "$TMP/run5.log" "Plugin hello 1.0 loaded" && ok "MSFS_LINUX_COMPANION_PLUGIN_PATH is searched" || bad "MSFS_LINUX_COMPANION_PLUGIN_PATH was not searched"
echo '{"version":4,"port":1,"active_profile":"x","profiles":[{"name":"x","bindings":[]}],"plugins":{"disabled":["hello"],"settings":{"hello":{"greet":false}}}}' > "$TMP/off.json"
timeout 3 "$BIN" --headless --config "$TMP/off.json" --plugin "$TMP/plugins/hello" >"$TMP/run6.log" 2>&1
has "$TMP/run6.log" "Plugin hello: disabled" && ok "a disabled plugin is not loaded" || bad "a disabled plugin was loaded"
has "$TMP/run6.log" "hello: started" && bad "a disabled plugin ran its start hook" || ok "a disabled plugin runs nothing"
python3 -I -c 'import json,sys; j=json.load(open(sys.argv[1])); sys.exit(0 if j["plugins"]["disabled"]==["hello"] and j["plugins"]["settings"]["hello"]["greet"] is False else 1)' "$TMP/off.json" \
    && ok "plugin settings stay in the config" || bad "plugin settings were lost"
# its settings are read: greet false means no greeting
sed 's/"disabled":\["hello"\]/"disabled":[]/' "$TMP/off.json" > "$TMP/on.json"
timeout 3 "$BIN" --headless --config "$TMP/on.json" --plugin "$TMP/plugins/hello" >"$TMP/run7.log" 2>&1
has "$TMP/run7.log" "Plugin hello 1.0 loaded" && ! has "$TMP/run7.log" "hello: started" && ok "a plugin reads its settings" || bad "the plugin did not read its settings"

[ $fail -eq 0 ] || { echo "--- logs"; tail -n 20 "$TMP"/run*.log; }
exit $fail
