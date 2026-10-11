# Claude review handoff for prebuilt aircraft meshes

## Scope and current status

Review branch: `codex/prebuilt-aircraft-review-20261011`

Base: Solace Express v3.46.0, `72d0af0b23defb8067dd697f87d94384b39a412b`

Handoff date: 11 October 2026

This implementation moves the existing full-quality aircraft bake into release preparation and adds a read-only portable-geometry loader. It targets first-launch mesh construction without changing aircraft geometry settings or rendering quality. There are 15 aircraft, with an exterior and cockpit body each: 30 assets including the research fleet.

The user authorized publishing this review branch on 11 October 2026. This handoff accompanies that publication for Claude review; no merge or release publication is claimed. This document separates completed local runtime/render verification from remaining hardware and release acceptance. The earlier startup recommendation remains a separate design document; where it proposed future work, the source and this handoff describe what is actually implemented.

## Full quality is preserved by the export path

The exporter calls the production `Renderer::bakePlaneMesh` path. Sampling, surface extraction, refinement, existing simplification tolerances, material/AO values, moving-state sweeps and rigid-part generation are unchanged. It captures the final arrays before GPU upload and writes their float32/uint32 bits unchanged. There is no additional decimation, quantization, normal repacking, dropped geometry or flattened animation.

The portable payload includes static vertices and indices, the fine-patch split, moving-hull triangles and eye flag, and rigid parts with their stable types and geometry. Runtime upload and pose handling continue through the existing renderer. Research aircraft retain their separate production builders and shaders.

The code-path guarantee is now backed by an independent exact comparison of all 30 exported payloads against original production bakes, including the complete fine-patch split. The extracted release package also passes all 44 recorded native camera/animation-state fixtures, with exact pixels and GPU data. This does not establish every runtime view, animation state or driver; the tested scope and remaining acceptance are recorded below.

Relevant code:

- [Production integration and export capture](../src/aircraft_mesh.cpp)
- [Full-quality bake-pair tracking](../src/aircraft_hull.cpp)
- [Portable format contract](../src/aircraft_mesh_asset.h) and [runtime codec](../src/aircraft_mesh_asset.cpp)

## Portable identities use source semantics

An early identity approach based on computed packed-model float bits was unsuitable: strict and fast floating-point builds produced different bits. The Windows game uses `/fp:fast`; equivalent producer/runtime source must not select different filenames merely because a compiler evaluates constants differently.

The implemented identity therefore uses the source contract and model/view semantics, not computed model floats or host-generated decimal model constants. `prebuiltAircraftIdentity` combines format/profile/algorithm versions, model index and stable ID, view, and `meshGeometryStamp`. The latter includes the generated CPU/producer source digest, algorithm manifest, and pruned full-quality vertex/distance/normal shader sources. No GL context or driver strings are needed to calculate portable filenames.

Only the canonical model is eligible: the runtime still checks its supplied packed parameters against that runtime's own `packModelOf` output. Custom parameters and diagnostic overrides use the existing fallback path. This local eligibility check is intentionally distinct from the cross-build portable identity.

The portable source contract is conservative. It covers all relevant CPU/producer files and full-family shader dependencies rather than promising narrowly selective per-aircraft invalidation. The legacy driver-local cache retains its existing per-aircraft/view source stamps. Broad portable invalidation is preferable to unsafe reuse while the contract is reviewed.

Cross-platform checkout bytes matter because source files are hashed. [.gitattributes](../.gitattributes) enforces LF for `src/**`, `tools/**` and `CMakeLists.txt`. Do not remove this or regenerate manifests from a CRLF-mutated checkout. The generated header and JSON manifest come from the build dependency; never edit them to bless an old exporter.

Review [the source input list](../tools/aircraft_geometry_inputs.cmake), [manifest generator](../tools/generate_aircraft_geometry_source.cmake), [shader identity](../src/renderer.cpp), [strict/fast identity regression](../tests/portable_mesh_identity_test.py), and [checkout regression](../tests/portable_mesh_checkout_test.py). Matching identities do not assert that independent GPU bakes on different drivers produce identical geometry bits. The release uses one canonical producer's fixed exported bytes.

