#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
# Usage: cli_options.sh <binary> <config fixture>
# Runs the binary headless with every value-taking option once. Exit status 2 means the parser rejected an
# option; a timeout (124) or a normal exit are both fine. Options that exit immediately are checked separately.
set -u
export MSFS_LINUX_COMPANION_ISOLATED=1  # never probe a simulator that happens to be running, never open the real joysticks or Winwing devices
BIN="$1"; CFG="$2"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
export XDG_STATE_HOME="$TMP/state"  # session logs of the test runs stay out of the real state directory
export XDG_CONFIG_HOME="$TMP/xdg_default"  # a run without --config must not read (or rewrite) the developer's own settings
cp "$CFG" "$TMP/config.json"
fail=0
check() {  # description, args...
    local d="$1"; shift
    timeout 2 "$BIN" "$@" >"$TMP/out.txt" 2>&1
    local rc=$?
    if [ "$rc" -eq 2 ]; then echo "FAIL: $d: $(head -1 "$TMP/out.txt")"; fail=1; else echo "ok:   $d (rc=$rc)"; fi
}
check "headless + config + host + port + profile + catalog + quiet" \
    --headless --config "$TMP/config.json" --host 127.0.0.1 --port 1 --profile A32NX --catalog "$(dirname "$CFG")/../../data/a32nx_catalog.json" --quiet
check "motor-sweep with and without device" --headless --config "$TMP/config.json" --motor-sweep 0x04 --quiet
check "probe-winwing" --probe-winwing --quiet
check "dump-winwing-config" --dump-winwing-config --quiet
"$BIN" --version >/dev/null 2>&1 || { echo "FAIL: --version"; fail=1; }
"$BIN" --help 2>&1 | grep -q -- '--config' || { echo "FAIL: --help does not list --config"; fail=1; }
# the config must have been read, not rejected: the log names the profile
timeout 2 "$BIN" --headless --config "$TMP/config.json" --port 1 2>&1 | grep -q 'Config: .*profile "A32NX", 2 bindings' || { echo "FAIL: --config was not applied"; fail=1; }
# the settings of the former name (msfs-linuxrebind) are copied once to the new folder and the old one stays
XDG="$TMP/xdg"; mkdir -p "$XDG/msfs-linuxrebind/live"
cp "$CFG" "$XDG/msfs-linuxrebind/config.json"; echo '{}' > "$XDG/msfs-linuxrebind/live/x.json"
XDG_CONFIG_HOME="$XDG" timeout 2 "$BIN" --headless --port 1 2>&1 | grep -q 'Config: .*msfs-linux-companion/config.json (profile "A32NX", 2 bindings' \
    || { echo "FAIL: the former settings folder was not migrated"; fail=1; }
[ -f "$XDG/msfs-linux-companion/live/x.json" ] && [ -f "$XDG/msfs-linuxrebind/config.json" ] \
    || { echo "FAIL: migration did not copy the live folder or removed the old one"; fail=1; }
[ -e "$XDG/msfs-linux-companion.migrating" ] && { echo "FAIL: temporary migration folder left behind"; fail=1; }
echo '{"profiles":[{"name":"Mine","aircraft":"A32NX","bindings":[]}],"active_profile":"Mine"}' > "$XDG/msfs-linux-companion/config.json"
XDG_CONFIG_HOME="$XDG" timeout 2 "$BIN" --headless --port 1 2>&1 | grep -q 'profile "Mine"' \
    || { echo "FAIL: a second start replaced the new settings with the old ones"; fail=1; }
# a number that is not a number, or does not fit, is refused instead of becoming 0 or wrapping around (70000 -> 4464)
for bad in "--port abc" "--port 0" "--port 70000" "--port -5" "--motor-sweep abc" "--motor-sweep 300"; do
    timeout 2 "$BIN" --headless $bad >"$TMP/out.txt" 2>&1
    [ $? -eq 2 ] || { echo "FAIL: '$bad' was not refused"; fail=1; }
done
# a config that cannot be read is kept aside, and the file itself is not written over
echo '{"profiles": {"oops": ' > "$TMP/broken.json"; cp "$TMP/broken.json" "$TMP/broken.orig"
timeout 2 "$BIN" --headless --config "$TMP/broken.json" --port 1 >/dev/null 2>&1
ls "$TMP"/broken.json.unreadable-* >/dev/null 2>&1 || { echo "FAIL: an unreadable config was not kept aside"; fail=1; }
cmp -s "$TMP/broken.json" "$TMP/broken.orig" || { echo "FAIL: an unreadable config was overwritten"; fail=1; }
# with the search switched off nothing is probed, even when a wineserver (a real simulator) is running: the port 1 does not answer
timeout 6 "$BIN" --headless --port 1 2>&1 | grep -q 'probing' && { echo "FAIL: the search for the simulator ignored MSFS_LINUX_COMPANION_ISOLATED"; fail=1; }
exit $fail
