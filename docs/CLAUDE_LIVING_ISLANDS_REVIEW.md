# Living islands: environment review proposal

Working branch: `codex/living-islands-review-20261010`.

Integrated upstream head: `18f617869a207f0085e3efc5b4c7d0e94defbc17`; the original preservation baseline remains `52317bb1f74a5ee9e66e063e2f17d5be786955a0`.

**Review proposal with known limits:** the four-tier rebuild covers all 52 environmental asset kinds. Multi-angle checks have closed the identified vehicle, building and nature geometry defects, and the night-window balance is revised. Close roof/balcony shadow edges still show aliasing. Narrower shadow-map experiments improved close detail but lost readable house/pump shadows farther away, so the original production coverage is retained. See [Close detail](living-islands/CLOSE_DETAIL.md) and [shadow evidence](living-islands/SHADOW_READBACK.md).

This is a review proposal, not a release or a measured RTX 3070 performance guarantee. The requested target is native 1920×1080 at a minimum of 60 FPS. Source/build validation is complete; native Windows/GPU acceptance remains required before merging.

## Scope

- Street-facing, terrain-aware community lots, connected settlement roads, dense city cores with skyscrapers, and less dense towns/villages. Eighteen inhabited communities, with service properties, farms, courtyard planting and parked vehicles. New scenery is static; this does not introduce pedestrian or town-life AI.
- Reworked building details, vehicle body/glazing details, connected tree branches and rock surfaces. Existing entity classes, collision metadata and LOD budgets remain the basis of the system.
- Airport service yards, structures, vehicles and gate/access clearances.
- CC0 photographic grass, asphalt, concrete, brick, plaster, roof tile and bark, physically scaled and colour-managed. Environment-only 2K arrays with a coherent natural-colour 512 fallback, offline runtime assets and recursive Windows packaging.
- Depth-dependent shallow-water transmission with terrain beneath it, derivative-correct texture breakup, existing reflections/waves/foam and a cheaper deep-water path.
- Mount Kaleo's physical summit crater, hot fissures, scanned basalt walls, bounded wind-driven smoke and ballistic embers, and one localized lava point light.

## Protected contracts

The immutable preservation baseline is `52317bb1f74a5ee9e66e063e2f17d5be786955a0`. Every runway retains its exact position, heading, elevation, length, width, surface classification, threshold and paint layout, contact ground and navigation fixtures. Photographic material inputs and environmental illumination can change runway appearance.

The only terrain-shape exception is the requested crater at `(25000, -9000)`: 558 heightmap texels inside 520 m; interpolation influence stays inside the independently audited 600 m disk. All other heightmap texels remain bit-identical to the preservation baseline. World caches advance to WLD3, and bounds/envelopes rebuild from the same terrain source used by collision.

Do not regenerate preservation fixtures from this proposal. See [QA](living-islands/QA.md) and [fixture provenance](../tests/fixtures/RUNWAY_BASELINE.md).

Aircraft/career/autopilot changes are outside this proposal. Preserve upstream aircraft and scenery-class shader specializations when integrating environmental material helpers; do not restore monolithic plane shaders.

## Integration and resource notes

- Copy `assets/materials` recursively when packaging. The two new subdirectories are necessary for the photographic set; missing optional maps fall back but visibly change the intended result.
- The seven 2K layers plus seven 512 layers use approximately 317.3 MiB of texture storage with mipmaps and 60.4 MiB of bundled files. This is about 181.3 MiB more GPU storage than the earlier three-layer checkpoint. They do not enlarge the original aircraft material array.
- Preserve environment sampler capability checks, complete-set fallback and upload-stage callbacks. No per-frame material uploads are introduced.
- Community accepted/rejected lots and support heights are prepared once after masks exist, including on world-cache load. Preserve that cache initialization order.
- The shader/mesh caches must invalidate normally after source changes. Test both warm startup and cold startup separately.
- The Linux CI named build targets must also build every registered binary test. This branch explicitly connects the upstream `breakup_test` to a named CI target to avoid a CTest "Not Run" result.
- Volcano smoke is bounded billboarding, not volumetric fluid simulation. No lava damage, ash-weather physics or eruption gameplay is added.
- Close foliage, fitted car lamps/glazing, building openings and airport construction have been inspected from multiple angles. This is a substantial realism pass, not a claim that every asset is photorealistic. Remaining limits include close shadow-edge artifacts, simplified interiors and the sea-stack cap's visible radial construction from above.
- High-rise night windows now use restrained, varied warm/cool emission and floor/zone occupancy. This is visual occupancy variation, not simulated inhabitants.
- Upstream Linux sanitizer CI sets `FLIGHT_QUICK=1`, which samples aircraft/routes/altitudes. Full flight mode requires removing the variable entirely; setting it to `0` still enables quick mode. Local full-mode flight was verified separately.

