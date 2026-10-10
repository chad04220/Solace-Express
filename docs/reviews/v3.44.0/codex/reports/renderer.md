# Renderer and aircraft-mesh audit: Solace Express v3.43.0

## Scope and verdict

Pinned production revision: `91212bf4a131c02d2da41efbdb09248a09bc3599`. Read-only audit. The unreleased environment merge was not used for source, shaders, geometry or conclusions.

The two headline changes, mesh-backed XR-40 cloak and faster mesh extraction, hold up well in the tests below. No blocking defect introduced by either change was reproduced. Four actual cloak A/B views are visually very close, the new traversal matches the former traversal on 48 sanitizer fixtures, and the parallel simplifier is byte-identical to the prior simplifier on the tested inputs. Actual XR-40 exterior and cockpit builds both complete, validate and survive cache reload.

Two actionable renderer-related findings remain: a pre-existing cloak/TAA depth-class mismatch (source plus numerical reproduction, visual impact not measured), and the bundled CPU-renderer regression runner failing before its advertised tests execute. Mesh identity and thread/resource observations below should not be confused with demonstrated gameplay failures.

## Environment and evidence limits

- Actual GL execution: surfaceless EGL; llvmpipe LLVM 19.1.7, Mesa 25.0.7, OpenGL 4.5 core. Context request was OpenGL 3.3 core.
- Target display check: actual 1920×1080, render scale 1.0, quality 1. Most controlled A/B captures are 960×540 at render scale 1.0.
- One rendering process at a time, two llvmpipe threads, 6 GiB virtual-address ceiling. CPU saw nine hardware threads. These times are not RTX 3070 frame-rate evidence.
- `ENTSTREAM` unset: stock harness sets synchronous entity generation. Captures use three fixed update/render frames followed by three static frames. A separate same-scene capture uses 20 additional static frames.
- EGL adapter changes context setup only. A second, explicitly labelled fixture pre-bakes the selected production XR-40 mesh before its first view, as normal game prewarming does, and disables AI traffic to avoid a mixed-aircraft cold compiler path. It does not change aircraft geometry, material/effect shaders, shadows, TAA, weather or render quality.
- The otherwise stock EGL harness entered the all-aircraft Objects shader compiler and exited 139 under the 6 GiB address limit. No image came from that attempt. This is a constrained llvmpipe validation limitation, not a reproduced NVIDIA/Windows launch failure.
- Production hashes, clean Git status, and exact context/isolation diffs are in `repros/renderer/source_manifest.json` and `*_adapter.diff`.

## RENDER-1: cloak pixels use aircraft-motion reprojection with background depth

**Priority:** P2 candidate / medium. **Age:** pre-existing, retained by v3.43. **Confidence:** high in the depth/class mismatch; gameplay ghosting severity unverified. **Evidence:** source plus numerical reproduction, not a captured visual regression.

**Source:** `src/shaders/effects_fs.glsl:14-15,36-46`; `src/shaders/taa_fs.glsl:52-65`.

The cloak leaves the craft out of the opaque depth buffer, composites a refracted view of the world, and labels the result `taaFlag = 0.5`. The TAA pass interprets 0.5 as rigid-aircraft motion, but reconstructs the point using the unchanged world/terrain depth. It rotates/translates a point potentially kilometres behind the craft as though that point belonged to the craft.

**Repro:** run `python3 repros/renderer/cloak_taa_probe.py`. Fixture: native 1920×1080, 55° vertical FOV, fixed camera, aircraft pivot at (0,0,-20 m), cloak surface at 16 m, terrain at 2 km, relative prior yaw 5°. The actual class/depth combination reprojects the centre pixel to x=870.156; the cloak-surface motion would place it at x=982.581, and stationary background motion at x=960. The discrepancy is 112.425 px versus the surface and 89.844 px versus the background. A smaller 0.5° relative yaw gives an 11.225 px surface discrepancy, so this is not confined to the 5° stress case.

