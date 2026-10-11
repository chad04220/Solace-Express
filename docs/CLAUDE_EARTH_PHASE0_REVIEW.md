# Real Earth Phase 0 review

Branch: `codex/earth-data-phase0-20261011`
Base: `efbe9369483286bc557d83496bfcf95bfc6fa944` on Claude's working branch.
Status: validated local offline review prototype. No runtime world integration or publication.

## Scope

This phase provides offline source adapters, tangent-cube elevation tiles, road topology clipping, airport-table normalization, and a checksummed world-pack directory. It does not replace terrain, alter the current Solace archipelago, change aircraft meshes, relocate airports, change saves, or introduce globe physics. Those remain later phases.

The user approved developing Phase 0 in the existing workspace and routine coordination in issue #2. A review-branch push still needs approval.

## Source choices

See [source review](EARTH_DATA_SOURCES.md) for official URLs, pinned versions, hashes, license notices and the owner-selected GRIP CC BY 4.0 working assumption. Conflicting publisher notices remain documented. No GRIP geometry or raw global dataset is committed. The checked-in fixture is small, synthetic and clearly labeled.

Actual sample work uses NOAA ETOPO 2022 surface elevations at 60 and 30 arc-seconds, plus a pinned OurAirports TXKF airport/runway record. Source elevations use EGM2008 metres. They are not navigation data, and metre quantization does not imply metre source accuracy.

## Parallel imagery handoff

The owner has a separate ChatGPT agent investigating USGS EarthExplorer imagery. This branch adds no imagery implementation. USGS identifies Landsat data as [public domain](https://www.usgs.gov/faqs/are-landsat-data-cloud-still-considered-be-within-public-domain) and permits reuse/redistribution with [requested source credit](https://www.usgs.gov/faqs/are-there-any-restrictions-use-or-redistribution-landsat-data). Other datasets offered through the portal still require their own license review. Later alignment needs dataset/product IDs, acquisition dates, geographic bounds, horizontal CRS, pixel size, and the DEM's vertical datum; source checksums and processing history should accompany that handoff.

## Validation checkpoint

- Linux Release baseline builds successfully.
- Windows LLVM-MinGW cross-build links successfully. Native Windows runtime was not tested locally.
- All 91 pre-existing CTest entries completed without failures; `ci_test_selection` was initially skipped before the CMake file-API reply existed. After reconfiguration, it passed.
- The Earth suite passes all 55 tests, including real GDAL adapter regressions, 67,584 seam address pairs, corner/apron checks, topology, deterministic outputs, malformed packs and Windows checkout hashes. The final focused CTest run passes 4/4 (`earth_pack`, `ci_test_selection`, `shader_prune`, `aircraft_geometry_source`), with no skips in that run. An independent 12-check adapter/safety smoke also passes.
- No `src/` or shader changes are part of this phase.

## Sample measurement boundaries

The downloaded source window is about 600 km across. The test packs contain two adjacent level-6 tiles around Bermuda, not the entire source window. Both source resolutions use the same tile footprint. Each elevation tile has 256 by 256 cell-centred samples plus a one-cell apron. Heights are globally quantized to whole metres with a per-tile offset, then losslessly delta-compressed using the recorded codec version.

Repeated builds in this environment matched every output hash and adjacent aprons exactly. Cross-platform numerical and compression-version byte identity has not been independently established. Bilinear interpolation with finite Jacobian-weighted quadrature is a resampling approximation, not an exact area integral or a recovery of detail absent from the source.

Full-world compressed packs were not built. Published NOAA source-file sizes and any future projections must not be labeled measured world-pack sizes. The regional sample is predominantly ocean and cannot justify extrapolating its compression ratio to all landforms.

## Remaining decisions and limitations

- Solace's final latitude/longitude, regional scope and shipping-data budget remain owner decisions before Phase 1.
- Fine coastline/airport terrain requires higher-resolution regional sources; the coarse global base is not sufficient for low-altitude detail by itself.
- [Land-cover research](EARTH_LANDCOVER_REVIEW.md) records the owner-suggested Esri/Impact Observatory/Microsoft 10 m product, source CC BY 4.0 terms, service-license distinction and mirror/version caveats. No land-cover sampler or in-game scenery placement is implemented here.
- Pack SHA-256 checks establish integrity against the manifest, not publisher authentication. Verification expects an isolated directory with no concurrent writers.
- Runtime streaming, CPU/GPU height parity, road grading, real-airport placement and global career design are outside Phase 0.


## Measured representative packs

Both packs contain the same two adjacent L6 tiles (`ny/6/49_56`, `ny/6/50_56`), the one TXKF airport/runway record, and attribution notices. No roads are in these real samples. Their core bounds are approximately 66.09375°W to 63.28125°W, 30.83016°N to 32.77583°N, with numerical spherical area 39,899.6 km².

| Source | Whole pack bytes | Elevation tile bytes | Observed build seconds | Max integer-quantization error |
|---|---:|---:|---:|---:|
| ETOPO 60 arc-seconds | 111,804 | 105,681 | 4.66 | 0.4999988 m |
| ETOPO 30 arc-seconds | 122,443 | 116,312 | 4.91 | 0.4999996 m |

Timings are observations on a shared development host. The error bound is only relative to the unrounded resampling result, not actual terrain truth. A separate 512-pixel convergence probe comparing 2×2 with 8×8 quadrature found maximum differences of 1.86 m and 4.84 m respectively; this is not a global error bound. Both repeat builds matched all file hashes, and shared aprons matched exactly.

[Exact measurement records](validation/earth-phase0/sample-measurements.json) include tile and manifest hashes. [Quadrature diagnostic](validation/earth-phase0/quadrature-probe.json) separates sampling error from quantization.

## Evidence and reproduction

- [Tool README](../tools/earth/README.md): exact commands, format, dependencies, source hashes, fixtures and limitations.
- [Existing CTest checkpoint](validation/earth-phase0/baseline-ctest.log), [CI selector follow-up](validation/earth-phase0/ci-selector.log).
- [Linux build](validation/earth-phase0/linux-build.log), [Windows cross-build](validation/earth-phase0/windows-build.log).
- [Final 4/4 CTest run](validation/earth-phase0/final-phase0-ctest.log), [final 55-test Earth suite](validation/earth-phase0/final-earth-tests.log).
- [Independent audit results](validation/earth-phase0/independent-audit-final.json).

The small downloadable sample packs are development data artifacts, not files to install into the current game. The game has no Earth-pack loader in Phase 0. Keep raw source rasters outside Git. Optional zstd and GDAL dependencies are explicit; nothing is downloaded or installed implicitly by the tools.
