# Career, persistence, progression and research audit

## Scope and evidence

- Audited published **v3.43.0**, commit **91212bf4a131c02d2da41efbdb09248a09bc3599** in `solace-v343-audit`. No finding below is inferred from the newer unpublished working head.
- Read the full career implementation and save parser; related Game launch/restart/continuation/settlement/interrupt paths; career/hangar/airline/debrief controls; research completion and settings persistence. Read `save_test.cpp`, `progression_test.cpp`, career-related `gameplay_test.cpp` sections, `state_regression.inc`, and `free_flight_regression.inc`.
- New probes link all 32 **unmodified release** `game_objs` objects. Test access uses the release's existing `friend struct GameTest`. No source or stock test was edited.
- Runtime is Linux/headless with the release's radio stub. Flight state is positioned at meaningful boundaries (a safely stopped aircraft, a completed checkpoint list, a recorded tower violation); the production update/launch/settlement functions perform the transition under test. These probes are not claims that every route was manually flown from takeoff to landing.
- Reproduce from the workspace with `bash solace-v343-review/repros/career/run_probes.sh` after the root aggregate build has completed. To use another checkout/build location, run `SOURCE_DIR=/absolute/path/to/v3.43.0 BUILD_DIR=/absolute/path/to/build bash solace-v343-review/repros/career/run_probes.sh`. The sibling `solace-v343-audit` / `solace-v343-audit-build` defaults remain unchanged. The script verifies the exact source commit and compiles just one new translation unit at a time.
- Artifacts: `repros/career/career_probe.cpp`, `repros/career/career_edge_probe.cpp`; logs `logs/career/career_probe.log`, `logs/career/career_edge_probe.log`.

## Findings, ordered by practical impact

### CAREER-01 · P2 · Completed checkpoints are still included in continuation eligibility and fuel/time planning

**Status:** Reproduced, high confidence. Normal generated contract and real debrief Continue path.

**Source:** `src/career.h:195`; `src/career.cpp:206-213,226-245,258-263,283-333`; `src/game.cpp:386-404,438-445`; `src/game_ui.cpp:565,697-705,775-779`.

`JobState::continuation()` changes the starting airport but retains every original waypoint. Eligibility/range checks and launch planning traverse that complete list. The actual flight subsequently starts at `wpDone`, skipping completed checkpoints. The quote and aircraft gate therefore describe a route the player will not fly.

**Reproduction:** `career_edge_probe::continuationRefusal` uses the normal generated board: ATP, story index 4, Meadowbrook, seed 1, contract `F1_3` (survey to LHK), Islander Twin rental. Its initial `canFly` succeeds. Set all six checkpoints complete, then stop at CAP and let `Game::update` trigger the normal diversion. Invoke the same `retryFromDebrief` action used by Continue job.

**Actual:**

- Actual remaining CAP→LHK distance: **45.2769 km**.
- Original six checkpoints are all complete.
- Continue rejects the Islander: **“Range 90 km too short (need 96 km incl. approach and reserve)”** and returns to the hub.
- Running the same eligibility check on the remaining route with completed waypoints removed returns `SRC_RENT`.
- A sweep of **8,192** generated survey/previously-eligible aircraft/diversion combinations found **1,582** such false rejections. This is a planning-state sweep, not 8,192 physically flown diversions.
- Another real generated survey, `F1_5`, quoted **18.1151 min / 78.7403 kg** after all rings were complete, versus **15.0541 min / 64.0519 kg** for the remaining route. Actual launch skips all six rings but accepts the larger quote.

**Expected / impact:** A completed section of a job must not disqualify a capable aircraft or inflate the remaining flight's estimates. Players can be forced to change/rent aircraft after a safe diversion, despite their aircraft being able to complete the job. Owned-aircraft fuel choices can also overbuy or warn unnecessarily.

**Minimal fix:** Introduce a remaining-route view for eligibility, quotes and background simulation. Preserve original contract/checkpoint indices for stored progress; do not simply erase the vector while retaining an unadjusted global `wpDone`. Regression-test both a partial route and an all-checkpoints-complete diversion through the actual Continue button handler.

### CAREER-02 · P2 · A safe diversion cannot close a waypoint job until every checkpoint is complete

**Status:** Reproduced, high confidence, through the actual `Game::update` completion detector with a matched control.

**Source:** `src/game.cpp:1619-1632`, especially the `wpIndex < contract.wps.size()` branch at 1628. Intended recovery data and behavior: `src/career.h:176-179,185-195`; `src/game.cpp:1064-1070`.

**Reproduction:** `career_probe::waypointDiversion` uses a release-generated PPL survey `F1_5`, MDB→ORC, six checkpoints. Mark only the first checkpoint passed, safely stop the Wren at CAP, and run 600 real updates at 60 Hz. Repeat the same setup with all six checkpoints passed.

**Actual:**

