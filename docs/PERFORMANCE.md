# Performance

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
* **Flight data only when it is used.** Vibration telemetry (20 variables) is requested only while vibration is enabled *and* a device
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
