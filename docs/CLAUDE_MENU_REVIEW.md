# Menu, hangar, loading and Free Flight review

Integration base: `9608d678725fbfc9f092a8ec44ffcaf9ba326e5a` on
`claude/compassionate-davinci-4cfo20`, the published v3.40.0 release commit. The initial owner-selected base was `473d4201eb22a59ef6549b487172cc404ed3b34b`; the owner then requested integration against the new release. Verified against published v3.40.0 (2026-10-09 21:12:23 UTC); combined native and Windows cross-build checks passed on this exact base.
Local proposal branch: `codex/menu-hangar-review-20261009`.
The owner approved publication to this separate review branch on 2026-10-10. Merge and release remain with Claude and the owner.

## Implemented

- Consistent dark aviation menus, clearer cards and hierarchy, independent scrolling panels and stable keyboard/D-pad focus through scrolling.
- The career hangar previews the selected real aircraft parked inside a cached 3D hangar. All nine career aircraft retain their existing acquisition, finance, fuel and service actions.
- Four research teasers follow the career aircraft in XR-10 / XR-20 / XR-30 / XR-40 order. They show redacted information and no economic actions. A geometry-only low-key lighting pass reveals the outline while ignoring liveries, registrations, displays and emissions. The owner reviewed and accepted the brighter rim-light level.
- The research-terminal redesign and changed default selection were withdrawn at the owner’s request. The original terminal layout and behavior are retained. Classified aircraft in the career hangar remain part of this proposal.
- Main-menu **Free Flight** opens a separate hangar selector: all nine ordinary aircraft, all 16 airports, runway or airborne starts. It supplies full tanks without ownership, licence or money requirements. No research aircraft can launch through this mode. Short runways produce a warning, not a restriction.
- All eight propeller aircraft and AI/airline traffic use shared tapered, curved blade silhouettes and continuously integrated blur. Full-speed blur is phase independent. Existing radii, hubs and blade counts are preserved. No extra geometry or blur-sampling loops; see `PROPELLER_VISUALS.md`.
- Loading progress accounts for completed work, including all **41 logical shader programs**, renderer resources, performance calculations and scenery/airframe preparation. Helper cache preparation is distinct from main-context compilation. Failed optional helper work is explicitly skipped, never reported as compiled. Long indivisible driver calls can still pause the percentage while the independent animation continues.
- Smoothed original logo/icon assets, time-based orbital animation, and antialiased UI strokes without additional per-line draw calls.

- Foliage LOD and draw ranges extend about 10–12% at every main-view tier. High tree ranges are now 400 m / 2 km / 7.8 km; camera-feed and shadow budgets remain at the base settings. See `FOLIAGE_DRAW_RANGES.md` for all presets and analytical cost estimates; RTX 3070 frame-rate validation remains outstanding.

## Free Flight isolation and future research unlocks

Free Flight does not quote, accept, save or settle a career job. It uses a separate zero-fee launch plan and an early return before scoring/settlement. Restart refills for free. Loading cancellation, crash and pause-menu return lead back to the free hangar. Existing landed-and-stopped completion behavior also returns to the free hangar; it is not an indefinitely taxiable session after completion.

The shared `hangarResearchLocked` hook always locks research craft today. Future special missions and persistent unlock state are **not implemented**. Existing hidden research-terminal access is unchanged. Career's buyable roster and save format are unchanged.

## Protected integration boundaries

`Game::debugScene` and `Game::updateFlight` match the approved base byte-for-byte, as does the Windows `--analyze` section through the end of the file. Hashes are recorded in `ui-review/protected-code.json`.

Aircraft geometry, gear mechanisms, career economy rules and save format were not redesigned. The static showroom computes tyre support positions without altering flight physics. `FLEET_ON`, `JET_ON` and `WRAITH_ON` specialization boundaries are preserved; new player propeller code is gated by `FLEET_ON`.

## Verification

- Final native Release build and all **39 CTests passed** after integrating the incoming cloud-accumulation and shader-pruning changes, foliage extension and research-menu restoration (76.80 seconds).
- Final Windows x64 cross-build passed; this is a compile/link result, not a Windows runtime test.
- Free Flight: all **288** aircraft × airport × start-mode combinations pass. Additional active/recovery-job tests preserve serialized career bytes and save-file modification time across live updates, restart/refill, landing, all end outcomes, crash, abort and loading cancellation. Research/invalid selections are rejected, and unsaved career transactions neither block nor leak into Free Flight.
- Real UI interaction tests cover research selection, all 13 catalog entries, keyboard/Enter and gamepad-event activation, lock/no-economic-control boundaries, all nine free aircraft and 16 airports, start toggles and Escape. No physical controller was available.
- Analytical showroom checks cover all 13 aircraft, framing at multiple resolution/UI-scale combinations, grounded tyre supports within 0.1 mm and stopped prop state. A regression additionally checks that every free preview selects its own aircraft model rather than the career hangar's selection.
- Seven shader-loader fallback/failure cases verify logical completion counts without inventing progress.
- Production propeller GLSL tests cover stopped/partial/full blur, phase stability, alpha conservation, small projected discs and player/traffic parity.

## Render evidence and limitations

Actual 1920×1080 captures use a **test-only focused startup adapter**. It omits eight unused generic/research/marched startup programs, pre-bakes the selected exact production fleet mesh, and aborts if an omitted marched aircraft/shadow path is required. Aircraft geometry, materials, gear, effects, lighting and shadow passes are not substituted. It supports the compatible fleet models and XR-10/XR-20, not XR-30/XR-40 scenes. See `tests/UI_REVIEW.md` for reproducing it.

Normal full-scene initialization on software Mesa was terminated with exit 137 during heavy shader compilation. Its cause was not established with an OOM event log. Successful focused captures do not establish full mixed-fleet startup on that driver. UI-only layout screenshots explicitly omit the 3D backdrop.

Native Windows runtime, physical-controller behavior and RTX 3070 performance remain unverified. Test native 1920×1080, cold and warm startup, rapid aircraft selection, classified/ordinary transitions, Free Flight entry/restart/return, and returning to career. No 60-FPS claim is made from software-renderer timings.

The loading animation sample holds at 42% for six seconds to show motion. That earlier preview uses a 40-program label; the integrated runtime now accounts for 41 programs, including cloud accumulation. It is not a startup benchmark. Icon assets are reproducible with `tools/generate_icon.py`.

## v3.40.0 release integration

The new upstream cloud temporal accumulation, shader pruning before compilation/cache lookup, shader-pruner regression test, and raw/pruned shader validation are preserved. The loader separately counts cloud composition and accumulation. Eight mocked shader initialization cases pass, including optional cloud-accumulation unavailability.

A fresh normal full-production Mesa startup was retried after integration and again terminated with exit 137 before the first initialization callback or scene frame. The log cannot identify the responsible shader or establish an out-of-memory cause. Native Windows/GPU rendering remains a required downstream check; earlier focused-adapter images remain design evidence rather than full integrated rendering validation.
