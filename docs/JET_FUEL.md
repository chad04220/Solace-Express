# Jet fuel calibration

> **Superseded in part by [FUEL.md](FUEL.md).** The engine model below (thrust times TSFC, idle, reheat, 240 Hz
> metering) stands. The research jets' TSFC and endurance, the XR-40's burn (now by its power setting: hours, not
> minutes), and the note that propeller types keep their old calibration are replaced there, with every type's real
> tank and burn.

## Scope and units

All six jet aircraft use **usable fuel mass in kilograms**, fuel flow in **kg/s**, thrust in N and true airspeed in m/s. The model is independent of map size. Piston and turboprop consumption retains its previous gameplay calibration; this is deliberately a jet-only change.

`maxFuel` is capacity, not a mandatory starting load. Free Flight still starts with full tanks. Accepted launch plans and the job-card fuel selection retain their explicit initial kilograms. Ordinary short missions need not consume most of a tank. A future mission that deliberately restricts fuel should set its launch plan's `fuelLoadKg`; it must not shorten the aircraft's physical range. There is no arbitrary fleet-wide mission fuel reduction.

## Audit and reference scale

The previous model divided tank capacity by a game-scaled range and assumed 80% of maximum flow at cruise. It made Emissary's 1,100 kg last only 260 km and Galleon's 14,000 kg only 350 km. XR-10 and XR-20 used the same compressed convention. XR-30 and XR-40 skipped the consumption line entirely. Career estimates duplicated the old formula and treated special aircraft as consuming zero fuel.

References are class-scale checks, **not copied designs or certified performance for these fictional aircraft**:

