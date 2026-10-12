# Headless production UI review

`ui_review_harness` renders the real game UI using Mesa surfaceless EGL/OpenGL3.3. It needs no X server, EGL development headers, or changes to production render code. Build explicitly:

```sh
cmake --build build --target ui_review_harness -j2
```

Run from the repository root so material/icon/loading resources resolve. Save data is isolated to a temporary directory; review interactions must not touch checked-in settings or career files.

```sh
export LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=4
export MESA_SHADER_CACHE_DIR=/tmp/solace-ui-mesa
export SHADERCACHE=/tmp/solace-ui-cache
build/ui_review_harness interactions,menu,contracts,airline,pause,hangar0,research10,research20,research,research40 /tmp/ui1080 1920 1080 1
build/ui_review_harness interactions,menu,hangar0,research10,research20,research,research40 /tmp/ui720 1280 720 1.4
build/ui_review_harness intro_0_1,intro_0.42_3,intro_0.999_5,intro_1_8,loading_0.57_5,loading_ready /tmp/loading 1280 720 1
python3 tools/ui_review_contact_sheet.py /tmp/loading /tmp/loading-contact.png
```

UI-only captures are layout previews: the game draws the actual widgets over a clear background, with **no live 3D backdrop**. Loading photographs, if available, remain the game's packaged assets. Do not present layout previews as evidence that a live aircraft/environment rendered successfully. The `intro_<progress>_<seconds>` arguments set independent completed progress and animation time. `loading_ready` exercises the ready-to-fly state.

To render an actual scene, append `scene`; this compiles real production scene shaders and can take several minutes on software Mesa. `FRAMES` defaults to1 in scene mode; use more frames for temporal convergence. Retain caches between runs.

```sh
FRAMES=3 build/ui_review_harness hangar0 /tmp/hangar1080 1920 1080 1 scene
```

`hangarN` selects career aircraft indexN. Research names follow existing debug scenes: `research10`, `research20`, `research` (XR-30), `research40`. Nothing in the harness changes the protected production debug-scene dispatch. The optional `UI_ICON` environment variable supplies a PNG decoded from the actual Windows icon for exact asset comparison.

The research terminal is restored to approved base `473d4201eb22a59ef6549b487172cc404ed3b34b`, including its original XR-30 default selection, animated presentation, adaptive airframe cards, preview callouts, and sortie controls. Research scenes exercise that original terminal; the discarded redesign is not part of this review.

The `interactions` mode asserts the original XR-30 default, three full keyboard selection cycles, three mouse-selection cycles using the original adaptive card layout, incompatible research test reset, departure site wrap in both directions, the original runway/airborne and weather chip targets, and Escape return. It intentionally avoids launch or purchasing actions.

Regression mode also checks hangar/airline keyboard focus identity through automatic scrolling, loading readiness gated by distinct rendered frames, absent optional shadow-shader readiness, and all fifteen catalog aircraft dimension endpoints projected inside the production hangar viewport. Projection checks are CPU geometry evidence, not rendered-aircraft evidence.

## Native aircraft preview diagnostic

`ui_review_hangar_harness` is a separate review target that copies three renderer translation units and adds preparation and verification assertions. It builds every production startup program, pre-bakes the actual selected aircraft mesh before drawing, and preserves geometry, materials, moving parts, gear, camera, lighting, shadows, terrain and post-processing. It accepts all 15 Hangar models, the 11 career Free Flight models, all four Research models, and locked/unlocked Military assignment fixtures. It is a rendered-preview check, not a native Windows cold-start benchmark.

With `UI_VERIFY_PREVIEWS=1`, each rendered frame is checked for actual aircraft pixels in the G-buffer. A UI diagram or loading photograph cannot satisfy this check. Classified frames additionally require a visible geometry outline and a hidden ink interior in the lighting result before the UI is drawn. A missing mesh, blank aircraft pass or invalid classified result aborts the diagnostic.

```sh
cmake --build build --target ui_review_hangar_harness -j2
SHADERCACHE=/tmp/somnium-scene-cache UI_VERIFY_PREVIEWS=1 FRAMES=2 \
  build/ui_review_hangar_harness hangar0,hangar9,hangar10,hangar11,hangar12,research10,research20,research30,research40,military4_locked,military4_unlocked /tmp/aircraft-previews 1280 720 1 scene
```

Final camera checks additionally cover all three transformed tyre supports (within0.1mm of the showroom floor) and zero propeller blur/phase for parked prop aircraft. Verified at720p/140%,1080p/100%, and1080p/115%.

The interactions batch also traverses all15hangar rows through keyboard and D-pad activation, checks mouse selection and absent economic controls for all4classified entries, traverses all11FreeFlight aircraft and all16airports, and checks runway/airborne setup, Escape return, and unchanged career state. Every Free Flight selection also builds the actual preview frame with a conflicting classified career selection and asserts that the rendered model is the selected free aircraft, with normal showroom lighting. Core launch/pause/retry/career-file isolation has separate gameplay regressions.


## Military terminal and sortie review

The `militaryN` scenes select stable military mission ID N on a fresh career (board IDs 3,4,5,6,7,1,8,9,2); only ID 3 is initially available. Use `military4_locked`, `military4_unlocked` and `military4_completed` to compare the same XR-20 silhouette, unlocked aircraft and replay state. The unlocked/completed screenshot fixtures explicitly seed prerequisite success records; gameplay tests separately prove genuine objective/extraction unlocks. `militaryNscroll` exercises the right-hand briefing's scroll region. `military_loadingN` launches the real supplied mission and updates the loading readiness path between completed render frames; `military_loadingNlive` suppresses only the packaged photo in the fixture to exercise native exterior fallback. `military_pauseN` and `military_debriefN` show production military pause and aborted-sortie results.

For this review, full production startup and native aircraft rendering succeeded on Mesa llvmpipe using the normal rendering programs. See `docs/CLAUDE_MILITARY_CAREER_REVIEW.md` for measured coverage and committed native captures. UI-only mode remains layout evidence only.

## Research equipment and preview controls

Use `research40`, `research40_kinetic_emp_underside`, `research40_heavy_penetrator_underside`, `research40_testcard` and optional `_focus`/`_scroll` suffixes. The selected-kit fixtures alter only UI selection/inspection pose and use the native preview model. `_scroll` needs at least three frames. At 1280×720 with UI scale1.4, check the compact two-by-two airframe tiles and actual test-card targets, not just the equipment rail.

`hangar0_rotated`, `hangar0_zoomed`, `hangar12_rotated` (classified XR-40) and `freeflight0_rotated` use the native safe showroom turntable/lens. The shared controls should remain below the aircraft and leave purchase/departure state untouched.
