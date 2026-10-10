## What's new

### Fuel estimates
- The fuel quoted for a flight now matches what the aircraft burns. It used to assume the engine ran at 84% of full power the whole way, when on average it runs at 44–62%: full power in the climb, each type's own cruise setting en route, and much less coming down. So quotes were 1.4 to 2 times the real burn. The PPL checkride in the Kestrel T2, for example, was quoted 78 kg for its 70 kg tanks and warned you to expect to run dry, when it burns about 43 kg. It's now quoted 51 kg, a 36% reserve.
- The new figure is worked out from each phase of the flight at the power it is flown at, checked against 355 autopilot flights in every ordinary aircraft. It sits about 15% above the expected burn, so nine flights in ten use less than quoted. The owned-aircraft fuel costs on the job board use the same model.

### Aerobatics
- An aerobatic figure is flown once. When it's done and the aircraft is level again, it goes back to how it was before you started: yours, if you were hand-flying, or the autopilot's route or hold if that was flying.
- Any flight input during a figure hands the aircraft straight back to you: stick, rudder, throttle, flaps, gear, brakes, trim or any flight key or button. Pressing the aerobatics key again does too. Changing the view, the HUD, the map, the radio or pausing doesn't.

### Trees and bushes
- Trees, bushes and boulders no longer pop into view as you fly towards them. Out in the distance only a share of them is drawn, thinning out further away, and each used to switch on all at once when its turn came: trees one by one from about a kilometre out, bushes and boulders from as close as 150 m. Now each one fades in over a stretch of the distance instead, and the far edge of the forest fades out the same way, about 8 km out on High. The forest is just as dense as before.
- Checked in fast flight too: even at 600 m/s, every patch of trees is loaded at the far edge of the view, never closer.
- Trees and bushes no longer visibly switch to a simpler model as they get further away. Where they change detail (about 400 m and 2 km out for a tree on High, 240 m and 1 km for a bush), the nearer model now dissolves into the simpler one over the last stretch before the switch.

### Where the game keeps its cache
- What the game builds for your machine (the compiled shaders, the islands, each aircraft's learned performance and its body meshes) now lives in its own folder, `%LOCALAPPDATA%\SolaceExpress`, instead of a `shadercache` folder next to the game. It's in Local rather than Roaming because it belongs to this machine: the shaders are compiled for your GPU and driver, and a roaming profile would copy hundreds of MB between computers at every sign-in. Saves and settings stay in `%APPDATA%\SolaceExpress`.
- The first launch moves an existing cache over (instantly, on the same drive), so nothing is rebuilt that didn't have to be. If the game is on another drive, the old cache is cleared instead. Only if Local can't be written does the game fall back to the old places.
- Deleting the folder is safe: the next launch builds it again.

### Diagnostics v3
- `diagnostics.bat` finds the cache in its new place. It also collects the shader compile log (how long each program took, first launch and later), and the error log if the game left one. It reports the cache's size and its largest files, and times a new scene: a low flight over the island's longest forest, which measures what the trees cost, their fade-ins and detail cross-fades included.
