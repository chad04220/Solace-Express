# Flight, autopilot, ATC and mission-safety audit

Release audited: v3.43.0, commit `91212bf4a131c02d2da41efbdb09248a09bc3599` only. Source checkout `/workspace/scratch/87b29863f015/solace-v343-audit` remains unchanged. Diagnostics live in `repros/flight`; log paths below are relative to this review folder. Objects were compiled by the root review task from that exact checkout, Release `-O3 -DNDEBUG`, Linux GCC, with generated headers from `solace-v343-audit-build/gen`. No `PILOT=new`; experimental professional-pilot code is not the released behavior. `FLIGHT_QUICK` was unset, not set to 0.

## Confirmed findings

### F-STUNT-1: Split-S accepts insufficient recovery height and flies research aircraft into the sea

Severity: high (P1 autonomous aircraft-loss defect); confidence high, actual player-key + `Game::update` reproduction.

- Code: `src/aircraft_stunt.cpp:47` chooses structural-limit-derived `apStuntN`; `75-80` limits the pull by the current stall envelope but sizes the maneuver radius using the much larger structural number; `95-104` admits the figure based on that radius. Ground detection/abort is at `111-120`; recovery is at `181-196`.
- Repro: `env -u FLIGHT_QUICK -u PILOT repros/flight/game_flight_probe stunt 12 700`. This uses the real configured aerobatics key and `flightControls`, botControl=false, no direct call to stunt guidance. XR-40, ordinary 85 kg pilot payload, 75% fuel, heading 45°, 700 m above CAP elevation, cruise×0.8, light weather (wind 3 m/s, gust 1, turbulence .1). No player input after the key.
- Actual: setup runs for 131.85 s and holds ~720 m AGL. It then accepts split-S at 143 m/s IAS. The plane is inverted and descending at 129.8 m/s by t=136 s. At t=136.52, 482 m AGL and -146.9 m/s, it aborts for ground, but recovery still hits the sea at t=139.72. `apStuntN` is 72.68; measured pull during recovery is around 6 g, not the authority used to size entry clearance.
- Expected: setup should climb sufficiently or refuse; if it starts, the abort/recovery must remain achievable. This is exactly the safety promise of the code's own entry/ground checks, not a request for a player to perform a difficult maneuver manually.
- Passing control: identical real-key XR-40 replay from 1,500 m returns to hold, no abort/crash, at t=142.68 s. Log `logs/flight/stunt_game_12_1500.log`.
- Initial 26-case physics sweep (all 13 types, LOOP and SPLIT-S) also crashes XR-30 and XR-20 on low-altitude split-S. Every LOOP is safe or declined/aborted safely; most maximum-load civil loops time out safely for insufficient performance. Research cases use normal test payload 150 kg, not maximum cargo. Actual-key repeats at the ordinary 85 kg pilot payload confirm XR-30 crashes at 124.93 s and XR-20 at 83.17 s, both after a ground abort (`stunt_game_10_700.log`, `stunt_game_11_700.log`).
- Evidence: `logs/flight/wraith_stunt_game_700.log`, `logs/flight/stunt_game_12_1500.log`, `logs/flight/loaded_stunts.log`. Independent root replay reproduces the Wraith 139.72 s crash in `logs/flight/root-wraith-stunt-700.log`. Measured comparison plot: `previews/wraith_split_s_repro.png` (game simulation only; no real-aircraft/certification claim).
- Fix direction: compute entry clearance from the attainable, speed/load/configuration-dependent pull and energy trajectory, not structural g alone; include pitch-response and roll-to-recovery lag, a conservative remaining-trajectory envelope, and low-altitude tests for all research craft. Ground-abort recovery needs sufficient margin before a vertical high-speed dive becomes unrecoverable.

### F-EMERGENCY-1: Re-engaging autoland after a single-engine failure discards a reachable runway for an impossible climb/orbit

Severity: high (P1 autonomous aircraft loss in a supported emergency state); confidence high, actual game failure/normal-key reproduction and matched successful landing control. The failure is not that the pilot must manually handle an emergency: the game automatically disconnects on failure correctly. The defect occurs when re-engagement is accepted without warning and then commands an impossible plan.

