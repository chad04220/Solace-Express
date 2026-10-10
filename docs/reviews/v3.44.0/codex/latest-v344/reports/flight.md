# v3.44.0 applicability of the five flight findings

Scope: bounded source comparison of released v3.43.0 `91212bf4a131c02d2da41efbdb09248a09bc3599` with newly published v3.44.0 `b22c2fca66fff88b62801a79f9f0c6edf06dcb9e`. The original pinned report and production checkouts are unchanged. This addendum does not transfer the earlier 134 custom-case count or earlier exact timing measurements to a newly executed v3.44 test run.

## Conclusion

None of the five defects has a code fix in v3.44.0. The aircraft physics/autopilot/stunt files and the relevant game-loop/ATC functions are unchanged. All five source-level mechanisms remain applicable. Fresh validation against the complete v3.44 object set now reproduces all five mechanisms and the three selected terminal failures, with passing controls. The five new mode logs are byte-identical to their v3.43 counterparts. XR-30/XR-20 low-altitude stunts were not rerun; their unchanged source mechanism remains applicable, while the fresh stunt crash/control pair is XR-40 only.

`src/game.cpp` has only two edits: a new `volcano_effects.h` include at line 2 and `volcano::append` inside `Game::render` (line 3771). The latter constructs visual sprites/lights in frame/render data. It does not update `Plane`, the autopilot, `AtcFlight`, `Contract`, or `FlightResult`. Every finding-related `game.cpp` reference shifts by exactly +1 line; aircraft/stunt/career references do not shift.

## Exact finding mapping

| Finding | v3.43 location | v3.44 location | Relevant functions and source status |
|---|---|---|---|
| F-STUNT-1: low-altitude split-S admission/recovery | `aircraft_stunt.cpp:47,75-80,95-104,111-120,182-196`; `game.cpp:1284-1295` | Same stunt lines; `game.cpp:1285-1296` | `Plane::apStuntBegin`, `Plane::apStuntFly`, `Game::flightControls` unchanged. Structural-g radius still admits a maneuver whose actual pull is stall limited. |
| F-EMERGENCY-1: failed-engine AP re-engagement plans impossible climb/orbit | `game.cpp:882-884,1140-1146`; `aircraft.cpp:823-848,1166,1184-1190` | `game.cpp:883-885,1141-1147`; same aircraft lines | `Game::fireFailure`, `Game::engageAutopilot`, `Plane::apEngage`, `Plane::apGuidance` unchanged. The normal NAV planner still clamps nonpositive climb to +0.5 before a climb/orbit. |
| F-ATC-1: engaged autoland ignores tower go-around, player is penalized | `game.cpp:3483-3486,1489`; `aircraft.cpp:1325-1345` | `game.cpp:3484-3487,1490`; same aircraft lines | `Game::updateAtc`, `Game::updateFlight`, `Plane::apGuidance` unchanged. `F.goAround` is still scored on touchdown without being supplied to autoland guidance. `Career::settle` also byte-identical. |
| F-ATC-2: post-clearance occupancy not monitored; long departure hold expires despite occupancy | `game.cpp:3470-3500,3503-3511,3397-3399` | `game.cpp:3471-3501,3504-3512,3398-3400` | `Game::updateAtc` unchanged. Arrival phase 5 still has no traffic recheck; departure hold still has the explicit 90 s escape. |
| F-ATC-3: GPS alternate leaves arrival tower fixed to contract airport | `game.cpp:1006,1140-1153,3334-3475` | `game.cpp:1007,1141-1154,3335-3476` | `Game::startFlight`, `Game::engageAutopilot`, `Game::updateAtc` unchanged. `atcF.arr` is still fixed from `c.to`; AP selects `apDest` independently. |

Important emergency wording: `Game::fireFailure` does disconnect an already engaged autopilot when the engine fails. F-EMERGENCY-1 concerns the player's explicit subsequent AP re-engagement, which is accepted without a refusal and then uses an impossible powered route. It must not be described as an autopilot that stayed engaged through the failure.

## Files verified byte-identical

- Aircraft model/envelope/control: `src/aircraft.cpp`, `aircraft.h`, `aircraft_stunt.cpp`, `aircraft_perf.cpp`
- Aerodynamics/weather: `src/aero.cpp`, `aero.h`, `aero_strips.cpp`, `aero_wake.cpp`, `weather.cpp`, `weather.h`
- Gameplay state/scoring/traffic/voice: `src/game.h`, `career.cpp`, `career.h`, `traffic.cpp`, `traffic.h`, `atc.cpp`, `atc.h`
- Existing coverage: `tests/flight_test.cpp`, `gameplay_test.cpp`, `autoland_sweep.cpp`, `pilot_exam.cpp`