## Review guide

Start with the [final actual-render selection and provenance](living-islands/previews/final-review/README.md).

- [Communities and placement](living-islands/COMMUNITIES.md)
- [Asset models](living-islands/ASSETS.md)
- [Airport grounds](living-islands/AIRPORTS.md)
- [Photographic materials and provenance](living-islands/PHOTOGRAPHIC_MATERIALS.md)
- [Shader changes and cost](living-islands/MATERIAL_RENDERING.md)
- [Volcano](living-islands/VOLCANO.md)
- [Scale audit](living-islands/SCALE_AUDIT.md)
- [Runway/world QA and CPU streaming evidence](living-islands/QA.md)
- [Render harness](../tools/validation/ENVIRONMENT_REVIEW.md)

Render evidence uses the actual production environmental shaders and assets with a test-only software-renderer harness. The current integrated harness compiles the full specialized startup program set; older baseline captures explicitly omitted aircraft programs. Shader compilation is not gameplay validation. The staged aircraft-scale view is separately labeled; it is not an ordinary gameplay location.

## Native RTX 3070 acceptance

Use the existing methodology in [Environment benchmark](ENVIRONMENT_BENCHMARK.md), whose historical branch numbers refer to the earlier instrumentation work. For this proposal compare its exact merged upstream base with the final proposal commit on the same driver/settings/caches.

Test native 1920×1080, scale 1, dense Capital/Port Verde, low forest flybys, airport approaches/taxi grounds, coast and shallow seabed views, and Mount Kaleo by day/night. Include moving chunk transitions, first visits, camera reversals, cockpit/exterior changes, and adverse weather. Record GPU frame time, CPU submission/update, presentation cadence, p95/p99/max and all hitches. Fewer submitted triangles or successful software renders do not prove 60 FPS.

## Validation ledger

The held v6 production checkpoint passed all **54 Release CTests** (231.57 s) and **17 focused ASan/UBSan suites** (351.49 s) on 2026-10-10. Production sources remained unchanged during those runs; separately scoped review-harness and documentation edits are recorded in the validation evidence. LeakSanitizer is unavailable under this executor's ptrace constraints and was disabled; this is not a leak-free claim.

The full suite includes preserved runway/world contracts, community placement, all four mesh tiers, fitted facade scale, authored/animated culling bounds, close construction, material arithmetic and shader checks. Two earlier checkpoint failures were corrected before this clean rerun: fir/spruce upper shoots exceeded the strict 1 cm crown envelope, and a former shader-grid string assertion no longer described the fitted facade geometry. The crown tolerance was retained, and the facade test now verifies nominal grid alignment and actual physical floor spacing.

Shader validation covers 308 assembled/pruned stage variants and the two exact inline wreck-shadow stages, including their pair link. All 13 aircraft specializations remain isolated. Upstream aircraft, flight models, breakup and aircraft lighting helpers are byte-identical to the integrated upstream head.

Final Windows x64 Release cross-build passed with the accepted geometry/night materials, latest upstream and restored fixed shadow coverage: executable SHA-256 `78ad1140e077cbf06ab129b115818f383c1606f46b88fed619f2472a8f795f28` (7,539,712 bytes). Cross-building is not a native Windows or RTX 3070 playtest.

Local checkpoints `a13d079` and `131d2db` retain the asset work. Latest upstream `18f6178` merged cleanly as `4f2ed656`. Its explicit breakup-test CI dependency, stale-error-log cleanup, research screenshot timing and full/quick flight-test modes are preserved alongside prior aircraft breakup, dynamic impact craters, wreck clipping and 34-program startup.

The post-merge v7 checkpoint passed **54/54 Release tests** in 178.86 s, including full flight with `FLIGHT_QUICK` absent. [Exact logs and manifests](living-islands/performance/qa-post18-v7/ctest.log) are retained. The 17 v6 sanitizer suites carry forward only for byte-identical accepted CPU inputs; they are not reported as a new rerun. The final restored-policy rerun passed **54/54 Release tests in 173.85 s**, including full flight in 98.71 s. Every source-manifest entry stayed unchanged through builds/tests, and the final Windows cross-build passed. See [QA](living-islands/QA.md) for retained final logs and hashes. Historical 46-test/11-sanitizer and older Windows results are superseded by the checkpoints above.
