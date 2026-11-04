# Vibration (Winwing URSA MINOR and friends)

Vibration only runs while you fly: not in a menu or a loading screen (MSFS 2024 keeps sending the values of the menu's aircraft there, with warnings and
touchdowns that never happened) and not while the sim is paused. The status bar says *(menu)* or *(paused)*, and the log notes each change (*Simulator: in a flight* / *in a menu*).

MSFS only vibrates XInput gamepads and DirectInput force-feedback devices; the Winwing kernel
driver does not expose the URSA MINOR motors as force feedback, so under Proton the game cannot
reach them. Instead, this tool subscribes to flight data (ground speed, touchdown, stall,
overspeed, gear/flaps motion, reversers, speed brake, G load, braking, and for a helicopter the rotor speed and the collective) and writes motor levels to
the device with Winwing's own HID protocol (output report `0x02`, opcode `0x49`), the way
SimAppPro does on Windows. Enable it in the **Vibration** tab; everything is off by default.

A helicopter has two rules of its own: *Rotor* is a steady shake while the main rotor turns, a little with the rotor speed and more with the collective, and *Low rotor speed* pulses in flight when the rotor falls below 85 % of its speed (the lower limit of the Cabri G2's rotor gauge, where its horn sounds). Both read the sim's rotor variables and do nothing on an aircraft without a rotor; the variable and its unit (a fraction of 1) were not read back on a running helicopter yet.

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
