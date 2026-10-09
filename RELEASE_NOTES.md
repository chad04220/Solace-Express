## What's new

### Faster research jet cockpits
- The XR-30 and XR-40 cockpits are drawn with a shader of their own. Before, they used one that carried every aircraft's code, and as the airliner and light aircraft cockpits grew, the XR-40's cockpit took about 3 ms longer to draw (the XR-30's about 4 ms) than in v3.34.
- The textures on every aircraft's surfaces are read once per pixel on a flat face instead of three times. That is about 15% off drawing a cockpit.
- The research jets' displays are drawn half at a time, every frame, in a smaller format. Before, all of them were drawn every other frame, which put about 3 ms on alternate frames: a stutter at 60 fps.

### Fixes
- Rain no longer streams across the inside of the XR-30 and XR-40 cockpits. Their windows are displays, so there is no windscreen for it to run down.

### Flap asymmetry
- A flap failure now jams the left flap where it was, and the right one keeps following the lever. Lower the flaps past the stuck one and the aircraft rolls towards it. Before, both flaps stopped together and nothing rolled. You can see it from outside: the two flaps are drawn at their own angles.
- The warning says where the left flap stuck: put the lever back there and both flaps match again. The autopilot does this itself, flies the approach at the speed for that flap setting, and allows for the longer landing roll.

### Diagnostics
- The diagnostics run once and stop. Before, opening the results folder at the end started the whole run again, and the new run deleted the last one's results.
- The system report gives the graphics card's full memory. Before, any card with 4 GB or more showed 4095 MB.
- If the shader cache can't be set aside for the first-launch timing, the diagnostics say so (`cache_aside.txt`).
- A new scene is benchmarked, photographed and analysed: the XR-40's cockpit at night in a storm, coming in low over Solace Capital.

### For developers
- `Plane::flapLeft()` gives the left flap's position; the flight model and the drawing both use it. `flight_test` checks the split: the roll, the aileron needed to hold it, and the autopilot matching the lever. The `m<aircraft>f` render scene shows it from behind.
- Shader builds: `FLEET_ON`, `JET_ON` and `WRAITH_ON` (plane_common.glsl) say which aircraft a build carries. The aircraft mesh pass has four builds (every aircraft, `AF_LIGHT`, `AF_JET`, `AF_WRAITH`) and `drawPlaneMesh` picks each aircraft's own. `shader_check` validates all of them.
- `wr_<mode>_<yaw>_<pitch>_<dist>_<secs>` takes an optional airport, weather, hour and a low pass over the nearest town: `wr_8_0_-8_0_1_3_2_22.5_1`.
