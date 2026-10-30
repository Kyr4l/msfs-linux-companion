# Third-party components

msfs-linux-companion itself is licensed under the GNU General Public License v3.0 only (see `LICENSE`).
It includes or links the following components, each under its own licence, all of which are compatible with the GPL-3.0:

| Component | Use | Licence |
|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) (git submodule, `third_party/imgui`) | user interface | MIT, `third_party/imgui/LICENSE.txt` |
| [nlohmann/json](https://github.com/nlohmann/json) (`third_party/nlohmann/json.hpp`) | config and catalog files | MIT, `third_party/nlohmann/LICENSE.MIT` |
| [GLFW](https://www.glfw.org/) | windowing (Wayland and X11); linked dynamically, bundled in the AppImage | zlib/libpng |
| libstdc++ and libgcc | linked statically in the release binaries | GPL-3.0 with the GCC Runtime Library Exception |
| [MobiFlight WASM module](https://github.com/MobiFlight/MobiFlight-WASM-Module) | optional, runs inside MSFS; this tool only talks to it over SimConnect | MIT, installed separately by the user |

## Data

* `data/a32nx_catalog.json` and `data/a380x_catalog.json` list control and variable names of the FlyByWire
  [A32NX / A380X](https://github.com/flybywiresim/aircraft) (GPL-3.0). They are generated from the FlyByWire
  [documentation](https://github.com/flybywiresim/docs) and event lists by `tools/build_catalog.py`.
* `data/standard_catalog.json` is a list of SimConnect event names found in an MSFS installation by
  `tools/build_standard_catalog.py` (names only, no text or files from the game are copied).
* Per-aircraft control lists generated from a running simulator contain only the names the simulator reports.

## Protocol knowledge

The SimConnect wire protocol was implemented from the public SimConnect documentation and the open implementation
[node-simconnect](https://github.com/EvenAR/node-simconnect) (MIT). The Winwing device frames were worked out from
community reverse engineering ([winctrl-xplane-plugin](https://github.com/rswilem/winctrl-xplane-plugin),
[WinctrlConfig](https://github.com/f3nr1l/WinctrlConfig), the Linux `hid-winwing` driver) and confirmed on real hardware.
No code from those projects is copied.

"Microsoft Flight Simulator", "SimConnect", "WINWING", "WINCTRL" and "URSA MINOR" belong to their respective owners.
This project is independent and not affiliated with them.