## Canonical producer and package trust

The Linux EGL exporter performs fresh production bakes, bypassing both shipped assets and legacy writable caches. It uses canonical model parameters and the production moving-state sweep. Each body is baked in a separate serial worker process, releasing that worker's GL and process memory before the next body.

The producer also reproduces the actual first prewarm frame's propeller/fan state: `Pr[0] = (3.f + 1.f/60.f) * 250.f` (float32 bits `443c8aab`), running blur `1`, `max(authored blades, 2)`, no split-flap failure, and health `1` for existing engines / `0` for absent ones. Independent evaluation of `Game::prewarm -> update(1/60) -> menuTour -> fillPlaneVisual` established these values for all 30 views.

This state detail matters: the unchanged Atlas fan AO calculation has microscopic phase-dependent float variation even when geometry, normals, materials, indices and hulls are identical. A neutral-phase producer was therefore corrected to reproduce actual first-launch prewarming, rather than accepting an AO-byte discrepancy or changing the geometry algorithm. The corrected Atlas cockpit matches every one of the original startup bake's 51,172,628 payload bytes. The packaging test suite pins the copied startup source rules, and the producer source participates in the runtime identity so changing the contract invalidates older assets.

The exporter refuses `AF_ALL`, `CLIPDBG` and `NV_SAFE_GEAR` overrides. It also refuses a failed, shared or reduced-gear builder. `NV_SAFE_GEAR` can remove retractable-gear features, so a fallback result cannot silently become a full-quality release asset. Both distance and normal programs must satisfy the full-quality pair contract.

Every worker supplies a source/model/view/quality/GL receipt. The coordinator decodes and validates each file, then publishes the directory only after all 30 succeed and the complete `bake-report.json` exists. Individual worker outputs are not valid release packages. There is no legacy-cache import or conversion command: an old filename or plausible source stamp cannot prove full-quality provenance.

The Python bundler independently rehashes the current checkout, checks compiled/source digests and the exact roster, and validates binary contents and producer report. It rejects stale binaries, mixed sources, unexpected files, malformed paths and incomplete sets. SHA256 checksums provide identity and corruption checking within this trusted build pipeline; they are not a signature proving an arbitrary third-party producer is trustworthy.

ZIP DEFLATE is lossless transport compression. The game reads extracted `.mesh` files, not compressed streams at startup. Repackaging identical exported bytes with the same source revision and Python/zlib implementation is deterministic. This does not promise identical bytes from independent driver bakes.

Build, bake, validate and extract commands are maintained in [tools/mesh_assets/README.md](../tools/mesh_assets/README.md). Follow that file rather than copying player cache files into a release.

## Verified canonical production and byte parity

A complete fresh canonical fleet export and actual ZIP round trip have passed. The producer was Mesa llvmpipe (LLVM 19.1.7, 256 bits), OpenGL 4.5 Core, Mesa 25.0.7-2+deb13u1, with one bake worker and `LP_NUM_THREADS=1`. Its binary SHA256 is `55bc7c58f7fcf0872b5e73644b5c0305fddbf22d723937f25b190112d27290af`.

The final [source manifest](validation/prebuilt-aircraft/source-manifest.json) records 120 input files:

- Complete build/producer digest: `25b0ebe6af771a254ed9011d43a42b59cb21625ece22a53bcc84340c04be062c`
- CPU/producer geometry digest: `12e3710931e791adb24b9531501db3e7a5132e4866249f36e9a5837ee9d75743`
- The final Windows LLVM-MinGW build's generated manifest is exactly equal to the Linux exporter's manifest, including all file hashes and both digests. This is cross-build evidence, not a native MSVC or Windows GPU run.

Actual production/package results:

