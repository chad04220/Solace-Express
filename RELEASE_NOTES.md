## What's new

A new flight model worked out from each aircraft's own shape, all thirteen cockpits redesigned, and an afterburner flame you can see in daylight.

**Back up your career before trying this version.** It saves in a new format (4). It reads your existing saves, but older versions can't read the new ones.

### A new flight model
- Every career aircraft, the XR-10 and the XR-20 now fly on forces worked out from the shape the game draws: the wing, the tail or canard, the fin, the fuselage and the propellers. Before, each type flew on a table of numbers. The XR-30 and XR-40 keep their fly-by-wire models.
- Each part of the wing meets its own air. Rolling and yawing change the air along the span, a gust can catch one wing before the other, the propeller's slipstream and swirl blow over the wing roots and the tail, and the wing's downwash reaches the tail a moment later. Near the ground, ground effect builds under the wing.
- Stalls happen part by part: the wing doesn't let go everywhere at once, and one wing drops. A control surface does less at large deflections, as a real one does.
- Each aircraft is balanced the way its designer would balance it: the centre of gravity ahead of the point where the lift changes, the tail set so it flies hands-off at cruise, and a little aileron and rudder trim built in against the propeller's torque.
- Engines push along their own lines. A propeller twists the aircraft, swings its nose on a climb at full power (P-factor) and blows over the tail. A stopped propeller windmills on a single and is feathered on a twin.
- Flaps add lift and drag the way real ones do: the first notch is mostly lift, and the last is mostly drag.
- Twins can lose an engine and be held straight with rudder down to their minimum control speed. The Islander's and Osprey's fins are a third bigger so that is a realistic speed.
- The controls move as far as drawn: the elevator 28 degrees up and 20 down, the rudder 29 each way. What you see the surfaces do is what the aircraft feels.
- The landing gear stands where a designer would put it for the balance point. The main wheels are a little behind the centre of gravity on a nosewheel aircraft and a little ahead of it on the Bushmaster. The wheels you see are the ones it rolls on.
- Stall speeds are the ones you can actually reach with the elevator. The XR-20's canard stalls before its wing, so it now approaches at about 140 kt with no flap and needs more runway (Meadowbrook's is now too short for it). The Bushmaster's elevator runs out just before its wing does.

### On the ground
- Tyres grip by how far they are slipping sideways, as real ones do. On the runway the rudder can now kick out a crab, and crosswind landings stay much nearer the centreline.
- Toe brakes: braking while steering with the rudder brakes that side's wheel harder. It is how a taildragger is kept straight under braking. The parking brake still holds both wheels.
- The landing gear takes a firm arrival without springing back into the air.
- At full power on the parking brake, the lighter powerful types and the jets now slide, because the thrust beats the tyres' grip. Run up at part power.

### The autopilot
- It flies the new model: rudder trim for the propeller's swirl, and on a twin with an engine out it takes power off the live engine when full rudder can no longer hold it.
- Crosswind rollouts: it holds rudder into the wind and keeps the centreline.
- Short fields: full flap and a slower approach (1.3 times the stall speed), the speed the landing distances are measured at.
- The flare puts the main wheels down first, never pushes the nose over, and adds power if a gust balloons it.
- The approach now clears the trees and buildings under the final as well as the ground.
- The XR-10 and XR-20 fly an ordinary final. Only the XR-30 and XR-40 can rear up and shed their speed with a belly-up.
- The Bushmaster holds the stick back on its rollout and brakes only once the tail is down.

### Cockpits
All thirteen cockpits have been redesigned:
- **Trainers and tourers (Kestrel, Wren):** distinct analog panels, compact centre sticks, and power levers mounted at the sides.
- **Bushmaster:** raised panels and central dogleg power levers.
- **Islander, Pelican, Osprey:** full-size yokes on floor columns, with twin or single power controls.
- **Q400 and Starling:** glass displays and full-size yokes. The Starling has a status display up on the glareshield.
- **Swift:** compact yokes and a central row of push-pull controls.
- **XR-10:** a glass cockpit with a raised power bank.
- **XR-30 and XR-40:** new research panels and displays.
- **XR-20:** keeps its single-seat layout.

The cockpit view zooms smoothly onto what you look at. The engine displays show the actual number, type and health of the engines. The glass cockpits' engine page no longer shows "DRY" on aircraft without an afterburner.

### Afterburner
- The afterburner flame is visible in daylight on the XR-10, XR-20, XR-30 and XR-40. It is hot and nearly white at the nozzle, with shock diamonds, and turns orange then red downstream. Before, it was a faint beige smear with a smoky trail, and the trail is gone.

### Aircraft numbers
The aircraft table in the README is regenerated from the new flight model. Most runway needs moved by a few percent. The Q400 needs 780 m (it was 1,129 m), because it now leaves the ground in a shorter roll.

### For developers
- New `src/aero_strips.cpp` (with `aero.h`): the strip model and each type's calibration, about 4.5 microseconds per evaluation. `tests/aero_test.cpp` checks every type's geometry, calibration, trim from cruise down to 1.1 Vs0 with flap, the signs of the stability and damping derivatives, and every control.
- The performance learner also measures how far each type's nose comes up on a full pull at 1.7 Vref (`rearPitch`, which gates the belly-up). A taildragger's landing roll is measured as the autopilot flies it.
- `Scenery::obstacleTop(x, z, r)` gives the highest treetop or roof near a point. The approach planner clears these by half the ground's margin.
- Autoland sweep: 846 of 882 cases land, 36 are declined (runway too short, the wrong surface, or terrain), and none fail. With crosswind the miss from the centreline is smaller than before for every type: the Q400's median went from 7.3 m to 1.4 m.
- Debug: `GAVTHR=<throttle>` runs the `gav_` scenes' engines (reheat from 0.85). `CKCTL=pitch,roll,yaw,throttle` holds the `ckv` cockpit scenes' controls. The sweep's `APDBG` output now includes the peak-g moment and the flap and speed asked for on the final.
