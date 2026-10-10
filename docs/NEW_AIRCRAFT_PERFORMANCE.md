# Larkspur L4 and Atlas A180: flight-model validation

Validated against the v3.44.0 b22 development source, 2026-10-10. These are original fictional aircraft, using the same geometry-derived strip aerodynamics, engines, weather, failures, ground contacts and career systems as the existing conventional fleet. They are not certified replicas or real-world operating guidance.

## Two distinct roles

| | Larkspur L4 | Atlas A180 |
|---|---:|---:|
| Role | Four-seat shoulder-wing touring single | 180-seat twin-underwing transport |
| Roster index | 13 | 14 |
| Persisted string ID | `larkspur_l4` | `atlas_a180` |
| Length / span | 8.8 / 11.6 m | 42.6 / 39.8 m |
| Wing area | 18.4 m² | 138 m² |
| Empty / maximum permitted mass | 930 / 1,501 kg | 42,000 / 74,385 kg |
| Engines | One 190 kW six-cylinder piston, three-blade propeller | Two 105 kN conventional turbofans |
| Gear / takeoff flap | Fixed tricycle / 20% | Retractable, wide main trucks / 50% |
| Structural limits | +3.8 / −1.52 g | +2.5 / −1.0 g |
| Career licence | PPL | ATP |
| Conservative sea-level dispatch floor | 600 m | 1,900 m |
| Measured 75% cruise | 153 mph TAS at 1,500 m | 540 mph TAS, Mach 0.807, at 10,000 m |
| Maximum-weight full-flap approach, 1.3 Vs | 69 mph | 168 mph |

The Larkspur's measured cruise sits between the Wren (146 mph) and Swift S6 (197 mph) under the same 1,500 m, 75%-power comparison. It trades retractable-gear speed for simpler fixed gear and a roomy cantilever shoulder-wing cabin. It is not a STOL or rough-field replacement for the Bushmaster.

Atlas is the largest conventional or research aircraft in this roster by length, span and mass. At 10,000 m it cruises slightly faster than the measured Starling (525 mph); the Meridian's 1,500 m comparison is 301 mph. Atlas has much higher landing speeds, inertia, runway requirements and wake strength than the lighter fleet. Its engines use the ordinary subsonic jet path, with no reheat, thrust vectoring or research flight-control bypass.

At low altitude, the Atlas can reach 590 mph at 75% power in the simulator. This is an unrestricted physical capability measurement, not a normal operating recommendation; real-world airspace speed restrictions and airline procedures are not simulated. The shared transonic drag-rise model is approximate. Its 0.78 drag-rise setting produces the measured high-altitude Mach 0.807 equilibrium, rather than imposing a hard speed limiter.

## What was actually measured

The new `new_aircraft_performance_test` target independently checks the actual flight model. It does not rely on the existing autopilot successfully holding a course or altitude.

- A force-and-pitch-moment trim solver calls the production `aeroForces`, `aeroGeom`, atmosphere and `Plane::thrustAt` routines. Cruise is the speed where trimmed thrust and drag balance.
- Separate 90-second, six-degree-of-freedom flights at fixed throttle verify the predictions. Larkspur held 153 mph at 1,500 m; Atlas held 590 mph at 1,500 m and 540 mph at 10,000 m, with negligible mean vertical-speed error. A deterministic test pilot makes ordinary elevator, aileron and rudder inputs; it is not a second in-game flight model.
- Actual takeoff runs integrate the wheel forces, engine spool, runway surface, rotation and climb to 15 m above wheel height. They distinguish wheels-off ground roll from clearance distance.
- Actual final approaches start 3 km before the runway on a 3° path to a 300 m aiming point. They include the flare, touchdown and braking to a stop. The threshold is crossed near 15 m; these are not merely synthetic rollout estimates.
- A separate repeatable landing benchmark starts on the wheels at 1.1 Vs0. Its 15 m distance adds a geometric 3° air segment. This shorter benchmark is explicitly different from a complete flown approach.
- Fuel is held constant in the independent handling/runway cases so game-range compression cannot quietly lighten a loaded aircraft during a measurement. The in-game learner still uses ordinary fuel burn.

### Runway results in calm, dry conditions

| Aircraft / load | Ground roll | Takeoff to 15 m | Braking roll | Synthetic landing from 15 m |
|---|---:|---:|---:|---:|
| Larkspur, 1,056 kg light | 110 m | 280 m | 70 m | 356 m |
| Larkspur, 1,501 kg maximum | 260 m | 507 m | 100 m | 386 m |
| Atlas, 46,285 kg light | 523 m | 684 m | 304 m | 591 m |
| Atlas, 74,385 kg maximum | 1,308 m | 1,526 m | 495 m | 781 m |