- Five checkpoints remaining: after **9.90 s stopped**, aircraft is grounded and not crashed, but still `SCR_FLIGHT`, job `ACTIVE`, career location MDB.
- Completed-ring control: after **1.21667 s stopped**, reaches `SCR_DEBRIEF`, `OUT_DIVERTED`, job `RECOVERY`, career location CAP.
- With remaining checkpoints, the common branch only says to take off again. It also blocks the off-airport recovery branch. The player cannot enter the hub to refuel/service and continue from the actual stop; abandoning returns the load to the departure and charges recovery instead.

**Expected / impact:** Remaining checkpoints should prevent successful delivery, while allowing a resumable survey/tour to stop safely at an alternate and continue from there. This is particularly consequential for a fuel or systems diversion.

**Minimal fix:** Gate destination success on waypoint completion, separately from alternate-airport/off-airport leg closure. Apply the intended retake policy to lessons/checkrides. Add actual stopped-flight detector tests with partial checkpoints; the existing state regressions call `endFlight(OUT_DIVERTED)` directly and therefore do not exercise this blocker.

### CAREER-03 · P2 · Research ground launches inherit an unrelated career fuel selection

**Status:** Reproduced, high confidence. Mode-boundary state leak; conventional XR-10 and XR-20 only.

**Source:** `src/game.cpp:351-359,370-374,947-975,1893-1917`; career fuel UI `src/game_ui.cpp:729-730`; research entry `src/game.cpp:3668-3672`. Free-flight's correctly isolated pattern is at `src/game.cpp:497-503`.

**Reproduction:** `career_edge_probe::researchFuel`. A career Kestrel fuel selection of **7 kg** is reachable using the existing 10%-minimum arrows. Return to the main menu without launching that career flight, enter research, choose a runway start in XR-10/XR-20, and launch. The probe sets exactly that selection and invokes the production research launcher and Restart.

**Actual:**

| Research aircraft | Tank capacity | Default ground start | Ground start after 7 kg career selection | After Restart |
|---|---:|---:|---:|---:|
| XR-10 | 1,300 kg | 337.036 kg | **7 kg** | 337.036 kg |
| XR-20 | 1,800 kg | 201.328 kg | **7 kg** | 201.328 kg |
| XR-30 | 3,000 kg | 3,000 kg | 3,000 kg | 3,000 kg |
| XR-40 | 3,000 kg | 3,000 kg | 3,000 kg | 3,000 kg |

The unrelated career setting is consumed/reset to `-1`. Airborne research explicitly replaces the aircraft with full tanks; only the ground path exposes this leak. Thus “Restart” also changes the initial research fuel load.

**Expected / impact:** An isolated research sortie should have its own consistent fuel policy. A previously chosen trainer fuel load must not silently put a research jet on the runway with 0.4–0.5% fuel and no research fuel-selector UI.

**Minimal fix:** Pass an explicit research launch plan/fuel load and preserve the career fuel choice, following the free-flight isolation pattern. Decide and display the intended research tank policy; ensure runway/airborne starts and restart use that same policy. Do not treat the low default research load alone as a separately proven defect.

### CAREER-04 · P2 · Divert-and-continue erases a recorded departure hold violation

**Status:** Reproduced at production state/settlement boundaries, high confidence in accounting result. The harness sets an already-recorded violation, rather than synthesizing the preceding tower exchange.

**Source:** `src/career.h:180-195` (no carried compliance fields), `src/career.cpp:598-638,640-660,754-755`; `src/game.cpp:401-410,982,1065`.

**Reproduction:** `career_probe::violationRecovery`. Fly the real first cargo contract with `result.holdViolated=true`. Compare delivery in that leg with a safe CAP diversion, Continue job, and then delivery. The same departure infraction is present before the split.

**Actual:**

- Direct delivery: **−$70**, 2 stars, reputation +2.
- Divert, continue, deliver: **$0 total hold penalty across both settlements**, 3 stars, reputation +3; the final recorded flag is false.

`closeLeg` neither charges this penalty nor carries its flag; the new leg resets `FlightResult`; `settleJob` aggregates ride/fragility/time but not compliance. The same omission exists for a recorded go-around violation, but this probe specifically verifies the departure hold flag.

**Expected / impact:** One delivery should not shed an already-earned compliance deduction merely by stopping at an alternate. This can avoid 10% of contract pay and restore a star. Do not confuse this with the intentional no-reputation-loss rule for an otherwise safe diversion.

**Minimal fix:** Carry job-wide compliance flags and OR them into final settlement, including save migration, or settle non-refundable compliance penalties when closing the leg. Add a matched direct/split-job test so an extra leg cannot erase the same recorded infraction.

### CAREER-05 · P3 · Research completion reports “signed off” when progress cannot be saved

**Status:** Reproduced with an injected local write failure; conditional persistence reliability issue.

**Source:** `src/game.cpp:241-257,669-674,909`.

**Reproduction:** `career_edge_probe::researchSaveFailure`. Create a normal settings file, then make its sibling `settings.cfg.tmp` a directory so the atomic writer cannot open it. Complete the last production step of research card XR10-1. Read settings into a new Game.

