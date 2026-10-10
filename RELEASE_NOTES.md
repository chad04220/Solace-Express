## What's new in v3.45.1

### Fixed: the ground lost its textures
- In v3.45.0 the ground lost its textures: hillsides and fields came out snow-white, and runways, fields, towns and roads were missing. A grey stripe could also run straight across beaches and shallows.
- The cause was the change that made the map wrap. It swapped the ground's height with its north–south position in the terrain shader, so the shader thought every patch of ground was thousands of metres up. It also looked for the runways and roads in the wrong place.
- The fix puts the height back. A test now checks that the shader keeps the height in place.

### Correction to the v3.45.0 notes
- The roads cross water and deep valleys on bridge spans, but the bridges themselves aren't modelled yet. For now, a road stops at the water's edge and carries on from the far side. The bridge decks are next on the list.

## What's new in v3.45.0

### Fly one way forever
- **The islands now sit in 10 km of open sea, and the map wraps at its edges.** Fly east past the sea and you come in from the west, over the far side's islands. The same holds north and south and on the diagonals. There's no edge of the chart any more: no warnings to turn back, and no flight is lost out there.
- **You see the far side's islands before you cross.** As you head out to sea, their hills and towns come up on the horizon. The sea, its waves and swell, the clouds and the rain all repeat with the map, so there's no visible join where you cross.
- **The crossing doesn't disturb the flight.** The aircraft, the air it's flying in (the gusts and eddies carry on as they were), the camera, the smoke, the wingtip vapour, the wreckage and the traffic flying near you all move across together.
- **The GPS and the minimap show the far side.** Across the seam they draw its islands, airports and checkpoints. The bearing, distance, ETE and route-remaining figures, and the target marker in the world, all take the shorter way round the map.
- **The autopilot takes the short way too.** Sent to a field across the seam, it flies over the open sea to get there instead of turning back over the islands. Its approach patterns still stay on the field's side of the seam.

### Roads
- A real road network now runs across the islands: 104 km of highway and 256 km of roads. It joins all 26 settlements and every airfield on an island that has one.
- The roads are built into the ground, with cuts and embankments. Each class has its own markings: dual carriageways with a central reserve, two-lane roads with centre and edge lines, lanes, and gravel tracks.
- Kaleo's empty eastern plain now has a port city (Kailani), a market town (Canefield) and three villages. Palm Bay's island, Far Isle and Nordholm's coast have a village each.

### Fixes from the v3.44.0 review
Claude and Codex both reviewed v3.44.0 in depth. Everything they found that affects play is fixed here, each with a test:
- **Diagnostics never touch your career or settings.** `diagnostics.bat` flies its crash scenes on a fresh career in memory and writes no save, settings or station file. It used to charge the crashes to your career.
- **Medevac and VIP:** rough air costs only the g it puts on the aircraft. Five minutes of straight and level flight on the autopilot in P4's air used to leave the patient at 10%.
- **Airline:** your routes fly by the minutes you fly, not one leg for every landing. Ten 90-second circuits used to pay ten legs.
- **Radio panel:** a click on a station no longer reaches the button beneath it. One click over the Hangar used to buy a Wren; in the XR-40 it fired the weapons.
- **Autoland:**
  - It flies the tower's go-around. It used to land anyway, and you were fined $500.
  - If it can't climb away, it lands, and that isn't held against you.
  - With no power, it glides a straight-in final it can reach, or tells you none is in reach. A Kestrel 2.5 km out used to ditch.
  - In passenger mode it settles onto the runway past the aim point instead of floating. A loaded Q400 floated 850 m and ran off Meadowbrook.
  - A go-around keeps the gear down until it is climbing clear.
- **Split-S:** the research jets climb to the height the figure needs before starting it. From 700 m they used to fly into the sea.
- **Job quotes:**
  - The background flight now steers its take-off roll. 12 of 61 quoted take-offs used to run off the runway, and their cards waited forever.
  - Deadlines come from the planned flight in its own weather, plus a quarter: 37 of 40 sampled timed jobs can now be made, up from 29.
  - P4 allows 20 minutes.
  - A4 accepts only the Starling, the type its client asks for.
- **Jobs with checkpoints:** you can take off again from a field's runway, or set the parking brake there to end the leg as a diversion. The next leg is planned, quoted and drawn over the checkpoints still left. A hold or go-around you ignored on one leg is still charged when the job is delivered.
- **Tower:**
  - Traffic that lines up after you're cleared to land goes around.
  - Nothing lines up in front of you on final.
  - After a long hold the tower clears the traffic that is stuck, never you onto it.
  - An autoland to a GPS alternate talks to that field's tower.
  - A hold counts from where you are once you've heard the call.
- **Sound:**
  - The radio follows the master volume.
  - Pausing mid-call brings the music back up.
  - A hazard warning is dropped once the hazard has passed.
  - Damaged voice clips are refused rather than played.
- **World:**
  - The craters you see are the ones the wheels, weapons and wreckage meet.
  - What a plasma bomb flattened stays flattened.
  - A wreck on a scenery boundary is solid on both sides of it.
- **Settings and menus:**
  - The UI scales to the window's width as well as its height.
  - The Windows window can't be shrunk below 800 × 600.
  - Toggles keep the keyboard focus as their labels change.
  - The hub tabs, settings pages and key bindings can be reached with the keyboard and the D-pad.
  - The FOV readout fits its box.
- **Twin engines:** an engine that has stopped no longer burns fuel. A twin on one engine burns half.
- **Saves:** malformed saves are rejected. These include two aircraft of the same type, an open loan with no payment, one aircraft on two routes, and negative reputation.
- **Research flights** start with their own fuel, and a test card is signed off only once it is on record.

### Correction
- The v3.44.0 notes said the new textures use about 180 MB more GPU memory. The real figure is about **317 MiB**; the 180 MB was measured from an earlier, smaller set. Low quality loads the same 2K set.

### Good to know
- **The first launch after updating takes longer, once.** The islands are regenerated, because the roads are now built into the ground.

Please run `diagnostics.bat` on this version.
