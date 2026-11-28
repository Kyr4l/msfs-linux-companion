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
| PERF | Media: player, track, previous, play, next, volume, shuffle, repeat. Only what the player can do is shown (a radio stream has no previous, next, shuffle or repeat); the player is named as it names itself; a browser that shows twice (its own entry and KDE's browser integration) shows once, as KDE's, which follows the tab playing media. **Mute** mutes that player's program only (its sound streams, through the sound server), and shows Unmute while it is muted |
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
| PERF | Media: one page per player (← → between them; the title shows 1/3 and ←→ when there are several; it opens on the player that is playing): track, shuffle and repeat, play, PREV / NEXT TRACK. R1 APP / ALL picks what ↑ ↓ (5 % a press) and MUTE act on: the player's program or the PC's output; the level shows next to VOLUME, and the choice is remembered |
| F-PLN, SEC F-PLN | Keys and SEC keys: the keys each shortcut sends are shown above it |
| RAD NAV | Text: the keys type on the PC |
| ATC COMM | Comms: microphone, voice and sound of the voice app, your key (the network is on DATA) |
| FUEL PRED | Power: CPU and GPU watts and temperatures, fans, every other temperature |
| AIRPORT | Weather: type the airport, press 1L |
| DIR | DIR TO: type in the scratchpad, then 1L or OVFY runs it; apps on the left, searches on the right |
| PROG | Progress: a side key hides a task |

Typing goes to the scratchpad (line 14). A side key next to a field takes what you typed, or copies the field into an empty
scratchpad. CLR takes a message away, then deletes. ← → go through the monitor pages; <RETURN on them goes back to DATA INDEX.

With MSFS running the unit belongs to the aircraft. Hold key 18 (the blank key above MCDU MENU) for about a second to get the
desktop pages anyway, and hold it again to go back to the aircraft, whose MCDU shows its current page. While the desktop pages are
on, the aircraft's lamps are off, its pop-out is not read, and what the desktop pages use (media players, PC status, the keyboard of
the KEYS pages) runs as without MSFS; a shortcut of the KEYS pages goes to the window that has the focus, often MSFS. **Desktop in
flight › Switch key** on the MCDU page sets the key: click it, then press the key on the unit or on the on-screen panel (BRT and
DIM stay the brightness); **Clear** turns the switch off. The key is kept by its number on the unit, so any key works. A key that
has a job (MCDU MENU, a letter ...) is always held, so that a short press keeps doing its job; for a key that does nothing else
(18, or 28 next to AIRPORT) **Switch with** offers a long or a short press. On the on-screen panel such a key switches on a click.

With MSFS running the unit belongs to the aircraft:

| Aircraft | Screen | Keys |
|---|---|---|
| FlyByWire A32NX | from SimBridge (must be running, port 8380) | through SimBridge, otherwise H events (needs MobiFlight) |
| iniBuilds A320neo V2, A321, A330 | read off the MCDU pop-out window (Right Alt + click), see below | yes (needs MobiFlight) |
| Cessna Citation CJ4 (MSFS 2024) | its FMC, read off the FMC pop-out window, see below | the FMC's keys (needs MobiFlight) |
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
| F-PLN | F-PLN: the leg the GPS flies (FROM, TO, bearing and distance, DTK, ETE, XTK, GS, waypoint n of N). The whole plan is the G1000's own and cannot be read from outside, so it is edited on the G1000 | L5 the G1000's FPL page, R5 DIRECT TO; L6 KEYS TO G1000 (aircraft with the G1000 control pad, the Corvalis): letters, digits, CLR (backspace), OVFY (ENT), DIR and the arrows (FMS knob of the MFD) go to the G1000 |
| PROG | PROG: bearing and distance to the next waypoint, DTK, XTK, GS, TAS, wind, OAT, altitude | |
| MCDU MENU | AVIONICS (back to the last page), G1000 PFD, G1000 MFD | |

In the Heart ES-30 FUEL PRED also shows the charge of the high-voltage batteries (HV BATT) and the 28 V battery (LV BATT).

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
(`AS1000_PFD_SOFTKEYS_1` ...).

In the Heart ES-30 the **GTC** page has its two touch controllers (← → pick LEFT or RIGHT), each with three knobs: KNOB 1 and
KNOB 2 (turn, push) and the frequency knob (outer, inner, push). Checked in the aircraft: the frequency knob tunes COM1 standby
on both controllers (outer 1 MHz, inner one 8.33 kHz step). Not checked: what KNOB 1, KNOB 2 and the pushes do.

Values were read live in the MSFS 2024 C172 (G1000 NXi); **the keys were not tried in the aircraft
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

## The CJ4: its FMC

In the CJ4 of MSFS 2024 (Working Title WT21 avionics) the unit works the aircraft's FMC: the captain's or the first officer's,
by the unit's seat. Its screen is read from the FMC pop-out window as for the iniBuilds Airbuses above (pop it out with Right
Alt + click). The FMC has 15 lines and the unit 14: the message line under the scratchpad shows on the last line while the
scratchpad is empty; once you type, the scratchpad shows. The samples are a file of their own, `mcdu-glyphs-wt21.bin`, in the
same folder.

Letters, digits, line keys, CLR, `.` `/` `+/-` and SP are the FMC's own. The other keys:

| Unit key | FMC key |
|---|---|
| INIT | IDX |
| F-PLN | FPLN |
| PROG | LEGS |
| AIRPORT | DEP ARR |
| PERF, FUEL PRED | PERF |
| DIR | DIR |
| RAD NAV | TUN |
| SEC F-PLN | MFD ADV |
| DATA | MFD DATA (shows or hides the text page on the MFD) |
| MCDU MENU | DSPL MENU |
| ATC COMM | MSG |
| OVFY | EXEC (the RDY lamp lights while a change waits for it) |
| CLR held 1 s | clears the whole scratchpad (the FMC's long CLR) |
| ← ↑ | PREV PAGE |
| → ↓ | NEXT PAGE |

Checked in the aircraft (2026-11-24): every page key, letters, digits, `. / -`, CLR, the line keys on the menu pages, and the
reading of 35 pages. 2026-11-26: EXEC (a modified flight plan became the active one), the long CLR, and the reading of EXEC on the
message line: the FMC draws it as dark letters in a white box, which the reader turns over and reads as EXEC. Not checked: the first
officer's FMC, the RDY lamp on the unit (from `L:FMC_EXEC_ACTIVE`, which the FMC sets while EXEC is lit).

## Font

The MCDU page's **Font** setting chooses the screen's font: **Built-in** (the unit's own), **B612** (the font Airbus had made for
cockpit screens) or **Aircraft**, made on your machine from large captures of the aircraft's own MCDU (`mcdu_aircraft-font-21x31.json`
in the config folder; it is never shipped, the shapes are the aircraft's). B612 is sent to the unit when it is plugged in or when the setting changes (604 packets, about a second; the screen is
blank meanwhile). The unit keeps a sent font until it loses power: after choosing Built-in, unplug the unit once to get its own font back.