**Actual:** Live `resCardDone=true`, record present, and toast **“TEST CARD XR10-1 COMPLETE - TRANSONIC DASH SIGNED OFF”**, but reloaded record is absent. `saveSettings` returns `void` and silently returns on write failure. Shutdown retries settings once, but also cannot report a continuing failure.

**Expected / impact:** A player who completes a research card while the save directory is blocked/full should be warned that the sign-off is not persisted. Research/trial achievements otherwise disappear on exit/restart without a visible warning.

**Minimal fix:** Return a success status from settings persistence; preserve a dirty/pending state and show a non-spamming “progress not saved” warning/retry outcome. This should mirror career transaction visibility without blocking unrelated free flight.

## Save-integrity hardening (not ordinary-player exploit findings)

`career_probe::saveMutationChecks` modified isolated copies of an otherwise valid save. All five invalid-state fixtures loaded successfully:

- Positive-balance loan with **payment 0** (`career.cpp:1156-1161`): `payLoan` takes zero and leaves the balance unchanged.
- Duplicate ownership of the same aircraft type (`1144-1150`): contrary to `buy`/`finance`/`buyUsed` invariants.
- Duplicate routes using the same aircraft and pilot (`1154,1234`): contrary to `assignRoute` invariants; both would tick.
- `finished 999` (`1140`) is accepted as true.
- Negative reputation (`1131,1236-1240`) is accepted.

These require malformed/edited save input and are **P3 defensive-validation opportunities**, not claims of a UI-reachable money duplication exploit. Validate positive open-loan payments, unique aircraft ownership/route assignment, strict boolean values, and nonnegative counters/reputation; preserve transactional rejection of a damaged save. Also retain legacy migrations deliberately rather than tightening old versions indiscriminately.

## Passed coverage and regressions that should remain closed

Independent probe added **80 passing assertions**:

- All story licence checkrides across 600/601-fpm boundary and combinations of ignored hold/go-around instructions.
- Financing each of the nine ordinary aircraft, completing the 24-payment schedule, verifying the total removed equals the original balance, then saving/reloading ownership.
- Research launch/restart/crash-return for all four research aircraft preserving career money, flight count and attempt count.

The root aggregate log (`logs/ctest-release.log`) reports **41/41 tests passed**, including `campaign_progression`, `career_saves` and `gameplay_loop`. Read and confirmed substantial existing protection:

- Current/legacy save round-trips, missing/truncated fields and non-finite rejection, atomic failed-write preservation and backup recovery.
- Repossession remaps airline fleet indices and removes only the lost aircraft's route.
- Failed career launch does not start a flight or leave a pending phantom attempt. Settlement write failure preserves the live career and retry cannot pay twice.
- Selected fuel uplift survives restart and multi-leg settlement without double billing; owned tanks retain the actual remainder.
- Survey weighted altitude history/checkpoints survive recovery/restart and old-save migration is explicitly marked unknown rather than awarding a false bonus. **This previously fixed issue is not reopened by CAREER-01/02.**
- Main-menu Free Flight already excludes research craft and isolates career jobs/loans/airline/save bytes across restart, crash, completion and loading cancellation.
- Existing recovery-job/earning-path sweeps cover airport×licence×balance×fleet combinations; rentals without up-front cash checks and broke-player courtesy positioning are deliberate product behavior, not bugs.

## Challenged candidates and practical improvements

- **Rejected successful-story replay exploit:** `retryFromDebrief` itself could replay an old story contract, but the production UI only offers Retry when `!lastSuccess` (`game_ui.cpp:2525`). No normal-player route to the proposed successful replay was established.
- **Rejected quote/overweight defect:** displayed quote uses the background-flown estimate whereas the button gate reuses the quick estimate (`game_ui.cpp:703-705,852-853`). A sweep of **1,063 eligible story/generated-contract aircraft pairs** found zero payloads close enough to the current mass limit to expose a mismatch. Unify the plan source for maintainability; do not count this as a reproduced player defect.
- Consider keying daily/gate trial records by concrete course identity/date/airport. Course generation changes with day or airport (`game.cpp:819-841`), whereas score storage uses only a static trial ID (`game.cpp:864`; `game.h:164-165`). This is an inspected scoring-design limitation, not a newly simulated multi-day finding.
- Test partial-checkpoint recovery through `Game::update`, not only direct calls to `endFlight`; otherwise a fully functioning storage layer can hide an unreachable recovery flow.
- Source-only polish item supplied by the UI audit: first-run Settings/Controls enters the full career hub (`game_ui.cpp:434-435`), whose Main Menu/Escape path calls `saveGame` (`515,526`). Consider preserving a settings-only return path so merely reviewing controls does not create a default career save. Not counted as an independently reproduced defect here.
- Add persisted state-machine property tests: accept → interruption/load → continue → divert → release/complete, combined with aircraft sale/repo/route assignment, and assert no duplicate pay/fees or unearned progress.

## Limits

The runtime probes cover real functions and realistic state boundaries, not a complete rendered UI playthrough or native Windows storage/device behavior. Root owns the aggregate CTest result. No fixes, commits or pushes were performed. The release source tree remained unchanged.
