# Throttle calibration: levers with gates

Some throttles have mechanical gates on the lever: the Winwing URSA MINOR has a cut-off stop below the idle
detent and an afterburner gate at the front. The sim's own calibration cannot teach an aircraft those points
(some aircraft have no throttle calibration at all), so the program does it:

1. **Calibrate the axis once** (Devices tab, *Calibrate* next to the axis, or from the binding editor).
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
2. **Tick *Gates*** in the binding of that axis (per profile, so each aircraft gets its own values). The editor shows a graph
   of what the lever sends over its travel, with the live position of the lever.
   * **Range**: *Idle to end* uses everything above the idle gate (an aircraft without afterburner: the cut-off part is left
     out, idle is the lowest value, the end of the travel the highest); *Idle to max gate* ends at the max gate and keeps the
     afterburner zone at the top value (the F/A-18E, whose afterburner is its own event).
   * **Values**: what the sim gets in the cut-off, at idle (exactly that value while the idle gate is closed) and at the top.
   * **Stages**: named points on the range (an Airbus: CL, FLX/MCT, TOGA). At a stage the value is the stage's value, held
     over its *Band* so the lever snaps into it; between stages it follows the lever linearly. Put the lever where the stage
     should be and press *Set*, or drag the stage in the graph. Positions are a share of the range, so they survive a new
     calibration.
   * **Presets**: *Linear* (idle to end, the whole range of the event, the cut-off sends idle; ticking *Gates* starts from it),
     *Airbus (FlyByWire)*, *F/A-18E left / right*. A preset sets values, not the event, except the F/A-18E one.
3. **Gate actions**: lists of actions that run once when the lever enters or leaves the cut-off or the afterburner
   zone (for example the aircraft's afterburner event on and off, or a fuel cut-off).

The *Airbus (FlyByWire)* preset is for `THROTTLEn_AXIS_SET_EX1` on the A32NX / A380X. It sends the middle of each region of the
default FlyByWire throttle mapping (`fbw-common` `ThrottleAxisMapping.cpp`, axis -1..1): idle -0.5 (-8192), CL 0, FLX/MCT 0.5 (8192),
TOGA 1 (16384), with FLX/MCT on the lever's max gate and TOGA at its end. Between idle and CL the thrust is manual, as on the real
lever. If the aircraft's own throttle calibration (flyPad) was changed, set the stage values to match it. The cut-off sends idle;
with reverse on the axis enabled in the flyPad, set it to -16384 for full reverse. Not yet run on the aircraft.

The *F/A-18E* presets fill in what the aircraft needs (the lever is a fraction of its travel: 0 is idle, the
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

## Recipes and what the cut-off zone does

**Start from** shows the lever recipes for the profile's aircraft as cards, the others in *Other recipes*. A recipe fills in the range,
the stages and what the zones do, with the engine number of the lever (`{n}`); after that the lever is edited as usual, and the editor
says *changed* (with *Back to the recipe*) when it no longer is what the recipe made. *Save as recipe...* keeps the lever in
`my_levers.json` in the settings folder, with its engine numbers turned back into `{n}`. Shipped recipes are in `data/levers.json`:
Airbus (iniBuilds) with reverse in the cut-off (checked on the A320neo V2), Airbus (FlyByWire), F/A-18E, business jet (Working Title CJ4).

**What it does**, on the *Cut-off zone* tab: *Nothing* (the lever only sends its cut-off value), *Custom actions* (your own lists)
or **Reverse thrust**. Reverse thrust is run by the program, for aircraft whose reverse is a switch and whose lever then sets the
amount (the iniBuilds A320neo: `K:THROTTLE_REVERSE_THRUST_TOGGLE`, one switch for both engines):

1. the lever enters the cut-off: reverse switches on (on the ground only, if ticked) when it is off;
2. once the aircraft reports reverse (*Reverse is on when*, calculator code), the engine goes to the reverse amount;
3. the lever leaves: the engine goes to reverse idle;
4. reverse switches off once no lever that shares the switch is in its cut-off.

The amount is never sent unless the aircraft reports reverse: on these aircraft the same axis value without reverse is forward
thrust. The second lever acts 300 ms after the first, so two levers pulled together switch reverse once. *What it sends* lists the
exact actions; it is the sequence checked step by step on the A320neo V2 (engines at idle, parking brake set).

