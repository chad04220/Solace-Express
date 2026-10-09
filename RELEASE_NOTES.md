## What's new

### Flap asymmetry
- A flap failure now jams the left flap where it was, and the right one keeps following the lever. Lower the flaps past the stuck one and the aircraft rolls towards it. Before, both flaps stopped together and nothing rolled. You can see it from outside: the two flaps are drawn at their own angles.
- The warning says where the left flap stuck: put the lever back there and both flaps match again. The autopilot does this itself, flies the approach at the speed for that flap setting, and allows for the longer landing roll.

### For developers
- `Plane::flapLeft()` gives the left flap's position; the flight model and the drawing both use it. `flight_test` checks the split: the roll, the aileron needed to hold it, and the autopilot matching the lever. The `m<aircraft>f` render scene shows it from behind.
