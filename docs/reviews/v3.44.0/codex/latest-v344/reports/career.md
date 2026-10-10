# v3.44.0 applicability pass: career, persistence and research

## Result

**All five career findings carry forward at the source level.** None of their affected functions or UI entry points was fixed in published **v3.44.0, b22c2fca66fff88b62801a79f9f0c6edf06dcb9e**.

This is a bounded comparison with the previously executed v3.43.0 audit at `91212bf4a131c02d2da41efbdb09248a09bc3599`. The original report remains unchanged at `../../reports/career.md`. **No fresh v3.44.0 runtime reproduction or CTest result is claimed in this report.** The root audit owns the new build and aggregate verification.

## Evidence level and method

- Verified the v3.44 checkout's exact HEAD and clean status.
- Compared file bytes and SHA-256 hashes for twelve relevant implementation/header/test files.
- Extracted and SHA-256 compared 31 complete functions, including a changed rendering function as a control: **30 unchanged; only `Game::render` changed**.
- `career.cpp`, `career.h`, `game.h`, `game_ui.cpp`, `game_research_ui.cpp`, `aircraft.cpp`, and all five selected career/gameplay test files are byte-identical.
- The only `game.cpp` changes are one `volcano_effects.h` include and a call to `volcano::append` during rendering. The include shifts the applicable gameplay source references by **+1 line**. The new header contains namespaced visual helpers, not career/persistence mutations.
- Complete hashes and line maps: `../logs/career/applicability.json`. Exact `game.cpp` diff: `../logs/career/game.cpp.diff`.
- Recreate the comparison with `python3 solace-v343-review/latest-v344/repros/career/check_applicability.py solace-v343-audit solace-v344-audit` from the workspace. The comparison script asserts both exact commit IDs and performs no build or production write.

World generation did change, including bounded summit terrain, road/community work and cache identity. Accordingly, prior numerical route estimates, the 8,192-state eligibility sweep, exact false-rejection count, and timing/fuel measurements remain **v3.43.0 runtime evidence** until repeated against the new build. That caveat does not change the verified persistence/state-machine omissions below.

## Finding-by-finding carryover

| Finding | v3.44 status | Actual latest source references | Evidence |
|---|---|---|---|
| CAREER-01, P2: completed waypoints still counted in continuation range/fuel/time planning | **Unfixed, source-identical** | `career.h:195`; `career.cpp:206-213,226-245,258-263,283-333`; `game.cpp:387-405,439-446`; `game_ui.cpp:565,697-705,775-779` | `continuation()` still retains the whole waypoint list; unchanged eligibility/planning traverse it; unchanged `applyJobLeg` skips completed checkpoints only at flight start. The old Islander CAP→LHK refusal is previously reproduced evidence, not newly executed on v3.44. |
| CAREER-02, P2: incomplete-waypoint jobs cannot close a safe diversion | **Unfixed, source-identical** | `game.cpp:1620-1633`, especially `1629`; recovery branches `1065-1071` | Entire `Game::updateFlight` is byte-identical. The remaining-checkpoint branch still prevents reaching diversion/off-airport handling, despite stored checkpoint recovery support. Old matched-control Game updates reproduced the failure. |
| CAREER-03, P2: research ground launch inherits career fuel choice | **Unfixed, source-identical** | `game.cpp:352-360,371-375,948-976,1894-1918`; `game_ui.cpp:729-730`; research entry `game.cpp:3669-3673` | All chosen-fuel, start, restart and research-launch functions plus UI fields are unchanged. XR-10/XR-20 still use the global career selection on ground; airborne reset and special XR-30/40 branch still differ. Previous 7 kg launch values are v3.43 results. |
| CAREER-04, P2: divert-and-continue discards recorded hold violation | **Unfixed, source-identical** | `career.h:180-195`; `career.cpp:598-662,754-755`; `game.cpp:402-411,983,1066` | JobState still lacks carried compliance fields; closeLeg/settleJob/reset functions and save format are unchanged. Prior matched settlement probe showed −$70/2 stars direct versus $0/3 stars after a split. |
| CAREER-05, P3: research “signed off” ignores save failure | **Unfixed, source-identical** | `game.cpp:242-259,670-675,910` | `saveSettings`, `updateResearchCard`, `shutdown` and settings load are unchanged. Save failure still has no return status/warning, while completion is announced unconditionally. Prior injected write-failure reproduction remains applicable at source level. |

