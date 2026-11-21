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
* Starting the program again opens the window of the running copy. There is never more than one.

## The MCDU

Plug the unit in; it is found by itself. All three seats (Captain, First officer, Observer) work. The **MCDU** page shows a panel on screen that works the same as the unit.

On the desktop:

| Key | Page |
|---|---|
| MCDU MENU | Menu |
| INIT | Clock |
| DATA | System: CPU, memory, every GPU, temperatures, every disk |
| PERF | Media: player, track, previous, play, next, volume, shuffle, repeat |
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

With MSFS running the unit belongs to the aircraft:

| Aircraft | Screen | Keys |
|---|---|---|
| FlyByWire A32NX | from SimBridge (must be running, port 8380) | through SimBridge, otherwise H events (needs MobiFlight) |
| iniBuilds A320neo V2, A321, A330 | read off the MCDU pop-out window (Right Alt + click), see below | yes (needs MobiFlight) |
| Others | "NO MCDU DATA" | not mapped yet |

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