- 30 of 30 fresh full-quality bodies completed, without shared/reduced-gear shader fallback or GL errors.
- Export wall time: **730.17 seconds (12 minutes 10 seconds)**. This is offline producer time, not player startup time or an RTX performance measurement.
- Raw portable meshes: **293,791,880 bytes** (280.18 MiB).
- Geometry retained: 4,354,468 static vertices / 6,935,836 static triangles, plus 1,341,150 rigid-part vertices / 2,357,734 rigid-part triangles. Original moving-hull and eye metadata are retained too.
- Lossless standalone ZIP: **149,316,793 bytes** (142.40 MiB), containing exactly 30 meshes, the complete bake report and deterministic release manifest. No driver shader binaries or player caches are included.
- ZIP SHA256: `dea672b5b6861ab1ff404aa2c9f45b2c2e7cbf795525ece5dc5730b79780f65b`.
- The actual ZIP was extracted and fully validated into a separate release-style `aircraft/` directory. Runtime testing uses this extracted copy, not the raw bake directory.

The independent [30-body payload comparison](validation/prebuilt-aircraft/payload-parity.json) passes every raw payload byte and fine split: positions, normals, material/AO values, indices, moving hull/eye data, rigid-part order/types and all part buffers. The reference is independently baked original `72d0af0` production geometry; Atlas cockpit uses the original renderer's measured actual first-launch state rather than its diagnostic camera's unrelated propeller phase. The original reference baker was not modified to match candidate output, and no old cache was promoted into a portable asset.

See the [production/package summary](validation/prebuilt-aircraft/production.json) and [complete release manifest](validation/prebuilt-aircraft/release-manifest.json) for counts, hashes and provenance. Binary mesh files are not committed to Git: release CI regenerates them through the strict exporter. The local ZIP is a verified review artifact, not a published release. The completed runtime/render/recovery checks are detailed below. Reliable RAM/VRAM peak characterization remains unmeasured.

## Runtime loading and recovery

`bakePlaneMesh` tries a compatible bundled asset first, then the existing driver-local writable cache, then the original procedural bake. Windows resolves `aircraft/` beside the executable; the development default is `assets/aircraft/`. The installation directory is read-only from this path. A successful bundle hit still uses the normal GPU upload and rendering path.

The v1 decoder checks the 192-byte header, version/profile/algorithm, expected source/model/view identity, reserved fields, section sizes, whole-file checksum, finite data, normal/AO validity, index ranges, fine split, moving-hull eye flag and rigid-part validity/uniqueness. File reads are bounded at 512 MiB before allocation. Decode failure leaves the caller's output unchanged. Writers publish through temporary files and rename only completed output.

A missing, individually stale or corrupt bundled body falls back without rewriting/deleting the package or invalidating valid siblings. The existing procedural/renderer recovery remains available. This recovery preserves baseline behavior; it must not be advertised as proof that reduced driver fallback shaders always meet the full-quality acceptance target.

`prebuiltMeshHits` and `prebuiltMeshMisses`, package-load log messages and `bakeBuilt` distinguish hits from fresh construction. `checkMeshCache` is only a loading-pacer hint; filename existence there is not validation. Actual reads validate fully.

Driver-specific shader binaries remain driver-local. Bundled geometry does not remove first-run display-shader compilation, menu/scenery work, file I/O, validation or uploads. The code does not implement speculative lazy warming or field-evaluator pipelining in this change.

## CI and Windows packaging

[The workflow](../.github/workflows/build.yml) adds an `aircraft-meshes` job on Ubuntu 24.04. It builds the exporter serially, tests packaging tools, creates a fresh 30-body llvmpipe bake, bundles and validates it, then uploads `SolaceExpress-aircraft.zip`. No previous player cache or mesh artifact is used as a canonical input.

The Windows job depends on that job. It builds the Windows game and generates its own source manifest, downloads the bundle, and validates/extracts it against the **Windows build's** manifest into `dist/SolaceExpress/aircraft`. The final Windows package includes that directory beside the executable. A source mismatch fails packaging. Release publication still requires the sanitizer, mesh and Windows jobs. Nightly sanitizer runs do not request the bake job.

This is the implemented workflow configuration, not evidence that a remote CI run or release has completed. The local fresh bake, package sizes, lossless round trip and final cross-build manifest match are verified above. Remote CI resource use, native Windows packaging/execution and release publication remain acceptance items. Generated binary meshes stay out of Git.