- [Textron Citation CJ3 Gen3 specifications](https://cessna.txtav.com/en/citation/cj3-gen3): 2,136 kg usable fuel, 6,291 kg MTOW, 3,778 km maximum range, 770 km/h maximum cruise and 13,716 m maximum operating altitude. The Emissary is a heavier fictional light jet, not a CJ3 replica. Manufacturer maximum range is not interchangeable with a still-air, cruise-only tank-to-empty calculation.
- [Airbus A320ceo specifications](https://www.aircraft.airbus.com/en/aircraft/a320-family/a320ceo): 180-seat maximum, 78 t MTOW, 27,200 L maximum fuel capacity and 6,200 km advertised range with Sharklets. Galleon's 20 t usable fuel is approximately 25,000 L at an explicitly assumed 0.80 kg/L, consistent with this aircraft class. No physical tank volume or fuel-temperature system is claimed.
- [NASA thrust-specific fuel consumption](https://www.grc.nasa.gov/www/k-12/airplane/sfc.html): fuel mass flow is thrust multiplied by TSFC; afterburning raises both thrust and TSFC. NASA's examples explain the method, not exact calibration of these engines.
- [USAF F-16 fact sheet](https://www.af.mil/About-Us/Fact-Sheets/Display/Article/104505/f-16-fighting-falcon/): 3,175 kg internal fuel illustrates a compact fighter-scale tank. Its separate ferry-range figure is **not used as an internal-fuel-only range claim**.
- [NASA X-29 description](https://www.nasa.gov/aeronautics/nasa-aircraft/x-29-demonstrator/) and [NASA flight-test technical report](https://ntrs.nasa.gov/api/citations/19900009909/downloads/19900009909.pdf): a light experimental jet had approximately 1,814 kg of fuel, a roughly one-hour experimental mission endurance and a 15,240 m maximum operating altitude. Mission endurance, cruise endurance and maximum range differ. The more powerful Reverie retains its fictional engine and larger tank.

The XR-30's extreme thrust/vectoring and the XR-40's four tilting pods, Mach-4 capability and structural limits are explicitly fictional. Their TSFC values are engineering assumptions. Finite propulsion does not make their airframes, acceleration, cloak or weapons real-world designs. Weapon ammunition, thermal limits and electrical/visual abilities retain their existing independent gameplay systems; they do not refill propulsion fuel.

## Aircraft configuration

The cruise reference is clean, still-air, dry-power flight. It is separate from the legacy `cruise` speed used by existing local-mission guidance, especially the XR high-speed types. There is no promise that every jet can cross an ocean.

| Aircraft | Usable tank kg | Empty kg | MTOW kg | Reference altitude / TAS | Reference flow kg/h | Nominal cruise-only range km |
|---|---:|---:|---:|---|---:|---:|
| Emissary 500 | 2,200 | 4,600 | 8,000 | 11,000 m / 200 m/s | 365 | 4,337 |
| XR-10 Vigil | 2,200 | 5,700 | 8,800 | 10,000 m / 240 m/s | 886 | 2,146 |
| XR-20 Reverie | 3,200 | 6,100 | 10,300 | 10,000 m / 240 m/s | 993 | 2,783 |
| XR-30 Nocturne | 6,000 | 9,000 | 15,800 | 10,000 m / 250 m/s | 2,784 | 1,939 |
| XR-40 Somnus | 6,000 | 10,500 | 18,000 | 10,000 m / 250 m/s | 1,553 | 3,478 |
| Galleon 180 | 20,000 | 42,000 | 79,000 | 11,000 m / 230 m/s | 2,852 | 5,806 |

These are conservative initial-heavy reference points established with actual `Plane::step` cruise flights, not fuel flow derived backward from range. Actual flow falls as fuel mass is burned. The rounded `rangeKm` roster statistic is descriptive and is never used to set a jet's engine flow.

A 30-minute **illustrative** reserve at the reference rate is respectively 183 / 443 / 497 / 1,392 / 776 / 1,426 kg. This is an engineering comparison, not a real-world legal dispatch rule. The above nominal ranges exclude reserves, takeoff, climb, descent, routing, wind, holding and alternate requirements. Long, high-altitude flights and short local flights have different fuel economy. Supersonic flight, boost and hover have dramatically shorter endurance.

### Weight tradeoffs

MTOW is now explicit rather than increasing implicitly whenever a tank is enlarged. Full fuel with the pilot weighs 6,885 / 7,985 / 9,385 / 15,085 / 16,585 / 62,085 kg in the same order. Actual payload is added to empty mass plus fuel, so fuel burned really reduces mass. The pilot is included in payload, not counted twice.

Emissary's maximum ordinary load is 1,380 kg including pilot, passengers and cargo. With that load it can carry 2,020 kg fuel, not the full 2,200 kg. Galleon's maximum cabin/cargo/pilot load is 21,885 kg; at 79 t MTOW that permits 15,115 kg fuel, not 20 t. With full tanks its total payload allowance is 17,000 kg. All nine existing Somnus weapon/bomb combinations plus pilot and full tanks fit its 18 t envelope; stores still add mass and released ammunition still removes mass.

`AircraftSpec::maxFuelForPayload()` exposes the legal mass-limited fuel. The UI continues to show overweight combinations instead of silently removing payload. The background flight quote uses a legal initial fuel mass and reports actual fuel consumed relative to that initial mass.

## Shared engine implementation

`Plane::fuelFlowAt()` is the canonical jet curve:

1. Thrust comes from the existing engine architecture and density/Mach/spool model. Throttle is **not** an instantaneous substitute for actual spool.
2. Sea-level dry TSFC is 15.5–25 milligrams/(N s), i.e. 0.0000155–0.000025 kg/(N s), depending on type. Reheat TSFC is 0.000054–0.000062 kg/(N s). Correction factors account approximately for density/temperature benefit, flight speed and partial-power inefficiency. These are declared assumptions, not engine certification data.
3. Reheat transitions continuously above 85% spool and pays both for its extra thrust and its poorer efficiency. Somnus ram-assisted thrust is also charged.
4. Running cores retain a finite idle flow, proportional to installed thrust and air density. Engine-off and empty-tank states consume zero. A dead engine's idle component disappears; powered burn follows the same health-scaled thrust as the physics.
5. Every jet, including both special research types, removes fuel each 240 Hz physics substep. Compensated accumulation retains fractions below the float tank quantity's representable increment. Without it, low flows in a 20 t tank could round away or be biased. Exhausting the remaining fuel scales the final substep's thrust and stops the engine.

`fuelFlowKgS()` reports current operation. `rangeLeftKm()` uses current airborne TAS and burn, so idle descent/boost/hover change its answer; this is a still-air estimate, not a destination guarantee. `nominalFuelFlowKgS()` and `nominalRangeKm()` use the documented efficient-cruise condition, independent of current boost. The Earth route picker uses the latter and labels it no-reserve.

Career estimates use the same engine curve at local-mission altitudes and phase speeds. The background autopilot flight replaces its quick approximation with actual integrated kilograms. Fuel prices remain per kilogram. Old saves retain their exact saved kilograms, not their former tank percentages; capacity upgrades do not award free fuel. No save version change is needed.

## Sustained-flight correction

A one-hour Galleon test exposed a pre-existing high-altitude autopilot Dutch roll: after about 15 minutes at 11 km, accumulated rudder trim drove alternating 8–10 degree sideslip. This raised hourly burn from approximately 2.81 t to 3.53 t while masking the issue behind correct speed and altitude.

The existing symmetric-transport yaw-integral decay policy now applies throughout airborne autopilot flight with healthy symmetric engines and unjammed flaps. The ordinary coordinated-rate/sideslip damper still operates. Asymmetric or degraded engines, flap failures, non-transport types and manual flight retain their existing paths. Heavy crosswind approach and go-around tests exposed the same instability now that realistic burn keeps the aircraft heavy; both the endurance test and the complete native-approach matrix validate the correction.

## Validation

`jet_fuel_test` checks every jet's throttle/altitude curve, finite idle, boost penalty, engine-off/starvation behavior, 240 Hz metering, actual cruise versus the HUD estimator, a two-minute freely hovering Somnus, all equipment mass combinations, payload-limited fuel, old save quantities and refueling bills. It verifies unchanged non-jet calibration. Default cruise integration is two minutes per type.

- `jet_fuel_test --long --cruise-only`: one uninterrupted hour per jet, real fuel consumption and changing mass.
- `jet_fuel_test --range --cruise-only`: actual integrated cruise until the illustrative 30-minute reference reserve, with no refueling, fuel reset or repositioning. The local world's existing coordinate wrap is retained; this tests endurance dynamics, not globe navigation or terrain streaming.
- `jet_fuel_test --long --cruise-only --only 14`: trace the high-altitude Galleon regression.

The old four-minute maximum-reheat research-tier fixture in `flight_test` is now explicitly fixed-fuel/fixed-weight. It measures aerodynamic maximum speed, not an operational endurance claim. Separate fuel tests exercise actual reheat consumption and starvation. Production-aircraft takeoff/landing, engine-out, save, career and autopilot regressions are rerun with the new mass/capacity values.

The calibration's native Release build passed `jet_fuel_test`, all six tank-to-reserve flights, and a 9/9 serial CTest run covering `flight_model_1/2/3`, `campaign_progression_2`, `career_saves`, `new_aircraft_autoland_galleon`, `new_aircraft_go_around`, `new_aircraft_performance` and their world fixture. Core-source hashes are recorded alongside the logs. This fuel validation does not claim a Windows run, sanitizer run, full combined application test suite, or Earth streaming/render validation.

### Integrated tank-to-reserve result

The following continuous flights started at the reference cruise altitude, with full tanks and changing mass. Payload was 500 kg for Emissary, 15,385 kg for Galleon, and 85 kg for each research jet. They stopped at the illustrative 30-minute reference-flow reserve. No fuel or flight state was reset. All stayed airborne, held commanded altitude/TAS, and matched the integrated engine-flow meter.

| Aircraft | Time flown h | Distance actually flown km | Fuel remaining kg |
|---|---:|---:|---:|
| Emissary | 5.76 | 4,148 | 183 |
| XR-10 | 2.02 | 1,745 | 443 |
| XR-20 | 2.87 | 2,478 | 497 |
| XR-30 | 2.91 | 2,617 | 1,392 |
| XR-40 | 3.82 | 3,441 | 776 |
| Galleon | 6.93 | 5,739 | 1,426 |

These cruise-only results do not include departure, climb or arrival and must not be advertised as guaranteed mission ranges. The initial-heavy nominal estimate is intentionally conservative. In particular, the Nocturne's low-aspect-ratio wing has a large induced-drag penalty at full mass; actual cruise flow falls substantially as its large fuel fraction is burned. The live airborne estimate follows this change while the route picker's initial reference remains conservative.

The two-minute hover consumed 505.9 kg with a final rate of approximately 14,971 kg/h. CAP→PVI actual local flights used 104.3 kg in 11.24 minutes (Emissary) and 818.4 kg in 13.07 minutes (Galleon); quick estimates were 112.2 kg and 760.9 kg, respectively, within 8%. Background flown quotes replace these approximations with exact integrated use.
