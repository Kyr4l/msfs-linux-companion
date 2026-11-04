# Development

* `tools/mock_simconnect.py --port 5500` is a fake SimConnect server (answers Open, logs events,
  emulates the MobiFlight areas); run the app with `--port 5500` to test without MSFS.
* `tools/build_standard_catalog.py --packages <Official/Steam> --input <MSFS>/Input` regenerates `data/standard_catalog.json` from an installation (read-only, names only).
* `tools/mock_simconnect.py --aircraft Asobo_CJ4` poses as any aircraft (input events, MobiFlight client, L: variable list).
* `tools/build_catalog.py --all` regenerates `data/a32nx_catalog.json` and `data/a380x_catalog.json` from the FlyByWire docs.
* `tools/build_aircraft_catalog.py asobo_cj4` regenerates `data/asobo_cj4_catalog.json` from the event list stored in `tools/aircraft_rules/` and the rules module next to it; it refuses to run when an event has no rule. New aircraft are added by dumping their events with `--dump-controls FILE` and writing a rules module.
* `--script FILE` checks controls on a running aircraft (`set`, `toggle`, `press`, `get`, `code`, `expect`, `expectb`, `sleep`; see `src/app/Script.hpp`). It prints `ok` / `FAIL` lines and a count, and exits non-zero on failures. Restore what you changed: it does not.
* `tools/build_keypad_layout.py <flybywire-aircraft-a320-neo package> --preview FILE.png` regenerates `data/a32nx_keypads.json` (the MCDU key positions, read from the aircraft's 3D model) and writes a picture of it.
* `ctest --test-dir build` runs the config compatibility, command-line option, axis, live-catalog, curated-catalog, guide, plugin, idle-cost and live-control tests. The scripts set `MSFS_LINUX_COMPANION_ISOLATED=1`, which makes the program skip the search for a running simulator's port and the opening of joysticks and Winwing devices: a test run next to a real simulator (or with a stick in your hand) can neither reach the sim nor be disturbed by your input.
* `tools/virtual_joystick.py` is a uinput joystick for exercising bindings without hardware.
* `cmake -DCOMPANION_UI_LINT=ON` builds a variant that checks the way the windows are drawn on every frame (`src/ui/UiLint.hpp`): two widgets that share an identifier (Dear ImGui only complains about that when the widget is hovered, and a conflicting widget cannot be clicked) and the errors it repairs by itself (a `Begin` without `End`) are written once each to stderr as `[ui-lint] ...`. Run it on a virtual display, with `MSFS_LINUX_COMPANION_ISOLATED=1`, through the screens and popups you changed.
* `_cpr/` is git-ignored: put copies of files from the (read-only) MSFS install there if you need
  to edit them.

Protocol references: [node-simconnect](https://github.com/EvenAR/node-simconnect) (SimConnect wire
format), [MobiFlight WASM module](https://github.com/MobiFlight/MobiFlight-WASM-Module),
[FlyByWire A32NX flight deck API](https://docs.flybywiresim.com/aircraft/a32nx/a32nx-api/a32nx-flightdeck-api/).