Full SHA-256 pairs and equality flags: `latest-v344/logs/flight/source-identity.json`. Exact `game.cpp` diff: `latest-v344/logs/flight/game-render-only.diff`.

## Environment changes that prevent blanket numerical carryover

- `World::build` now carves a bounded Mount Kaleo summit region around (25000, -9000), radius 520 m; it adds connected-road/community-cache initialization and changes world-cache magic. The explicit crater is far from the CAP runway/nearby-sea repros, but source scope is not a substitute for a new run.
- `scenery.cpp`, `entities.cpp`, and `airport_scenery.cpp` change generated buildings, airport objects, and scenery/collision context. These can change an eventual obstacle collision even when the control law is identical.
- The source preserves legacy terrain-town and road effects outside its stated terrain change. No full terrain-array or all-airport flight equality is claimed by this worker; world verification is the parallel world/release audit.
- Do not reuse the earlier Linux executable or link a mixture of v3.43 and v3.44 objects. Compile the same diagnostic driver against current headers and the complete v3.44 `game_objs` set.

## Fresh v3.44 runs: complete

The existing diagnostic driver was copied byte-for-byte into `latest-v344/repros/flight/` and compiled with v3.44 headers, all 32 freshly built v3.44 `game_objs`, the matching radio stub and matching voice assets. Distinct binary and output/save directories were used. Five commands ran serially, never more than one flight process at once:

| Fresh mode | Observed v3.44 outcome | Log |
|---|---|---|
| Default ATC states | Before-clearance blocker triggers go-around; after-clearance blocker does not. Occupied departure clears at 94.02 s. GPS CAP→PVI leaves tower at CAP. | `latest-v344/logs/flight/states.log` |
| `engineout` | Normal player AP re-engagement after supported single-engine failure: ditch at 146.87 s. Matched direct-final control: successful CAP runway landing, 1.035 m/s touchdown, centerline 0 m, contract completed at 95.72 s. | `latest-v344/logs/flight/engineout.log` |
| `flown` | Engaged autoland hears go-around at 66.95 s, lands at 118.10 fpm, finishes at 105.22 s, receives -$500 instruction penalty from the $1,000 fixture contract. | `latest-v344/logs/flight/flown.log` |
| `stunt 12 700` | Real aerobatics-key XR-40 split-S: setup ends at 131.85 s; ground abort at 482 m AGL; sea impact at 139.72 s. | `latest-v344/logs/flight/stunt700.log` |
| `stunt 12 1500` | Same XR-40 key path at greater starting height: safe completion at 142.68 s, no crash/abort. | `latest-v344/logs/flight/stunt1500.log` |

These five commands contain nine fixtures: four ATC state checks, two engine-out paired flights, one occupied arrival, and two stunt paired flights. They are the only new runtime coverage claimed by this addendum. No broad sweep, ordinary-board rerun, or all-aircraft rerun was performed. The earlier 134-case data remain pinned to v3.43.

All five new logs are byte-identical to the corresponding historical logs; comparison and hashes are recorded in `latest-v344/logs/flight/runtime-comparison.json`. The copied driver and distinct binary checksums are in `latest-v344/logs/flight/diagnostic-sha256.txt`. This is actual fresh v3.44 execution, not a relabeling of old logs. Identical finite fixtures do not imply that every environment/flight outcome is unchanged.

## Portable reproduction

The new helper validates the v3.44 commit. It does not weaken or alter the original v3.43 hash guard.

```
export SOURCE_DIR=/path/to/v3.44.0-checkout
export BUILD_DIR=/path/to/v3.44.0-build
export AUDIT_OUTPUT_DIR=/path/to/v344-probe-output
mkdir -p "$AUDIT_OUTPUT_DIR"
# If needed, build game_objs once in the separate build directory.
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target game_objs -j2
bash latest-v344/repros/flight/build_probe.sh
unset FLIGHT_QUICK PILOT
latest-v344/repros/flight/game_flight_probe
latest-v344/repros/flight/game_flight_probe engineout
latest-v344/repros/flight/game_flight_probe flown
latest-v344/repros/flight/game_flight_probe stunt 12 700
latest-v344/repros/flight/game_flight_probe stunt 12 1500
```

No Ninja/CMake build was launched by this worker; it linked against root's confirmed-complete game objects. These are headless Linux game-simulation outcomes, not certified real-aircraft performance or a newly claimed Windows runtime test. The original report, source/binaries/logs, and original build-helper hash remain unchanged. Both production checkouts remain clean. All requested bounded latest-release validation is complete; no further sweeps are running.
