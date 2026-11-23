# Desktop controls and the WINCTRL MCDU

## Desktop controls (replaces antiMicroX)

While MSFS is not running, joysticks can move the mouse, turn the wheel and press keys. The moment MSFS starts, everything held is let go and the virtual mouse
and keyboard are removed; they come back 15 seconds after it has stopped.

1. Open the **Desktop** page and **Import** your antiMicroX profile (`.amgp`). The joystick has to be plugged in. Anything that could not be taken over is listed.
2. Close antiMicroX. Both programs running together move the mouse twice. **Disable** next to "antiMicroX starts at login" stops it from coming back.
3. Turn **Desktop controls** on.

Imported dead zones, speeds, axis modes and mouse curves are the ones antiMicroX uses. Only set 1 of a profile is imported. Turbo, toggle, set switching, easing curves and stick or virtual d-pad
modes are not supported.

An axis bound to the wheel scrolls in high resolution: the further it is pushed, the faster it scrolls, up to **Wheel max** at full travel, following the curve. A button scrolls in whole notches.

The page lists every axis with its live position. Select one to set its dead zone, max zone, mode, preset, the action of each side, its speed and its curve.

## Running in the background

* **Close to tray**: closing the window leaves the program in the tray. The window, its GPU context and its fonts are freed until it is opened again.
* **Start at login**: starts in the tray with your desktop session (`--tray`).
* **Tray icon** (Settings › Appearance): **Colour** (the logo, a heading indicator), or the logo in one colour to match the panel: **Light** for a dark panel, **Dark** for a light one.
* Starting the program again opens the window of the running copy. There is never more than one.

## The MCDU

Plug the unit in; it is found by itself. All three seats (Captain, First officer, Observer) work. The **MCDU** page shows a panel on screen that works the same as the unit.

On the desktop:

| Key | Page |
|---|---|
| MCDU MENU | Menu |
| INIT | Clock |
| DATA | System: CPU, memory, every GPU, temperatures, every disk |
| PERF | Media: player, track, previous, play, next, volume, shuffle, repeat. Only what the player can do is shown (a radio stream has no previous, next, shuffle or repeat); the player is named as it names itself; KDE's browser integration is skipped when the browser has its own entry |
| F-PLN | Keys: 12 shortcuts you choose on the MCDU page |
| RAD NAV | Type: the keys type on the PC (CLR deletes, OVFY is Enter) |
| FUEL PRED | Power: GPU watts, fans, every temperature |
| SEC F-PLN | Keys 2: 12 more shortcuts |
| ATC COMM | Comms: microphone, voice app mute and deafen with their real state, download and upload speed |
| DIR | Direct To: open your 6 apps through KRunner (press twice), or type something and search for it |
| PROG | Progress: every task that reports a progress bar to the desktop. A side key hides a task |
| AIRPORT | Weather: the METAR of your airport. Type its four letters, then press 1L |
| BRT / DIM | Screen brightness |

The side keys work on every page; the MCDU page of the program lists what they do on the page on screen. Under **System page** you choose the look (list or bars), how
GPUs are shown (both, one with a switch key, or on their own page) and how disks are shown (a switch key, a list to pick from, or all on a page). NVIDIA cards need the NVIDIA driver to show numbers.

**Load colours** colour the CPU, memory, GPU and disk loads: the normal colour, then **Warning** above one percentage and
**Alert** above another (by default amber above 80 % and red above 95 %). Each stage can be turned off, each colour chosen, or
the whole thing turned off to show every load in the normal colour.

### Airbus style

**Desktop pages › Style** on the MCDU page switches the desktop pages to an **Airbus** layout (**Classic** stays the default).
The function keys open the same pages, laid out like the aircraft's MCDU: small white labels over large values, green for what
is on or measured, cyan for what you can change or press, amber boxes for an entry still to make.