The fully flown maximum-weight Larkspur approach stopped 455 m beyond CAP's threshold, with a 1.72 m/s touchdown sink rate and 1.4 m final cross-track error.

| Atlas complete approach | Mid load, 59,942 kg | Maximum load, 74,385 kg |
|---|---:|---:|
| CAP, unchanged 2,800 m runway | 1,043 m to stop | 1,216 m to stop |
| PVI, unchanged 2,200 m runway | 1,027 m to stop | 1,235 m to stop |
| Touchdown sink rate | 1.06–1.07 m/s | 1.56–1.57 m/s |

The complete approach includes speed carried through the flare and wheel recontacts, so it must not be represented by the shorter synthetic landing benchmark. CAP and PVI both pass at maximum legal weight. The Atlas's 1,900 m dispatch floor preserves margin above the measured clearance and landing cases; the existing elevation correction excludes KLO's 1,900 m runway, and FAR's 1,700 m runway is too short. No airport geometry was enlarged. Wet, gusty, crosswind, hot/high, obstacle-limited and damaged-aircraft operations require additional margin; these calm tests do not certify those cases.

### Climb, failures and wake

- The standard learner measured Larkspur best-climb specific excess power of 7.0 m/s at its reference test mass. The independent trimmed-force sweep measured 6.4 m/s at a slightly lighter but higher-altitude comparison condition. Maximum-weight initial climb while clearing 15 m was 3.66 m/s.
- Atlas's reference learner returned 32.4 m/s of maximum specific excess power, versus 34.3 m/s in the independent force sweep. These are full-power energy-envelope measurements, not normal airline climb schedules. Maximum-weight initial departure climb at the 15 m measurement was 10.64 m/s.
- With the Atlas's left engine failed at maximum weight, a separate 90-second flight remained controllable, gained 628 m and averaged 7.44 m/s climb over its final 30 seconds. It needed approximately 40% corrective rudder. The initial heading excursion reached 8.9° before being corrected. Losing an underwing engine produces a real yaw moment; it does not simply halve a symmetric thrust scalar.
- A stopped Larkspur engine produces a descending glide, with an approximately 11.0:1 clean-configuration, fixed-gear windmilling glide ratio from the actual aerodynamic geometry.
- Atlas approach circulation was about 426 m²/s versus about 56 m²/s for a maximum-weight Larkspur approach. Wake sink was about 3.3 versus 1.3 m/s. The comparison deliberately keeps each aircraft at its own approach speed; the heavy transport's wake is materially stronger.

### Ground clearance and geometry

At maximum mass, the Atlas settles to 3.6196 m origin height above CAP, pitch +0.086°, with a 3.45 m main-wheel half-track and a CG-derived main station 4.173 m aft of the origin. Its lowest nacelle surface retains approximately 0.493 m clearance. A deliberate 5° ground bank at taxi speed triggers an actual engine-nacelle strike. The new pod strike contacts and dihedral wingtip contacts are read from the drawn model; low underwing fans cannot silently pass through the runway.

The Larkspur wing root is 0.94 m above the model origin, providing clearance above the seated pilot. Its normalized specification height and drawn root agree; its cantilever wing is not charged for nonexistent wing struts. Raising the root was followed by a full physics rerun, and its dispatch floor was increased to 600 m to retain margin over the measured 507 m maximum-weight takeoff to 15 m.

The Atlas's flap starts 4.05 m outboard of the centreline to clear the gear bay. Both visual and aerodynamic control geometry use that station. The full-flap lift and trim calibration is rerun for this reduced inner span.

## Native autopilot integration

`new_aircraft_autoland_test` runs the ordinary `AP_NAV` pilot through navigation, approach, flare and rollout, without test-pilot inputs. Its 34 deterministic cases cover both new aircraft at CAP and PVI, legal mid and maximum initial masses, standard and passenger-comfort modes, calm air and a 13.4 mph wind from 25° off the published runway heading with 0.15 turbulence. Its steady crosswind component is only about 5.7 mph; this is not a strong-crosswind certification. Two cases preserve the original Atlas regressions: the full-fuel, 100 kg-payload windy standard route that hit trees short of CAP, and the calm passenger-comfort route at that load. The latter retains the unchanged 26° en-route bank and 0.8–1.3 g assertions.