## Checks reported passing at this handoff

The local integration run reports:

- Full Release compilation passed.
- Portable-format/adversarial coverage: 1,341 checks passed in optimized, fast-math, and AddressSanitizer/UBSan builds.
- Python packaging-tool suite: 17 passed, including the canonical startup-state source contract.
- Python workflow suite: 6 passed.
- Python checkout suite: 19 passed.
- Python source-contract suite: 880 passed.
- Full current CTest suite: 90/90 passed in 312.95 seconds after the final canonical-state producer correction.
- All 30 source-semantic identities match strict/fast-math compilation.
- Windows LLVM-MinGW cross-build passed; final generated source manifests match the corrected Linux exporter exactly (digests recorded above).
- Fresh canonical export: 30/30 bodies completed; independent payload and fine-split parity: 30/30 exact.
- Complete deterministic-format ZIP creation, archive revalidation and lossless extraction passed for the actual fleet.
- Extracted-package native sweep: 44/44 pixel-identical 1920×1080 images, 1,620 exact GPU/pose-buffer comparisons, all 176 frames GL0, 30 package hits and zero misses/body bakes.
- Original cold and packaged uploads each passed 1,080 exact checks. Original driver-local loads passed 684 GPU checks plus 282 part-centre checks.
- Five representative Kestrel exterior precedence/recovery cases passed with exact pixels/buffers and unchanged package bytes.
- Sanitized renderer mocks: 12,578 bake/dispatch assertions and 57 shader-lifetime assertions passed.

These results do not substitute for live Windows CI or native GPU visual/performance validation. Re-run affected checks after subsequent edits. Full-fleet sanitizer execution has not been claimed; sanitizer coverage here is focused on the changed format and renderer paths.

## Completed runtime/render verification

The independently built original reference is pristine `72d0af0`; previous review executables/caches were not relabeled as current evidence. All 30 original meshes and all 30 portable meshes were freshly baked. The candidate runtime tests load the actual ZIP's independently validated extraction.

The [portable result summary](validation/prebuilt-aircraft/report.md), [metrics and conditions](validation/prebuilt-aircraft/runtime-validation.json), and [evidence index](validation/prebuilt-aircraft/README.md) record:

- **Exact transport and uploads:** every one of 293,786,120 payload bytes and every fine split matches. Positions, normals, material IDs, AO, indices, ordered rigid parts, moving hull and eye metadata are preserved. Original cold uploads and candidate package uploads each pass 1,080 checks; original driver-local loads pass 684 checks plus 282 exact part centres.
- **Native drawing and poses:** 44/44 native 1920×1080 images are pixel-identical, with 1,620 exact GPU-buffer/part-matrix comparisons and matching frame-state metadata. The plan covers both views of all 15 aircraft, Atlas gear at 0/50/100%, fan phases/failure and cockpit controls, Wraith cloak/weapons/empty bay/hover, Specter vectoring and Larkspur steering. These are fixed articulated states, each drawn four times, not a continuous-motion video. All 176 frames in each sweep had GL0.
- **Fresh writable-cache package path:** 30 package hits, zero misses and zero procedural body bakes. Writable shader/Mesa caches started empty. All installed package hashes remained unchanged. This does not mean display/world shaders were already compiled or that the OS file cache was cold.
- **Precedence/recovery:** five representative Kestrel exterior cases passed: valid package over valid local cache; missing, stale or checksum-corrupt package falling back to the original local cache; and missing package/local mesh falling back to the original full bake. Each case preserved exact pixels and buffers, correct counters, GL0 and read-only package contents. Disposable cases copied only the selected body; this is not all-model fault injection or a mixed-sibling corruption test.
- **Source invariants:** extraction, normal/AO, simplification, part generation and GPU upload/pose/draw logic remain unchanged; material files and all 60 pruned builder programs match. The explicit original-startup Atlas cockpit reference preserves exact AO bytes, and its three images are also pixel-identical to the separately retained diagnostic-phase originals.

### Timing scope

[Five repeated new-process trials](validation/prebuilt-aircraft/load-benchmarks.json) measure only production body load/checksum/upload, without SDF or shader work, at `LP_NUM_THREADS=1`:

