# Latest-release addendum: v3.44.0

## Provenance

- v3.44.0 published **2026-10-10 16:29:06 UTC**, during the original v3.43.0 audit.
- Exact latest source: **b22c2fca66fff88b62801a79f9f0c6edf06dcb9e**.
- [Release](https://github.com/chad04220/Solace-Express/releases/tag/v3.44.0)
- Released ZIP: **107,043,513 bytes**, SHA-256 **a6a559a817bc95b60617fb9bfbeca2ef79f41e8bf296e772a262033691c0643d**.
- Changes since the original pin: the Living Islands environment merge, new grass-layer blending, and release notes. The complete changed-file inventory is in `latest-v344/changed-files.txt`.

The original report/logs remain pinned to v3.43.0. This addendum identifies what was actually rerun on v3.44, what was established by source comparison, and what is fixed. Do not relabel original screenshots or sanitizer results as a full v3.44 rerun.

## What the new release fixes

**WR-4 airport-terminal culling is fixed.** The authored all-LOD bounds now ship, and all four previously failing airport/camera fixtures pass using the latest implementation. No duplicate fix is needed.

**WR-3 placement ownership improves substantially but remains open.** The same 2,205-chunk sweep changes from 130 mismatches among 1,225,372 entities to **13 among 1,214,634**. Remaining cases are near chunk boundaries, where float addition in `chunkOf` assigns the wrong damage owner. Destroyed objects still satisfy `owner_affected=0` / `coordinate_affected=1` in the latest repro. The broad garden/farm-offset placement issue is addressed; the numerical boundary defect remains.

## Still applicable

- **Flight:** all five reported mechanisms retain identical relevant guidance/ATC code. Fresh latest-object replays cover the key outcomes; see [latest flight report](latest-v344/reports/flight.md). Engine-failure disconnection itself is correct; the emergency defect is acceptance of an impossible powered plan after the player explicitly re-engages.
- **Career/research:** all five findings remain in identical relevant functions. The latest report supplies function hashes and line mappings. Original route-sweep counts and quotes remain measurements of the initial release, not a newly repeated world-wide v3.44 sweep. See [latest career report](latest-v344/reports/career.md).
- **UI/audio/platform:** the affected source files are byte-identical. The only related `game.cpp` addition is rendering the volcano. Radio click-through, focus/Abort issues, gain/duck/queued warnings, and conditional malformed-WAV handling remain applicable. See [latest UI report](latest-v344/reports/ui-platform.md).
- **World:** the crater's invisible physical floor, eighth-bomb collision restoration, residual boundary ownership and corrupt-cache acceptance are freshly reproduced. All 16 runway-elevation probes still pass. The incomplete packaged LICENSE reference remains. See [latest world report](latest-v344/reports/world-release.md).
- **Renderer:** aircraft mesh/cloak code retains the prior findings and comparison context, while shared/environment shader changes are reviewed separately. See [latest renderer report](latest-v344/reports/renderer.md). Earlier images are not claimed to show the new grass.

## New v3.44 startup finding

**R344-1: all 26 aircraft body cache keys change solely because an unused world-community declaration enters the builder source.** All52 builder programs differ by exactly that declaration; removing it restores byte identity. This forces avoidable first-launch rebuilding despite unchanged aircraft geometry. Keep the community declaration out of the aircraft builder assembly and add a cache-stability regression. Evidence is exact assembled-source/hash comparison; target-GPU rebuild time was not measured. See [the renderer addendum](latest-v344/reports/renderer.md).

## Quality and resource considerations

The replacement grass source is ambientCG Grass004, documented as procedural, rather than a photographic scan. Other material layers retain their individual documented provenance. Treat planned future roads, bridges, waterfronts and moving civilian traffic in the repository plan as unimplemented scope, not as features verified in this release.

The new environment arrays allocate approximately **317.3 MiB** of mipmapped RGBA8 texture storage before driver overhead. Documentation's approximately181 MiB increment is relative to an intermediate three-layer development set, not the v3.43 published baseline. Both full and small sets are preloaded, even when Low samples the smaller set; that is documented behavior, with lazy allocation a possible optimization rather than a newly demonstrated correctness defect. Measure native RTX 3070 VRAM/frame times before assigning a performance cost.

A full-resolution new-world quality/performance playtest remains necessary. Source equivalence of aircraft controls does not prove every new world camera/texture/traffic combination renders correctly. Follow [the hardware checklist](HARDWARE_CHECKLIST.md).

## Validation ledger

See `VALIDATION.json` for exact versions, local results, CI evidence, fixture counts and limits. The local v3.43 full sanitizer run passed41/41; deliberately corrupted WAV fixtures separately expose a decoder memory-safety failure. Latest-release GitHub CI passed Windows49/49 and Linux sanitizer50/50, with FLIGHT_QUICK=1 in the Linux job; those are attributed CI results, not an unqualified full local replay. The latest local full suite passed **54/54**, with FLIGHT_QUICK unset; its complete logs are included.

No production source edits, fixes, commits, pushes or release changes were performed by this audit.
