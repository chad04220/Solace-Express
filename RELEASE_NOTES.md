## What's new

### Fuel estimates
- The fuel quoted for a flight now matches what the aircraft burns. It used to assume the engine ran at 84% of full power the whole way, when on average it runs at 44–62%: full power in the climb, each type's own cruise setting en route, and much less coming down. So quotes were 1.4 to 2 times the real burn. The PPL checkride in the Kestrel T2, for example, was quoted 78 kg for its 70 kg tanks and warned you to expect to run dry, when it burns about 43 kg. It's now quoted 51 kg, a 36% reserve.
- The new figure is worked out from each phase of the flight at the power it is flown at, checked against 355 autopilot flights in every ordinary aircraft. It sits about 15% above the expected burn, so nine flights in ten use less than quoted. The owned-aircraft fuel costs on the job board use the same model.

### Aerobatics
- An aerobatic figure is flown once. When it's done and the aircraft is level again, it goes back to how it was before you started: yours, if you were hand-flying, or the autopilot's route or hold if that was flying.
- Any flight input during a figure hands the aircraft straight back to you: stick, rudder, throttle, flaps, gear, brakes, trim or any flight key or button. Pressing the aerobatics key again does too. Changing the view, the HUD, the map, the radio or pausing doesn't.

### Shaders
- Shaders are now kept apart so that a change compiles only what it touches. Every aircraft is drawn with a shader of its own that holds only that aircraft's code (the Kestrel's has no Swift, Osprey or XR-40 in it), and builds its bodies with a builder of its own; the same goes for the passes that trace an aircraft and its shadows. The launch itself compiles no shader with any aircraft's code: each aircraft's are compiled as the launch prepares it, or when it is first needed, and kept in the shader cache after that.
- So an update that changes one aircraft compiles that aircraft's shaders and rebuilds its bodies, and nothing else: before, any change to any aircraft compiled the 14 largest programs again at launch and rebuilt every aircraft's bodies. A change to the terrain, the clouds or the scenery compiles only those programs; a change that only re-indents or re-spaces shader code compiles nothing. (This version's own first launch builds everything once more.)
- A change to code the light aircraft all share (the landing gear, the cabins' materials) still reaches every one of them - now as one smaller shader each.
- The trees, the rocks and the buildings each have their own shader too (8, 5 and 19 KB against one 30 KB shader for all three), and every program is cut to what its build runs: a light aircraft's shader is about 110 KB against the 233 KB that carried every aircraft, its body builder about 75 KB against 148 KB, the shadows-from-maps pass 11 KB against 147 KB.
- Smaller programs keep fewer registers per pixel on the GPU, so this may make the aircraft passes faster; the diagnostics' per-pass times will show by how much on your GPU.