All 34 final cases passed, and the entire `flight_test` run returned zero failures. Atlas touchdown sinks ranged from 0.44 to 2.25 m/s, with maximum modeled gear-contact cross-track of 14.41 m; Larkspur ranged from 0.27 to 2.15 m/s and 6.76 m respectively. Atlas made one go-around in each of three cases: mid-load calm CAP comfort, maximum-load windy PVI standard, and the exact calm CAP comfort regression. Other cases made none. Navigation-to-stop times were 6.5–19.9 simulated minutes. Native fuel burn remains enabled, so maximum mass is the initial mass; the separate constant-mass final-approach tests above validate the heavier landing condition.

Each case must touch down below 3 m/s sink, stop within the actual runway length and width, finish without an overrun, and remain uncrashed. The aircraft origin and all three production main/nose contact centres must stay inside the actual pavement width throughout the roll, including any bounce back into flare. These transformed contact centres are the physics footprint, not a claim that every rendered tyre edge or truck corner remains paved. A crash, explicit refusal or 1,500 simulated seconds terminates an ordinary case. The completed landing must also clear the temporary terminal-rejoin state.

Eight additional recovery cases deliberately displace Atlas laterally during final, covering both fields, mid/maximum initial mass and calm/windy comfort mode. All eight made one go-around and subsequently landed with the same containment and touchdown assertions, within 14.9–24.6 simulated minutes. A separate 2,400-second limit permits one complete extra pattern; it does not relax runway or touchdown limits. Twenty-three focused checks cover runway-width-dependent capture decisions, healthy/one-/two-engine states, ground contact, jammed flaps, and engagement/disengagement/reset/landing/failure cleanup of the terminal-rejoin flag.

The original tree strike was a genuine controller integration issue: Atlas's slow lift response made the standard path loop oscillate vertically. Transport-category jets reuse the existing professional pilot's lag-aware path damping and conservative pattern speed, with progressive passenger flap commands and approach-speed roll lag. Non-emergency passenger NAV turns use the existing professional 6°/s roll schedule. These rules select conventional jets by structural capability rather than roster index.

The final 4.05 m flap-root geometry exposed a further lateral landing limit in the exact comfort fixture. It touched down 12.51 m off centre with 6.40 m/s lateral velocity, then reached 28.23 m origin cross-track on CAP's 25 m half-width. Steering anticipation alone still left the pavement. The bounded correction checks projected contact position against the actual runway width before flare and uses the existing go-around when a healthy transport has climb capability. Its gate is within 2,000–250 m along the approach and below 100 m runway-relative height; those approach distances are not heights. The missed approach gradually cleans up flaps before turning and explicitly retains the planned terminal speed on return. Healthy symmetric-engine yaw trim is washed out through that return and landing, preventing its low-speed oscillation from restarting at FINAL. Ground steering anticipates lateral motion and yaw rate, with integral anti-windup; braking physics is unchanged.

The exact comfort regression now lands after one go-around in 1,196 seconds, with 25.43° maximum NAV bank, 0.818–1.242 g and 3.46 m maximum contact cross-track. Independent same-state recovery comparisons caught and rejected intermediate candidates: a maximum-load PVI rejoin accelerated out of the chart, and maximum-load windy CAP reached 25.70 m contact cross-track versus 14.98 m in the baseline. The final consistent terminal-state candidate reduces that CAP comparison to 10.91 m. Full trajectories and rejected variants are retained in the [round-two guidance review](validation/new-aircraft-round2-guidance-review.txt).

**Remaining comfort limits:** maximum-load CAP reaches 27.12° NAV bank in calm air and 26.93° in the windy case, exceeding the optional 26° comfort goal. The prior mid-load calm excursion is resolved by the bounded recovery profile. Maximum-load windy CAP touches down at 2.25 m/s (443 ft/min), a functional landing below the 3 m/s assertion but a firm passenger touchdown. The exact legacy fixture keeps its stricter bank/g assertions; the expanded matrix is not a universal comfort-envelope guarantee.

**Remaining engine-out autopilot limits:** an additional eight-case Atlas comfort diagnostic with the left engine failed reproduces the same three failures as the unchanged round-two baseline: mid-load calm CAP hits terrain, maximum-load calm CAP hits trees, and maximum-load calm PVI collapses the gear on a hard landing. The other five land. The new lateral-rejection and terminal-return policies are disabled when engine power or flap function is degraded, and they do not attempt to certify an engine-out missed approach from clean best-climb data. Ground steering still applies, so successful rollout values can differ. The independent engine-out handling result above demonstrates physical controllability, not reliable native AP completion in every degraded case.

No legacy-aircraft controller, manual actuator, aerodynamic force, runway dimension or test threshold was altered by this guidance correction. The added terminal-rejoin boolean requires rebuilding every aircraft-header consumer; stale mixed objects are invalid even if the structure's total size happens not to change.

