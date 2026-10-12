# Real Earth Phase 0 review

Branch: `codex/earth-data-phase0-20261011`
Original Phase 0 base: `efbe9369483286bc557d83496bfcf95bfc6fa944`.
Release integration base: published [`v3.48.0`](https://github.com/chad04220/Solace-Express/releases/tag/v3.48.0), exact commit `4f27e198ccfc9a6e683acec46c473043f82031ca`.
Status: combined Phase 0 and hardened global-elevation handoff, updated to v3.48.0 for Claude review. Native CI for this release-aligned revision must pass before integration. No runtime Earth integration.

## Published v3.48.0 integration (2026-10-11)

The release was published at **06:52:35 UTC** and its tag was verified before
integration. This branch merges that exact release into the previously published
Phase 0/global-handoff head `b24e9f7fd14551eac553b9693bfcd6b8a9b84f88`, preserving
both histories. The complete runtime source and release workflow remain identical
to v3.48.0. All Earth tools, fixtures and the 56-test Earth / 38-test handoff suites
remain identical to the previously reviewed head. The only merge conflict was
third-party notices; both the Earth-data and Yale star-catalog attribution sections
are retained. Both offline suites stay registered alongside the new upstream tests.

The release includes diagnostic-save fix
[`2e47fa2`](https://github.com/chad04220/Solace-Express/commit/2e47fa2d6de898e84594f3c6c60ece044c0cae9f).
All six Windows tool-created Games are set diskless before initialization. Its
source-contract regression is preserved; this is not a local native Windows
diagnostics playthrough. The release also supplies the new sky, city skylines,
Hive/military work, controller navigation, career-menu fixes, and XR-40 aircraft
bundle correction without additional runtime edits in this integration.

The prior combined head passed [all native CI](https://github.com/chad04220/Solace-Express/actions/runs/38118013576),
including 87/87 Windows CTest entries, three Linux sanitizer shards, the aircraft
bake and Windows packaging. The earlier evidence below describes its own historical
checkpoints; it is not substituted for verification of this release-aligned tree.
Current local checks and exact preservation hashes are recorded in
[the v3.48 integration evidence](validation/earth-v348-integration/verification-summary.json).
Local validation passes all 112 configured CTest entries, all three additional
GLSL checks, 56 Earth tests, 38 handoff tests and 21 mesh-packaging checks. Both
the Linux Release and Windows LLVM-MinGW game builds pass. The pinned 20-tile
real-source rebuild matches all 22 delivered files and all 1,280 convergence probes.
The new branch-head CI must be checked separately before declaring it ready.

## Claude's review (2026-10-12): merged

Merged into `claude/compassionate-davinci-4cfo20` after v3.49.0, unchanged but for one fix.

- **Checked:** the pack verifier (paths, symlinks, inventory, budgets, bounded decompression), the cube grid's seam
  and corner ownership, the codec, the great-circle road clipping, the airport tables and the tools' subprocess use
  (argument lists, local GeoTIFF only, no shell, nothing fetched).
- **Passed here:** the Earth suite (all but the GDAL adapter test with the optional `zstandard` installed: the
  default codec's path) and the 38 handoff tests. On the merged tree the full CTest (120) and the Windows build pass,
  and the branch head's own CI (run 925) passed.
- **Fixed:** an ENVI header missing a field (or with a non-integer one) failed with Python's bare `int()` message.
  It now names the field (`ENVI header lacks lines`, `invalid ENVI data type`). The new test fails on the old code.
- **For Phase 1:** sampling is pure Python, about 2.3 s an L6 tile at N=2. A full-globe L6 pack (24,576 tiles) would
  take most of a day on one core, so a production build wants NumPy or many processes. The N2-to-N8 differences
  (up to 38 m) are the footprint averaging steep ground, as noted; choose N from the source and target spacing.

## Scope

This phase provides offline source adapters, tangent-cube elevation tiles, road topology clipping, airport-table normalization, and a checksummed world-pack directory. It does not replace terrain, alter the current Solace archipelago, change aircraft meshes, relocate airports, change saves, or introduce globe physics. Those remain later phases.

The user approved developing Phase 0 in the existing workspace, routine coordination in issue #2, and publishing this review branch.

## Combined review revision (2026-10-11)

This branch now includes the complete prepared-global-source handoff from
[PR #4](https://github.com/chad04220/Solace-Express/pull/4), its verified split-download
assembly helper, and the reviewed fixes. It preserves the Phase 0 Windows test
correction at `745b5538582e0d698205096cf1b869c34af983d5`. Review this single branch
against the release integration base above; do not merge the older PR #4 snapshot separately.

Start with [global source handoff and reproduction](USGS_WORLD_MAP.md), then review:

1. `make-sample` binds the binary **and header** to unique preparation-manifest
   inventory records; missing, duplicate, changed or symlinked inputs fail before
   config creation. The shared builder validates the optional expected header pin
   before parsing geometry. Legacy unpinned configs remain compatible.
2. `convergence` binds both source files to the pack's recorded source hashes
   before sampling. The 0.5 m quantization check uses the pack's own quadrature N;
   N8/N16 convergence comparisons are reported separately.
3. Config creation is exclusive, including existing files, dangling/live symlinks
   and a destination created after preflight. Rejection must not alter any target.
4. The 37-test `usgs_world_map` suite is registered with CTest. Review its hostile
   inputs and split-archive cleanup tests alongside the existing 56 Earth tests.

The audited code revision is `11bee2a804fd9eaf2a8b4bfb6851aeae4f86a8cb`; the initial
combined revision added review documentation and checked-in evidence without
changing that audited code. The native Windows follow-up below makes one further
assembly guard correction. [Verification summary](validation/earth-global-handoff/verification-summary.json)
records 56/56 Earth tests, 37/37 helper tests, five focused CTest passes, and four
expected regression failures against the older implementation.
The [real-source rebuild](validation/earth-global-handoff/pinned-real-rebuild.json)
uses the hardened header-pinned path: all 22 files of the 20-tile delivered sample
match byte-for-byte, and all 1,280 source-bound convergence probes reproduce.
The maximum measured quantization residual is 0.4999786 m.

The complete prepared source contains 233,280,000 Float32 cells. Raw sources and
large artifacts remain outside Git. Original-to-prepared bit equivalence remains
the producer's recorded audit; this independent recheck did not redownload NOAA's
GeoTIFF or the GMTED archive. No full-global tile pack or runtime Earth loader was
built. The sample's N2 quadrature has an observed N2-to-N8 difference up to 38.36 m;
review quadrature quality before approving a production global build.

### Combined native Windows follow-up

The [first combined CI run](https://github.com/chad04220/Solace-Express/actions/runs/38116185230)
passed all three Linux sanitizer shards, the aircraft bake and 86 of 87 native
Windows CTest entries. The new helper suite caught a Windows-specific behavior:
exclusive file creation followed an existing dangling staging symlink. The
assembly helper now explicitly rejects existing or symlinked staging entries
before opening them. The original target-preservation assertions are unchanged;
an additional platform-independent regression requires rejection before any open.
The helper suite now contains 38 tests. The [follow-up verification record](validation/earth-global-handoff/windows-staging-followup.json) records local tests and regression sensitivity. The correction subsequently passed [native CI](https://github.com/chad04220/Solace-Express/actions/runs/38118013576); the release-aligned revision has its own CI gate above.

## Source choices

See [source review](EARTH_DATA_SOURCES.md) for official URLs, pinned versions, hashes, license notices and the owner-selected GRIP CC BY 4.0 working assumption. Conflicting publisher notices remain documented. No GRIP geometry or raw global dataset is committed. The checked-in fixture is small, synthetic and clearly labeled.

Actual sample work uses NOAA ETOPO 2022 surface elevations at 60 and 30 arc-seconds, plus a pinned OurAirports TXKF airport/runway record. Source elevations use EGM2008 metres. They are not navigation data, and metre quantization does not imply metre source accuracy.

## Parallel imagery handoff

The owner has a separate ChatGPT agent investigating USGS EarthExplorer imagery. This branch adds no imagery implementation. USGS identifies Landsat data as [public domain](https://www.usgs.gov/faqs/are-landsat-data-cloud-still-considered-be-within-public-domain) and permits reuse/redistribution with [requested source credit](https://www.usgs.gov/faqs/are-there-any-restrictions-use-or-redistribution-landsat-data). Other datasets offered through the portal still require their own license review. Later alignment needs dataset/product IDs, acquisition dates, geographic bounds, horizontal CRS, pixel size, and the DEM's vertical datum; source checksums and processing history should accompany that handoff.

## Original Phase 0 validation checkpoint

- Linux Release baseline builds successfully.
- Windows LLVM-MinGW cross-build links successfully. Native Windows runtime was not tested locally.
- All 91 pre-existing CTest entries completed without failures; `ci_test_selection` was initially skipped before the CMake file-API reply existed. After reconfiguration, it passed.
- The Earth suite passes all 55 tests, including real GDAL adapter regressions, 67,584 seam address pairs, corner/apron checks, topology, deterministic outputs, malformed packs and Windows checkout hashes. The final focused CTest run passes 4/4 (`earth_pack`, `ci_test_selection`, `shader_prune`, `aircraft_geometry_source`), with no skips in that run. An independent 12-check adapter/safety smoke also passes.
- No `src/` or shader changes were authored by Phase 0; the release merge now carries upstream runtime changes unchanged.

### Native Windows CI portability correction

The [first review-branch CI run](https://github.com/chad04220/Solace-Express/actions/runs/38111775265) passed all 91 Linux sanitizer CTest cases and the complete aircraft bake. The native Windows build passed, but one of 86 Windows CTest cases failed: `earth_pack`. Its malicious-payload test raised `KeyError` before reaching the validator because the test snapshot helper used Windows backslash paths while manifest keys use forward slashes.

The follow-up changes only the test helper to `relative_to(root).as_posix()` and adds a `PureWindowsPath` regression that also exercises Windows spelling on Linux. The regression rejects the old helper. All 56 Earth tests pass locally with the correction; the validator and all malformed-payload assertions are unchanged. The [corrected Phase 0 CI run](https://github.com/chad04220/Solace-Express/actions/runs/38113246257) subsequently passed all 86 native Windows CTest entries, all three Linux sanitizer shards, and aircraft packaging. The pre-release combined revision later passed its own [native CI](https://github.com/chad04220/Solace-Express/actions/runs/38118013576).

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
