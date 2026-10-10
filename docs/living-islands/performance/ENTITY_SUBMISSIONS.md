# City and airport entity submission audit

Measured 2026-10-10 from isolated production-source snapshots, at 1920×1080 aspect, with identical frozen cameras and deterministic world placement. The original comparison is **994dbafbeb5f7cff844c94203565ff4b4150f594**, not an estimated old mesh inventory. The current snapshot is the tested v6 CPU source at 10:48 UTC, after merge `6952bd7`, arch-hangar and pump corrections, GA pane seating, and restored conifer crown envelopes. Source manifests, camera coordinates and full per-mode CSVs are beside this report.

The later clean merge of `18f617869a207f0085e3efc5b4c7d0e94defbc17` as `4f2ed656` does not change any compiled input to this probe, entity submission policy, or shadow matrix math. The archived source manifest intentionally retains the measured pre-merge `game.cpp` hash; its only subsequent change is the upstream research debug-scene initialization. That file supplied the documented sun convention and is not linked into the probe. Shader-only night-emission changes likewise do not change these geometry counts.

These are static scenery **submission counts**, not GPU timings, visible-pixel counts or an FPS promise. Conservative frustum acceptance still includes entities hidden behind other entities. Aircraft, dynamic ground vehicles, terrain, lights/effects outside entity meshes, pixel overdraw, material sampling and CPU gather time are excluded. All 52 scenery kinds are counted, including parked vehicles and airport structures. Shadow columns assume **both cascades are dirty and refreshed**; normal gameplay caches them and does not pay both totals every frame. Shadows never use hero geometry.

## Exact production totals

The baseline executes its own archived mesh, placement and range-policy code and reads its original WLD1 cache. Current code reads the WLD3 cache. Both caches were validated before use and remained byte-identical afterward. World/scenery/airport/entity/mesh sources at 994dbaf are unchanged from the original 52317bb baseline. Thus these comparisons intentionally include new community density, revised lower meshes and range policy, as well as hero detail. They do not attribute all added geometry to hero.

`q=1` is Medium and `q=2` is High. Hero counts include close cross-fade submissions only once; the companion base-LOD draw is included in view triangles. The current production mode is 3.

| Scene | Quality | Baseline view triangles | Current view triangles | Change | Frustum-accepted heroes | Hero triangles | Tower/sky hero triangles | Near-shadow triangles | Far-shadow triangles |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| capital_overview | Medium | 1,943,302 | 1,999,415 | +2.9% | 0 | 0 | 0 | 116,903 | 1,649,994 |
| capital_street | Medium | 3,021,016 | 3,631,234 | +20.2% | 49 | 290,990 | 245,532 | 90,860 | 1,608,305 |
| capital_tower_close | Medium | 3,035,936 | 3,581,646 | +18.0% | 51 | 280,089 | 252,892 | 87,842 | 1,601,845 |
| port_verde_overview | Medium | 1,288,040 | 1,329,288 | +3.2% | 0 | 0 | 0 | 99,779 | 908,112 |
| port_verde_street | Medium | 2,089,682 | 2,566,736 | +22.8% | 43 | 270,424 | 223,880 | 85,070 | 1,203,252 |
| port_verde_tower_close | Medium | 1,807,738 | 2,229,167 | +23.3% | 44 | 283,363 | 253,598 | 85,848 | 1,159,403 |
| airport_capital | Medium | 1,387,375 | 1,715,779 | +23.7% | 0 | 0 | 0 | 1,020 | 636,404 |
| airport_ramp_close | Medium | 1,617,397 | 2,217,959 | +37.1% | 9 | 71,024 | 0 | 594,875 | 1,177,910 |
| capital_overview | High | 3,427,639 | 3,769,758 | +10.0% | 0 | 0 | 0 | 181,323 | 2,409,710 |
| capital_street | High | 4,903,385 | 6,123,008 | +24.9% | 69 | 410,306 | 345,726 | 131,205 | 2,333,674 |
| capital_tower_close | High | 5,020,590 | 6,213,700 | +23.8% | 75 | 427,865 | 371,058 | 130,934 | 2,383,574 |
| port_verde_overview | High | 1,871,009 | 2,021,379 | +8.0% | 2 | 3,708 | 0 | 158,985 | 1,234,612 |
| port_verde_street | High | 3,314,850 | 4,381,311 | +32.2% | 60 | 386,117 | 328,460 | 121,924 | 1,542,340 |
| port_verde_tower_close | High | 3,059,882 | 4,051,271 | +32.4% | 57 | 392,883 | 356,338 | 125,802 | 1,504,910 |
| airport_capital | High | 2,214,059 | 2,882,541 | +30.2% | 0 | 0 | 0 | 2,312 | 967,356 |
| airport_ramp_close | High | 2,299,417 | 3,091,825 | +34.5% | 26 | 175,672 | 0 | 821,849 | 1,750,684 |

The Medium city street/close views are 18.0–23.3% above the original complete environment; High is 23.8–32.4%. The airport ramp is +37.1% Medium and +34.5% High. The corresponding all-dirty airport-ramp shadow refresh increases from 1,191,641 to 1,772,785 triangles on Medium and from 1,874,151 to 2,572,533 on High. That shadow increase comes from lower-tier geometry and placement, not hero.

A tower hero is 9,906 triangles; a skyscraper hero is 1,840. The highest sampled combined tower/skyscraper hero contribution is 371,058 triangles in the High Capital close view. This cost is explicit and is not a claim that the richer skyline is free.