| Key | Page |
|---|---|
| MCDU MENU | DESKTOP (back to the last page), mute, lock the PC, screen off |
| INIT | Local and UTC time, 24H/12H, chrono, home airport |
| DATA | DATA INDEX: PC status, and a monitor each for CPU (and its cores), memory, GPU, disks, network and power |
| PERF | Media: player, track, shuffle and repeat, play, mute, PREV / NEXT TRACK; ↑ ↓ change the volume |
| F-PLN, SEC F-PLN | Keys and SEC keys: the keys each shortcut sends are shown above it |
| RAD NAV | Text: the keys type on the PC |
| ATC COMM | Comms: microphone, voice and sound of the voice app, your key (the network is on DATA) |
| FUEL PRED | Power: CPU and GPU watts and temperatures, fans, every other temperature |
| AIRPORT | Weather: type the airport, press 1L |
| DIR | DIR TO: type in the scratchpad, then 1L or OVFY runs it; apps on the left, searches on the right |
| PROG | Progress: a side key hides a task |

Typing goes to the scratchpad (line 14). A side key next to a field takes what you typed, or copies the field into an empty
scratchpad. CLR takes a message away, then deletes. ← → go through the monitor pages; <RETURN on them goes back to DATA INDEX.

With MSFS running the unit belongs to the aircraft:

| Aircraft | Screen | Keys |
|---|---|---|
| FlyByWire A32NX | from SimBridge (must be running, port 8380) | through SimBridge, otherwise H events (needs MobiFlight) |
| iniBuilds A320neo V2, A321, A330 | read off the MCDU pop-out window (Right Alt + click), see below | yes (needs MobiFlight) |
| Others (Cessna 172 and its G1000 NXi, light aircraft, helicopters) | the program's own avionics pages, see below | yes (needs MobiFlight) |

### Avionics pages (aircraft without an MCDU)

For an aircraft whose MCDU is not known, the unit shows pages of the program's own, laid out like an Airbus MCDU and made from the
sim's standard variables and events (read and sent through the MobiFlight module), so they work in most aircraft. Green: actual
values and engaged modes; cyan: values to change; white: labels and actions.
Temperatures (**Aircraft without an MCDU › Temperatures** on the MCDU page): as the aircraft shows them (engine in °F, outside air
in °C), all in Celsius, all in Fahrenheit, or both (the other unit small, on the label line).
Fuel (**Fuel**, same place): gallons, litres, kilograms, or gallons or litres with the mass small on the label line. The mass
uses the fuel's weight per gallon as the aircraft reports it (avgas about 6 lb, jet fuel about 6.7 lb).

| Key | Page | Line keys |
|---|---|---|
| RAD NAV | RADIO COMM, RADIO NAV (← → or RAD NAV again) | 1: active, 2: standby, 3: SWAP; NAV 4: CRS |
| PERF | FCU: SPD, HDG, ALT, V/S, the mode line (lateral, vertical, AP FD) | 1-2: targets; 3-5: HDG, ALT, NAV, V/S, APR, FLC; 6: AP, FD |
| ATC COMM | XPDR: code, mode (STBY, ON, ALT), IDENT, QNH, STD | L1 code, R1-R3 mode, L2 IDENT, L4 QNH, R4 STD / QNH (back to the local setting the G1000 remembered: one click of its baro knob, checked on the C400) |
| FUEL PRED | FUEL PRED: ETE and fuel at the next waypoint, FOB, FF, left/right, endurance, range | |
| DATA | ENGINE: RPM or N1, FF, oil, EGT, CHT | |
| PROG | PROG: bearing and distance to the next waypoint, DTK, XTK, GS, TAS, wind, OAT, altitude | |
| MCDU MENU | AVIONICS (back to the last page), G1000 PFD, G1000 MFD | |

A value is typed in the scratchpad, then put in its field with the field's line key; the key of a field with an empty scratchpad
copies the field into the scratchpad. Frequencies as `11830`, `118.3` or `118.305`; squawk as four digits; QNH as `2992`, `29.92`
or `1013`; V/S with **+/-** for a descent. A wrong entry gives FORMAT ERROR or ENTRY OUT OF RANGE and stays in the scratchpad;
CLR removes the message, then the last character. The sim does not give waypoint names this way: the next waypoint is "NEXT".

