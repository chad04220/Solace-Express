# Nature submission audit

Final source-verified refresh: 2026-10-10 10:37 UTC, after the targeted pine distribution, fractured-column and strict conifer-height corrections. All source hashes matched before and after compilation, tests and audit.

Measured 2026-10-10 from the actual deterministic production world placement, at native 1920×1080 aspect, for the frozen foliage/forest/rock closeup cameras plus four ground-level forest headings. These are CPU submission counts, **not GPU timings or a 60 FPS claim**. The renderer must still be measured on the target RTX 3070 at 1080p, including alpha overdraw, terrain, lighting, and transient work.

## What is measured

`tools/validation/nature_submission_probe.cpp` loads a verified existing world cache read-only and calls the real production world/scenery placement code. It mirrors the production `entity_render.cpp` gather: chunk frustum, chunk distance min/max, bulk-prefix thinning, per-instance thinning, the current extended distance policy, complementary LOD submissions, and shadow-caster reach. All current triangle counts come from `buildEntityMeshes`, not guessed model complexity. Only the thirteen nature kinds are counted, so totals exclude buildings, vehicles, terrain and effects.

The baseline uses the explicit prior three-LOD nature inventory (the previously captured proposed-final meshes), the same world placement, and the same extended view-distance policy. It does not silently shorten foliage distance to pay for close geometry.

CSV modes:

- `0`: previous three-LOD meshes, no close-detail slot
- `1`: revised slots 0/1/2, no close-detail slot
- `2`: revised close-detail slot and its cross-fade, before per-close-instance frustum culling
- `3`: revised close-detail slot with authored/animated conservative per-instance bounds; rejects the close mesh and its base cross-fade partner together, retaining the former parent-chunk bound
- `4`: production result: mode 3 plus cached exact all-LOD authored/animated parent-chunk bounds

`q=1` is Medium; `q=2` is High. The separate `nature-submission-details.txt` records retain per-species slot 0/1/2/3 instance totals for the foliage closeup. Shadow totals assume both cascades are dirty and refreshed. Actual gameplay caches these cascades, so those totals are not paid every frame. The shadow policy remains unchanged and never submits slot 3.

The two `close_offscreen_*` columns are a diagnostic comparison using the earlier nominal-object estimate. The actual mode-3/4 decision uses the authored mesh bounds, including foliage deformation, wheel roll, windsock animation, and buried geometry.

## Measured production results (mode 4)

Medium foliage closeup:

- Previous: 5,670,069 nature view triangles
- Revised without per-instance culling: 6,893,779
- Revised with culling: 5,951,083, a 5.0% increase over the previous mesh inventory
- Close instances: 276 submitted before culling, 77 afterward
- Culling saves 942,696 submitted triangles, including the close-fade partners
- Near shadow triangles: 2,848,540 previous, 2,320,956 revised
- Far shadow triangles: unchanged at 2,164,048

Medium Cedar Ridge overview:

- Previous: 6,369,439 view triangles
- Revised without culling: 7,078,563
- Revised with culling: 6,402,503, a 0.5% increase
- Close instances: 151 before culling, 15 afterward

Four Medium ground-level forest headings retain 88–104 close instances, rather than 285–288 from chunk-only culling. Revised total view submissions are 4.6–9.3% above the old geometry for those sampled headings.

High foliage closeup: 9,085,870 unculled revised view triangles become 7,646,908 with culling, versus 7,302,958 previously (+4.7%). Close instances drop from 417 to 111.

The close-only density correction is deliberate: actual rendered images rejected the first pass's sparse conifer and terminal-only broadleaf foliage. The added sprays are limited to close range; culling removes the large number of those instances that previously lay outside the camera but inside a visible 256 m chunk.

The exact parent bound further removes 572 submitted nature triangles in the Medium foliage view and 2,646–31,010 across the four Medium ground views relative to mode 3. It can also retain genuine edge geometry that the former fixed 12 m padding missed; the small net savings do not measure that correctness improvement. Mode 3 is retained in the CSV to make this follow-up independently reproducible.

## Cold-forest stress sample and GPU acceptance camera

