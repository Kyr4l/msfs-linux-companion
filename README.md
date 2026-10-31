# msfs-linux-companion

A native Linux companion for Microsoft Flight Simulator 2020 (formerly *msfs-linuxrebind*): HOTAS bindings, curated cockpit catalogs, in-app guides and Winwing vibration.

Bind HOTAS buttons to Microsoft Flight Simulator 2020 cockpit controls **from Linux**, including the
FlyByWire A32NX functions that the in-game controls menu cannot bind (FCU pushes/pulls,
autobrake, EFIS buttons, chrono, ...). Also drives the vibration motors of Winwing devices from
flight data, which MSFS under Proton cannot do by itself.

* Native Linux, no Wine/Proton component: talks the **SimConnect TCP protocol** directly to the sim.
* Reads joysticks straight from **evdev** (`/dev/input/event*`), Winwing devices through **hidraw**.
* **Dear ImGui** UI on **Wayland or X11** (GLFW 3.4+, runtime platform selection), or `--headless`.
* Profiles with press / release actions, repeat-while-held (for FCU knobs), 2-position switches.
* **Axes** as inputs: an axis can send its position as a value (brightness knobs, QNH set...) or act as a +/- knob, with dead zones, inversion and rate limiting.
* **Throttle calibration** for levers with gates (cut-off, idle, afterburner), for aircraft whose own quadrant cannot be calibrated.
* A visual **panels view** of the cockpit to find the control you want, plus a plain list; searchable catalogs of cockpit functions for the FlyByWire **A32NX** and **A380X**, generated from the FlyByWire docs, with a per-profile aircraft selector.

## AI-assisted code, real-hardware tested

This code base was written with AI assistance (Claude), under the direction of and reviewed by the
maintainer. That is a statement about how it was produced, not about how it was validated: every
hardware path in it was exercised on the real devices it targets, and no issues were found.

