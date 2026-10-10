# Renderer delta audit: v3.44.0

## Scope and verdict

Pinned release: `b22c2fca66fff88b62801a79f9f0c6edf06dcb9e`. Baseline: v3.43.0 `91212bf4a131c02d2da41efbdb09248a09bc3599`. This is a bounded source-delta review with fresh CPU/asset checks. **No v3.44 scene render, GPU timing, aircraft rebuild or visual certification was performed.** The original report and its v3.43 images remain historical evidence, not current screenshots.

The aircraft-specific geometry, hull, simplifier, raster-cloak and temporal-resolve files are unchanged. Two previous findings carry over: the cloak's depth/motion-class mismatch and the broken standalone CPU-renderer regression runner. New exact-input evidence explains why this environment-only update nevertheless invalidates every aircraft mesh cache. New texture arrays also cost substantially more than the release note's stated increase relative to v3.43.

The new grass is **ambientCG Grass004, a procedural source, not a photograph**; the release's own `docs/living-islands/PHOTOGRAPHIC_MATERIALS.md:8–14` explicitly acknowledges this. The other six environment layers use the recorded Poly Haven sources. Passing the asset tests verifies the delivered files and their recorded metadata, not independent photographic provenance.

## R344-1: unused community constants invalidate every aircraft body cache

**Priority:** P2 optimization / update-startup regression. **Confidence:** reproduced at exact pruned-source/cache-key level. **Player impact:** unnecessary first-launch aircraft builds; no changed aircraft shape demonstrated. The release notes warn about rebuilding, so this is an avoidable cost, not an undisclosed launch failure. The claimed RTX 3070 rebuild duration was not measured here.

**Source:** `src/shaders/terrain_material.glsl:47–48`; `src/shaders.h:33–41`; `src/shader_prune.h:3–10,406`; `src/renderer.cpp:266–279`; `src/aircraft_mesh.cpp:273–300`.

`worldLibAssembly` includes terrain material declarations in the aircraft builder source. The pruner removes unreachable functions but deliberately retains top-level constants. This newly introduced line survives every aircraft bake specialization:

```glsl
const int COMMUNITY_INFO=320,COMMUNITY_PLAN=352,COMMUNITY_META=383;
```

The body cache hashes the resulting source. The declaration is unused in every tested builder: all three names occur exactly once, in that declaration itself.

**Reproduction:** `repros/renderer/run_aircraft_input_comparison.sh`, documented in its README. The emitter uses each release's real shader assembly and pruner, real packed models, both exterior/cockpit defines, and normal/attribute programs. No GL driver is involved.

**Expected:** an environment-only declaration should not change the aircraft builder fingerprint when its reachable program and mesh algorithm are unchanged. **Actual:** all 52 builder programs (13 models × 2 body slots × 2 programs) differ by exactly this one unused declaration. Removing only that declaration makes each builder source byte-identical to v3.43. The algorithm manifest is unchanged. All 26 model/slot cache keys change when the exact hash algorithm is evaluated under the same fixed driver-identity fixture.

An additional 26 mesh/effects programs differ only by unused environment G-buffer constants and harmless formatting of the unchanged base material lookup. This supports semantic carryover for the sampled program families; it is not a replacement for executing the new full renderer.

**Fix:** keep community-only constants inside `communityGrid`, or outside the aircraft builder assembly. Prefer this narrow ownership fix over deleting arbitrary globals from the pruner without dependency analysis. Add a cache-stability test that introduces an unused environment declaration and asserts all aircraft body fingerprints stay unchanged.

Evidence: `logs/renderer/aircraft_input_comparison.json`, `.diff`, `.log`; `cache_key_delta.json`; `portable_comparison_run.log`. The full emitted sources are regenerable and need not be packaged.

## R344-2: release-note GPU-memory increase uses an intermediate baseline

**Priority:** P3 documentation/resource-planning defect. **Confidence:** exact allocation arguments plus reproduced loader control flow. **No physical VRAM or target-GPU performance measurement.**

**Source:** `RELEASE_NOTES.md:16–17`; `src/renderer.cpp:421–479`; `src/materials.h:9–10`; `docs/living-islands/PHOTOGRAPHIC_MATERIALS.md:56–63`.

The release note says the new textures use about 180 MB more GPU memory. Against v3.43, which has no optional environment arrays, the new logical uncompressed RGBA8 mip allocation is:

| New storage | Bytes | MiB |
|---|---:|---:|
| Two seven-layer 2048² arrays, complete mip chains | 313,174,680 | 298.666649 |
| Two seven-layer 512² fallback arrays, complete mip chains | 19,573,400 | 18.666649 |
| Total additional material storage | 332,748,080 | 317.333298 |

The original 30-layer 512² arrays remain resident. Including those, logical material storage is approximately 397.33 MiB, before driver overhead. These are texture-format/mipmap calculations, not an assertion about measured NVIDIA resident VRAM.

The documentation explains the 181.3 MiB figure: it compares seven layers to an earlier **three-layer development set**, rather than the released v3.43 baseline. Correct the release note to approximately 317 MiB (333 MB) more logical texture storage, or explicitly identify another measured baseline and method.

### Low-quality resource behavior, intentional but worth measuring