The supplementary `nature_cold_density_probe.cpp` scans a deterministic 512 m world grid for cold/high-altitude forest with planting-density factor above 0.8, checks 32 high-ranked sites separated by at least 1,024 m using real production placements, and selects the largest summed Medium close-mesh cost. It found 393 eligible grid sites. This is a sampled stress case, **not an exhaustive global worst case**.

Reproduce the selected dense-ground view on the target GPU:

- Camera: `(-8280.000000, 849.224731, -3160.000000)` metres
- Vertical field of view: 60 degrees; aspect: 1920/1080; sun time: 14.3 hours
- Four targets: camera plus `(50,3,0)`, `(0,3,50)`, `(-50,3,0)`, and `(0,3,-50)`
- Medium and High presets; retain the existing view-distance and thinning policies
- Within the nominal Medium close radii: 131 firs, 128 spruces, 52 pines and 2 bushes, totalling 1,676,030 omnidirectional close-mesh triangles before the camera cull

The saved `nature-cold-submissions.csv` records all four headings, both quality presets, and modes 0/2/4. Medium production submissions are 6.33–7.19 million nature view triangles (+9.4–11.4% versus the prior meshes), retaining 96–105 close instances. High production submissions are 8.79–10.38 million, retaining 127–149 close instances. Culling saves 0.75–1.25 million Medium view triangles relative to unculled close detail in those headings. Selection metadata is preserved in `nature-cold-selection.txt`.

**Alpha-overdraw caveat:** the denser overlapping sprays can raise fragment shading, alpha-mask evaluation and shadow alpha work even when the submitted-triangle increase is modest. Transparent portions of cards still consume raster/fragment work before discard; overdraw also varies by heading, resolution and canopy overlap. These CPU probes neither measure those costs nor establish RTX 3070 frame rate. GPU acceptance needs actual frame-time/overdraw measurements at the above dense-ground cameras, including shadow-refresh frames and normal cached-shadow frames, with the full production terrain, lighting, buildings and effects enabled.

## Bound safety

`tests/environment_close_bounds_test.cpp` compares the bounds to CPU references of the production vertex shader's actual deformations for every entity kind, eight instance seeds, four yaws, six nonuniform/signed scales, and three animation times, for both close-pair and all-LOD envelopes. The 2026-10-10 run passed 68,475,802 checks, including 34,025,712 visible transformed samples, with no containment or frustum false negatives.

`tests/environment_chunk_bounds_test.cpp` passes 29,320 checks, including 2,010 adversarial nominal-reach-zero instances whose transformed geometry actually protrudes beyond the original chunk edge. It exercises same-address and same-size mutation, source replacement, real generation and detail upgrades, trimming, clear/regeneration, asynchronous replacement, and stale-epoch rejection. The globally monotonic chunk revision invalidates the cached union when content changes; moving the camera or advancing wind does not trigger a full rebuild.

`tests/environment_nature_detail_test.cpp` separately checks geometry validity, explicit per-species budgets, small spray size, adjacent-LOD crown envelopes from eight azimuths, actual palm-leaflet tags, and a surface-intersection support graph connecting every woody component to ground. These are structural contracts; rendered appearance is evaluated separately.

## Reproduce

From the repository root, build an isolated audit executable without touching the renderer's build graph:

    g++ -std=c++17 -O2 -I src tools/validation/nature_submission_probe.cpp src/entity_mesh.cpp src/world.cpp src/scenery.cpp src/entities.cpp src/airport_scenery.cpp -pthread -o /tmp/nature_submission_probe
    /tmp/nature_submission_probe /path/to/existing/world.bin > nature-submissions.csv 2> nature-submission-details.txt

The measured cache is 131,785,928 bytes, SHA-256 `6604ea5f68f855ad6be3582af82d47ba25510ffe9bd7a684ea04308d483c76fe`. The supplementary cold probe is built with the same command, substituting `tools/validation/nature_cold_density_probe.cpp`; redirect stdout to `nature-cold-submissions.csv` and stderr to `nature-cold-selection.txt`.

The cache header and full payload are validated before initialization. A rejected cache aborts rather than regenerating or replacing it. The exact source snapshot used for this ledger is listed in `nature-submission-sources.sha256`.