**Expected:** history motion should correspond to the composited layer, or history should be rejected/reduced for this moving refractive effect. **Actual:** neither the aircraft surface nor the background supplies the reprojected location.

**Player impact:** plausible shimmer, incorrect history sampling or trailing while banking a cloaked craft over detailed terrain. Neighbourhood clamping can conceal or limit it, so the numerical discrepancy is not itself a measured amount of visible smear.

**Suggested fix:** give cloak its own temporal class and policy. A dedicated cloak surface depth/motion buffer can anchor the skin; the refracted background needs world motion or low/rejected history. Merely overwriting opaque scene depth can break other post-effects and is not a complete transparent-layer solution. Add moving, terrain-backed and sky-backed cloak sequences to regression coverage.

Evidence: `logs/renderer/cloak_taa_probe.json`.

## RENDER-2: advertised CPU renderer regression suite no longer runs

**Priority:** P2 quality-gate defect, low direct player severity. **Confidence:** reproduced.

**Source:** `tools/validation/extract.py:35-51`; additional stale calls in `tools/validation/bake_dispatch_test.cpp:182-189`.

**Repro:** from the pinned checkout run `python3 tools/validation/run_cpu.py --output <review-output>`.

**Expected:** extract current production methods, compile and execute sanitized dispatch/cache/fallback/lifetime tests, as the tool's README promises.

**Actual:** extraction exits 1 on its assertion, before either advertised C++ test is built. Two source checks are stale:

1. The comparison fixture predates the `HAS_CABIN` specialization guards.
2. The mesh-stamp check searches for direct `kHullBakeMain` text, while production now fingerprints pruned assembled bake programs.

This is not evidence that cache invalidation is broken. Production includes the bake main through the assembled program. The mock also still calls the former `compileHull(vs, fs)` interface, whereas current production separates `compileHull(step)` and `linkBakePair`.

**Player impact:** indirect: renderer changes lack the intended reproducible CPU fallback/transport regression gate. A stock CTest pass does not establish that this separate suite ran.

**Suggested fix:** update the reference/contract checks deliberately, adapt the mocks to the new paired-builder and per-aircraft APIs, and register a CI invocation that fails when extraction or compilation fails. Do not simply remove the assertions or suppress the failure.

Evidence: `logs/renderer/stock_cpu.log`, `repros/renderer/stock-cpu/result.json`, `repros/renderer/stock-cpu/source-contract.json`.

## Verified latest-release behavior

### Mesh-backed cloak

Actual production new path versus `CLOAKMARCH=1`, with matched frame sequence and settings:

| Fixture | Whole-frame mean absolute RGB error, 0–255 | Pixels with any channel difference >5 / 518,400 |
|---|---:|---:|
| Partial, rear quarter, 16 m: `wr_3_210_12_16_3` | 0.03011 | 1,183 |
| Full cloak, rear quarter, 30 m: `wr_4_210_12_30_3` | 0.02713 | 1,042 |
| Full cloak, underside, 16 m: `wr_4_30_-12_16_3` | 0.02156 | 312 |
| Partial, side, 16 m: `wr_3_90_0_16_3` | 0.01267 | 499 |

Images and 8× differences were visually inspected. Differences are localized to the cloaked craft and thin edges. No broad background mutation, missing major component, gross silhouette loss or cloak-front discontinuity was seen in these fixtures. Whole-frame means dilute local differences, so the images and threshold counts accompany them. These checks do not prove exact visual equality at every camera, animation state or GPU.

- `previews/renderer/cloak_ab_contact_sheet.png`
- `previews/renderer/cloak_mesh_native1080.png` (actual native-1080 image, not an upscale)
- `logs/renderer/cloak_ab_metrics.json`

### CPU traversal and simplification