The wake regression had two invalid geometric assumptions for the new strongly tapered, washed-out wing. Pair spacing is now checked against the actual strip circulation and lift, with a separate exact elliptic reference. Flap-edge vortices are checked against the authored flap boundary rather than the outermost rolled-up vortex centroid; the tip and outer-flap wake can merge. An additional same-speed, same-angle assertion requires deployed flaps to increase lift. No production wake strength was reduced.

## Loading, persistence and limitations

The maximum-mass formula follows the existing game's convention: empty mass + 75% fuel + hold cargo + 85 kg per passenger + 85 kg pilot. Cargo capacity is separate from passenger mass. Full-fuel flights therefore require less payload. Independent maximum-weight tests use exactly that legal combination; the complete approach tests also retain legal fuel/payload combinations.

`fullCabinEnvelope` opts the new aircraft into a shared cabin-aware learning reference. Reference payload is half of hold cargo + passenger mass + pilot mass. The strip trim calibration, performance learner and `apSense` use the same reference. Maximum-weight runway learning and landing-distance scaling use the same design maximum. The legacy fleet keeps its earlier reference settings to avoid silently changing its established handling and career dispatch.

Existing research roster indices 9–12 are unchanged. The production career map explicitly includes the two appended indices. Header defaults preserve prior aircraft; authored gear height, track and takeoff flap remain ordinary specification data.

The independent physics tests establish physical controllability and runway capability. The separate native-autoland matrix establishes the specified gameplay cases. Neither is universal autopilot or adverse-weather approval; the repository's broader flight and pilot suites remain additional regression gates.

### Game range is intentionally compressed

The Larkspur's 115 km and Atlas's 350 km values are game-map ranges. `fuelFlowMax()` derives burn from those ranges, so they are not real-world endurance or fuel-efficiency claims. Physical mass, lift, thrust, gear contact and stall/approach relationships are kept separate from this deliberate game pacing. The original designs use plausible class-level proportions rather than copying a real manufacturer's aircraft.

## Reproduce and inspect

Build `new_aircraft_performance_test`, `new_aircraft_autoland_test`, `aero_wake_test` and `aero_test` with the repository's CMake configuration. Run:

```
new_aircraft_performance_test --report
new_aircraft_autoland_test
new_aircraft_autoland_test --go-around
aero_wake_test
aero_test
```

The focused regressions run under CTest as `new_aircraft_performance`, `new_aircraft_autoland` and `aero_wake`. Passing `14` to `new_aircraft_autoland_test` runs only the 18 Atlas cases. The optional `--engine-out` diagnostic intentionally reports the three documented baseline failures; it is not a passing release gate. Raw values remain in SI units:

- [Full performance run](validation/new-aircraft-performance.txt)
- [Native autopilot matrix](validation/new-aircraft-autoland.csv) and [complete state-check log](validation/new-aircraft-autoland.txt)
- [Induced go-around matrix](validation/new-aircraft-go-around.csv)
- [Engine-out diagnostic](validation/new-aircraft-engine-out.csv)
- [Independent guidance review](validation/new-aircraft-independent-guidance-review.txt)
- [Entire flight-model test run](validation/new-aircraft-flight.txt)
- [Wake regression run](validation/new-aircraft-wake.txt)
- [Focused aircraft source hashes](validation/new-aircraft-source-sha256.txt)
- [Whole-roster aero/trim run](validation/new-aircraft-aero.txt)
- [Same-condition fleet comparison CSV](validation/new-aircraft-cruise-comparison.csv)

## Primary reference context

These references inform aircraft-class plausibility, not a claim that the fictional models reproduce certified performance:

- Textron Aviation's [Cessna Skylane product card](https://cessna.txtav.com/-/media/cessna/files/product-cards/piston/skylane_product_card.ashx) gives an 8.84 m long, 10.97 m span, four-seat piston class with 1,406 kg maximum mass and a 230 hp engine. The Larkspur is an independently shaped, slightly wider and more powerful design in this general class.
- Airbus's [A321neo page](https://www.aircraft.airbus.com/en/aircraft/a320-family/a321neo) and [airport-planning document](https://www.aircraft.airbus.com/sites/g/files/jlcbta126/files/2025-07/AC_A321_20250715.pdf) ground the single-aisle transport scale: 44.51 m overall length and 35.80 m span. Atlas deliberately uses a different 42.6 m fuselage and 39.8 m higher-aspect-ratio wing, with original cockpit, tail, engines and landing gear geometry.
