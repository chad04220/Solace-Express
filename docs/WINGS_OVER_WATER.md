# Wings over Water (4.0)

Somnium Volandi is a game of islands, and until 4.0 the sea between them was only something to fall into: touch it
anywhere off a runway and the flight ended, "Ditched in the sea". Version 4.0 makes the water a place to fly from and
to: a float plane with its own physics, seaplane bases in the harbours, fjords and lagoons, and villages that no
runway reaches. This note is the plan and the record of the decisions behind it. Where it and the code disagree, the
code wins: fix the note in the same change.

## What it is, in a page

- **The Kittiwake A9 Amphibian.** A single-turboprop utility amphibian on amphibious floats: nine seats, the wheels
  retracting into the floats. Down, it lands on a runway; up, on the water. A Commercial pilot's aircraft, rented at
  the seaplane bases or bought from the hangar.
- **Water as a runway.** Floats with real hydrostatics and hydrodynamics: they float at their draft, push a bow wave
  through the hump, climb onto the step and plane, and leave the water when the wing can carry the aircraft. Water
  rudders steer it at taxi speeds; the wind weathervanes it when it stops. There is no brake on the water.
- **The classic mistakes, simulated.** Land on the water with the wheels down and the aircraft goes over on its nose.
  Land on a runway with them up and it slides on its keels, damaged. Run onto a rock or a beach too fast and it is
  aground. Rough water off an open coast is a hard landing waiting to happen; a sheltered lagoon is glass.
- **Seaplane bases.** Water aerodromes in the islands: a marked water lane between buoys, a dock with a shed and a
  windsock, and a fuel float. Some sit beside the islands' airports (the harbour below Port Verde, Fjordhaven's fjord);
  others serve villages no runway reaches.
- **The career on the water.** Jobs to and from the water bases, flown in the amphibian; the board dispatches by what
  each field allows (a water lane needs floats, a gravel strip doesn't take an amphibian's small wheels).
- **The autopilot on the water.** It learns the amphibian's water take-off and landing distances by flying them, as it
  learns the runway's, lands on a water lane into wind, and taxis to the dock.

## Decisions

### Floats are gear, derived from the model row

The physics and the drawn aircraft must agree on where the floats are, as they already do for the wheels
(`gearStations`). A float plane sets `floats` in its `AircraftSpec` row and gear type 5 in its `ModelDef` row.
`floatStations(spec)` derives each float from what the packed model already carries: its centre line on the mains'
track, its step at the mains' station, its keel 30 cm above the wheels' contact, and its length, beam and depth as
fixed shares of the fuselage's length (`kFloatLength`, `kFloatBeam`, `kFloatDepth` in `aircraft.h`). The shader
(`plane_parts.glsl` `floatSt`) derives the same hull from the same packed numbers, so no new model fields were needed.
No physics keys on an aircraft's index: anything with floats floats.

### The amphibian's wheels fold into its floats

Each main wheel swings forward and up into a well in its float, about a pivot inside it; each bow carries a castoring
nose wheel that folds a quarter turn aft. The two bow wheels are one rigid part (the nose gear's), drawn straight. The
water rudders are drawn folded up at the sterns, as they fly; on the water their steering is the physics' alone.

### The water is the sea at sea level

The islands have one water surface that matters: the sea, at height 0, wherever the ground is below it and no land
runway is laid. (The Fjordhaven reservoir stays scenery.) On it rides a wind-driven chop, a sum of a few travelling
waves down the wind, whose height grows with the wind and the open water upwind of the aircraft (its fetch): a
lagoon or a fjord stays calm when the open coast is rough. The renderer's sea is a normal map on a flat plane, so the
chop is kept within the height a float can visibly ride (a few tens of centimetres).

### The hydrodynamic model

Each float is sampled at stations along its keel. At each wetted station:

- **Buoyancy:** the weight of the water its submerged section displaces, the section a V-bottom's.
- **Planing pressure:** a normal force on the bottom proportional to the water's dynamic pressure and to the bottom's
  local angle of attack to the flow (Savitsky's planing lift is close to linear in trim), plus a quadratic heave
  damping, so the float climbs onto the step as the speed builds and settles back as it falls.
- **Drag:** skin friction on the wetted surface, the planing pressure's own aft component, and a wave-making drag that
  peaks at the hump (a length Froude number near one) and is gone once the float is on the step.
- **Lateral resistance:** the keel resists sideways motion strongly; with the fin aft of the floats' centre of lateral
  resistance, the aircraft weathervanes into the wind.
- **Water rudders:** at the floats' sterns, down below 15 knots and raised above, steering with the rudder pedals.

The wheels, down, add their drag at their stations under the water: the nose wheels at the bows dig in and the
aircraft goes over on its nose. On land with the wheels up the keels are skids, survivable when gentle, as a belly
landing is.

### Seaplane bases are airports with a water surface

A water aerodrome is an `Airport` with `SURF_WATER`: its centre, heading, length and width describe the water lane.
They are appended to the islands' table, so every existing airport keeps its index (saves, stamps, the story). They
shape no terrain and clear no scenery (the islands' ground stays bit-for-bit what it was), carry no runway paint, edge
lights or PAPI, and have no tower: an uncontrolled field, its traffic calls made on the common frequency. AI traffic
doesn't use them. `onRunway` reports a water lane only to the callers that ask for one; the physics never mistakes
one for ground.

### Mooring ends a flight on the water

On the water there is no parking brake to set. The same control moors the aircraft when it has stopped at its dock (or
anywhere slow within the base): the leg ends there as setting the brake ends it ashore, with the same airmanship bonus
for shutting the engine down first.

## Phases

Each phase is built, tested and committed on its own.

1. **Float hydrodynamics** in the flight model, with tests: the static draft, a water taxi, the take-off run through
   the hump onto the step, a landing on the water, the wheels-down landing going over, a keel landing on a runway.
2. **The Kittiwake A9:** its specification and model rows, the floats and amphibious wheels in the distance field,
   the water rudders, its cockpit, sound and loading picture; the roster's tables and tests.
3. **Seaplane bases in the islands:** surveyed water lanes, `SURF_WATER` through every airport consumer, buoys, docks
   and windsocks drawn, and the maps' symbols.
4. **Water operations:** starting afloat, mooring, touchdown grading on the water, gear advisories ("gear is up for a
   water landing"), the common-frequency calls.
5. **The career:** dispatch by surface, jobs to the water bases, the amphibian in the hangar, progression tests.
6. **The autopilot:** learned water distances, autoland on a water lane, taxiing to the dock.
7. **Spray, wakes and water sounds.**
8. **The generated regions:** seaplane bases in their lagoons, sounds and fjord heads.
