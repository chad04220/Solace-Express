# Real fuel

Until this pass the tanks were sized to the islands' distances: a trainer flew 45 km on fifteen minutes of fuel, an
airliner 170 km, and the XR-40 emptied six tonnes in eighty seconds of boost. Now every type carries the fuel an
airframe of its class does, and burns it at a real rate, so a full tank is hours of flying. A long haul across the
real Earth, or around it in the XR-40, is a matter of tanks, not of the game's limits. The missions that are about
fuel give you a fixed, short load instead.

docs/JET_FUEL.md keeps the jets' engine model (thrust times TSFC, idle, reheat, metering at 240 Hz). This page
supersedes its research-jet numbers and its note that the propeller types kept their old calibration.

## Tanks and burn

Flown with full tanks and half the cargo, level on the altitude hold at the type's cruise power (pistons and
turboprops at 2,000 m, jets at their cruise altitude), from `fprobe` over the flight model:

| Type | Tank kg | Cruise TAS m/s | Burn kg/h | Endurance h | Range km (book) | Full power |
|---|---:|---:|---:|---:|---:|---|
| Dawnlark T2 | 70 | 58 | 15.5 | 4.5 | 942 (770) | 3.4 h |
| Ambler 180 | 150 | 64 | 26.6 | 5.6 | 1,286 (1,150) | 4.3 h |
| Scrubrunner STOL | 200 | 66 | 34.2 | 5.9 | 1,382 (1,100) | 4.4 h |
| Skerry Twin | 370 | 63 | 58.7 | 6.3 | 1,424 (1,400) | 4.8 h |
| Longshore Hauler | 1,000 | 71 | 171 | 5.9 | 1,500 (1,700) | 4.4 h |
| Archipel 400 | 4,500 | 130 | 1,346 | 3.3 | 1,561 (1,600) | 2.5 h |
| Emissary 500 | 2,200 | 201 | 365 | 6.0 | 4,352 (4,350) | 81 min |
| Cutwater S6 | 200 | 85 | 34.6 | 5.8 | 1,772 (1,500) | 4.4 h |
| Wavecrest C6 | 390 | 78 | 54.7 | 7.1 | 1,992 (1,800) | 5.4 h |
| Thistledown L4 | 235 | 66 | 34.6 | 6.8 | 1,602 (1,600) | 5.2 h |
| Kittiwake A9 | 1,000 | 80 | 179 | 5.6 | 1,605 (1,450) | 4.2 h |
| Galleon 180 | 20,000 | 233 | 2,848 | 7.0 | 5,883 (5,800) | 106 min |
| XR-10 Vigil | 2,200 | 240 | 765 | 2.9 | 2,485 (2,500) | 8.5 min on full reheat |
| XR-20 Reverie | 3,200 | 241 | 735 | 4.4 | 3,776 (3,700) | 8.4 min on full reheat |
| XR-30 Nocturne | 6,000 | 290 | 1,504 | 4.0 | 4,164 (4,100) | 7.9 min on full reheat |
| XR-40 Somnus | 6,000 | 251 | 348 | 17.2 | 15,577 (15,000) | 2.1 h on full boost |

"Full power" is the tank divided by the flow at full throttle low down; at altitude it lasts longer.

- **The propeller types** burn by their row: the flow at full power is the tank over the book range's time at cruise,
  at 80% power (aircraft.cpp `fuelFlowMax`), and it falls with power to a fifth at idle. A fitted engine
  (docs/HANGAR.md) burns in proportion to the power it makes. The tanks kit holds more without burning faster.
- **The reheat jets** (XR-10, XR-20, XR-30) are fighter-realistic: dry TSFC 1.2-1.9 × 10⁻⁵ kg/(N·s), wet 1.7-2.6 ×
  10⁻⁵. About eight minutes on full reheat, three to four hours at cruise.
- **The XR-40's pods** are a fictional power plant and burn by their power setting, not a turbofan's thrust. Full
  boost burns 6,000 kg in two hours. The dry burn runs from 0.10 kg/s at idle to 0.24 kg/s at 85% spool, and the
  boost adds 0.56 kg/s at full. Thinner air takes up to 15% off. That is two hours at full boost, seven
  at full dry power and seventeen at cruise: like a strategic bomber, it can fly for hours at full throttle.
- **The flight model is unchanged.** Trim, rigging, the learned take-off and landing envelopes and the weight limits
  stay at each type's design load (`AircraftSpec::designFuel()`, the old tank sizes). Real tanks filled to the brim
  are heavy: the take-off weight limit decides how much of them a full cabin leaves room for
  (`maxFuelForPayload`).
- A propeller type's burn, like a jet's, is now metered to a fraction of a gram each 240 Hz step. At real flows a
  step's tenth of a gram fell below a float's resolution.

## Planned fuel, reserves and warnings

- **The fuel a job card plans** is the trip with a tenth to spare and the reserve the rules ask: thirty minutes of
  cruise, forty-five for a jet (`plannedFuelKg`), never more than the tanks hold. A rental comes as full as the weight
  limit allows.
- **The warnings are in minutes of cruise, not tank fractions** (`cruiseMinutes`). LOW FUEL shows under twenty
  minutes, the status reads "low fuel" under fifteen, and the settlement says "reserve kept" at thirty or more and "on
  fumes" under ten.
- The HUD's FUEL tile and the job card show the endurance in hours and minutes; the tile turns red under twenty
  minutes.

## Missions about fuel

A mission whose point is the fuel starts with a fixed, short load. You can't add to it.

- `Contract::rationKm`: the job leaves with fuel for that many kilometres and the reserve, and the job card's fuel
  row shows it locked, labelled "rationed". The Nordholm Crossing (story O3) is flown on fuel for 90 km.
- `ResCard::fuelKg`: a research card's fixed load. The XR-40's Mach 4 card (XR40-1) launches with 300 kg: enough for
  a full-boost climb to 25 km, a short dash and the way home, with little to spare.

Everything else leaves with the planned load, or full tanks in Free Flight.

## Long hauls

On the real Earth the career's hub list adds six intercontinental destinations, 3,000 to 9,000 km away: the large
scheduled airports, one per country, open to an ATP. The board can offer a long haul across an ocean alongside the
near ones. The Galleon, the Emissary and the XR-40 have the tanks for them.

## Tests

`jet_fuel_test` keeps its jet checks and adds a realism check per type: endurance at cruise between the bounds for
its class, the reheat jets' minutes on reheat, the XR-40's hours at full boost and its range under 16,000 km, and a
two-minute hover that burns 10 to 60 kg. Fixtures in `menu_plan_cache`, `new_aircraft_performance`, `save_format`,
`gameplay`, `military_career` and `flight_test` moved to the design load and the reserve rule.
