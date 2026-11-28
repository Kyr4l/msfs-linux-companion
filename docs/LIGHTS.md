# Lights: device files

The **Lights** page shows the colours stored in a device, drawn on a picture of it. It is **read only** for now: nothing is
written to a device.

Each device is described by one file, `data/<name>_lights.json`. Adding or correcting a device means sharing that one file.
A file in `~/.config/msfs-linux-companion/` (Flatpak: `~/.var/app/io.github.Kyr4l.MsfsLinuxCompanion/config/msfs-linux-companion/`)
or in a plugin's `data/` folder replaces the shipped one for the same USB id.

## The file

```json
{
 "name": "URSA MINOR Combat Throttle",
 "usb": "4098:b970",
 "driver": "winwing",
 "winwing": {"device": "0x40", "family": "0xb9"},
 "aspect": 1.5,
 "states": [{"name": "Start-up", "offset": "0x210"}, {"name": "Normal", "offset": "0x240"}],
 "leds": [{"id": 1, "name": "B labels", "guess": true}],
 "parts": [{"text": "B1", "shape": "key", "rect": [0.03, 0.30, 0.12, 0.14], "text_led": 1, "bar_led": 3}]
}
```

| Key | Meaning |
|---|---|
| `name` | shown on the page |
| `usb` | vendor and product id, hexadecimal |
| `driver` | how the colours are read: `winwing` (more later) |
| `winwing` | the controller that stores the colours (`device`, `family`), as `--probe-winwing` lists them |
| `aspect` | width / height of the picture |
| `states` | the lighting states and where each one is stored: 4 bytes per LED (red, green, blue, brightness), in LED order |
| `leds` | the LEDs in stored order; `guess: true` while nobody has confirmed which part an LED lights |
| `parts` | what is drawn: `rect` is `[x, y, width, height]` from 0 to 1; `text_led` colours its text, `bar_led` its bar |
| `settings` | values the device stores, shown read only: `name`, `kind`, the controller (`device`, `family`), the 4-byte word (`offset`) and its `byte` (0 to 3). Kinds: `percent` (0..255), `on_off` (0 = off), `off_on` (0 = on), `key_axis` (1 key, 2 axis, 3 both). `save: true` lets the page save a light setting to the device (see below) |
| `levels` | lights whose brightness the page sets live: `name`, the controller, the light's `index`, and the `setting` it starts from. Live means the light command only (Winwing `0x49`): the device forgets it when unplugged, nothing is saved |

`states`, `leds` and `parts` are optional for a device that only has settings or levels.

Shapes: `key` (a button with a lit bar), `label`, `tag` (a framed label), `knob`, `lever`, `toggle`, `rocker`.

## Finding where a device stores its colours (Winwing)

Reading is safe: the settings of a Winwing controller are read 4 bytes at a time with opcode `0x05`. Dump them, change one
colour in SimAppPro (on Windows), dump again, and compare: the bytes that changed are that LED. Never send other opcodes to
find out: a wrong write can change the device's stored settings.

## Saving to a device (Winwing)

A setting marked `save: true` gets a **Save** button (a level) or **Turn on / Turn off** (a switch). Saving writes one 4-byte word
of the controller's stored settings with opcode `0x06`, as SimAppPro does (`[02 dev fam 00 00 08 06 offset x3 data x4]`, acknowledged
with `[06 offset x3]`). Before it, the words around it are read and kept as a backup file in
`~/.local/state/msfs-linux-companion/winwing-backups/` (the newest 20); after it, they are read again and only the changed byte may
differ, otherwise the original word is written back. Key / axis modes cannot be saved yet. Checked on the URSA MINOR Fighter Stick R
(2026-11-28): backlight and breathing light saved and kept after unplugging.

## Devices

| Device | Status |
|---|---|
| URSA MINOR Combat Throttle Metal.EX (4098:b970) | 3 states of 12 LEDs at 0x210 / 0x240 / 0x270 of controller 0x40/0xb9 (which state is which is not known). The LEDs, from SimAppPro2's definition of this throttle: key backlight, panel labels, B8 to B1, mode selected, mode others. Settings: three inputs set to key, axis or both (which controls they are is not known yet) |
| URSA MINOR Fighter Flight Stick R (4098:bc2a) | from SimAppPro2's definition: backlight on the J5 base (controller 0x20/0xbb), light 0, stored in byte 0 of word 0xEC (0..255); breathing light in byte 0 of word 0xF8 (0 = on). The page reads both and sets the backlight live |
| WINCTRL MCDU | 3 brightness levels and 9 lamps, set live by the companion (see MCDU.md) |
| Saitek X52 Pro | not yet: 11 LEDs that are red, amber or green, set over USB as libx52 does |
