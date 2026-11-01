# Plugins

A plugin adds to the program without changing it: a new aircraft, a tab, a menu, a command your bindings can run, a vibration rule,
something else to vibrate, or anything else. There are two kinds:

* **Data plugins**: a folder with a `data/` folder in it. The catalogs (`*_catalog.json`), guides (`*_guide.json`) and keypads
  (`*_keypads.json`) in it are read like the program's own files in [`data/`](../data). This is how an aircraft is added without
  writing code. See [`examples/plugins/my-aircraft`](../examples/plugins/my-aircraft) and [GUIDES.md](GUIDES.md).
* **Code plugins**: a shared library (`.so`) written in C++. It is loaded into the program and can use all of it: the connection to the
  sim, the binding engine, the catalogs, the settings, the vibration engine, and Dear ImGui to draw. See
  [`examples/plugins/hello`](../examples/plugins/hello), which uses every hook below.

A plugin can be both: a library next to a `data/` folder.

## Where plugins go

| Folder | |
|---|---|
| `~/.config/msfs-linux-companion/plugins/<name>/` | your plugins (`$XDG_CONFIG_HOME` is used when set) |
| `<prefix>/lib/msfs-linux-companion/plugins/<name>/` | installed with the program |
| `$MSFS_LINUX_COMPANION_PLUGIN_PATH` | more folders, separated by `:` |
| `--plugin PATH` | one more: a `.so`, a plugin folder, or a folder of plugins |

A `.so` placed directly in a plugins folder is loaded too. `--no-plugins` skips the first two folders. Plugins are listed in
**Settings > Plugins**, where each can be switched off (after a restart). A plugin that fails to load is listed with its error and
the log says why; the program carries on without it.

A data plugin may have a `plugin.json` with `id`, `name`, `version` and `description`; without it, the folder name is its id.
A plugin's data comes after your own files in `~/.config/msfs-linux-companion/` and before the shipped ones, so a plugin file with the
name of a shipped one replaces it.

## Writing a code plugin

```cpp
#include "plugin/Plugin.hpp"

COMPANION_PLUGIN("hello", "Hello", "1.0", "Says hello") {
    host.addTab("Hello", [] { ImGui::TextUnformatted("Hello"); });
    host.addCommand("wave", "Hello: wave", [](double value) { LOG_INFO("hello: wave %g", value); });
    return true;
}
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(hello-plugin LANGUAGES CXX)
set(COMPANION_SOURCE_DIR /path/to/msfs-linux-companion)
include(${COMPANION_SOURCE_DIR}/cmake/CompanionPlugin.cmake)
companion_add_plugin(hello hello.cpp)
```

```sh
cmake -S . -B build && cmake --build build
mkdir -p ~/.config/msfs-linux-companion/plugins/hello && cp build/hello.so ~/.config/msfs-linux-companion/plugins/hello/
```

`companion_add_plugin` sets the include paths (the program's `src/`, Dear ImGui, nlohmann/json) and the version the plugin is built for.
The plugin does not link Dear ImGui: it uses the program's, so it draws in the same window.

### What the host offers

Everything is registered from the load function (`COMPANION_PLUGIN(...) { ... }`); calls made later are refused.
The full interface with comments is [`src/plugin/Plugin.hpp`](../src/plugin/Plugin.hpp).

| Call | Runs on | |
|---|---|---|
| `addTab(name, draw)` | window | a tab after Vibration; `--tab name` opens it |
| `addMenu(name, draw)` | window | a menu in the menu bar; `draw` adds the items |
| `addStatusItem(draw)` | window | something at the end of the status line |
| `addSettingsSection(title, draw)` | window | a section in the Settings tab |
| `addWindow(draw)` | window | called every frame: own windows, popups |
| `addCommand(name, label, run)` | input / window | a command for bindings (below) |
| `addInputListener(fn)` | input | every joystick event, before the bindings |
| `addVibrationRule(id, label, description, level)` | haptics | a rule in the Vibration tab: flight data to a level 0..1 |
| `addVibrationOutput(fn)` | haptics | something else to vibrate: gets the computed levels, the level of each rule and the flight data about 30 times a second while flying (the flight data also while vibration is off) |
| `onStart(fn)` / `onShutdown(fn)` | main | after everything has started / when the program ends |
| `settings()` / `setSettings(json)` | window | the plugin's settings, saved in `config.json` under `plugins` |
| `app()` | any | the whole program (`app::AppContext`): `bridge` (send events, read L: vars and input events, flight data), `engine`, `catalogs`, `guides`, `settings`, `haptics`, `winwing`, `input` |

A draw function, a command or a rule that throws is logged and switched off until the next start; the window carries on.

### Commands in bindings

A command is run by an action of the kind **Plugin command** (in the binding editor, pick the command from the list), saved as
`{"kind": "plugin", "name": "<plugin id>.<command>", "value": 1}`. On an axis binding the value is the axis position, scaled to the
binding's output range. A catalog entry with `"kind": "plugin"` puts a command in the Catalog tab. From the command line:
`--fire plugin:hello.wave=2`.

### Compatibility

A plugin runs inside the program and uses its types directly, so it is built for one version of it:

* the plugin API (`lr::plugin::kApiVersion`) and the Dear ImGui version must match, or the plugin is refused;
* a plugin built for another version of the program is loaded with a warning in the log. Rebuild it when you update the program;
  the types it uses (`AppContext`, `Telemetry` ...) may have changed.

Build plugins with the compiler the program was built with, without `-fvisibility=hidden` (the helper takes care of that).
The AppImage carries its own C++ runtime; a plugin for it is safest built from the same source on the same machine.

## Testing

`tests/plugins.sh` (part of `ctest`) loads the example plugin, the example aircraft and two broken plugins into the real program.
Test runs (`MSFS_LINUX_COMPANION_ISOLATED=1`) only load the plugins given with `--plugin` or `MSFS_LINUX_COMPANION_PLUGIN_PATH`.
