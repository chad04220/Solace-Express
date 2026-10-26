# The hangar: floats and the mechanic

4.0 put the water on the map with one aircraft built for it, the Kittiwake A9. This pass lets the aircraft you
already own use it too. Six types can take a set of amphibious floats in place of their wheels, bought once and
swapped on and off. A mechanic in every hangar sells six kits that make an owned aircraft perform better. Both are
fitted to one airframe, your own: a rental of the same type comes on its wheels and stock.

## One airframe as it is fitted

Each owned aircraft carries a `Fit` (aircraft.h): whether its floats are on, and a bit for each kit. `fitted(type,
fit)` (airframe_fit.cpp) returns that airframe's `AircraftSpec`. It is a copy of the type's row with the floats and
the kits applied, made once and kept for the run, so a pointer to it is stable and can key the caches below. A stock
fit returns the row itself, so nothing changes for a stock aircraft. `typeOf(spec)` gives back the type of any spec,
fitted or not, through its `fittedFrom` field.

What follows from the fitted spec:

- **The flight model** flies it. Its empty weight, power, fuel capacity, lift and gear are the spec's own fields.
  Two new fields carry the rest: `dragFactor` scales every drag area (aero.cpp `aeroDragAreas`, `aeroCD0`), and
  `propFactor` scales a propeller's static and dynamic thrust (aircraft.cpp `thrustAt`).
- **The aerodynamic models** (aero.cpp, aero_strips.cpp) are built once per fitted spec and kept in a map beside the
  per-type tables.
- **The trim, the rigging and the learned envelopes** stay at the type's design load (`designFuel()`). Floats get
  their own learned slot (`2 × kAircraftCount` slots, aircraft_perf.cpp): their water take-off and landing distances
  are learned from their own runs, cached and saved like the type's. The kits change the numbers too little for a
  slot of their own and use the type's.
- **The career** plans, quotes, settles, fuels and dispatches by the airframe it flies: `Career::airframe(fleetIndex)`
  for your own, `specFor(type, source)` for a job card, `Game::airframeFor(type, source)` for a flight. A job quote
  is flown in the background on the airframe that takes it, and the quote cache's key carries its fit.
- **The burn** of a piston or turboprop is set from the type's row, with the power fitted over the row's power: a
  tuned engine burns what it makes, and long-range tanks hold more without burning faster.

## Floats

### What takes them

`floatKitFor(type)`: a high wing, a piston or turboprop, under 4 t empty, and not on floats already. That covers six
types:

| Type | Why it suits floats |
|---|---|
| Dawnlark T2 | the trainer; real two-seat trainers are flown on floats |
| Ambler 180 | the four-seat tourer, the commonest float plane there is |
| Scrubrunner STOL | the bush plane: floats are where it lives |
| Skerry Twin | the light twin: its engines sit high on the wing, clear of spray |
| Longshore Hauler | the utility turboprop, the classic heavy single on floats |
| Thistledown L4 | the high-winged single turboprop |

Left out: a low wing (its tip meets the water in a bank and its spray hits the wing), the Archipel (twelve tonnes),
every jet, and the research craft. The Kittiwake is built on its own floats.

### What fitting them does

`fitFloats` (airframe_fit.cpp) gives the airframe the same floats the Kittiwake flies on, sized to its fuselage:

- **The floats' proportions** are the Kittiwake's (aircraft.h `kFloatLength`, `kFloatDepth`, `kFloatBeam`): a float
  as long as most of the fuselage, its deck three-quarters of a tenth of the fuselage's length below the belly at the
  step, on struts. The track puts each float's centre line out past the fuselage's side, and never under a tenth of
  the span. That keeps the propeller's tips about half a metre over the water at rest, as on a real float plane.
- **Amphibious:** the wheels fold up into the floats. Wheels down it uses a runway, wheels up the water. A rough strip
  is no place for small amphibious wheels, so floats close the rough strips. A taildragger stands on its floats'
  bows.