| Measured scope | Seconds |
|---|---:|
| Original cold procedural body work, all 30 | 746.113 |
| Existing driver-local load/upload, median of five new processes | 0.953 |
| Packaged identity/checksum/validation/load/upload, median of five | 1.847 |
| First new-process packaged load | 1.847 |
| Candidate fresh-cache native sweep body work, 30 separate processes | 9.811 |
| Candidate sweep cockpit display-shader work, separate | 4.083 |

The OS file cache was warm and was not flushed. Each load trial created a new EGL process and fresh GPU allocations. The packaged path took about 0.894 s longer across all 30 than the existing warm native cache, including identity/checksum/validation work; checksum cost was not isolated. The 44-fixture quality sweep used `LP_NUM_THREADS=3`. Its totals span 30 independent exterior/cockpit processes, not one game launch. Shader timers overlap body/init intervals and must not be added to derive startup time. Geometry precomputation does not remove display/world compilation. The [fallback evidence](validation/prebuilt-aircraft/runtime-paths.json) records separate body timings for the representative recovery cases.

These are llvmpipe observations, not RTX 3070 performance, a minimum-FPS result, cold-disk measurements or a full player-startup speedup. The [source-only reproduction harness](validation/prebuilt-aircraft/reproduce/README.md) regenerates large raw images/buffers outside Git.

## Remaining acceptance

The recorded same-driver quality/transport/runtime checks pass. The following remain unmeasured or outside this fixture scope:

1. **Windows/native hardware:** Windows LLVM-MinGW cross-build and exact generated-manifest parity pass, but native MSVC, Windows GPU execution and Charles's RTX 3070 still need testing. Record native 1080p frame times/hitches with unchanged settings against the minimum-60-fps target; neither software timings nor an average above 60 fps establish that target.
2. **Complete startup:** measure real application milestones, first display/scenery shader costs, storage conditions and cold/warm launches on the target hardware. The load-only median is not total startup time.
3. **Broader behavior/fault cases:** remaining cameras, continuous motion, damage/breakup and exhaustive control states; mixed-package sibling corruption, unwritable writable-cache behavior and failed runtime publication scenarios were not covered by the five native integration fixtures. Focused codec/tool failure tests are separate evidence.
4. **Distribution/resources:** raw mesh and standalone ZIP sizes are verified. Final Windows archive size, extraction time, reliable process/GPU RAM/VRAM peaks and overall installation footprint remain to be measured.
5. **Remote release pipeline:** the workflow is implemented and locally validated, but no successful remote CI release or publication is claimed. A source-changing integration must rebuild/rebake and pass the new manifests before release.

The branch is ready for code review with the above local acceptance evidence, not a claim of complete hardware/release acceptance.

## Regeneration after Hive integration

These artifacts are tied to the exact source digests above. Do not merge Hive or other upstream aircraft, model, cabin, mesh-builder, animation-state or source-contract changes and carry this ZIP forward by renaming files, relabeling a manifest or retaining old keys. Preserve both projects' unrelated code, regenerate the build manifests, rebuild the exporter and Windows game, and make a new complete 30-body canonical bake (or explicitly update the roster/export contract if aircraft are added). Re-run source/strict-fast/checkout tests, exact payload and relevant visual/animation checks, and validate packaging against the newly integrated Windows manifest. A fresh successful CI bundle is required before publishing that integrated release.

## Upstream integration note

This branch is based on released v3.46.0, commit `72d0af0b23defb8067dd697f87d94384b39a412b`. The newer bridge commit `5923859287284744df596be00bca53b7ab354064` was observed failing its Windows link on 2026-10-11: `buildBridgeMeshes` is referenced but `src/bridge_mesh.cpp` is absent from the separate `GAME_SOURCES` list. Preserve the bridge work when merging CMake changes, and include its production source in the Windows executable. This is an upstream integration warning, not a failure of this branch’s cross-build. See [the upstream Windows job](https://github.com/chad04220/Solace-Express/actions/runs/38098113632/job/114348173342). Recheck upstream status before merging.
