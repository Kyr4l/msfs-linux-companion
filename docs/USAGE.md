# Using the program

Throttle levers with gates have their own page: [THROTTLE.md](THROTTLE.md).

## Getting started

> Only one copy of the tool should talk to the sim at a time (the GUI, or a `--script` / `--fire` run, not both): they share one private MobiFlight client, so one would clear the other's watched variables.

1. Start MSFS (any point, even the main menu) and this tool.
2. **Devices** tab: press buttons on the HOTAS to see their numbers.
3. **Bindings** tab: *Add binding* → *Detect* → press the button → add actions by hand or
   *From catalog* → *Save*. *Test* fires the press actions immediately.
4. Leave the same buttons **unassigned in the MSFS controls menu**, otherwise both fire.

**Which input is this control? (Cockpit tab).** Press *Start*, keep your hands off the cockpit for three seconds (what changes by itself, such
as a clock or a needle, is learned and hidden), then click or turn a control in the simulator with the mouse: the cockpit input events (B:) whose value
changes are listed, newest first, with what they changed from and to. *Bind* opens a new binding that sets the event to that value and
waits for the button, *Toggle* does the same for a switch with whole positions (0 to N-1) and steps through them at each press, *Copy* puts the
name on the clipboard. The sim is asked to announce every change (`SimConnect_SubscribeInputEvent`, all events), and every value is also read back
about four times a second, so a change is found even if one of the two does not work on your sim (the tab says which found how many). Watching stops
when you leave the tab. Only B: input events are watched: the L: variables of some aircraft (the A32NX) are too many to read back, and K: events or H
events cannot be seen from outside.

**A binding like another one.** *Copy* in a row opens the editor on a new binding that does everything the original does (its actions, its axis settings,
its group, its repeat) and waits for you to press the button or move the axis it is for: the same button would fire both. It is added to the profile,
right after the original, when you save it. *Delete* asks first. The box next to *View* filters the list: every word you type has to be in the row's name, its
input, its device, its group or one of its actions.

**Arranging the list of bindings.** *View* above the table arranges it: *Device* (one group per stick or throttle),
*Input type* (axes, buttons, hats) or *My groups*, where each binding has its own group name (the *Group* field of the
editor; *Auto-group* gives every ungrouped binding one: Throttle for throttle levers and events, otherwise the
panel section of its control in the catalog). Rows are in the order you made: **drag a row** onto another to put it in
its place (in *My groups* also onto a group's header to move it into that group), or use *Up* / *Down*. *Sort*
sorts the groups by name and the rows by device and input instead; dragging then only changes a binding's group. Groups
fold with their arrow. The choices are saved with the config.


## Tips

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

## Axes

Move an axis while *Detect* is on (about a quarter of its travel; small drift is ignored) and
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
aircraft can use: **Baro up / down** (`KOHLSMAN_INC` / `KOHLSMAN_DEC`), **STD baro** (`BAROMETRIC_STD_PRESSURE`), **Baro: local** (`BAROMETRIC`) and,
for the Garmin G1000, **G1000 baro up / down** (H events, they need the MobiFlight module). Find them with "baro", "std" or "qnh". None of them is a toggle:
according to the public source of the G1000 NXi (the C172's and DA40's avionics), `BAROMETRIC_STD_PRESSURE` only switches STD on, `BAROMETRIC` switches
STD on above 18000 ft and off below it (leaving the setting at 29.92), and the knob steps `AS1000_PFD_BARO_INC` / `_DEC` leave STD and put back the
local setting that was remembered. So the way back from STD is *G1000 baro up / down*, not a second press of the STD button. Not yet tried on a real
cockpit.

The last 1 % at each end of the travel counts as the end, so a lever that stops a little short of the range its device reports (the URSA MINOR throttles read about 99.7 % at their stop) still sends the full value.

*Use travel from/to* turns the ends of the physical travel into dead zones and *Invert* flips the
direction. `tools/virtual_joystick.py demo` creates a fake joystick to try all of this without hardware.


## Aircraft profiles

Each profile is made for one or more aircraft. The first one decides which catalog the editor and the *Catalog* tab offer; it
never changes existing bindings, so you can re-assign a profile at any time.

The **Profiles** page (on the rail, or *Manage profiles...* in Bindings) lists every profile in a table:

* **Aircraft**: click the cell to tick the aircraft the profile works with, for example an A320neo profile that also flies the
  A321. The list marks the aircraft loaded in the sim and names the other profiles that already have each aircraft.
* **Switch automatically**: when the loaded aircraft changes and the active profile is not made for it, the first profile in
  the list with this switch on that has the aircraft becomes the active one. It only happens on a change of aircraft, so a
  profile picked by hand stays. Drag the dots on the left (or *Move up* / *Move down* in the row menu) to set which profile
  wins; a line above the table names any aircraft that several switching profiles want. Without the switch, the Bindings tab
  says the profiles do not match and offers a one-click *Use "<profile>"* (or *New profile*).
* **Theme**: the colours of the companion while this profile is active, when *Use the profile's theme* is on in Settings.
* **Bindings**: how many; an empty profile offers *Copy from...* another profile.
* The row menu: *Edit bindings*, *Use this profile*, *Rename* (or double-click the name), *Duplicate* (the copy does not switch
  automatically), *Delete*.
* **New profile** adds a row for the aircraft loaded in the sim, with the switch off.
* *Catalog* has an aircraft selector for browsing the other aircraft's list ("browsing only" is shown).
* Numbered controls such as `ENG {NUM} BLEED` are expanded into one entry per unit (1 to 4, as the
  FlyByWire docs define the placeholder; the ADIRS and the air-conditioning packs stop at 3). A number
  the aircraft does not have simply does nothing.
* Catalog files are `data/<aircraft>_catalog.json`; any `*_catalog.json` placed in
  `~/.config/msfs-linux-companion/` overrides the shipped one with the same name, and `--catalog FILE`
  adds one for a single run. `tools/build_catalog.py --all` regenerates the shipped ones.
* An aircraft without a catalog file gets an automatic one (see [TESTED.md](TESTED.md)). The program finds the sim's package folders through
  the Steam libraries and the `InstalledPackagesPath` of each sim's `UserCfg.opt` under Proton; `MSFS_PACKAGES_DIRS=dir1:dir2` overrides that.


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
  in the catalog, whose steps have no source, or whose flow has dead ends. The format is in [GUIDES.md](GUIDES.md).
