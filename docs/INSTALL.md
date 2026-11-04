# Install and set up

## Requirements

* Linux, GCC 12+ (C++20), CMake 3.20+, Ninja or Make
* GLFW >= 3.4 development package (`glfw-devel` / `libglfw3-dev`), OpenGL/EGL, pkg-config
* Read access to your joystick's `/dev/input/event*` node and read/write to its `/dev/hidraw*` node
  (most distributions grant both to the logged-in user via udev `uaccess`; otherwise see
  [Permissions](TROUBLESHOOTING.md#permissions))

## Build

```sh
git clone --recursive https://github.com/Kyr4l/msfs-linux-companion.git
cd msfs-linux-companion
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
./build/msfs-linux-companion
```

Options: `--wayland` / `--x11` (force a platform), `--headless` (no window, run the active profile),
`--host`, `--port`, `--profile NAME`, `--config FILE`, `--catalog FILE`, `--plugin PATH`, `--no-plugins`, `--quiet`,
`--probe-winwing`, `--motor-sweep [DEV]`, `--dump-winwing-config`,
`--fire KIND:NAME[=V]` (one-shot action, e.g. `--fire lvar_toggle:A32NX_OVHD_APU_MASTER_SW_PB_IS_ON`),
`--read NAME` (print an `L:` var), `--read-input NAME` (print a cockpit input event), `--list-controls` (print the loaded aircraft's input events and L: variables).


### AppImage

```sh
./packaging/build-appimage.sh      # -> dist/msfs-linux-companion-<version>-x86_64.AppImage
```

The script builds with libstdc++/libgcc linked statically, bundles GLFW (3.4+ is often missing on
older distributions) and downloads `appimagetool` plus the AppImage runtime into `packaging/tools/`
on first use. libGL, the X11/Wayland client libraries and glibc come from the host. Icons live in
`packaging/icons/` (`make_icons.py` regenerates the PNG sizes and the embedded window icon from the
same shapes as the SVG). `sudo ninja -C build install` installs the binary, catalog, `.desktop`
file and hicolor icons the classic way instead.

Config is stored in `~/.config/msfs-linux-companion/config.json` and saved automatically.

**Coming from msfs-linuxrebind (0.6.x and earlier)?** The program was renamed in 0.7.0. The first start copies
`~/.config/msfs-linuxrebind/` (profiles, window layout, live catalogs) to the new folder and leaves the old one in place; once
you are happy, delete it, and `~/.local/state/msfs-linuxrebind/` (old session logs) too. A udev rule you installed under the
old file name keeps working, it only matches devices. The `msfs-linuxrebind` binary, `.desktop` file and window class are now
`msfs-linux-companion`: update launchers and window rules.

## MSFS side: SimConnect over TCP

MSFS listens where its `SimConnect.xml` says. Under Steam/Proton that file is

```
<steam library>/steamapps/compatdata/1250410/pfx/drive_c/users/steamuser/AppData/Roaming/Microsoft Flight Simulator/SimConnect.xml        (MSFS 2020)
<steam library>/steamapps/compatdata/2537590/pfx/drive_c/users/steamuser/AppData/Roaming/Microsoft Flight Simulator 2024/SimConnect.xml  (MSFS 2024)
```

and must contain an IPv4 block with a fixed port (the default file shipped by MSFS already has
`<Port>500</Port>` with `<Scope>local</Scope>`, which is all this tool needs):

```xml
<SimConnect.Comm>
    <Descr>Static IP4 port</Descr>
    <Protocol>IPv4</Protocol>
    <Scope>local</Scope>
    <Port>500</Port>
    <MaxClients>64</MaxClients>
    <MaxRecvSize>41088</MaxRecvSize>
</SimConnect.Comm>
```

**Proton cannot bind port 500**: Linux reserves ports below 1024, so MSFS silently falls back to
its "Dynamic IP4 port" entry and picks a random port on every launch (it does not record it
anywhere Linux can read). The tool handles this: when the configured port refuses, it scans the
loopback listeners owned by `wineserver`, sends the SimConnect Open handshake to each, and uses the
one that answers (the status bar shows the detected port). Setting a fixed `<Port>` above 1024 in
`SimConnect.xml` (e.g. 5500) and in the tool's settings skips the scan. The tool reconnects
automatically whenever the sim is (re)started.

## Action kinds and the MobiFlight module

| Action kind | Transport | Needs |
|---|---|---|
| `K:` event (`GEAR_UP`, `A32NX.FCU_AP_1_PUSH`, ...) | SimConnect `MapClientEventToSimEvent` + `TransmitClientEvent` | nothing |
| `H:` event (`A32NX_EFIS_L_CHRONO_PUSHED`) | calculator code through the MobiFlight WASM module client-data area | MobiFlight WASM module |
| `L:` var set (`A32NX_AUTOBRAKES_ARMED_MODE_SET = 2`) | same | MobiFlight WASM module |
| `L:` var toggle (`A32NX_OVHD_APU_MASTER_SW_PB_IS_ON`) | RPN `(L:X) ! (>L:X)`, evaluated in the sim | MobiFlight WASM module |
| RPN calculator code (`1 (>L:FOO)`) | same | MobiFlight WASM module |

The A32NX `A32NX.*` custom events cover the FCU, EFIS and autobrake and need **no** extra module.
For `H:` events and `L:` vars install the
[MobiFlight WASM module](https://github.com/MobiFlight/MobiFlight-WASM-Module/releases)
(unzip `mobiflight-event-module` into the MSFS `Community` folder). The status bar shows whether it
was detected.

## Upgrading does not break your configuration

The config is versioned (`version` in `config.json`; 2 since 0.2.0, 3 since 0.5.0, which added `B:` actions, 4 since 0.8.0, which added gated levers, 5 since 0.8.9, which added lever stages). Profiles written by older builds
have no aircraft and load as A32NX profiles with every binding, haptics setting and device address
intact. The first time a newer build saves over an older file it keeps the original once as
`config.json.v<old version>.bak` (for example `config.json.v2.bak`) (never overwritten afterwards), so rolling back is a file copy. Unknown or newer
fields are ignored rather than rejected. `ctest` runs a round-trip test of exactly this (a schema-1
fixture, optionally your own config: `build/config_roundtrip ~/.config/msfs-linux-companion/config.json`,
which only ever works on a copy), plus a test that every documented command-line option is accepted.