The prior recommended fixes still apply without revision: remaining-route planning with explicit checkpoint mapping; independent diversion/completion gates; isolated research fuel plans; carried or immediately settled compliance penalties; and visible/retriable research-progress persistence.

## Selected unchanged function hashes

Hashes below are the shared v3.43/v3.44 **SHA-256 prefixes**; the JSON contains full 64-hex hashes for both versions.

| Function | v3.43 lines → v3.44 lines | Shared hash prefix |
|---|---|---|
| `Career::canFly` | 226–250 → 226–250 | `4a4d7d8d40f2923d` |
| `Career::plan` | 283–334 → 283–334 | `8f555f428c681f41` |
| `Career::closeLeg` | 598–639 → 598–639 | `952feee5b40554d6` |
| `Career::settleJob` | 640–662 → 640–662 | `b0eadedc70f8e42f` |
| `Career::load` | 1116–1252 → 1116–1252 | `ee15128e57e34fe6` |
| `Game::chosenFuel` | 351–360 → 352–361 | `eef579572a2d8395` |
| `Game::continueJob` | 386–399 → 387–400 | `bed7e7990aba43d5` |
| `Game::applyJobLeg` | 401–412 → 402–413 | `8c69237d9e5f754a` |
| `Game::retryFromDebrief` | 438–453 → 439–454 | `ad7e256d6c072504` |
| `Game::startFlight` | 947–1008 → 948–1009 | `608526d2c93b64be` |
| `Game::updateFlight` | 1324–1647 → 1325–1648 | `ac24b3f003ca7fca` |
| `Game::launchResearch` | 1893–1921 → 1894–1922 | `39984d38f16e1424` |
| `Game::saveSettings` | 241–258 → 242–259 | `0aa8cebb718f60a8` |
| `Game::updateResearchCard` | 646–676 → 647–677 | `b1f539806e02ffc2` |

Changed-function control: `Game::render` moves 3737–3826 → 3738–3827 and has a different hash, consistent with the explicit volcano rendering addition. No state/persistence method above changes with it.

## Save-hardening concerns

All previously reported malformed-save concerns remain in the **identical full parser**, `Career::load`, and unchanged downstream functions:

- Positive-balance, zero-payment loan: `career.cpp:1156-1161`; `payLoan:1015-1036` unchanged.
- Duplicate ownership of an aircraft type: `1144-1150`; purchase/finance invariants remain unchanged.
- Duplicate aircraft/pilot route assignments: `1154,1234`; `assignRoute:886-902` unchanged.
- Non-boolean `finished` value: `1140`.
- Negative reputation accepted: `1131,1236-1240`.

**Evidence level:** v3.43 malformed-save runtime fixtures + byte-identical latest parser and accounting code. Still classify these as defensive hardening requiring malformed/edited input, not a new player-reachable duplication exploit or a freshly executed v3.44 test.

## Existing protection and closed candidates

The five inspected stock career/gameplay test files are unchanged. The earlier 41/41 aggregate pass and 80 independent checks belong to v3.43 and are not relabeled as latest runtime passes here. The selected-fuel, survey-history, atomic-transaction, repossession and Free Flight isolation code remains unchanged by this release.

The successful-story retry and quote/overweight candidates remain unproven ordinary-player defects, with the relevant UI guards unchanged. The original report's daily-course score identity and settings-only return-path suggestions remain source-level improvements. This bounded pass did not expand scope or alter finding severity.

## Preservation

No production files, stock tests, build configuration, original v3.43 report, or remote repository state were modified. Only this latest-version applicability report and its read-only comparison evidence/script were added.
