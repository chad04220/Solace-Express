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
shape no terrain (the islands' ground stays bit-for-bit what it was: `runway_preservation_test`), take no slot in the
ground shader's airport table, carry no runway paint, edge lights or PAPI, and have no tower: an uncontrolled field,
its traffic calls made on the common frequency. AI traffic and the road network's airport links leave them out (a
base's town is its road node). `onRunway` reports a water lane with its surface, and every physics caller that asks
treats `SURF_WATER` as the sea, never as ground.

The islands have seven, each lane 1000 m by 60 m, surveyed so that the lane, 200 m beyond each end and 50 m to either
side lie in water at least 1.5 m deep, its approaches under a 1 in 18 slope for 3 km, as sheltered as the coast
allows. No landmark or harbour boat stands on the lane or within 600 m of its ends (`airport_layout_test`):

| Code | Base | Where |
|---|---|---|
| VBY | Verde Bay | a sheltered bay on Verdana's south-west coast, below Port Verde |
| FJW | Fjordhaven Water | the fjord above Fjordhaven, steep-sided |
| ICH | Ice Harbor | a fishing village on Nordholm's coast that no runway reaches |
| PBL | Palm Bay Lagoon | glassy water behind Palm Bay's reef |
| SLM | Saltmarsh | a marsh village on Verdana's south coast |
| WVL | Westvale | a west-coast village's harbour, open to the Atlantic swell in a westerly |
| TBY | Turtle Bay | Far Isle's resort lagoon |

**The dock** is not authored: `World::seaplaneDocks` finds it when the world is built (and again from the world
cache), as the nearest gentle shore (0.3 to 3 m above the sea) beside the lane but off it, its ends' 200 m and 90 m to
either side, from which a jetty reaches open water within 10 m, a metre deep 20 m out and 2 m deep 30 to 70 m out
across 24 m: room to moor alongside the head and turn away. The aircraft moors 25 m out (`Airport::dockHead`).

**What is drawn** (`airport_scenery.cpp` `seaplaneBase`): ten buoys, yellow cans with a cross topmark, pairing the
lane's edges at its ends, quarter points and middle; the dock (`EK_SEADOCK`: a timber pier on piles, a gangway down to
a pontoon on white floats, fenders, cleats, the fuel pump and hose reel, a life ring); a small office on dry, level
ground behind the dock; and a windsock beside it. Trees and houses keep clear of the dock. The minimap and the big map
draw the lane dashed in blue, with a ring on the big map; Free Flight lists the bases with a SEAPLANE BASE tag.

**Dispatch.** A water lane takes floats only (`surfaceOK`). The water run a float plane needs, `waterRunNeeded`, is
`max(2 x take-off roll, landing roll) x 1.15`, from the distances the aircraft has learned (or `1.5 x runwayM` before
it has learned them), floored at `runwayM` for a full cabin and lengthened 1/3000 per metre of elevation: water drag
roughly doubles a float plane's take-off run, while its landing run on the water is short. Fuel at a seaplane base
costs half as much again as at the islands' main fields. A Free Flight start at a base in an aircraft without floats
begins in the air on the approach.

### Mooring ends a flight on the water

On the water there is no parking brake to set. The same control moors the aircraft once it is all but stopped (under
1.5 m/s; faster, it says to slow down first) and casts it off again. Moored, two lines hold it from each float's bow
and stern station to where they were made fast: stiff, well-damped springs in the horizontal (a period of some six
seconds), so it rides the chop and swings a little in the gusts but stays put (`water_test`: within half a metre of
its berth in a 9 m/s wind on the beam). The leg ends moored as setting the brake ends it ashore. The airmanship bonus
for shutting down first is paid moored at the dock, as ashore it is paid clear of the runway.

A float plane's flight from a seaplane base starts moored at the dock, alongside the pontoon on the fuel pump's side
(its left float to the dock), facing out along it: cast off and taxi out to the lane. Its idle creep on the water is a
walking pace or less, so it can be stopped at the dock on the engine and the water rudders alone. The dock is solid
as its pier, gangway and pontoon (the pontoon only half a metre up), so a float that runs into it is a collision while
a wing passes over it.

### The gear advisory

An amphibian's wheels must be up for the water and down for a runway, and getting it wrong is the type's classic
accident. Sinking through 150 m within 4 km of a field, the aircraft says once what its gear is set for against what
the field is ("Gear is up for a water landing", or, wrong, "Gear is DOWN - a water landing: raise it", with a chime).
The HUD's GEAR warning follows the same rule for an amphibian: wheels down near a water lane, or up near a runway.

