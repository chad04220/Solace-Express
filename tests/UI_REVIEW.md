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

Regression mode also checks hangar/airline keyboard focus identity through automatic scrolling, loading readiness gated by distinct rendered frames, absent optional shadow-shader readiness, and all thirteen catalog aircraft dimension endpoints projected inside the production hangar viewport. Projection checks are CPU geometry evidence, not rendered-aircraft evidence.

## Explicit fleet-only startup diagnostic

On the review VM, full production initialization was killed (exit137) twice: first at the hull builder, then with serialized/no-optimization Mesa at the generic all-aircraft renderer. A third attempt omitting only research variants was also killed at the fleet objects shader. No actual-scene frame came from these attempts. UI-only captures do not cover that failure.

`ui_review_hangar_harness` is an explicitly separate diagnostic target. Its build-time generator copies three C++ translation units into the build directory and omits eight unused startup programs: generic and fleet marched aircraft objects/shadows, generic effects, generic aircraft mesh, and XR-30/XR-40 mesh variants. The selected production aircraft mesh is fully baked before the first frame. Runtime assertions abort if either the aircraft objects march or marched shadow proxy would be needed; neither can be silently omitted. The existing maps-only shadow variant is selected only when the production condition already selects it. It compiles the exact existing fleet (`AF_LIGHT`) mesh and effects variants, with no shader-text, geometry, material, gear, pose, lighting, shadow, terrain, or post-process substitutions. It validates the reduced completed-program total (production total minus8). Production files and the normal application build stay unchanged.

The executable permits only compatible fleet hangar scenes, freeflight0..8 setup scenes, research10/research20 (which use the same standard fleet specialization), and menuT5 (the fixed first tour shot). Every frame additionally requires a compatible special=0 model and aborts if either omitted march would be needed. XR-30/XR-40 are rejected. Images from it can validate actual compatible aircraft and scene passes, but cannot validate full production startup or XR-30/XR-40 rendering. The diagnostic limitation must accompany delivered captures.

```sh
cmake --build build --target ui_review_hangar_harness -j2
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=1 GALLIVM_PERF=nopt \
  MESA_SHADER_CACHE_MAX_SIZE=8G FRAMES=3 \
  build/ui_review_hangar_harness hangar0 /tmp/fleet-hangar1080 1920 1080 1 scene
```

Final camera checks additionally cover all three transformed tyre supports (within0.1mm of the showroom floor) and zero propeller blur/phase for parked prop aircraft. Verified at720p/140%,1080p/100%, and1080p/115%.

The interactions batch also traverses all13hangar rows through keyboard and D-pad activation, checks mouse selection and absent economic controls for all4classified entries, traverses all9FreeFlight aircraft and all16airports, and checks runway/airborne setup, Escape return, and unchanged career state. Every Free Flight selection also builds the actual preview frame with a conflicting classified career selection and asserts that the rendered model is the selected free aircraft, with normal showroom lighting. Core launch/pause/retry/career-file isolation has separate gameplay regressions.