- Code: `src/game.cpp:882-884` correctly disconnects on engine failure, but `1140-1146` permits normal autoland re-engagement (only a dark battery is rejected). `src/aircraft.cpp:823-848` always starts normal NAV/descent-orbit planning. In the NAV climb plan, `1166` replaces negative/zero `E.climbPlan` with positive 0.5 m/s, and `1184-1190` circles while commanding more altitude, with no all-power-lost alternative. `E.canGoAround=false` protects only later approach go-arounds, not this planning path.
- Repro: `env -u FLIGHT_QUICK -u PILOT repros/flight/game_flight_probe engineout`. Kestrel, ordinary 150 kg payload (65 kg cargo + 85 kg pilot), 60% fuel, calm CAP runway 32 approach, 2,500 m before threshold and 350 m above field, aligned heading 320°, IAS initially 1.35×Vref. Inject the supported single engine failure through `Game::fireFailure`, choose CAP, press the real configured AP key; then no inputs. No forced engine-out impossible terrain assumption: the control below proves the runway is reachable through the same physics.
- Actual normal-key outcome: autoland accepts runway 32 with no refusal; commands the NAV climb/orbit, turns away from the runway, continues “climbing” while descending without power, and ditches at 146.87 s, ~5,371 m along the runway axis and 253 m cross-track, no touchdown.
- Matched control: only change `apStage=APS_FINAL` immediately after the same AP key; retain aircraft position, mass, failure, weather, physics and game loop. It lands at 1.035 m/s, finishes the contract successfully at 95.72 s, exact centerline, CAP runway index 3. Stop is 1,011 m from runway center, within its 1,400 m half-length. This is a diagnostic control, not a proposed user-accessible workaround or an implemented fix.
- Evidence: `logs/flight/engineout_game.log`; supplementary matched-heading physics cases in `logs/flight/engineout_matched_headings.log`.
- Fix direction: give zero/negative climb capability an energy/range-aware admission and routing branch. Capture an already reachable aligned approach, choose a proven reachable alternate, or refuse and keep control with the pilot with a clear reason. Do not silently promise a powered climb by clamping negative capability positive. Add the actual failure → re-engage → terminal-outcome regression; the stock “both out” flight test currently checks only envelope/climb flags.

### F-ATC-1: Autoland ignores a live tower go-around and incurs the player's penalty

Severity: high (P1 gameplay integration); confidence high, actual Game::update + real physics + decoded tower voice + settlement.

- Code: `src/game.cpp:3483-3486` creates the go-around flag; `src/game.cpp:1489` records noncompliance on touchdown; `src/aircraft.cpp:1325-1345` has its own approach go-around conditions, without receiving tower traffic state. The flag is never fed to autopilot guidance.
- Expected: when an engaged, climb-capable autoland receives a tower go-around due to a runway blocker, its flight-control mode must go around, or hand control back clearly before a consequential committed descent. It must not continue under autopilot and charge the player for disobeying the tower.
- Actual: Wren 180, calm CAP approach, persistent far-end traffic blocker, receives “Go around. Aircraft on the runway.” at 66.95 s, continues to a 118.10 fpm landing and completes at 105.22 s. The $1,000 contract is charged `$500` for “Landed against a go-around instruction.” The blocker is kept far from the touchdown/stop zone to isolate the integration/scoring error rather than manufacture a collision.
- Repro: `env -u FLIGHT_QUICK -u PILOT repros/flight/game_flight_probe flown`
- Evidence: `logs/flight/atc_flown_occupied.log`. Fixed seed `srand(77)`, Plane reset's deterministic weather state, 1/60 s real game frames.
- Fix direction: expose tower clearance/runway occupancy as a flight-guidance input, transition to a safe go-around once (respecting actual climb capability), cancel stale landing clearances, and test with real `Game::update` through final settlement. Test the arrival while blocked before clearance and an incursion after clearance.

### F-ATC-2: Clearance stops monitoring occupancy; an occupied departure runway is cleared after a timeout

Severity: medium (P2 game traffic safety/ATC correctness); confidence high, isolated production state-machine tests.

