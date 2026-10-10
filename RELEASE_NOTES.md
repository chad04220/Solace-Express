## What's new

### Fuel estimates
- The fuel quoted for a flight now matches what the aircraft burns. It used to assume the engine ran at 84% of full power the whole way, when on average it runs at 44–62%: full power in the climb, each type's own cruise setting en route, and much less coming down. So quotes were 1.4 to 2 times the real burn. The PPL checkride in the Kestrel T2, for example, was quoted 78 kg for its 70 kg tanks and warned you to expect to run dry, when it burns about 43 kg. It's now quoted 51 kg, a 36% reserve.
- The new figure is worked out from each phase of the flight at the power it is flown at, checked against 355 autopilot flights in every ordinary aircraft. It sits about 15% above the expected burn, so nine flights in ten use less than quoted. The owned-aircraft fuel costs on the job board use the same model.

### Aerobatics
- An aerobatic figure is flown once. When it's done and the aircraft is level again, it goes back to how it was before you started: yours, if you were hand-flying, or the autopilot's route or hold if that was flying.
- Any flight input during a figure hands the aircraft straight back to you: stick, rudder, throttle, flaps, gear, brakes, trim or any flight key or button. Pressing the aerobatics key again does too. Changing the view, the HUD, the map, the radio or pausing doesn't.

### Shaders
- Every aircraft is now drawn with a shader of its own that holds only that aircraft's code: the Kestrel's has no Swift, Osprey or XR-40 in it. The same goes for the program that builds each aircraft's body. A light aircraft's program is about 129 KB instead of the 233 KB that carried every aircraft (the XR-30's 107 KB); a body's builder is 56–93 KB instead of 148 KB. Each one is compiled the first time its aircraft is prepared at launch and kept in the shader cache after that.
- An update that changes one aircraft now rebuilds only that aircraft's bodies at the next launch, not every aircraft's. (This version's own first launch builds them all once more.)
- The trees, the rocks and the buildings each have their own shader too (10, 5 and 24 KB instead of one 30 KB shader for all three), and every other program is cut to the build it is: the light-aircraft objects pass went from 251 to 174 KB, the light aircraft's shadows and effects from 147 and 164 to 108 and 125 KB.
- Smaller programs keep fewer registers per pixel on the GPU, so this may make the aircraft passes faster; the diagnostics' per-pass times will show by how much on your GPU.