| What | How it was verified |
|---|---|
| SimConnect wire protocol | live handshake and event traffic against MSFS 2020 under Proton (KittyHawk 11.0.282174.999), plus a mock server in `tools/` |
| MobiFlight module path (`L:` set/toggle, readback) | APU MASTER toggled from the tool on a cold & dark FlyByWire A32NX and read back 0 → 1 → 0 |
| WINCTRL URSA MINOR Combat Throttle Metal.EX (`4098:b970`) | controllers discovered over hidraw, both grip motors driven and felt, addresses confirmed by hand, config words dumped before and after |
| WINCTRL URSA MINOR Combat Joystick (`4098:bc29`/`bc2a`) | discovered the same way; motor address from the community kernel driver and X-Plane plugin |
| Axis bindings | verified end to end with a virtual (uinput) joystick through the real app and a mock simulator: value mode sent 10, 20 ... 100, step mode sent the right number of +/- events, and "Listen for input" captured the moved axis in the GUI. Your physical axes were read (ranges and rest positions) but not yet moved by hand with this tool |
| Live controls of any aircraft | the whole path (aircraft identification, listing the cockpit input events, set / toggle / cycle / read back, the L: variable list, the catalog, the cache, the profile banner) was verified through the real app against a mock simulator that speaks the protocol, and then against a real Asobo CJ4 (below). **Not yet run against the F/A-18E or any other default aircraft** |
| Asobo CJ4 catalog (`data/asobo_cj4_catalog.json`) | all 309 cockpit input events the sim reports are labeled by panel (338 entries). 67 entries are marked *verified* and were checked on a running CJ4 with `--script` (set the control, read the simulator's own `(A:...)` state, compare): a complete engine start through the app (battery, generators, avionics master, both starters, RUN at 20 % N2, one engine stopped and restarted), the exterior and cabin lights, anti-ice, fuel pumps, cabin signs, parking brake, flaps and spoilers, brightness knobs, the HDG / ALT / SPD / VS / BARO-1 / CRS-1 knobs, the three trims, and the AP, YD, FD, HDG, ALT, VS and FLC buttons. Everything else (the FMS CDUs, the AS3000 keys, gear, ignition, crossfeed, pressure source, ...) takes its behaviour from the control's name or from what was seen, and says so in its note. The thrust-lever event is not an axis (10 or less = idle, 11 or more = full power) and `LIGHTING_PANEL_1` misbehaves, so they are listed without a behaviour and with a note |
| What the CJ4 taught about cockpit input events | rotary knobs act once per event (`+1` up, `-1` down); the three trims want a value of 2 or more (up) or 0 or less (down); the autopilot, yaw damper, flight director and mode buttons **toggle on every event whatever its value**, so a push button sends exactly one event on press and nothing on release; lights, anti-ice, pumps, signs and the electrical switches are set-value controls (battery and generators: 0 = ON; avionics master: 1 = OFF, 0 = ON, 2 = DISPATCH); flaps and spoilers are 0..16384 axes; **a variable that looks right can fail to follow its control** (`GENERAL ENG GENERATOR SWITCH` never moved, `GENERAL ENG MASTER ALTERNATOR` does): every check is therefore read back from the sim before it is trusted |
| Asobo F/A-18E catalog (`data/asobo_fa18e_catalog.json`) | all 142 cockpit input events the sim reports are labeled (156 entries); 37 are marked *verified* on a running F/A-18E with `--script`, including a full engine start through the app (battery, APU, crank switch left and right, throttles to idle at 12 % N2, both engines running at idle): battery, generators, APU, external power, the crank switch, the throttle levers, the cut-off and the afterburner event (see below), flaps, anti-skid, wing fold, tail hook, altimeter, landing and cabin lights, pitot heat. Its polarity differs from the CJ4 and is recorded per control (the battery is 1 = ON; the strobes, engine anti-ice and windshield de-ice are inverted, 0 = ON). The 90 cockpit-specific controls (60 display-unit pushbuttons, the UFCD, HUD, EFD) were labeled but not tried. **The cut-off and the afterburner** work like this (read from the sim's throttle template, then run on the aircraft with the engines off): STOP is refused unless the throttle is at exactly 0, and a 0 from higher up stops at the idle gate first, so it takes throttle 0, a short wait, 0 again, a short wait, STOP 1 (the fuel valves then read closed; any later throttle value reopens them); the afterburner event moves the throttle to `L:XMLVAR_LastAB<n>ThrottleSet`, which is 0 until the game's own throttle has been used, so with 0 the throttle falls to idle: set it to 1 first. **Not found:** releasing the parking brake, the spoilers and the nav-light knob: they are listed without a behaviour and say so |
| Spirit of St. Louis catalog (`data/microsoft_spirit_of_stlouis_catalog.json`) | all 19 cockpit input events the sim reports are labeled, plus the K: events and L: variables the aircraft's model and its own checklist use (hand prop, primer, magnetos, the six fuel valves, throttle and mixture with their 0 to 16383 range, the periscope lever). The names and positions come from the cockpit model and the aircraft's tooltips; the events and parameters come from the sim's own checklist, whose copilot sends them. **Nothing is marked *verified* yet**: none of it was tried through this program on a running Spirit, and every note says where the entry comes from |
| FlyByWire A380X | catalog generated from the FlyByWire A380X docs; the APU, FCU and many overhead names were confirmed present in an installed A380X package. **Not yet exercised on the A380X in flight with this tool**: verify an A380X binding with its *Test* button before relying on it. The docs describe the development branch: the stable build's baro knob uses its own `A380X_EFIS_CP_BARO_PUSH/PULL_<n>` H events and lacks the `A32NX.FCU_EFIS_*_BARO_*` events the docs list, so the A380X catalog has no baro recipes and its EFIS baro entries may do nothing on older builds |
| Guides | the flow logic is unit-tested and the validator checks every guide. The app was run against a real CJ4: the questions were answered from the sim (on the ground; none, one or both engines through a two-variable sum), and the steps of "Start the engines" were confirmed by the sim's own variables while a real start was done through the app. The A32NX and A380X guides were written from the FlyByWire documentation (every step names its page) and have **not** been run against the real aircraft: their ticks and questions are unchecked, and each guide says so on its topics page. The F/A-18E guide (15 topics) was written from the sim's own checklist and instrument files plus three community guides on the MSFS forum (there is no official manual; community steps are labelled as such, and their figures were checked against the original posts); it has **not** been run on the aircraft, and steps that need the controls not yet understood there (parking brake release, speed brake, navigation light knob) carry no tick check and say so. The Spirit of St. Louis guide (4 topics) follows the sim's own checklist for that aircraft and its cockpit model, step by step with the file and item each comes from; it has **not** been run on the aircraft either, and says where the sim's checklist file contradicts itself (speeds in its comments, "RPM" items that test the throttle, an empty After landing page) |
| Device safety | the tool only sends identity queries, config *reads* and volatile motor levels; it never writes flash, restarts or re-flashes a device (`--dump-winwing-config` gives a baseline to diff) |

If you have a Winwing device that is not listed, discovery plus `--motor-sweep` will usually find
its motor in a minute; please report the result so it can be added to the defaults.

## How it works

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

## Requirements

* Linux, GCC 12+ (C++20), CMake 3.20+, Ninja or Make
* GLFW >= 3.4 development package (`glfw-devel` / `libglfw3-dev`), OpenGL/EGL, pkg-config
* Read access to your joystick's `/dev/input/event*` node and read/write to its `/dev/hidraw*` node
  (most distributions grant both to the logged-in user via udev `uaccess`; otherwise see
  [Permissions](#permissions))

## Build

```sh
git clone --recursive https://github.com/Kyr4l/msfs-linux-companion.git
cd msfs-linux-companion
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
./build/msfs-linux-companion
```

Options: `--wayland` / `--x11` (force a platform), `--headless` (no window, run the active profile),
`--host`, `--port`, `--profile NAME`, `--config FILE`, `--catalog FILE`, `--quiet`,
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

## Usage

> Only one copy of the tool should talk to the sim at a time (the GUI, or a `--script` / `--fire` run, not both): they share one private MobiFlight client, so one would clear the other's watched variables.

1. Start MSFS (any point, even the main menu) and this tool.
2. **Devices** tab: press buttons on the HOTAS to see their numbers.
3. **Bindings** tab: *Add binding* → *Listen for input* → press the button → add actions by hand or
   *from catalog* → *Save binding*. *Test* fires the press actions immediately.
4. Leave the same buttons **unassigned in the MSFS controls menu**, otherwise both fire.

**A binding like another one.** *Copy* in a row opens the editor on a new binding that does everything the original does (its actions, its axis settings,
its group, its repeat) and waits for you to press the button or move the axis it is for: the same button would fire both. It is added to the profile,
right after the original, when you save it. *Delete* asks first. The box next to *View* filters the list: every word you type has to be in the row's name, its
input, its device, its group or one of its actions.

**Arranging the list of bindings.** *Group by* above the table arranges it: *Device* (one group per stick or throttle),
*Input type* (axes, buttons, hats) or *My groups*, where each binding has its own group name (the *Group* field of the
editor; *Fill in the empty groups* gives every ungrouped binding one: Throttle for throttle levers and events, otherwise the
panel section of its control in the catalog). Rows are in the order you made: **drag a row** onto another to put it in
its place (in *My groups* also onto a group's header to move it into that group), or use *Up* / *Down*. *Sort automatically*
sorts the groups by name and the rows by device and input instead; dragging then only changes a binding's group. Groups
fold with their arrow. The choices are saved with the config.

### Axes

Move an axis while *Listen for input* is on (about a quarter of its travel; small drift is ignored) and
the binding becomes an axis binding. The **Devices** tab shows every axis live, which is the easy way to
find out which one is which. Two modes:

* **Value**: the position is rescaled to the output range you set (default 0 to 100, whole numbers) and
  used as the parameter: the value of a `K:` event, the value written by *L: var set*, or `{v}` inside
  RPN code, for example `{v} (>L:A32NX_SOME_BRIGHTNESS)`. Updates are rate limited (40 ms by default) and
  the last position is never lost.
* **Steps**: moving up runs the *up* actions and moving down the *down* actions, once per step of travel
  (default 10 %). Pair `A32NX.FCU_EFIS_L_BARO_INC` and `_DEC` for a QNH knob from a slider.

**Sim axis events need their own range.** Events such as `THROTTLE1_AXIS_SET_EX1`, `AILERON_SET` or
`AXIS_LEFT_BRAKE_SET` take values around -16384 to 16384 (throttles: full reverse to full thrust), not
0 to 100. The catalog stores the range the FlyByWire docs give for each of them: a new axis binding for
such an event gets it automatically, the editor warns (with a *Set range* button) when the output range is
wrong, and the Bindings tab shows a *Fix the ranges* banner for existing bindings (the tool logs them at
start-up too). *Output range* also has presets (percent, sim axis with or without reverse, 0 to 1). Leave
the same physical axis unassigned in the MSFS controls menu, otherwise both inputs fight.

**Any aircraft, whatever its catalog.** In the editor of an axis binding the **Control** list sets the axis up in one click: throttle
(all engines or 1 to 4), mixture, propeller, flaps, elevator trim, spoilers, the toe brakes, elevator, ailerons and rudder. It
writes the sim event and the range of values the event takes, so a plane with no catalog of its own (the Spirit of St. Louis, a
Cessna, a Cub) needs no hunting for event names. The ranges come from the MSFS SDK where it states them, and from the sim's own aircraft
checklists, whose copilot sets the lever with a parameter and then reads the lever back (the Spirit's throttle: `THROTTLE1_SET` 0 closed, 16383 full;
its mixture: `MIXTURE1_SET` 0 lean, 16383 rich; the Pitts' propeller: `AXIS_PROPELLER_SET` -16383 to 16383); an entry whose range the SDK
does not give says so. None of them is marked verified on a running aircraft yet. The same controls are listed in the Catalog tab
of aircraft without a catalog of their own, and in the curated catalogs that ask for them, together with a few named buttons every
aircraft can use: **Baro setting up / down** (`KOHLSMAN_INC` / `KOHLSMAN_DEC`, the events the sim's controls menu binds), **STD baro (set standard
pressure)** (`BAROMETRIC_STD_PRESSURE`) and **Baro: local pressure** (`BAROMETRIC`), and for the Garmin G1000 two H events of its baro knob. They are
findable by "baro", "std" or "qnh". None of them is a toggle: according to the public source of the G1000 NXi (the C172's, DA40's avionics),
`BAROMETRIC_STD_PRESSURE` only switches STD on, `BAROMETRIC` switches STD on above 18000 ft and off below it (leaving the setting at 29.92), and the knob
steps `AS1000_PFD_BARO_INC` / `_DEC` leave STD and put back the local setting that was remembered. So the way back from STD is the knob entries
(*G1000 baro knob up / down*, they need the MobiFlight module), not a second press of the STD button. Not yet tried on a real cockpit.

*Use travel from/to* turns the ends of the physical travel into dead zones and *Invert* flips the
direction. `tools/virtual_joystick.py demo` creates a fake joystick to try all of this without hardware.

#### Throttle calibration: levers with gates

Some throttles have mechanical gates on the lever: the Winwing URSA MINOR has a cut-off stop below the idle
detent and an afterburner gate at the front. The sim's own calibration cannot teach an aircraft those points
(some aircraft have no throttle calibration at all), so the program does it:

1. **Calibrate the axis once** (Devices tab, *Calibrate...* next to the axis, or from the binding editor).
   * **Gate buttons**: many throttles have a switch at each gate, which closes while the lever is in it (on the URSA MINOR
     Combat Throttle, for example, buttons 13 / 18 / 17 / 16 for the cut-off, idle, end of the dry range and afterburner of one lever
     and 23 / 22 / 21 / 20 for the other; on the unit this was developed with the two levers read as the Rx and Ry axes, and X and Y were something else: the *Devices* tab shows which axis moves). Press *Listen* for a gate and
     move the lever into it: the program notices which button closed. From then on that button decides, exactly, whether the
     lever is in the cut-off or the afterburner, and when it is at idle or at the end of the dry range, however close the gates
     are: a lever with a centimetre between cut-off and idle has gate *positions* that overlap, buttons never do.
   * **Positions**: move the lever up through its travel once; the program learns where the idle gate ends and where the
     dry range stops (the position at the moment the idle button opens and the end button closes), or set them by hand.
     That is all the axis is needed for: the linear part in between.
   * **A gate without a button** is judged from the position of the lever (set with *Set from the lever*), which only works
     when the gates are far enough apart.
   The result is stored in the config and shared by every profile; the bindings are paused while the window is open. A
   lever that reads the other way round is detected.
2. **Tick *Lever with gates*** in the binding of that axis and give the values the sim should get: at the cut-off, at
   idle (exactly that value while the idle gate is closed) and at the end of the dry
   range. Between idle and the end of the dry range the value follows the lever linearly; in the afterburner zone it
   stays at the last value.
3. **Gate actions**: lists of actions that run once when the lever enters or leaves the cut-off or the afterburner
   zone (for example the aircraft's afterburner event on and off).

The *F/A-18E throttle 1 / 2* presets fill in what the aircraft needs (the lever is a fraction of its travel: 0 is idle, the
value of the sim's own *Idle* preset, and 0.8 is military power):

* **Idle**: 0. A throttle set to 0 from higher up stops at the aircraft's idle gate (10 % on the engine display); only a second 0
  reaches 0. So when the lever comes down onto idle the program sends the 0 once more 100 ms later (dropped if the lever has moved on).
* **Cut-off**: throttle 0, a *Wait* action, throttle 0 again, *Wait*, `ENGINE_Throttle_n_STOP` = 1. The aircraft refuses STOP
  unless the throttle is at exactly 0, and a single 0 from higher up only reaches the idle gate; the waits are what it needs
  (50 ms worked, the preset uses 100). A *Wait* in an action list holds back what follows it without stopping anything, and
  is dropped if the lever changes zone first, so a cut-off still on its way never fires for a lever that has left the gate.
  Leaving the cut-off sends STOP = 0 (a throttle value that does not change the throttle, such as 0 at 0, would not clear it).
* **Afterburner**: `XMLVAR_LastAB<n>ThrottleSet` = 1, wait, `ENGINE_Throttle_n_Afterburner` = 1. The aircraft sets the throttle to
  that variable when the afterburner comes on; it is 0 until the game's own throttle has been used, and with 0 the throttle
  falls to idle (a lever that "worked" had simply been used in the game before). Leaving the afterburner sends the event 0 and the
  throttle value again.

A lever that is not calibrated behaves as a plain axis. This was run end to end on the real F/A-18E with the engines off: a
virtual stick drove both levers through idle, military power, afterburner and cut-off in fast cycles through the real program,
and the aircraft was read back each time (throttle, afterburner state, cut-off state, fuel valves). Not yet watched: a lit
afterburner and the engines winding down, and the real throttle.

Tips:

* Momentary cockpit push buttons: one `K:` event on press (e.g. `A32NX.FCU_AP_1_PUSH`).
* Overhead push buttons that are `L:` vars (APU MASTER, APU START, most of the overhead): use
  *L: var toggle*, which flips the switch each press whatever changed it in between. The catalog
  picks toggle automatically for `0|1` vars. The Bindings table shows the live value next to the action.
* FCU knobs on a rotary/hat: `A32NX.FCU_HDG_INC` with *Repeat while held*.
* Two-position switches (e.g. an autobrake toggle): `A32NX.AUTOBRAKE_SET 2` on press and
  `A32NX.AUTOBRAKE_SET 0` on release.
* `A32NX.FCU_ALT_INCREMENT_SET 1000` / `100` to pick the ALT knob step.
* Baro / QNH on three buttons: filter the catalog for `baro`. *Baro STD / QNH toggle* (captain,
  first officer, or both) is an RPN recipe that reads the current mode and pushes or pulls the knob
  accordingly; `A32NX.FCU_EFIS_L_BARO_INC` / `_DEC` with *repeat while held* make two buttons act
  as the knob. Tick *Both sides* before adding a captain-side (`_L_`) entry to get the first
  officer twin in the same binding, or use the *(both sides)* recipes.

## Aircraft profiles

Each profile belongs to an aircraft (A32NX or A380X). The aircraft decides which catalog the editor and
the *Catalog* tab offer; it never changes existing bindings, so you can re-assign a profile at any time.

* **New profile** asks for the aircraft; the default is the aircraft currently loaded in the sim.
* When the loaded aircraft does not match the active profile, the Bindings tab says so and offers a
  one-click *Switch to "<profile>"* (or *Create a profile for it*). Nothing is switched automatically.
* *Catalog* has an aircraft selector for browsing the other aircraft's list ("browsing only" is shown).
* Numbered controls such as `ENG {NUM} BLEED` are expanded into one entry per unit (1 to 4, as the
  FlyByWire docs define the placeholder; the ADIRS and the air-conditioning packs stop at 3). A number
  the aircraft does not have simply does nothing.
* Catalog files are `data/<aircraft>_catalog.json`; any `*_catalog.json` placed in
  `~/.config/msfs-linux-companion/` overrides the shipped one with the same name, and `--catalog FILE`
  adds one for a single run. `tools/build_catalog.py --all` regenerates the shipped ones.

### Upgrading does not break your configuration

The config is versioned (`version` in `config.json`; 2 since 0.2.0, 3 since 0.5.0, which added `B:` actions). Profiles written by older builds
have no aircraft and load as A32NX profiles with every binding, haptics setting and device address
intact. The first time a newer build saves over an older file it keeps the original once as
`config.json.v<old version>.bak` (for example `config.json.v2.bak`) (never overwritten afterwards), so rolling back is a file copy. Unknown or newer
fields are ignored rather than rejected. `ctest` runs a round-trip test of exactly this (a schema-1
fixture, optionally your own config: `build/config_roundtrip ~/.config/msfs-linux-companion/config.json`,
which only ever works on a copy), plus a test that every documented command-line option is accepted.

## Guides

The *Guides* tab holds step-by-step guides per aircraft (`data/<aircraft>_guide.json`). A guide works like a
flowchart: pick a topic ("Start the engines"), answer a few questions, follow the checklists.

* **Questions are answered by the simulator when it can**: "is the aircraft on the ground?", "how many engines are running?"
  are read from the sim (the answer it suggests is highlighted; you can always pick another). Without a connection, or
  without the MobiFlight module for sim variables, you answer by hand.
* **Each step shows where the control is**: the panel section of the control from the aircraft's catalog, with the
  control lit. It is a schematic of the catalog's sections, not a photo of the cockpit; where a source says where to look
  (for example the camera the sim's own checklist jumps to) the step says so.
* **MCDU key presses are drawn on a keypad** (A32NX): a step that says "press INIT, then LSK2R" shows the MCDU keyboard with
  those keys lit and numbered in the order to press them. The layout is not hand-drawn: `tools/build_keypad_layout.py`
  reads the key positions and the key list from the aircraft's own 3D model and behaviour files
  (`data/a32nx_keypads.json`; hover a key to see the H event it sends). Steps that only say "the LSK next to the waypoint"
  show no keys, since which one depends on the page.
* **Steps tick themselves** when the simulator confirms them (parking brake set, generators on ...) and can be ticked by
  hand; some show live readouts (N2, ITT) for "wait until ..." steps.
* **Sources**: every step names the document it comes from. Procedures are not written from memory; a step that is
  technique rather than a documented procedure says so. `tools/validate_guides.py` refuses guides whose controls are not
  in the catalog, whose steps have no source, or whose flow has dead ends. The format is in `docs/GUIDES.md`.

## Vibration (Winwing URSA MINOR and friends)

MSFS only vibrates XInput gamepads and DirectInput force-feedback devices; the Winwing kernel
driver does not expose the URSA MINOR motors as force feedback, so under Proton the game cannot
reach them. Instead, this tool subscribes to flight data (ground speed, touchdown, stall,
overspeed, gear/flaps motion, reversers, speed brake, G load, braking) and writes motor levels to
the device with Winwing's own HID protocol (output report `0x02`, opcode `0x49`), the way
SimAppPro does on Windows. Enable it in the **Vibration** tab; everything is off by default.

Motor addressing per product comes from community reverse engineering
([winctrl-xplane-plugin](https://github.com/rswilem/winctrl-xplane-plugin),
[WinctrlConfig](https://github.com/f3nr1l/WinctrlConfig), the kernel `hid-winwing` rumble patch):

| Product id | Device | Controllers (device / family) | Motors (device, SET_LEDX index) |
|---|---|---|---|
| `b970` | URSA MINOR Combat Throttle Metal.EX | grip L `0x04`, grip R `0x08`, base `0x40` / `0xb9` | left (`0x04`, `0x0e`), right (`0x08`, `0x10`) |
| `b920`, `b930` | URSA MINOR 32 Throttle Metal L/R | `0x10` / `0xb9` | left (`0x10`, `0x0e`), right (`0x10`, `0x10`) |
| `bc27`, `bc28` | URSA MINOR Airline Joystick L/R | grip `0x07`/`0x08` / `0xbf`, lights `0x20` / `0xbb` | single (grip, 0) |
| `bc29`, `bc2a` | URSA MINOR Fighter Joystick | grip `0x09`/`0x0a` / `0xbf`, lights `0x20` / `0xbb` | single (grip, 0) |

Unknown products are handled by discovery: a request to the broadcast address (device `0x01`,
family `0x00`) makes every controller behind the USB device answer with its id, and the product
name is then read from its flash. Controllers named "... L" / "... R" become the left/right
motors (index 0, which may need adjusting: run `--motor-sweep` to find the index that buzzes). *Discover controllers* in the Vibration tab shows the list and lets you pick
which one is which; the *LED test* (index 0 on the base = backlight) and *Pulse* buttons confirm
it. `msfs-linux-companion --probe-winwing` prints the same from the command line.

## Performance

The tool runs next to a simulator that wants every core and every cycle, so it is built to cost next to nothing when it has
nothing to do, and to ask the simulator for as little as possible.

Measured on the author's machine against a running MSFS (headless, a copy of the real configuration with vibration enabled
and two Winwing devices; one thread's wake-ups counted per second):

| | before these changes | now |
|---|---|---|
| CPU, connected, vibration on | 0.57 % of one core | 0.17 - 0.20 % |
| thread wake-ups per second | about 270 | about 58 (28 with vibration off) |
| memory (headless) | 12 MiB | 12 MiB |

What changed, and why it matters to the sim:

* **Nothing polls.** The bridge thread sleeps until the sim sends something or a command arrives (an eventfd wakes it at once),
  the input thread sleeps until a device moves or a held button needs its next repeat, and the vibration thread runs at about 30 Hz
  only while something vibrates or flight data arrives, otherwise four times a second.
* **Flight data only when it is used.** Vibration telemetry (18 variables) is requested only while vibration is enabled *and* a device
  is plugged in, and stopped again otherwise. It used to be every third sim frame, which is about 60 updates a second at 180 fps;
  it now adapts to about 20 updates a second whatever the frame rate.
* **Nothing is asked for that is not used.** The block that carries watched `L:` / `A:` values is defined, and its requests made,
  only when something is watched (a binding, or an open guide page); input events are polled at 2 Hz and only the watched ones.
* **A closed sim costs nothing.** When the configured port does not answer, the tool looks for the simulator's own (`wineserver`)
  sockets and tries only those; with no Wine process it tries nothing (it used to handshake with every local service, every few
  seconds, waiting up to 700 ms on each). Retries slow from 3 s to 10 s after ten failures.
* **The window is the least important thread.** It draws about four frames a second when nothing happens, at full rate only while you
  interact (and for a second after), not at all while minimised, and runs at a lower scheduling priority than the input, bridge and
  vibration threads, so the simulator wins when the machine is busy.
* **Logging does not write on every line:** warnings and errors reach the disk at once, other lines within a quarter of a second.

`tests/idle_cost.sh` runs the real program against the mock simulator and fails if it wakes up more than 60 times a second, uses more
than 1.5 % of a core when connected and idle, or sends a handshake to anything that is not the simulator. Not measured: the window's
cost on a real GPU (the virtual display used for testing renders in software, so its CPU figures do not carry over). The session log
records `GUI: N frames in 10 s` so that is easy to check on your machine.

## When the sim crashes

MSFS under Proton crashes now and then on its own (the crash reports in `.../Microsoft Flight Simulator/MSFSReport-OldCrashes/`
show stacks inside the sim's audio and UI code), so a crash is not automatically this tool's doing. To tell, every run writes
a timestamped, line-flushed log to `~/.local/state/msfs-linux-companion/session-<date>-<time>-<pid>.log` (the last 40 are kept),
including everything the tool sent to the sim. Compare the time of the sim's crash report (`TimeUTC=`, and the
`[SimConnect_connection]` section, which lists the clients that were connected) with the last lines of the log of that time.
A crash that follows a burst of commands is worth reporting together with the two files.

One crash investigation is already in the tool's history: watching new simulator values (the Guides tab does it on every
page) used to stop the old request, clear the client-data definition and define it again; the sim stopped answering a few
seconds later in two sessions. The definition is now created once per connection and never changed, and watching a new
value only appends it to the module's list.

## Permissions

If the Log tab reports "permission denied":

```
# /etc/udev/rules.d/70-msfs-linux-companion.rules
# evdev joystick nodes and Winwing hidraw nodes for the logged-in user
SUBSYSTEM=="input", ENV{ID_INPUT_JOYSTICK}=="1", TAG+="uaccess"
KERNEL=="hidraw*", ATTRS{idVendor}=="4098", TAG+="uaccess"
```

then `sudo udevadm control --reload && sudo udevadm trigger` and re-plug the device.

## Development

* `tools/mock_simconnect.py --port 5500` is a fake SimConnect server (answers Open, logs events,
  emulates the MobiFlight areas); run the app with `--port 5500` to test without MSFS.
* `tools/build_standard_catalog.py --packages <Official/Steam> --input <MSFS>/Input` regenerates `data/standard_catalog.json` from an installation (read-only, names only).
* `tools/mock_simconnect.py --aircraft Asobo_CJ4` poses as any aircraft (input events, MobiFlight client, L: variable list).
* `tools/build_catalog.py --all` regenerates `data/a32nx_catalog.json` and `data/a380x_catalog.json` from the FlyByWire docs.
* `tools/build_aircraft_catalog.py asobo_cj4` regenerates `data/asobo_cj4_catalog.json` from the event list stored in `tools/aircraft_rules/` and the rules module next to it; it refuses to run when an event has no rule. New aircraft are added by dumping their events with `--dump-controls FILE` and writing a rules module.
* `--script FILE` checks controls on a running aircraft (`set`, `toggle`, `press`, `get`, `code`, `expect`, `expectb`, `sleep`; see `src/app/Script.hpp`). It prints `ok` / `FAIL` lines and a count, and exits non-zero on failures. Restore what you changed: it does not.
* `tools/build_keypad_layout.py <flybywire-aircraft-a320-neo package> --preview FILE.png` regenerates `data/a32nx_keypads.json` (the MCDU key positions, read from the aircraft's 3D model) and writes a picture of it.
* `ctest --test-dir build` runs the config compatibility, command-line option, axis, live-catalog, curated-catalog, guide, idle-cost and live-control tests. The scripts set `MSFS_LINUX_COMPANION_ISOLATED=1`, which makes the program skip the search for a running simulator's port and the opening of joysticks and Winwing devices: a test run next to a real simulator (or with a stick in your hand) can neither reach the sim nor be disturbed by your input.
* `tools/virtual_joystick.py` is a uinput joystick for exercising bindings without hardware.
* `cmake -DCOMPANION_UI_LINT=ON` builds a variant that checks the way the windows are drawn on every frame (`src/ui/UiLint.hpp`): two widgets that share an identifier (Dear ImGui only complains about that when the widget is hovered, and a conflicting widget cannot be clicked) and the errors it repairs by itself (a `Begin` without `End`) are written once each to stderr as `[ui-lint] ...`. Run it on a virtual display, with `MSFS_LINUX_COMPANION_ISOLATED=1`, through the screens and popups you changed.
* `_cpr/` is git-ignored: put copies of files from the (read-only) MSFS install there if you need
  to edit them.

Protocol references: [node-simconnect](https://github.com/EvenAR/node-simconnect) (SimConnect wire
format), [MobiFlight WASM module](https://github.com/MobiFlight/MobiFlight-WASM-Module),
[FlyByWire A32NX flight deck API](https://docs.flybywiresim.com/aircraft/a32nx/a32nx-api/a32nx-flightdeck-api/).

## License

Copyright (C) 2026 Kyr4l. Free software under the **GNU General Public License v3.0 only** (`GPL-3.0-only`, see
[LICENSE](LICENSE)): you may use, study, share and modify it, and any version you distribute must stay under the same licence
with its source available. The source of every release is this repository at the matching tag.
There is no warranty. Third-party components (Dear ImGui and nlohmann/json under MIT, GLFW under zlib, ...) and the data
sources are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Releases up to 0.5.0 were published under the MIT licence; those copies stay MIT for whoever received them. Everything from the
next release on is GPL-3.0-only.
