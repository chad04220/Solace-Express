# Potential procedural-region visibility

Counts cover full 360-degree conservative land envelopes, before fog, field of
view, pixel-size or terrain occlusion. Camera and hypothetical summit heights
are above sea level. Earth radius is 6,371 km, with no atmospheric refraction.
The generated 38 km square half-width requires a 53.740115 km enclosing disk;
Solace has a separate conservative 71 km assumption in this measurement.

Sample: 108,549 viewpoints, including all centers, unique edges and vertices,
poles, date line, Solace, structured offsets and 65,536 area-uniform positions.
Typical percentiles use area-uniform viewpoints. Maxima include all structured
samples and are observations, not all-position proofs. The implementation uses
a 64-slot cap and explicitly reports truncation if a request exceeds it.

| Camera km | Summit km | Median | p95 | Sampled maximum |
| ---: | ---: | ---: | ---: | ---: |
| 13.716 | 0 | 8 | 10 | 12 |
| 13.716 | 1 | 13 | 15 | 17 |
| 13.716 | 3 | 17 | 20 | 24 |
| 16 | 0 | 10 | 12 | 14 |
| 16 | 1 | 14 | 17 | 20 |
| 16 | 3 | 19 | 22 | 24 |
| 25 | 0 | 14 | 17 | 20 |
| 25 | 1 | 20 | 23 | 26 |
| 25 | 3 | 25 | 29 | 32 |
| 28 | 0 | 16 | 18 | 21 |
| 28 | 1 | 22 | 25 | 28 |
| 28 | 3 | 27 | 31 | 34 |
| 30 | 0 | 17 | 20 | 24 |
| 30 | 1 | 23 | 27 | 30 |
| 30 | 3 | 28 | 32 | 36 |

The 13.716 km case represents a conventional light-jet ceiling analogue. The
25/28/30 km cases cover fictional high-performance research aircraft; current
XR-30 missions include 28 km targets and XR-40 includes 25 km. No real-world
service ceiling is claimed for those aircraft, and the game does not presently
author an enforced service ceiling for every type.

The horizon arc is R*acos(R/(R+h)); add the camera and summit horizon arcs, then
the land-envelope radius when testing center distance. At most 3.75 MiB of GPU
triangle payload per 129-square coarse mesh gives conservative totals of 90 MiB
for 24, 120 MiB for 32, 135 MiB for 36, and 240 MiB for 64. Actual dry-land triangle
payload is measured separately; these bounds exclude CPU transients, full
nearby Worlds, texture assets, and driver/process overhead. Keeping complete
Worlds for every distant group would be substantially more expensive.

Reproduce from the repository:
`python3 tools/validation/visible_region_counts.py --output /tmp/counts.json`
The script performs independent brute-force cross-checks and generates no
terrain, World arrays or packs. Full JSON evidence is retained locally under
`procedural-region-evidence/visibility/counts-with28km.json`.

Historical CPU-only proxy benchmarks, before retained collision grids and the
naturalistic recipe revision: 24 groups at 16 km took 275.1 ms to build,
with 17.69 MiB mesh arrays and 20.89 MiB peak process RSS. A 36-group view at
30 km took 364.7 ms, with 23.55 MiB mesh arrays and 26.82 MiB peak RSS.
The same 30 km view plus normal 3 km altitude lookahead selected 36 groups,
took 360.8 ms and 26.80 MiB peak RSS. These exclude rendering/driver costs.
Cold center-catalog plus selection cost was 5.3–5.8 ms. See horizon/*.json.
Do not use those historical allocation figures as final combined memory. The
paired renderer measurement and its retained-grid limits are documented in
`renderer-acceptance.md`; full World and driver costs remain separate.

Final revision-2 paired-mesh CPU checks, in fresh quiet processes:
- 24 groups: 311.5 ms, 24.11 MiB expanded vertex arrays + 6.09 MiB retained grids = 30.20 MiB combined arrays; 33.42 MiB process peak. No full World heightmaps were allocated.
- 36 groups: 426.1 ms, 23.72 MiB expanded vertex arrays + 9.14 MiB retained grids = 32.86 MiB combined arrays; 36.11 MiB process peak. No full World heightmaps were allocated.
