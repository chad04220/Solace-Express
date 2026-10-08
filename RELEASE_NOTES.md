## What's new

### Fixes
- The game starts again on NVIDIA graphics. v3.35.0 stopped at "Graphics initialisation failed: Shadow proxy shader" because NVIDIA's shader compiler couldn't handle two ways the new landing gear code was written (several structures declared in one line, and small arrays declared in a list). Both are rewritten, and a test now rejects them. If a driver ever fails on that shader again, the game starts anyway: aircraft shadows then come from the shadow maps alone, and the failure is noted in startup.log.
- Landing gear doors are painted to match each aircraft. Nose-gear doors take the fuselage's livery, main-gear doors the wing's paint around them, the Q400's nacelle doors the nacelle's, and the XR-30's and XR-40's doors their own skin. They used to be bright white or bare metal.
- Seen from close underneath, an aircraft's belly no longer goes missing in jagged patches (most visible on the Q400, where the sunlit inside of the top of the fuselage showed through). The baked airframe's long triangles got their depth wrong close to the camera, so the far side of the fuselage was drawn in front of the near one. No edge of the outside meshes is now longer than half a metre, which keeps that error to a centimetre or two. The sun also fades in more softly where it only skims a curved surface.
- No hole in the cabin floor: from the cockpit, the nose-gear well cut through the floor beside the rudder pedals (seen in the Swift), showing the ground below. Wells are now only cut into the outside of the airframe.
- The Q400's leg doors (the strips of nacelle skin that ride on the main legs and close the leg slots) are now held to the legs by two brackets. They hung beside the legs on nothing, and looked like loose rods ending in mid-air.

### XR-30 and XR-40
- The main gear and its bays sit further back, under the middle of the delta wings (1.7 m behind the centre of gravity instead of 0.7 m); they stood near the wings' leading edges. The wheels the aircraft rolls on moved with them, so what you see is still what it lands on. Both still take off, land and park as before.

### XR-10 Nightjar
- Now supersonic, with reheat above 85% throttle. Two 30 kN engines and conventional controls, as before.
- In level flight at 8,000 m with 70% fuel it reaches about Mach 1.44 (it was held just under Mach 1).

### XR-20 Mantis: a new airframe
- A new shape: a low chine body, a raised single-seat canopy, a sharper forward sweep, cropped canards, and twin canted fins with a rudder each.
- One large centreline engine fed by a dorsal intake replaces the two side engines, with the same total thrust. It has one round nozzle, one exhaust plume and one exhaust light, and the aft navigation light sits on the nozzle rim. An engine failure now leaves no engine at all, but no asymmetric yaw either.
- A new single-seat centreline cockpit with live displays (one engine gauge), a side stick on the right and the throttle on the left.
- In level flight at 8,000 m with 70% fuel it reaches about Mach 2.06.
- Its folding landing gear is fitted to the new body: the mains fold inboard under the wing roots into fairings with doors, the nose leg folds aft into the belly.

### Research terminal
- The XR-10 and XR-20 cards show what the aircraft actually do: top speed (Mach 1.44 and 2.06 at 8 km), thrust-to-weight in full reheat (1.8 and 2.4), roll rate at 200 m/s (190 and 215 deg/s) and how long a full tank lasts in full reheat (12 and 30 minutes). The envelope charts and the 3D callouts match the new aircraft.

### Checks
- Every aircraft with retractable gear (Q400, Starling, Swift, Osprey, XR-10, XR-20, XR-30, XR-40) checked with the gear up, half way and down, from below, the side and above: no wheel or leg through a wing or the fuselage, and every bay inside the fuselage, wing, fairing or nacelle.
- Autoland: the XR-10 lands in all 48 test cases; the XR-20 lands or refuses with a reason in all 48, and again with the wind turning behind it on the final.
