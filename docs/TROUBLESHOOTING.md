# Troubleshooting

## Permissions

If the Log tab reports "permission denied":

```
# /etc/udev/rules.d/70-msfs-linux-companion.rules
# evdev joystick nodes and Winwing hidraw nodes for the logged-in user
SUBSYSTEM=="input", ENV{ID_INPUT_JOYSTICK}=="1", TAG+="uaccess"
KERNEL=="hidraw*", ATTRS{idVendor}=="4098", TAG+="uaccess"
```

then `sudo udevadm control --reload && sudo udevadm trigger` and re-plug the device.

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