- Code: `src/game.cpp:3470-3500` checks traffic only in arrival phase 4; phase 5 at `3503-3511` never calls `rwyTraffic` again. Departure `3397-3399` explicitly limits the hold to 90 s regardless of continued occupancy, then issues clearance.
- Repro: `env -u FLIGHT_QUICK -u PILOT repros/flight/game_flight_probe`
- Before-clearance control: aircraft 500 m short, 45 m high, runway occupied, phase 4 → phase 5 with `goAround=1`.
- After-clearance case: same aircraft state, obtain clearance on an empty runway, introduce the identical blocker; after 120 controller steps it remains phase 5 with `goAround=0`, despite the runway still being occupied.
- Departure: with a persistent blocker and a stationary aircraft that is obeying the hold, production controller issues phase-2 takeoff clearance at 94.02 s and clears the holding flag while occupancy remains 1. This reproduces the deliberately coded 90 s wait escape, but its safety outcome contradicts the live occupancy.
- Evidence: `logs/flight/atc_states.log`.
- Fix direction: revalidate runway conflicts until touchdown/takeoff; resolve stalled AI traffic independently rather than expiring its safety constraint. Add a late-incursion test and a hold longer than 90 s. Existing gameplay tests cover only occupancy before clearance and a 30 s hold, so they miss both transitions.

### F-ATC-3: GPS alternate selection does not retarget the tower

Severity: medium (P2 navigation/ATC integration); confidence high for stale state; full alternate-landing audio not claimed.

- Code: `src/game.cpp:1006` fixes `atcF.arr=c.to`; `1140-1153` engages `apDest` without changing arrival ATC state; `3334-3475` continues using `F.arr`.
- Repro: same `game_flight_probe` state command. Contract destination CAP, select PVI on GPS and engage autoland while inbound to PVI.
- Actual: `contract=CAP ap=PVI tower_target=CAP phase=3`. PVI cannot provide its arrival sequencing/clearance or runway-blocker go-around because the controller still watches CAP.
- Expected: the contract's delivery destination can remain CAP, while operational arrival airport/ATC follow the selected diversion. Merely choosing an alternate must not change contract completion semantics.
- Evidence: `logs/flight/atc_states.log`.
- Fix direction: track an operational destination separately from commercial destination, cancel/retarget the voice phase and clearance when the alternate is engaged, and keep the independent career-diversion logic intact.

## Legal maximum-load stress coverage (not ordinary generated-contract proof)

`flight_probe.cpp` is a clearly marked diagnostic copy of released `tests/pilot_exam.cpp`, with extra inputs `AUDIT_PAYLOAD`, `AUDIT_FUEL`, `AUDIT_FAILURE`, `AUDIT_ICE`, and first/worst touchdown plus the game's airborne-g fragile thresholds. It does not change physics or autopilot. `AUDIT_PAYLOAD=-1 AUDIT_FUEL=.75` uses cargo capacity + every passenger + 85 kg pilot, at the exact `AircraftSpec::maxMass()` envelope. This mixed maximum is legal by the aircraft's aggregate limit but exceeds loads normally created for a single cargo or passenger board job. Do not present it as a discovered ordinary fragile-board failure.

- 18 flights: all 9 career types, CAP, gust weather, both inbound headings. 18/18 land and stop on the runway, zero go-arounds. Fifteen meet fragile thresholds; Islander both starts hit 2.078 and 2.563 m/s touchdown (>400 fpm), and Starling start 1 reaches 2.362 airborne g. Existing `pilot_exam` still labels these landings `ok` because its touchdown limit is 3 m/s; only 2/18 are stabilized by its own approach-quality rubric. `logs/flight/all_career_max_mass_cap_gusts.{log,csv}`.
- 10 flights: maximum-load Islander, FJH, calm/cross/gust/shift/storm × two headings. 10/10 land/stop; 5/10 stabilized; gust start 0 reaches 2.246 airborne g with minimum IAS/stall 1.02 and ~32° nose-down before recovery. `logs/flight/islander_max_mass_fjh_all.{log,csv}`.
- 2 flights: maximum-load Meridian, CAP calm, both headings: clean runway stops, ~0.9 m/s touchdowns; neither approach stabilized under examiner rubric. `logs/flight/meridian_max_mass_cap_calm.{log,csv}`.
- Real-game synthetic mixed-load reproduction: `game_flight_probe fragile 3 FJH 2 0 1` produces 2.246 g at ~15.9 s, a 131 fpm butter landing, $150 passenger discomfort and $400 fragile-cargo damage. `logs/flight/islander_max_mass_game.log`.
- Important passing control: same route/weather/aircraft at maximum cargo-only 985 kg payload: no cargo damage, 1.320 maximum airborne g, 171 fpm touchdown, successful contract. `game_flight_probe fragile 3 FJH 2 0 0`; `logs/flight/islander_cargo_game.log`.

