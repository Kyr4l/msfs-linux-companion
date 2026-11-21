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

Shapes: `key` (a button with a lit bar), `label`, `tag` (a framed label), `knob`, `lever`, `toggle`, `rocker`.

## Finding where a device stores its colours (Winwing)

Reading is safe: the settings of a Winwing controller are read 4 bytes at a time with opcode `0x05`. Dump them, change one
colour in SimAppPro (on Windows), dump again, and compare: the bytes that changed are that LED. Never send other opcodes to
find out: a wrong write can change the device's stored settings.

## Devices

| Device | Status |
|---|---|
| URSA MINOR Combat Throttle Metal.EX (4098:b970) | 3 states of 12 LEDs at 0x210 / 0x240 / 0x270 of controller 0x40/0xb9; which part each LED lights is inferred from one profile and a photo, not confirmed |
| URSA MINOR Combat Joystick R (4098:bc2a) | white, dimmable; no stored colours found (controllers 0x0a/0xbf and 0x20/0xbb); one byte 0x80 at 0xEC of 0x20/0xbb may be the brightness, not confirmed |
| WINCTRL MCDU | 3 brightness levels and 9 lamps, set live by the companion (see MCDU.md) |
| Saitek X52 Pro | not yet: 11 LEDs that are red, amber or green, set over USB as libx52 does |