### The career on the water

There is no seaplane lesson in the story: the story is one chain whose place every save keeps, so a new step would move
every one after it. The water comes to the career through the job board instead. With a float plane to hand (owned or
for hire under the pilot's licence), every board carries a seaplane charter to one of the bases, a little over the going
rate. It is drawn from a random stream of its own, so the rest of the board and its long hauls are exactly what they
were. At a seaplane base every job on the board is a float plane's (the dispatch rule allows nothing else there). The pilot's profile collects the bases apart from the land airports:
"Splashdown" for the first landing on the water, "Harbour Master" for alighting at all seven, while "Every Field" still
counts the land airports alone.

### The autopilot on the water

The autoland flies a water lane as it flies a runway, with four differences. The amphibian's wheels stay up all the
way down (`ctl.gearDown` is the field's: down for a runway, up for the water). Its height above the lane is measured
to the float keels, 30 cm above where the wheels would touch (`Plane::touchHeight`). The run it needs is the water
run (`waterRunNeeded`), and its stopping distance has no brakes in it: the floats' drag stops it at about half the
deceleration wheel brakes would (`apStopNeed`). Stopped on the lane, it taxis to the dock (`APS_DOCK`,
`Plane::apDockTaxi`): out to a gate 45 m off the pontoon's head on the berth's line, then in along it, slowing with
the distance left and cutting the engine 4 m short, the water rudders steering; beside the pontoon at a walking pace
it hands over, the mooring lines are made fast, and the leg ends at the dock (`water_test`: from 6 km out to moored at
Verde Bay in under seven minutes).

The water run itself is not learned: the dispatch rule estimates it from the learned land roll (twice it, with 15% on
top), and the water test checks the Kittiwake's real run (382 m against a 195 m land roll) inside that estimate.

A float bumping the dock at a walking pace (under 2 m/s) meets its fenders: the motion toward the dock is stopped
and the aircraft is eased off. Faster than that it is a collision.

### Spray, wakes and the sound of the water

`Game::updateWaterEffects` makes the water show what the floats are doing. Spray is thrown out and up from each float
where the bow wave breaks, a third of the way back from the bow at low speed and aft to the step on the plane. It is
heaviest through the hump and thins to a sheet from the steps once on the plane, more of it the more of the float is
wet. Where the propeller's blast meets the water at high power and low speed it raises a mist. A touchdown on the water
throws a sheet of spray off each float, as hard as the arrival was, and a splash (`SFX_SPLASH`) in place of the tyres'
chirp and smoke.

The wake is drawn flat on the water (`buildSprites`): a soft patch of foam left at each float's stern sixteen times a
second, spreading and fading where it was left, and every half second a ring at its edge. Together they make a band of
white behind a planing float and a fan of rings behind a slow one. They are carried across the map's seam with the
smoke and cleared with it.

On the water the rolling sound is the floats' hiss through it: the `Water` audio surface gives the roll channel a
broad wash round 650 Hz over the chop's slap, growing with the speed.

### Seaplane bases in the generated regions

A generated region gets its seaplane bases when its world is built (`World::surveySeaplaneBases`, on the region's own
ground, before the roads and the scenery are laid): one in a small region, two in a larger one. Going out from each
town in turn, the fields' towns first, the survey takes the nearest lane centre 0.5 to 3.5 km from the town that has
water 2 m deep along the lane, 200 m past its ends and 80 m to either side. Its approaches must stay under a 1 in 18
slope for 3 km, within the chart, clear of every field and base by 3.5 km, and it must have a dock (`World::seaplaneDock`,
the same search the islands' docks use). Each lane centre gets one dock search and a region forty, so the survey stays
cheap: under 10 ms in most regions, about 150 ms in the worst one sampled (which gives up and has none). It is
deterministic, so a cached world finds the same bases. Of the first eleven generated regions in the catalog, nine have
bases, seventeen bases in all, every one with its dock.

The bases are appended after the region's own airports, so the indices its travel, long hauls and saves keep are
unchanged. The region's recipe does not know them: its count of airports, which the "Charted" achievement and the
profile's stamps go by, is the land fields'. A region's bases are not stamped. Their names, "<town> Seaplane Base",
are stored with the world (`World::seaplaneNames`, shared, so an `Airport`'s pointers survive the world being moved).
The real Earth's charts have none: their airports are the real ones.

Two places that stepped through every airport now keep to the land fields: the research craft's launch sites (nowhere
on a seaplane base to stand one) and the military reconnaissance objectives (a base has no tower or hangars).

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