- **The cost of the floats:** a sixth more empty weight (the floats and struts). The take-off weight limit stays the
  type's, so the floats come out of the useful load. Four per cent more drag goes on top of the hulls' own drag in the
  aerodynamics, for the struts, spreader bars and fittings. The book cruise and range drop by a twelfth.
- **The hydrodynamics** are 4.0's (docs/WINGS_OVER_WATER.md): buoyancy at the floats' own draft, the bow wave and the
  hump, the step, water rudders, weathervaning, the gear-down-on-water nose-over, beaching.
- **The pictures:** the type's own model, with the Kittiwake's floats and struts drawn under it (the
  `AF_FLOATS` shader define). Each float variant is its own renderer model (`kAircraftCount + type`). Its shader
  programs and its mesh are built on the player's PC the first time it flies on floats, behind that flight's loading
  screen, and kept in the caches for the next launch. The launch's prewarm doesn't build them, so a player who never
  fits floats never waits for them. The hangar's preview and the traffic draw the type as it left the factory; drawing
  a variant not yet built there would stop the menu while it compiled. Nothing new is packaged, so the build pipeline
  and the download don't change.

As fitted, from `hangar_test` and the numbers' probe (half fuel, a third of the cargo):

| Type | Empty kg (wheels → floats) | Useful load kg | Cruise km/h | Range km | Draft / float depth m | Propeller over the water m | Off the water in |
|---|---|---|---|---|---|---|---:|
| Dawnlark T2 | 530 → 615 | 342 → 258 | 180 → 166 | 770 → 708 | 0.31 / 0.60 | 0.43 | 209 m |
| Ambler 180 | 820 → 951 | 742 → 611 | 216 → 199 | 1,150 → 1,058 | 0.35 / 0.68 | 0.52 | 222 m |
| Scrubrunner STOL | 760 → 882 | 995 → 873 | 198 → 182 | 1,100 → 1,012 | 0.37 / 0.66 | 0.44 | 105 m |
| Skerry Twin | 1,750 → 2,030 | 1,945 → 1,665 | 234 → 215 | 1,400 → 1,288 | 0.47 / 0.89 | 1.80 (wing engines) | 340 m |
| Longshore Hauler | 2,150 → 2,494 | 2,820 → 2,476 | 306 → 282 | 1,700 → 1,564 | 0.52 / 0.94 | 0.67 | 463 m |
| Thistledown L4 | 930 → 1,079 | 571 → 422 | 248 → 229 | 1,600 → 1,472 | 0.36 / 0.72 | 0.63 | 627 m |

The Ambler flown level at 75% power: 66.6 m/s on its wheels, 62.5 on floats.

### Buying and swapping

- **A set of floats** costs 22% of the aircraft's price (a Dawnlark's $3,960, a Longshore's $26,400). It is fitted
  at a land field, where the aircraft stands on its wheels.
- **Swapping** them off and on again costs a mechanic's fee: 1.5% of the price, never under $150. The floats wait in
  the hangar while it flies on its wheels; once bought, they stay with that aircraft.
- **The work is done where the aircraft stands:** it must be parked at the field you're at, not flying a route and
  not on an open job (`Career::workshopReady`).
- **On floats**, your own aircraft takes the jobs a float plane can: a seaplane base's, and the board's seaplane
  charters, which count it as a float plane to hand. On wheels it takes a runway's jobs, as before.
- **At the sale** an aircraft is worth half of what was fitted to it, over its own value.
- **In Free Flight** the six types have an "On floats / On wheels" button beside the aircraft.

## The mechanic

The hangar's MECHANIC section lists the kits for the aircraft you own, each with what it does and its price, and
fits one with a click. A kit, once fitted, stays fitted; kits stack, and they work with floats on or off.

