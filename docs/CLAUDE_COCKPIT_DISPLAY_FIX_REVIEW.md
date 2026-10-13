# XR-30 and Atlas cockpit display fixes

Local review prepared on upstream `bfcd7ca0c60d0ea35babacd2ef6609fcefcb6858` (2026-10-13). No release or remote publication is part of this review.

## XR-30: smooth primary-display top edge

The lower cylindrical panoramic bezel intersected the upper corners of the flat primary attitude display. The resulting curve acquired additional small teeth from the baked mesh. The display artwork and its exact per-pixel mask were correct.

The fix adds a panel-aligned mounting relief to that bezel. The opening follows the existing chamfered glass outline plus 14 mm, bounded to 18 mm behind / 42 mm in front of the glass plane. The original 18 mm module border covers this opening. Panel position, size, artwork, focus target, neighboring modules, saddle, moving controls and panorama opening are unchanged.

The physical bezel field now lives in `research_cockpit_layout.glsl` so the production geometry and CPU regression use exactly the same expression. `mapJetCockpit` consumes it. Tests cover a dense glass grid, exact chamfer corners, near-face rays from the nominal eye and six 25 mm offsets, neighboring modules, saddle clearance and supporting overlap, plus preserved solid bezel witnesses outside the relief.

- 10,615,443 research-layout assertions pass; minimum sampled bezel clearance is 12.0 mm.
- Exact-outline neighbor clearance is 1.5 mm; the existing interior-grid minimum remains 6.5 mm. No existing clearance gate was weakened.
- Replacing only the relief result with the original bezel field makes 256,085 checks fail, with -11.8 mm minimum near-face clearance.
- Pilot, look-right, look-left and night production-render captures show a straight top edge, with the original small chamfered corners.

## Atlas A180: readable overhead text

The downward-facing overhead utility display used `(q.x, -q.z)`, reversing its texture handedness when viewed from below. Mapping it with `(q.x, q.z)` restores readable HDG, FLAP, GEAR, FUEL and live values. No screen geometry, atlas size, emission, state values or physical switches changed.

The source-bound surface regression derives tangents from the actual mapping, checks the underside normal, and checks handedness from both seat eyes and an oblique eye position. The original mapping fails the new assertions; the corrected mapping passes. Actual pilot, copilot and oblique render comparisons were inspected independently. Significant pixel differences remain confined to the display face.

## Verification

- Native Linux Release build of the production objects, renderer harness and focused tests passed.
- All 19 selected CTest checks passed, including cockpit layout/camera/focus, source generation, surface contracts, research layout, aircraft build interfaces, mesh orientation/assets, shader pruning, GLSL validation, shader specialization and shader loading. This is a focused suite, not the entire game test suite.
- MinGW-w64 Windows cross-build produced `SomniumVolandi.exe`; this is not an MSVC build or a native Windows runtime test.
- The research layout test also passed AddressSanitizer and UndefinedBehaviorSanitizer with leak detection disabled: LeakSanitizer is unsupported under this executor’s tracing environment.
- Actual full production renderer: surfaceless EGL, llvmpipe (LLVM 19.1.7), Mesa 25.0.7, 1600×900, Medium, render scale 1, eight static frames, HUD/UI hidden, traffic disabled. No geometry overrides or alternate display rendering were used.
- The two XR-30 cockpit bake programs changed as expected. All other 58 specialized bake programs across 15 aircraft × two bodies × distance/normal variants are byte-identical to baseline, including XR-30 exterior and both Atlas bodies. The intentional shared/AF_JET hash fixtures are updated; AF_LIGHT remains unchanged.
- Final XR-30 geometry was baked from the final shaders. A separate warm-cache run loaded that geometry and reproduced the pilot capture byte-for-byte. Atlas retained its unchanged, valid production mesh.

The first combined XR-30/Atlas software-render run exhausted memory during Atlas's cold cockpit bake. Running Atlas alone completed the original full-quality bake, after which all three baseline and final views used the valid cache. No mesh-quality reduction or forged cache identity was used. Hardware frame-time claims and Windows/RTX visual acceptance remain outside this review.

## Evidence

`docs/validation/cockpit-display-fixes/` contains the two labeled before/after sheets, specialized bake SHA256 comparisons, final focused test log, sanitizer log and warm-cache verification. The sheets use matched crops/resizing and labels only; game pixels were not retouched.

XR scenes: `ckv10_0_-18_11`, `ckv10_-12_-18_11`, `ckv10_12_-18_11`, `ckv10_0_-28_22`, zoom 1.

Atlas scenes, zoom 2.5:
- Pilot: `ckv14_-70.14479_30.89195_11`, original eye.
- Copilot: `ckv14_70.14479_30.89195_11`, body-space eye offset `(1.44,0,0)`.
- Oblique: `ckv14_-51.84277_38.07906_11`, body-space eye offset `(.16,-.10,.18)`.

The diagnostic capture adapter uses the repository EGL UI-review initialization and `Game::debugScene`/`Game::render`, sets only these camera/view settings, and reports actual camera world coordinates. Before and after camera coordinates match. There are no production camera changes.
