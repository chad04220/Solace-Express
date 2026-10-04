## What's new

**Loading screens**
- New pre-rendered loading pictures for all 16 airports, and an in-flight picture for every aircraft (the Kestrel, Wren, Bushmaster, Islander and Pelican now have their own too)
- `render_menu.bat` renders the main menu's montage on your GPU into `menu.mp4` (the whole 128 s loop, 1920x1080, 30 fps, every frame with its scenery complete). With that video next to the exe the main menu plays it instead of ray tracing the montage live, so the menu runs at full frame rate; without it the menu works as before

**Autopilot: flown to the edge**
- The autopilot now learns every aircraft by flying it (stall speeds, best climb, roll rate, and how hard and how quickly the airframe answers the elevator and ailerons) and flies each one to its own limits: turns at 63-87 degrees of bank, pulls up to what the structure takes less a margin for gusts, dives up to 25 degrees, climbs at full power. No passenger or comfort limiters
- **Aerobatics** (`;` key, rebindable): the autopilot flies a loop, aileron roll, barrel roll, Immelmann, split-S, Cuban eight or wingover, each sized to the aircraft (a 4 g loop in the Kestrel, 40 g in the Wraith). It dives or climbs first for the speed and height the figure needs, and any ground in the way turns the figure into a recovery. Press again to stop and level off; any stick input hands control back
- Steadier on every type: a heavy aircraft is flown with gains matched to how slowly it answers (no more porpoising Q400 on final), turns are flown on the air-relative heading with a yaw damper (no Dutch roll), and it flares earlier in aircraft that rotate slowly
- Faster to the destination (median autoland flight down from 522 s to about 420 s in the test sweep)

**Aerodynamics from the airframe's shape**
- Drag is now built up from each aircraft's geometry: the fuselage from its length and width, the wing and tails from their area and thickness, nacelles, struts, landing gear sized by the aircraft's weight (big unfaired tyres on the rough-field types), engine cooling, rivets and antennas. Skin friction follows the Reynolds number, so drag changes with speed, altitude and size like a real aircraft's. The lift slope and span efficiency come from the wing's aspect ratio, weight sets the induced drag, the wing's drag rises near its critical Mach number, and sideslip or a high angle of attack pushes the fuselage broadside through the air
- Cruise speeds and climb rates now come out of the physics instead of being calibrated in, so some aircraft fly a little differently (the Bushmaster and Starling are faster, the Caravan and Q400 a little slower at low altitude)
- Propellers are limited by their disk at low speed (momentum theory, the disk sized from the engine's power), so static thrust is realistic: every aircraft now holds full power on its parking brake, the Bushmaster and Islander included
- More power for the trainers: the Kestrel T2 now has 110 kW (was 82) and the Wren 180 has 180 kW (was 135), roughly 1500 and 1750 ft/min of climb at sea level

**Fixes**
- The climb-out from a field you'll land back at (the traffic-pattern lesson) no longer counts as a go-around in the debrief
- The research terminal's sortie bar scales to narrow windows (at 1024x768 the INITIATE button overlapped the weather buttons)