`genMaterials` loads both resolutions without checking `quality`; `bindEnvironmentMaterials` selects 512² at quality 0. This is documented intent, not a demonstrated sampling bug. Low therefore retains the same successful 2K allocations and startup decode work as Medium/High. If Low is intended to help memory-constrained systems, defer/release the 2K set, with an explicit quality-change loading policy.

The extracted production loader/binder test uses real CPU vectors, mocked image bytes and mocked GL transport/capabilities. Seven scenarios passed:

- Low/Medium/High: both sets loaded; Low binds 512², others 2048²; 317.333298 MiB additional logical storage each.
- Failure uploading the second 2K texture: both partial high handles deleted; complete 512² fallback selected.
- Missing high file and maximum texture size 512: complete 512² fallback selected.
- Insufficient combined texture units: optional sets not uploaded.

The test used about 252,484 KiB peak child RSS in this VM; this is mock-fixture CPU memory, not the real JPEG decoder's peak. Production high staging is 224 MiB plus decoded layer images and driver/transient allocations.

Evidence: `logs/renderer/material_loader_probe.log`; `repros/renderer/material_loader_actual.inc`, `material_loader_probe.cpp`, `make_material_loader_probe.py`, `material_loader_manifest.json`.

## Previous findings: current status

### RENDER-1, cloak temporal depth mismatch: carries over

`src/shaders/effects_fs.glsl` and `taa_fs.glsl` are byte-identical to v3.43. Cloak composites over opaque scene depth and assigns rigid-aircraft history class 0.5; TAA reprojects the background-depth point with aircraft motion. The original numerical reproduction remains applicable to these unchanged expressions. This supplement does not newly establish visible ghosting. Preserve the original classification: P2 candidate, high confidence in the source mismatch, unmeasured visual severity.

### RENDER-2, standalone renderer CPU regression runner: freshly reproduced

Running the exact v3.44 `tools/validation/run_cpu.py` exits 1 at `extract.py:51`, before either advertised sanitized C++ test builds. The same two checks fail: the stale reference bake-shader normalization, and the direct `kHullBakeMain` substring expectation in `meshCacheStamp`. The latter does not show broken cache invalidation: production hashes assembled builder programs. The old mock API mismatch also remains. This is separate from root's normal CTest run.

Evidence: `logs/renderer/stock_cpu.log`; `repros/renderer/stock-cpu/result.json` and `source-contract.json`. No assertion was removed and no failed test was relabelled as passing.

### Aircraft geometry checks and former visual observations

The unchanged aircraft mesh/hull/simplifier/model sources retain the relevance of the v3.43 algorithm differential and sanitizer findings. Actual builds and rendered image comparisons were not rerun. The old blue rectangular landscape patches cannot be called current: terrain, foliage, material and lighting paths changed extensively. Neither persistence nor correction of that old quality observation was visually established here.

## New environment shader/material review

- Environment texture arrays are separate from legacy aircraft/cockpit arrays. `ENV_MATERIALS` is enabled for terrain/water assembly; entity shader routing is explicit. CPU, GLSL and manifest layer orders agree. Typed unit 29/31 use is documented, and the unsupported-capability path avoids out-of-range bindings. No concrete sampler collision was established by this review.
- Entity relief now uses explicit derivatives captured before dissolve/cutout and transforms projection normals in the matching frame. Aircraft sampling helpers are guarded/preserved. Close-LOD dithering uses complementary ranges; bounds correctness is covered by the separate world addendum.
- Environment lighting uses environment-only glass/clearcoat flags and a bounded close shadow-receiver correction. Aircraft lighting classes retain their old branch. The changed shadow/glass appearance was not rendered here.
- Shallow water adds bounded refracted bottom sampling and Beer–Lambert attenuation; texture gradients are explicit inside the conditional. Deep water skips bottom work. This is source behavior, not proof of a speedup or a complete refraction-footprint/temporal-quality audit.
- Grass commit `1cc22ed8cc886f0b3903532b141501b08e0b3d06` replaces the intermediate leafy-soil asset with procedural Grass004, matches its mean linear color/roughness to legacy grass, restores saturation 0.68 and the original macro tint, and retains the new 2 m tile scale. The delivered 512/2048 grass mean linear RGB differs from the legacy layer by less than 0.001 per channel; mean roughness is 0.91555/0.91517 versus legacy 0.91703. This validates the palette/finish matching at asset level, not the final island appearance under lighting.
- The latest full asset test passes **495 checks**. It verifies all 42 delivered JPEGs, dimensions/hashes, recorded provenance, color/roughness encoding and normal tolerances. Original upstream ZIP/PNG download-cache contents were not supplied to this run, so their bytes were not independently reverified.
- The exact-release production-extracted terrain/environment arithmetic test passes **745,961 checks**, including material frames, filtered patterns, close-shadow math and bounded water helpers. This is a CPU mathematical check, not GLSL driver execution or rendered acceptance.

Evidence: `logs/renderer/scan_assets_full.log`, `terrain_material_test.log`, `grass_pixel_means.json`, `source_manifest.json`.

## Remaining acceptance work

Run the latest Windows build on the user's native-1080 RTX 3070: record actual memory use, startup/cold-cache work, frame-time percentiles and Low/Medium transitions; inspect grass at altitude and ground level, oblique water, close building shadows/glass, foliage LOD transitions and moving cloak. No llvmpipe timings or old screenshots should be substituted for that evidence.

No production edits, commits, pushes or heavy renderer processes were used for this addendum.