These are bounded robustness tests and evidence of the gap between “lands without crashing” and “meets the mission's comfort/fragile requirements,” not a claim that every gust landing fails. Root runs the unchanged full stock suites separately.

## Ordinary generated fragile jobs: complete passing counterevidence

The maximum mixed-load fixture above was not accepted as proof of an ordinary board defect. Three actual `Career::refreshBoard` contracts were regenerated with ATP license, completed story, $1,000,000, specified location/board seed; their cargo, weather and route were preserved. The real game finalized fuel (260 kg), ran engine start and a bounded scripted runway takeoff, then normal autopilot engagement at 150 m AGL, all the way through debrief. No artificial repositioning after takeoff, no overridden cargo, no scripted touchdown, no settlement shortcut. Random in-flight failures and incidental AI traffic were disabled to isolate guidance.

| Contract and seed | Route | Cargo | Time | Touchdown | Max airborne g | Result |
|---|---|---:|---:|---:|---:|---|
| F138_3, seed 138, location 0 | MDB → FJH | 640 kg | 1,099.23 s | 193.91 fpm | 1.412 | Delivered, no fragile penalty |
| F36_4, seed 36, location 2 | PVI → FJH | 310 kg | 1,394.67 s | 249.77 fpm | 1.460 | Delivered, no fragile penalty |
| F225_1, seed 225, location 2 | PVI → FJH | 530 kg | 1,180.60 s | 131.71 fpm | 1.453 | Delivered, no fragile penalty |

Commands: `game_flight_probe board 0 138 F138_3 3`, `game_flight_probe board 2 36 F36_4 3`, `game_flight_probe board 2 225 F225_1 3`. Logs: `logs/flight/board_F138_3_islander.log`, `board_F36_4_islander.log`, `board_F225_1_islander.log`. The old/general claim that routine Islander fragile jobs stall or damage cargo is not reasserted on this evidence.

## Limits and non-findings


- Split-S findings have been promoted to F-STUNT-1 above. The lower-altitude actual-key failures (all three research types) and higher-altitude Wraith passing control are verified.
- Bounce bookkeeping: source stores each latest touchdown over the previous touchdown at `game.cpp:1495`; an initial 36-case real-physics landing-contact sweep did not produce a hard-first/soft-last successful arrival, so no confirmed hard-landing/cargo escape claim is made. One soft-soft two-touchdown control confirms replacement mechanics; most nose-high poorly flown fixtures crashed as expected. `logs/flight/bounce_landings.log`.
- No dynamic loading/CG configuration exists in the current public Plane API: payload/fuel change mass, but aerodynamic/ground-contact CG is the aircraft geometry's fixed `aeroGeom(s).cg` (`aircraft.cpp:347`, `640-656`; `aero_strips.cpp:276-317`). Alternate CG distributions cannot be tested without inventing a production model edit; this is a model/coverage limitation, not an asserted regression.

## Additional emergency and physics coverage

The corrected emergency diagnostic contains 18 cases: Kestrel/Islander/Starling, 2.5/4.0 km approaches, both headings, normal engagement plus a direct-final control only when its heading matches the planned runway end. All 12 normal-engagement cases fail to complete autoland (11 crashes, one off-airport ground timeout); all 6 direct-final controls reach ground without crashing, but two stop beyond the runway and are correctly marked `apOverrun`. Only the successful on-runway controls support F-EMERGENCY-1; no “no crash means success” inference is made. The original experimental log with unmatched direct-final heading is deliberately named `engineout_initial_unmatched_final_do_not_score.log` and excluded from all findings/counts.

A bounded one-second fuel differential (same throttle .7, same initial mass/speed, one engine failed) shows twin fuel use unchanged after losing one engine: Islander .179443 kg, Meridian 1.171875 kg, Starling .805664 kg for both healthy and one-out cases. Kestrel one-out uses 0 vs .074158 kg healthy. Source `aircraft.cpp:332-334` gates fuel burn on aggregate `engineRunning` and throttle, not active engine count. This is documented here as a model-fidelity limitation, not a release-blocking safety finding; it may be an intentional simplification. `logs/flight/engineout_fuel.log`; `emergency_stunt_probe fuel`.

