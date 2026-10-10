# Solace Express v3.44.0 — gameplay, code and quality review

## Latest release and evidence provenance

v3.44.0 was published during the original v3.43.0 audit. Start with [the latest-release applicability report](LATEST_RELEASE.md). Fresh v3.44 tests and comparisons are under `latest-v344/`. The original five domain reports intentionally keep their v3.43 version, dates, line numbers and screenshots. The terminal-culling remedy described as unreleased in those historical reports now ships in v3.44 and is verified fixed. Remaining cross-chunk ownership cases are newly reproduced in v3.44.

## Original baseline

- Published release: [v3.43.0](https://github.com/chad04220/Solace-Express/releases/tag/v3.43.0)
- Source commit: `91212bf4a131c02d2da41efbdb09248a09bc3599`
- Release ZIP SHA-256: `19205ebcad8c3a201386e39edfde970060054634aaf479e6583bb44574b6a728`
- Review date: 10 October 2026. The separately inspected development commit `84b8d1a6f1a9dfbabf457873b4bd4728163ad523` contains later world improvements. It is not the source of the release tests.
- This package is an analysis and reproduction handoff. Production source was not edited, committed or pushed.

## Verified results

- Latest v3.44.0: **54/54 local stock tests passed**, full flight mode; exact-release GitHub Windows **49/49** and Linux sanitizer **50/50** passed (Linux CI uses the quick flight mode).
- Initial v3.43.0: **41/41 local stock and 41/41 full ASan/UBSan suites passed**, plus Windows cross-build. Leak detection was disabled in this VM; this is not a leak audit.
- The original voice bank's **1,068 indexed recordings decoded**. Separate deliberately malformed WAV files reveal a real sanitizer failure, described as conditional damaged-asset hardening.
- Fresh latest-object flight/world replays confirm the important carryovers; terminal culling is fixed and ownership mismatches reduced. See the latest addendum for exact scope.
- GPU performance remains unmeasured. A passing CPU suite or Windows CI build is not a native1080p60FPS guarantee.

## Recommended fix order

1. **Give overlays exclusive input ownership.** A radio station click purchases a hidden $30,000 Wren and persists the purchase. In an XR-40 flight, radio clicks also arm and fire weapons. See **UI-01** in [UI/platform/audio](reports/ui-platform.md). Blocking the later radio pass alone is insufficient because underlying actions have already run.
2. **Reject aerobatics that cannot fit the available flight envelope and altitude.** The normal research stunt-key path accepts a Split-S from about 700 m AGL in XR-20, XR-30 and XR-40; all three subsequently crash despite a ground-abort decision. XR-40's 1,500 m control passes. See [flight/ATC](reports/flight.md). Admission must use achievable stall-limited turn authority and recovery distance, rather than only a structural g limit.
3. **Connect tower decisions to autoland and keep clearance live.** A full Game-loop Wren replay receives “Go around. Aircraft on the runway,” lands under autopilot, and charges the player $500 for disobedience. A runway incursion after clearance is ignored, an occupied departure runway is cleared after the wait timeout, and GPS diversion leaves ATC targeting the original airport. These are distinct state/integration defects. A separate reachable-runway engine-out replay also loses the runway when normal AP navigation chooses a climb/orbit; its matched final-approach control lands. See **F-ATC-1–3** and the emergency-planner finding.
4. **Repair partial-job diversion and continuation.** Remaining survey checkpoints prevent entering the recovery flow; completed checkpoints still inflate continuation fuel/time and can falsely reject a capable aircraft. Separate delivery success from safe leg closure and use only the unflown route for planning. See **CAREER-01/02** in [career/persistence](reports/career.md).
5. **Unify destruction graphics, collision and persistence.** Plasma crater rendering lowers the ground by 8 m while aircraft still rest on its old invisible floor. Eighth-bomb eviction restores previously destroyed scenery collision. Some cross-chunk objects remain visible after their collision is destroyed. See **WR-1–3** in [world/release](reports/world-release.md).
6. **Finish mode isolation, input accessibility and audio state transitions.** Research ground starts can inherit a 7 kg career fuel choice; diversion discards a hold penalty; scaled research controls overlap; settings toggles lose focus; master volume and paused speech ducking do not consistently control radio. Exact steps and fixes are in the domain reports.

Priority labels are game-development triage, not claims of real-aircraft safety certification or security exploit severity. Hardening cases requiring corrupted files or failed writes are explicitly separate from ordinary player actions.

## Read the detailed evidence

- [Career, progression, save/recovery, research fuel](reports/career.md)
- [Flight, ATC, aerobatics and mission quality](reports/flight.md)
- [UI, input, audio and Windows inspection](reports/ui-platform.md)
- [World, collision, streaming, traffic and release integrity](reports/world-release.md)
- [Renderer, procedural mesh and cloak comparisons](reports/renderer.md)
- [Scope and method](AUDIT_SCOPE.md)
- [Target-hardware acceptance checklist](HARDWARE_CHECKLIST.md)

Each report identifies exact code locations, expected/actual behavior, diagnostic adaptations, commands, logs, severity and remaining limits. Test-only sources are under `repros/`; they are not proposed production patches. Evidence filenames retain the original source identifiers used in reports.

## Important interpretation

- A passing stock suite does not cover every player interaction. The new failures exercise transitions and cross-system boundaries that existing assertions miss.
- The legal maximum-mass flight fixtures deliberately combine maximum passengers and cargo. They expose comfort/fragile thresholds, but are **not proof of an ordinary generated fragile-contract failure**. Three actual generated fragile contracts passed full takeoff-to-debrief replays.
- Terminal frustum culling was defective in v3.43, but v3.44 ships the authored-bounds remedy and passes all four original repros. Keep its regression test; do not write a duplicate replacement. New garden/farm placement reduces ownership mismatches from 130 to 13 in the sampled chunks; the boundary-index cases still reproduce in v3.44.
- UI screenshots show actual production widgets with a blank 3D backdrop to isolate layout/input. The blank aircraft preview area is a diagnostic choice, not a missing-aircraft finding.
- Cloak comparisons use actual release rendering with a selected-aircraft fixture, traffic disabled to avoid compiling unrelated software-renderer shaders, and explicitly recorded resolutions. They do not certify full startup or frame rate on NVIDIA hardware.
- Numerical/source concerns and visual observations without a reproduced player-visible failure are labeled separately. Do not turn them into additional confirmed regressions.

## Reproducing safely

Use a separate checkout at the exact commit, an isolated build directory and disposable diagnostic save/cache folders. Never point these probes at an installed player's career or settings. The small `wav-fixtures/` inputs are deliberately malformed decoder test files; do not copy them into the game's voice assets. Build the release's `game_objs` and tests using its CMake instructions. Domain scripts support source/build overrides where provided; consult each repro README and report for dependencies. Some full flight and sanitizer runs take several minutes.

The archive includes13 key previews at their original resolution. The remaining layout matrix, duplicate A/B source frames and PPM originals are retained locally; their measurements, hashes and reproduction scripts are included. Some detailed reports refer to those optional captures by their original filename.

The archive intentionally excludes the downloaded release ZIP, compiled executables, large generated world caches and temporary save directories. Download the original release separately if needed. File hashes in `SHA256SUMS` cover the included evidence. No bundled diagnostic binary needs to be trusted or executed; rebuild from the provided sources.

## Scope limits

This is a broad, evidence-driven audit, not proof that all possible states are error-free. Native Windows execution, hardware audio/Media Foundation streaming, physical controller behavior, all world locations and weather seeds, and RTX 3070 performance remain unverified. The target remains native 1920×1080 at 60+ FPS; software-renderer timings must not be converted into target-GPU estimates.