| Kit | Effect (airframe_fit.cpp) | Fits | Price (share of the aircraft's) |
|---|---|---|---:|
| Engine overhaul and tune / power section upgrade / engine uprate | 8% more power (6% more thrust for a jet). It burns as much more. | every career type | 12% |
| Composite propeller | 5% more propeller thrust from the same power, 15% more at the start of the roll | propeller types | 6% |
| Speed kit / winglets and gap seals | 6% less drag (4% for a jet) | every career type | 7% |
| Lightweight interior | 5% off the empty weight: that much more payload under the same limit | every career type | 8% |
| Long-range tanks / auxiliary tanks | 30% more fuel, 1% more empty weight. The burn is unchanged. | every career type | 5% |
| STOL kit | 10% more lift, 8% more from the flaps, 1% more drag, 12% shorter runways in the book | propeller types | 6% |

The weight limit stays the type's whatever is fitted: long-range tanks filled to the brim eat into the payload, and
the lightweight interior gives it back.

Measured on the Ambler 180: the engine kit adds 3% to the cruise and 8% to the burn; long-range tanks take its
cruise endurance from 5.3 to 6.9 hours; every kit together flies it 6.5% faster (66.6 → 70.8 m/s at 75% power).

Prices for the career types:

| Type | Price | Floats | Swap | Engine | Propeller | Aero | Light | Tanks | STOL |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Dawnlark T2 | 18,000 | 3,960 | 270 | 2,160 | 1,080 | 1,260 | 1,440 | 900 | 1,080 |
| Ambler 180 | 30,000 | 6,600 | 450 | 3,600 | 1,800 | 2,100 | 2,400 | 1,500 | 1,800 |
| Scrubrunner STOL | 40,000 | 8,800 | 600 | 4,800 | 2,400 | 2,800 | 3,200 | 2,000 | 2,400 |
| Skerry Twin | 85,000 | 18,700 | 1,270 | 10,200 | 5,100 | 5,950 | 6,800 | 4,250 | 5,100 |
| Longshore Hauler | 120,000 | 26,400 | 1,800 | 14,400 | 7,200 | 8,400 | 9,600 | 6,000 | 7,200 |
| Archipel 400 | 600,000 | - | - | 72,000 | 36,000 | 42,000 | 48,000 | 30,000 | 36,000 |
| Emissary 500 | 260,000 | - | - | 31,200 | - | 18,200 | 20,800 | 13,000 | - |
| Cutwater S6 | 68,000 | - | - | 8,160 | 4,080 | 4,760 | 5,440 | 3,400 | 4,080 |
| Wavecrest C6 | 68,000 | - | - | 8,160 | 4,080 | 4,760 | 5,440 | 3,400 | 4,080 |
| Thistledown L4 | 46,000 | 10,120 | 690 | 5,520 | 2,760 | 3,220 | 3,680 | 2,300 | 2,760 |
| Galleon 180 | 1,600,000 | - | - | 192,000 | - | 112,000 | 128,000 | 80,000 | - |
| Kittiwake A9 | 165,000 | - | - | 19,800 | 9,900 | 11,550 | 13,200 | 8,250 | 9,900 |

## The save

A career with anything fitted saves as version 9, with one line for each fitted aircraft:
`plane_fit <fleet index> <kit bits> <floats owned> <floats on>`. A career with nothing fitted writes the version it
always did, so it still loads in an older build; a version 9 save needs this build. Fuel in the tanks is kept to the
fitted capacity on load.

## Tests

`hangar_test` (ctest `hangar`):

- what takes floats and which kits fit each type; a stock fit is the type's own row; a fitted airframe is made once
  and knows its type; nothing fits a research craft;
- each kit's effect, the weight limit kept, the tanks' unchanged burn and longer hours, the tuned engine's burn, every
  kit faster and floats slower in level flight;
- each of the six types on floats: the floats below the belly, the propeller clear of the water, afloat at its draft,
  off the water in under 1,200 m, and both water lanes and runways open to it;
- the career: an Ambler on wheels refused a job to a seaplane base, floats bought at a land field, then the job taken;
  a rental of the type on its wheels; tanks fitted once; the sale value; no work while it's parked elsewhere, nor a
  swap on the water; the swap and its fee; the save as version 9 and loaded with its fit; a career with nothing fitted
  saved as version 6, as before.