1. **Surface-net extraction, ASan/UBSan:** extracted actual old/new lambda implementations, 48 fixtures, seed 343042. Sphere, thin spherical shell, sharp box, thin rod plus disconnected sphere; sub=4/8; exterior/interior projection; complete cell bands, random missing cells and shuffled cell order. Vertex attributes match exactly; oriented triangles match exactly after canonicalizing triangle traversal order. No sanitizer errors. This isolates traversal and connectivity, using deterministic analytic fields, not the full GPU aircraft field.
2. **Simplifier, optimized and ASan/UBSan:** actual former/current implementations, painted boxes, edge-restricted box, spheres, 9,602–65,538 vertices. Cases cross the 32,768-vertex threading threshold. Old/new output vertex/index arrays, retained ranges and removal counts match exactly, including repeat runs. ThreadSanitizer was not run; ASan/UBSan are not race detectors. Inspection found scoring writes partitioned by vertex and mesh mutation occurring only after thread joins.
3. **Sparse rigid-part traversal:** actual new block-selection/growth code against a full-grid oracle for a mathematical CPU transcription of the XR-40 fan field. 189×189×113 lattice; 4,036,473 full samples; 935,170 sparse samples including the block survey; 119,848 crossing cubes; zero missed crossing cubes; zero disagreements in sampled field values. This is algorithm coverage, not GPU floating-point equivalence or all-part proof.

Evidence: `logs/renderer/nets_differential.log`, `simplifier_differential.log`, `simplifier_differential_asan.log`, `part_block_probe.log`; corresponding source generators and generated harnesses in `repros/renderer/`.

### Actual cold builds, completeness and cache reload

- Exterior: 152,348 band cells, zero moving/zero thin cells, 10,442,269 lattice samples; 1,841,800 raw body triangles → 111,512 final triangles / 55,474 vertices. All 22 distinct authored exterior rigid-part types completed. Moving hull empty, consistent with the XR-40's moving pieces being represented by rigid parts.
- Cockpit: 19,392 band cells, zero moving/4,777 thin cells, 6,954,433 lattice samples; 1,011,246 raw body triangles → 175,918 final triangles / 132,908 vertices. The three distinct cockpit part types built; pedals are instanced. Moving hull empty.
- New logs recorded exterior 55.2M field points and cockpit 15.8M. Actual run times are host/compiler/context dependent; they do not validate the release's RTX-laptop timing claims.
- Subsequent launches loaded and rendered the exterior/cockpit caches successfully. Finite/unit-normal/index/blob validation ran in the actual production build path.
- Source review confirms size/count checks, finite attributes and normals, index-range checks, temporary cache publication and fallback on rejected builds. A coordinated thread join precedes mesh validation/use. No production cache mutations were made by the audit other than the game's own writes inside dedicated review cache directories.

Evidence: `logs/renderer/render_isolated_mesh.log`, `render_isolated_march.log`, `render_native1080.log`; `previews/renderer/xr40_new_cockpit_960.png`.

### “Same bodies” is visual/shape intent, not literal mesh identity

An actual baseline linked the pre-`cbd3180` extraction and matching simplifier into otherwise identical release rendering. Fields, model, shaders and settings were held constant. The raw body triangle counts, retained band cells, fine-cell counts and lattice sample counts agree. Both exterior and cockpit rigid-part cache blobs are byte-identical between the old and new builds.

The final meshes differ because raw triangle traversal order changed before simplification:

| Body | Prior final vertices / triangles | v3.43 final vertices / triangles |
|---|---:|---:|
| XR-40 exterior | 54,780 / 110,124 | 55,474 / 111,512 |
| XR-40 cockpit | 136,129 / 180,338 | 132,908 / 175,918 |

Matched fallback-cloak rendered images differ by mean 0.01257/255 in the partial exterior view, 0.00250/255 side-on, and 0.11753/255 in the cockpit. Full-cloak/underside images using the common march are pixel-identical, as expected when the opaque mesh is hidden. These are small sampled visual changes, not proof of literal identity or a new visible defect.