## Isolating close detail and culling

The same current world and lower meshes are also measured with hero disabled while retaining the **same exact parent-chunk bounds** (mode 4). This isolates the combined hero/cross-fade/per-close-instance-culling policy from unrelated density and lower-mesh changes:

| Scene | Quality | Current without hero | Production current | Net close-policy change |
| --- | --- | ---: | ---: | ---: |
| capital_street | Medium | 3,350,630 | 3,631,234 | +8.37% |
| capital_tower_close | Medium | 3,320,089 | 3,581,646 | +7.88% |
| port_verde_street | Medium | 2,315,476 | 2,566,736 | +10.85% |
| port_verde_tower_close | Medium | 1,959,358 | 2,229,167 | +13.77% |
| airport_ramp_close | Medium | 2,152,197 | 2,217,959 | +3.06% |
| capital_street | High | 5,725,512 | 6,123,008 | +6.94% |
| capital_tower_close | High | 5,809,093 | 6,213,700 | +6.97% |
| port_verde_street | High | 4,019,570 | 4,381,311 | +9.00% |
| port_verde_tower_close | High | 3,676,530 | 4,051,271 | +10.19% |
| airport_ramp_close | High | 2,928,453 | 3,091,825 | +5.58% |

Close-frustum and exact parent-bound culling together save up to **688,558 submitted triangles** against the same new hero meshes with the former chunk-only acceptance (High Capital close camera). That camera drops from 176 to 75 hero instances. Medium Port Verde street drops from 123 to 43 heroes, saving 524,771 triangles. Those savings include cross-fade partner submissions.

CSV modes, applied to the current source only:

- `0`: current slots 0/1/2 and placement, no hero, original fixed-padding parent bound.
- `1`: current hero policy, original parent bound, no per-hero frustum rejection.
- `2`: mode 1 plus authored/animated close-instance bounds.
- `3`: production mode 2 plus cached exact all-LOD authored/animated parent bounds.
- `4`: no hero, exact parent bounds. This is the controlled no-hero comparison.

The baseline supports mode 0 only. CSV groups are all kinds, nature, structures (including fixtures/vehicles), and tower/skyscraper. Draw-call totals are the actual nonempty `(pass, kind, LOD)` static buckets. The four CSV groups overlap intentionally; do not sum them.

## Low-risk next investigations

No geometry or visible-quality reduction is justified by these counts alone. Preserve the detailed close models and measure target hardware first. Nature still dominates total view submissions in these cameras; optimizing only skyscrapers would miss most of the load.

A candidate with no intended visible-quality loss is conservative **per-instance frustum rejection for expensive non-hero tier-0 structures in boundary chunks**, while retaining bulk submission for fully inside chunks. The existing authored bounds can support it. This is a recommendation, not an implemented or measured saving: benchmark added CPU gather work and validate no false negatives before enabling it. A related diagnostic is measuring how much bulk-prefix thinning submits vertices that the vertex shader later rejects; only pursue changes if their CPU/GPU tradeoff is favorable.

Keep cascade reuse and the existing no-hero shadow policy. Do not cite the all-dirty shadow totals as steady-state cost or simplify shadow silhouettes solely to meet an unmeasured FPS target.

## Reproduce without the shared build tree

```sh
BASE=994dbafbeb5f7cff844c94203565ff4b4150f594
WORK=$(mktemp -d /tmp/solace-entity-submissions.XXXXXX)
mkdir -p "$WORK/base" "$WORK/current"
git archive "$BASE" src | tar -x -C "$WORK/base"
cp -a src "$WORK/current/"
for state in base current; do
  defs=; test "$state" != base || defs=-DLEGACY
  g++ -std=c++17 -O2 $defs -I"$WORK/$state/src" \
    tools/validation/entity_submission_probe.cpp \
    "$WORK/$state/src/entity_mesh.cpp" "$WORK/$state/src/entities.cpp" \
    "$WORK/$state/src/world.cpp" "$WORK/$state/src/scenery.cpp" \
    "$WORK/$state/src/airport_scenery.cpp" -pthread -o "$WORK/$state-probe"
done
# Both arguments must be verified existing caches from their respective source eras.
"$WORK/current-probe" /path/to/current/world.bin \
  docs/living-islands/performance/entity-submission-views.txt 0 > "$WORK/current.csv"
"$WORK/base-probe" /path/to/original/world.bin \
  docs/living-islands/performance/entity-submission-views.txt 0 > "$WORK/baseline.csv"
```

The final argument `0` reads the fixed cameras. Use `1` only with a new writable camera path to derive new overview/street/nearest-tower/airport-ramp cameras from current placement. Never regenerate cameras independently on each side of a comparison. A rejected cache aborts rather than being regenerated or replaced. Repeat source-manifest validation after future asset, placement, range or culling changes.

## Rejected adaptive-shadow experiment

The separate [settled-controller diagnostic](REJECTED_ADAPTIVE_SHADOW_SUBMISSIONS.md)
records the tested 96 m proposal. It was rejected after actual receiver-distance
images showed a quality regression and has no production effect. These fixed-policy
CSVs remain the accepted geometry ledger. The probe gained an optional, fail-closed
`settled` mode for reproducing the archived experiment; its default path was
rerun and produced this fixed-policy CSV byte-for-byte. The historical source
manifest above intentionally retains the exact earlier probe hash.