## Coverage summary and remaining boundaries

| Diagnostic group | Cases | Outcome / scope |
|---|---:|---|
| Maximum legal mass autoland | 30 | 30 runway stops; 4 fragile-threshold failures across synthetic mixed-load stresses |
| Synthetic actual-game cargo controls | 2 | Mixed-max load damages cargo; cargo-only maximum passes |
| Ordinary generated fragile jobs | 3 complete flights | 3 successes, no cargo penalties, real takeoff-to-debrief |
| Flight-model LOOP/SPLIT-S | 26 | 23 safe completion/refusal/abort; 3 low-altitude research split-S crashes |
| Actual-key research stunt | 4 | 3 low-altitude crashes; high-altitude Wraith passes |
| ATC controller states | 4 fixtures | Before/after clearance, long occupied departure, alternate selection |
| Actual occupied autoland | 1 complete flight | Tower ignored; successful landing with 50% instruction penalty |
| Matched engine-out physics | 18 | 12 NAV noncompletions; 4 safe direct-final stops, 2 overrun controls |
| Actual engine-out AP-key | 2 | NAV ditch; matched direct-final success |
| Landing-contact/bounce | 36 | No proven hard-first/soft-last penalty escape; contact failures and a soft bounce control |
| One-engine fuel differential | 8 | Active-engine-count simplification measured |

This is 134 custom diagnostic scenarios, several deliberately paired controls and synthetic fixtures; it is not 134 ordinary missions or a statistical failure rate. Independent root replays are additional validation, not added to this count. The root's stock full-suite run reports 41/41 passed. Headless physics cannot assess controller ergonomics, visual runway visibility, subjective motion/sound, or timing on a Windows GPU; those belong to the parallel UI/renderer/platform review. All airports × all planes × every weather/load/failure state was not exhaustively enumerated. The source's fixed CG prevents a truthful variable-loading-CG sweep. No production fix, commit or push was made.

## Portable rebuild and run

Linux/WSL, a C++17 compiler and CMake are sufficient; these headless diagnostics do not need a graphics context. Point `SOURCE_DIR` at a clean v3.43.0 checkout and `BUILD_DIR` at an out-of-tree build. The build helper refuses a different Git revision if `.git` is present.

```
export SOURCE_DIR=/path/to/Solace-Express-v3.43.0
export BUILD_DIR=/path/to/v343-audit-build
export AUDIT_OUTPUT_DIR=/path/to/repro-output
mkdir -p "$AUDIT_OUTPUT_DIR"
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target game_objs -j2
bash repros/flight/build_probes.sh
unset FLIGHT_QUICK PILOT
repros/flight/game_flight_probe flown
repros/flight/game_flight_probe engineout
repros/flight/game_flight_probe stunt 12 700
repros/flight/game_flight_probe stunt 12 1500
repros/flight/game_flight_probe stunt 10 700
repros/flight/game_flight_probe stunt 11 700
repros/flight/game_flight_probe
repros/flight/game_flight_probe board 0 138 F138_3 3
repros/flight/game_flight_probe board 2 36 F36_4 3
repros/flight/game_flight_probe board 2 225 F225_1 3
AUDIT_PAYLOAD=-1 AUDIT_FUEL=.75 repros/flight/flight_probe --airport CAP --wind 2 --comfort
AUDIT_PAYLOAD=-1 AUDIT_FUEL=.75 repros/flight/flight_probe --craft 3 --airport FJH --comfort
AUDIT_PAYLOAD=-1 AUDIT_FUEL=.75 repros/flight/flight_probe --craft 5 --airport CAP --wind 0 --comfort
repros/flight/emergency_stunt_probe stunts
repros/flight/game_flight_probe bounce
```

`build_probes.sh` accepts `CXX` and `AUDIT_BIN_DIR`; runtime `SOURCE_DIR` selects the voice assets and `AUDIT_OUTPUT_DIR` contains isolated `state-*` fixture saves. Both diagnostic programs and build helper were compiled/verified against the exact release. Test results refer to this Linux source build, not a claimed Windows numerical replay or real-world aircraft certification. The release's Windows artifact validity and stock suites are covered by the parent review.

All fixture seeds and parameters are preserved in source. Maximum two physics processes at a time. Production source was checked with `git diff --exit-code` and remained unchanged.
