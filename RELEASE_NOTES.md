## What's new

### Fuel estimates
- The fuel quoted for a flight now matches what the aircraft burns. It used to assume the engine ran at 84% of full power the whole way, when on average it runs at 44–62%: full power in the climb, each type's own cruise setting en route, and much less coming down. So quotes were 1.4 to 2 times the real burn. The PPL checkride in the Kestrel T2, for example, was quoted 78 kg for its 70 kg tanks and warned you to expect to run dry, when it burns about 43 kg. It's now quoted 51 kg, a 36% reserve.
- The new figure is worked out from each phase of the flight at the power it is flown at, checked against 355 autopilot flights in every ordinary aircraft. It sits about 15% above the expected burn, so nine flights in ten use less than quoted. The owned-aircraft fuel costs on the job board use the same model.

### Aerobatics
- An aerobatic figure is flown once. When it's done and the aircraft is level again, it goes back to how it was before you started: yours, if you were hand-flying, or the autopilot's route or hold if that was flying.
- Any flight input during a figure hands the aircraft straight back to you: stick, rudder, throttle, flaps, gear, brakes, trim or any flight key or button. Pressing the aerobatics key again does too. Changing the view, the HUD, the map, the radio or pausing doesn't.

### Shaders
- Shaders are now kept apart so that a change compiles only what it touches. Every aircraft is drawn with a shader of its own that holds only that aircraft's code (the Kestrel's has no Swift, Osprey or XR-40 in it), and builds its bodies with a builder of its own; the same goes for the passes that trace an aircraft and its shadows. The launch itself compiles no shader with any aircraft's code: each aircraft's are compiled as the launch prepares it, or when it is first needed, and kept in the shader cache after that.
- Each aircraft's outside body and its cockpit body are built by builders of their own, the outside one without any of the cockpit's code: a change to a cockpit rebuilds that cockpit's body alone.
- So an update that changes one aircraft compiles that aircraft's shaders and rebuilds its bodies, and nothing else: before, any change to any aircraft compiled the 14 largest programs again at launch and rebuilt every aircraft's bodies. A change to the terrain, the clouds or the scenery compiles only those programs; a change that only re-indents or re-spaces shader code compiles nothing. (This version's own first launch builds everything once more.)
- A change to code the light aircraft all share (the landing gear, the cabins' materials) still reaches every one of them - now as one smaller shader each.
- The trees, the rocks and the buildings each have their own shader too (8, 5 and 19 KB against one 30 KB shader for all three), and every program is cut to what its build runs: a light aircraft's shader is about 110 KB against the 233 KB that carried every aircraft, its body builder about 75 KB against 148 KB, the shadows-from-maps pass 11 KB against 147 KB.
- Smaller programs keep fewer registers per pixel on the GPU, so this may make the aircraft passes faster; the diagnostics' per-pass times will show by how much on your GPU.

### The g-force lens
- The old red rim is replaced by a red, bloodshot lens that closes over the view as you push the airframe to its limit. Nothing shows until 80% of the aircraft's limit load (positive or negative). From there the whole picture turns red and darker, the clear middle shrinks towards a small window, and a tree of veins reaches in from the edges towards the centre, swelling with a racing heartbeat. Past the clear middle the lens bends and softens the picture, with faint colour fringes, and its edge catches the light.
- It is most of the way over by the limit itself. The rest follows the overstress the airframe is taking, so it covers nearly everything at the moment the airframe breaks up, then clears as the wreck falls.
- The HUD goes under the lens with the scene. The pause menu, the map and messages stay clear.

### Clouds: every surface stirs them
- The wake through a cloud now comes from the aircraft's own surfaces. The flight model works out each strip's lift. Wherever that changes along a surface's span, the change leaves the trailing edge as a vortex, the way lifting-line theory has it. A clean wing trails one from near each tip, rolled up about pi/4 of the span apart. Flaps add a stronger pair at their outer edges that winds into the tips' within a few spans. An aileron moves the load and its vortex across. The tailplane trails a weaker pair the other way round, and the fin trails one in a sideslip. The research jets trail an elliptic wing's pair.
- The cloud winds round the vortex cores. The pair sinks under its own downwash, faster for a heavy aircraft or one pulling g, and towards the bank in a turn. It carries the air in its oval with it, so the channel reaches down below the path, and skimming a cloud top presses a trench into it. Propeller slipstreams twist the cloud; a jet's hot exhaust clears a core through it.
- The channel's walls are ragged and churning, and they grow rougher and patchier as it widens and fills in. An older channel pinches into a chain of bulges as the vortex pair links up into rings, and the cloud pushed aside piles up slightly round a young one.
- Cloud wisps streaming past move with the air round the aircraft. They lift ahead of the wing, swing round the nose, blow back in a slipstream, and once behind the wing the vortices catch them and turn them as they sink.
- The wingtip vapour curls inboard and down round the vortex cores, faster the harder the wing pulls.

### Fixes
- XR-30s flying as traffic look like themselves again: the formations that fly past, and the escort pair in their red-and-gold and blue-and-white display colours. Since v3.41.0 they were drawn with your own aircraft's colours, lights and engine glow, lit as if they were turned the way your aircraft is, with their panel lines sliding over the skin as they moved. Seen from the cockpit, they were also drawn clear of the haze and the clouds.
