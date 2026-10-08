## What's new

### Landing gear
- Retractable gear now folds up into the aircraft instead of sliding straight up through it. No wheel or leg shows through the top of a wing at any point while the gear cycles, and every well sits inside the fuselage, the wing or a nacelle, never in a flap or aileron.
  - Swift S6 and XR-10: the mains swing forward and turn their wheels flat into the wing. The turn is finished while the wheel still hangs below the wing.
  - Starling, Osprey and XR-20: the mains fold inboard and lie flat under the wing root in a streamlined fairing with doors.
  - Meridian Q400: the mains fold forward into the engine nacelles, with a door for the leg.
  - XR-30 and XR-40: slimmer main wheels swing into fairings under the wing and lie flat. The nose legs fold aft into wells along the belly.
  - Nose gear on every retractable type folds aft into a well that follows the belly, with doors that close flush.
- Stowed wheels lie deep enough that their hub caps stay under the wing's skin too, allowing for the wing thinning towards its trailing edge and the dihedral across the wheel. The caps used to break through the top of the wing once the gear was up.
- The gear you see is the gear the aircraft rolls on. The physics wheel positions and the drawn wheels now come from one shared set of gear stations. The Q400's mains, for example, now touch down where they are drawn, under the nacelles.

### XR-40
- The VTOL pod tops are clean, flat facets instead of a crumpled white look.

### Cockpits
- The bezel round each instrument cluster is one material all the way round. Parts of it showed as a different surface.
- XR-20 and other cockpits: the near edge of the sun visor is smooth, not jagged, and sits flush with the headliner.
- The light aircraft's cockpits no longer run the research craft's window shading, so they do less work each frame.

### Loading screen
- The status text no longer runs into the percentage. Long steps shrink a little, drop their notes in brackets, then shorten.
- The bar moves steadily from start to finish. It no longer sits at one percentage for most of the wait.

### Autoland and autopilot (review of v3.34.0)
- With passengers or a fragile load aboard, a stall or an upset now comes first: near the stall, or pitched or banked far, the autopilot flies the aircraft's whole envelope until it is well clear, then returns to the gentle limits. It never pushes below a quarter of a g while recovering. Before, the gentle limits held the nose up until a Bushmaster went over the top.
- The gentle flare for passengers and fragile loads starts higher in slow aircraft and holds power until the wheels are nearly on. In the gusty test cases every touchdown is now at about 400 fpm or less (the Q400 at Solace Capital: 2.2 m/s before, 1.0 now).
- A loaded aircraft no longer flies into the runway after a bumpy final. The glidepath correction carried a "push down" into the flare and took away half of its pull. A fully loaded passenger Starling at Port Verde now touches down at 1.5 m/s instead of 3.6.
- The stop is checked again down the final as the wind changes. If the runway can no longer stop the aircraft with the tailwind it now has, the autopilot goes around (outside the last 800 m) and plans the approach afresh, picking the other end if that one is safe. Before, it landed anyway and could run off the end.
- When the autopilot can't land at the field you picked (at engagement, or after such a go-around), it now circles where it is, clear of the ground, and tries the field again every 20 seconds in case the wind has changed. Before, it held its heading and could fly on into the hills.
- Full test matrix after these changes: every case lands or is refused with a reason, in both the normal and the gentle style, and again with the wind turning 120 degrees and rising to 8 m/s from the moment the final is captured (0 failures).

### Career (review of v3.34.0)
- The fuel you choose for your own aircraft is the fuel you're billed for. The route quote used to replace your choice in the bill after loading.
- A survey's altitude record carries across a diversion. Time flown outside the band before diverting still counts when the job is finished.
- If you have rebound a control, lesson hints that name it are no longer read out with the default control's name.

### Graphics (review of v3.34.0)
- Temporal anti-aliasing works from the camera's position, so moving scenery stays sharp far from the map's centre.