The G1000 PFD and MFD pages list the G1000's functions by name: DIRECT TO, FLIGHT PLAN, PROCEDURES, ENTER, CLEAR, MENU,
the FMS knob (L4 outer, L5 inner: one click per press, +/- first turns it back), RANGE in and out (R4, R5), CURSOR (push the FMS
knob) and SOFTKEYS> (the page with the 12 softkeys, 1-6 on the left and 7-12 on the right). ← → go through PFD, PFD SOFTKEYS, MFD and
MFD SOFTKEYS. The unit's own keys stay shortcuts there: DIR = Direct-to, F-PLN = FPL, PERF = PROC, DATA = MENU, CLR = CLR,
OVFY = ENT, ↑ ↓ = FMS inner knob; MCDU MENU leaves. The softkey labels are not shown: the G1000 does not tell them.

In the Cirrus Vision Jet (G3000 with touch controllers) MCDU MENU offers **GTC** instead: the page of one touch controller (← → pick
GTC 1, 2 or 3) with its softkeys PFD, MFD and NAV/COM, the frequency knob (outer, inner, SWAP), the map knob (turn, push) and the
checklist wheel (turn, push). A knob turns one click per press; +/- first turns it back. The keys are the aircraft's own cockpit
events, sent as calculator code (`1 (>B:AS3000_TSC_1_SOFTKEY_1_Set)`); setting a cockpit event that way was checked on the SF50, the
GTC keys themselves were not tried yet. The event names come from the Working Title G1000 NXi source
(`AS1000_PFD_SOFTKEYS_1` ...). Values were read live in the MSFS 2024 C172 (G1000 NXi); **the keys were not tried in the aircraft
yet**, nor the transponder mode, or leaving STD on the G1000.

The Weather page asks aviationweather.gov for the report of your airport while the page is on screen, at most every 10 minutes. Only the airport code is sent.
The Comms page mutes through the sound server, so it works whichever window has the focus. **Mic** is the default microphone for every program. **Voice** and **Deafen** act on the audio
streams of the voice app set on the MCDU page (GoofCord by default). The voice app does not know about it: its own mute icon stays as it is, and the others simply hear silence.

The Progress page reads the name of the program and the percentage. It does not read or keep the text of notifications. Programs that do not report progress to the desktop do not appear.

## What has been tested

The USB protocol comes from community sources (see `THIRD_PARTY_NOTICES.md`) and **has not been tried on a real unit yet**. Automated tests cover the packets, key decoding, pages,
import and mouse maths (`tests/desktop_mcdu.cpp`). To check on the real unit: screen colours and alignment, every key, the lights, brightness, the light sensors.

## The iniBuilds Airbuses: reading the pop-out

These aircraft draw their MCDU in their own compiled code and give the text to no program. The companion reads it from the
simulator's pop-out window instead: pop the MCDU out (Right Alt + click on its screen) and leave the window open, anywhere.
About 8 pictures a second are read through the X server, at the lowest priority, and only the character cells that changed are
compared again. Nothing runs inside the simulator. Measured: about 1 ms per picture, no change in frame rate; 99% of the
characters right on pages of both aircraft.

The comparison needs samples of the aircraft's characters, in `~/.local/share/msfs-linux-companion/mcdu-glyphs.bin`
(Flatpak: `~/.var/app/io.github.Kyr4l.MsfsLinuxCompanion/data/msfs-linux-companion/`). They come from the aircraft's font, so
they are not part of this repository; without them the unit shows "NO CHARACTER SAMPLES" and the keys still work.
The Flatpak needs X11 access for this (`--socket=x11`).

## Font

The MCDU page's **Font** setting chooses the screen's font: **Built-in** (the unit's own), **B612** (the font Airbus had made for
cockpit screens) or **Aircraft**, made on your machine from large captures of the aircraft's own MCDU (`mcdu_aircraft-font-21x31.json`
in the config folder; it is never shipped, the shapes are the aircraft's). B612 is sent to the unit when it is plugged in or when the setting changes (604 packets, about a second; the screen is
blank meanwhile). The unit keeps a sent font until it loses power: after choosing Built-in, unplug the unit once to get its own font back.
