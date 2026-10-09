# Renderer review handoff

This review branch combines the cockpit/render work with upstream `5660ef005a1a396a671a5f522bae59004d0c6055` (v3.37 weather and atmosphere). It is intended for rendering and review before merge. The user waived further local graphics attempts after the software-renderer compiler exceeded available memory. No final visual or native 60 FPS certification is implied.

## Included renderer changes

- Finite mesh generation and normal recovery; zero-area triangle removal; finite/index/cache gates; checked temporary cache publication; bounded simplifier workers; Nightjar-only overhead refinement; stable rigid-part instancing and exact pose reuse.
- Model-derived fixture geometry with separate player/traffic emission ownership; bounded AI prop discs; fitted live/vectoring research exhaust; actual cockpit engine count/type/health; camera-feed allocation fixes and redundant-pass/upload gates.
- Upstream camera-relative TAA, plus FOV-change history invalidation. Existing direct-window and legacy feed modes remain.
- Shader lifetime cleanup: release failed shader objects and detach/delete successful linked shader objects.
- Two-program aircraft baking: the existing exact mode-3 normal output is reused by the distance/material/AO program. The field, sampling epsilons, material selection, AO taps and rigid-part-to-cabin transform are unchanged. The additional normal texture is bounded to 64 MiB; only its partial final row needs 8 KiB of CPU staging. No per-frame pass or global quality cost was added.

The principal files are `src/renderer.cpp`, `src/renderer.h`, `src/raster_renderer.cpp`, `src/aircraft_hull.cpp`, `src/aircraft_mesh.cpp`, `src/camera_feeds.cpp`, the mesh/exhaust/prop helpers, and their corresponding shaders/tests. Shared cockpit layout/material code is described in [the cockpit review](CLAUDE_COCKPIT_REVIEW.md).

## Latest-upstream compatibility

The reconciliation retains the newer gear enumeration, fold/swing poses, matrix correction, gear material frames, cockpit-protecting well cuts, platform compile timeout, historical rejection, shader-binary cache and NVIDIA fallback paths. The older local gear-at-0.06 omission was dropped because it conflicts with the newer gear movement. No gear or cockpit geometry was removed to fit compiler memory.

The split baker stores its inputs on the CPU instead of reading optimized-out uniforms. Both programs receive the same model, cabin layout/fit, state arrays, selected state, part and side. UI yields restore the selected program and textures. If either baker requires the existing `NV_SAFE_GEAR` compatibility fallback, both use that same field; a failed pair clears both handles coherently. General/normal and full/safe source variants have distinct cache identities.

The merge with `5660ef0` was reviewed specifically for the expanded weather data:

- `beginHullBake` copies the complete `FrameParams` by value, including cloud detail/boil, all 20 wake points and strengths, wake bounds/count, rain flow and mist. There is no old-size byte copy or borrowed array pointer.
- Both programs and yield restoration use the merged `setRT`, which forwards the new cloud/wake uniforms. The game bounds wake count to the same 20-element capacity used by the shader.
- Windsock gust data remains in entity rendering; rain-flow and mist uniforms remain in post-processing. These do not require a separate bake upload.
- The GL loader retains `glDetachShader` and adds upstream `glUniform1fv`. Shared cloud-noise generation uses upstream weather data.
- Weather changes remain in the whole-shader fingerprint. The aircraft geometry fingerprint does not gain weather state, since weather does not alter baked geometry or cabin AO.

No interaction bug was found in this source review. It is not a GPU test of the newly merged weather shaders.

## Validation and its scope

Before the weather merge, the actual production normal/general shader branches matched the unchanged reference numerically across 195 cases and 49,920 finite float components, with maximum absolute error zero. This covered all 13 models, whole-body and representative cockpit parts on both sides, three control/gear/XR states, cabin AO and min/max state aggregation. The comparison used the utility/glass field before the final Swift geometry additions; it is not a visual result for the latest merge.

A CPU mock-GL harness extracted the production dispatch methods and passed 11,657 assertions, 87 draws, 7 hostile yields and 8 source checks under AddressSanitizer/UndefinedBehaviorSanitizer. It covered common input forwarding, optimized-out selectors, 524,305-point batching, normal-row padding, invalid input rejection, feed resets and paired NVIDIA fallback/failure cleanup. This run predates the weather merge. LeakSanitizer was unavailable under the sandbox's ptrace setup.

Other focused checks passed: 9,637 exhaust assertions, 100,493 prop-disc assertions, optimized/sanitized mesh-integrity checks, 26 interior/exterior part-list configurations, shader keyword/declaration/matrix guards and affected C++ compilation. These establish their stated CPU/source contracts, not completed final meshes or final pixels. The current merged branch's aggregate build/test result is recorded separately by the publishing integration pass.

The renderer-related repository tests are `hull_mesh`, `mesh_validation`, `mesh_simplify`, `traffic_prop_discs`, `reheat_exhaust` and `shader_keywords`; `shader_glsl` is available when its validator is installed. Renderer tests are attached to existing explicit sanitizer build targets so CTest does not discover unbuilt executables.

The packaged checks were rerun after the weather merge: 12,425 dispatch assertions, 87 mock draws, 7 hostile yields, 8 source checks and 57 shader-lifetime assertions passed with ASan/UBSan. This includes actual forwarding of the new cloud/wake arrays.

The CPU dispatch and shader-lifetime checks are shipped as source in [tools/validation](../tools/validation/README.md). Run `python3 tools/validation/run_cpu.py` from the repository root; the scripts extract current production methods and require no GL context. Their synthetic outputs validate state/data flow, not GLSL arithmetic.

## Local graphics limitation

The pre-weather, final Swift-inclusive bake programs successfully cold-linked in isolated software Mesa contexts: general program 35.14 seconds / 7,232,724 KiB peak RSS; normal program 20.01 seconds / 5,840,112 KiB. Both reported zero GL errors and zero attached shader handles.

The integrated application subsequently passed the mesh-builder, display and lighting stages, then was killed while linking the full aircraft G-buffer program, before any frame. A warm retry failed at the same stage. Compiling only the exact full G-buffer shader in a fresh context also failed: exit -9, 17.54 seconds, sampled peak 7,566,204 KiB. Eliminating previously live programs was therefore insufficient.

A report-only flattened-normal-loop experiment also failed and was not applied. Production retains the tested normal implementation and split baker. No geometry suppression, speculative G-buffer architecture or lower-quality mode was introduced. No further local GL attempt followed the user's waiver, and no GPU compilation or image test was performed after the weather merge.

## Recipient's rendering checks

Before merging, verify shader initialization on the target driver, fresh/warm aircraft mesh caches and finite normals/indices, cockpit day/night and control extremes, gear/vectoring motion, TAA history during camera motion/zoom, individual versus instanced material/depth/shadow output, and direct-window/legacy/bomb-feed transitions. Check the new weather/wake/rain behavior alongside those views. Diagnostic occupant proxies, where used, must remain clearly identified as diagnostics rather than game avatars.

There are no successful final-base native cockpit/occupant frames or completed fresh all-fleet caches from these final local attempts. Native GPU 60 FPS, frame-time tails, vendor behavior and thermals remain unmeasured. Broader LOD, shadow reuse, material/AA retuning, scratch pooling and higher global quality budgets remain deferred pending visual or target-hardware evidence.