Bidirectional stored-vertex checks were completed, with exact evaluation of every possible closer triangle for candidates over 1 mm. The cockpit has no >1 mm outlier in either direction (conservative maxima 0.997/0.998 mm). Exterior old vertices are within 1.843 mm of the new mesh; 14 new exterior vertices are >1 mm from the old mesh, with a maximum 19.655 mm at the thin left wingtip near body (-6.17969, -0.18774, 3.04729) m. This asymmetry can mean that the newer simplification preserves a tip the old one truncated; it does not establish a new degradation. No corresponding gross visual defect was seen in the paired views. “Same bodies” should not be treated as a strict pointwise 1 mm equivalence claim.

The completed evidence is `logs/renderer/mesh_distance_refined.json`, generated by `refine_cache_distance_fast.py`. Exact shared vertex positions are skipped as zero-distance cases; axis-aligned triangle-distance lower bounds safely eliminate triangles that cannot improve the candidate. The initial slower exhaustive implementation was intentionally interrupted after its exterior results were reproduced by this equivalent accelerated check. Candidate-centroid bounds in `mesh_cache_comparison.json` can overestimate distance and must not be reported as actual errors without refinement. Neither method establishes a continuous surface Hausdorff bound.

Evidence: `previews/renderer/mesh_geometry_ab_partial.png`, `mesh_geometry_ab_cockpit.png`, `logs/renderer/mesh_geometry_ab_metrics.json`, `render_prior_mesh.log`.

## Resource use and quality observations, not demonstrated new bugs

- Cloak target: `src/raster_renderer.cpp:652-664` allocates full render-resolution RGBA32F plus DEPTH_COMPONENT24. At native 1080 this is 31.64 MiB for color plus approximately 5.93–7.91 MiB for depth, before driver overhead, roughly 37.6–39.6 MiB combined. Lazy allocation and resize reallocation are present; there is no allocation/FBO-completeness fallback check. That missing check is a hardening opportunity, not a reproduced allocation failure.
- The exterior's 22 unique part meshes total 135,038 triangles, but its 99 authored instances plus static body amount to roughly 480,058 triangles per all-parts draw before clipping/visibility. Fans contribute 131,808 instanced triangles. Distance-based part/airframe LOD would be a useful future optimization to measure on the target GPU.
- `mesh_simplify.h:175-189` can use eight scoring threads per simplifier. `aircraft_mesh.cpp:658-659,869-872` can overlap static/fine plus two part simplifiers. A global bounded worker pool could avoid oversubscription and repeated thread creation on lower-core machines. No thread race or slowdown was reproduced in the tested fixtures.
- The real exterior captures show conspicuous rectangular blue-toned foliage/terrain regions. They persist after 20 additional static frames with synchronous entity generation and in native 1080. This is a quality observation shared with the world audit, not a diagnosed new streaming defect. Root-cause, intended biome styling, shadow/LOD contributions and target-driver reproduction remain open.
- The native cockpit is readable in the sampled forward view. This is not a full cockpit-envelope, every-aircraft, every-material or every-animation visual certification.

## Recommended next checks

1. Fix the broken renderer CPU regression runner before relying on it as a gate.
2. Add dedicated temporal cloak-layer coverage using moving aircraft over detailed terrain, with independent layer-depth/motion diagnostics.
3. Run cold and warm shader/cache/build diagnostics on the user's native-1080 RTX 3070, including mixed traffic, and record GPU timestamps, first-cloak activation and frame-time percentiles. Do not translate llvmpipe timing into target FPS.
4. Keep canonical traversal/parallel equivalence tests; add production all-aircraft mesh fixtures and explicit near-silhouette/overhead geometry tolerances. Retaining old cached bodies means cached and freshly rebuilt triangle layouts can differ even when the visual intent is preserved.

No source edits, pushes, repository changes or deployment actions were performed.
