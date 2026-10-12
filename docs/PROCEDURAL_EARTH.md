# Procedural Earth destinations

This review branch reuses the existing terrain, road routing, settlement growth,
airport facilities, environment meshes, collision system and flight model. The
new places are invented landscapes at stable geographic addresses, not a model
of real countries, coastlines or airports. No global elevation download is needed.

## World contract

- Solace's authored geometry and its sixteen airport indices are preserved.
  The original world regenerated to the same 146,389,954 cache bytes as the
  unchanged baseline in the first implementation check.
- Generator version 1 divides a tangent cube into 6,144 address cells, reserves
  the area around Solace, and derives a repeatable recipe from the canonical
  address and a world seed. The menu never generates or stores the whole planet.
- Recipes include archipelagos, volcanic chains, fjords, atolls and continental
  fragments. Latitude affects climate. Each recipe creates four to six airfields,
  separate protected town sites and terrain-aware roads between local places.
- A chart has an 80 km terrain square. Generated land fades to ocean before its
  boundary. Airport aprons, taxiways, runways and approaches use the same physical
  terrain as collision and rendering. Nearby detailed scenery is streamed by
  the existing bounded chunk system.
- The runtime budget is one active full chart and one pending/retiring full chart.
  Background generation uses a two-thread parallel-work cap. Disk caching is
  bounded to the authored world cache and one replaceable generated-world cache.
  The generated cache stamp incorporates generator version, terrain-recipe
  revision, address, world seed and build identity. Terrain-content revisions
  do not renumber addresses or change Solace's cache stamp. Airport and town
  text is owned by an immutable recipe.

## Loading and isolation

Free Flight offers recommended regions and a latitude/longitude address picker.
Selecting a descriptor is cheap; loading it replaces the actual terrain world.
An initial loading screen remains interactive while generation runs. Initial
world replacement joins old scenery workers and career quote readers, clears
region-specific airport/lighthouse/collision caches and uploads the initial
terrain. In-flight prefetch is a separate asynchronous path: candidate terrain
and CPU navigation/road/bridge data are prepared off-thread, candidate GPU data
is staged in bounded batches, and active flight continues while it is pending.
Baked lots, vegetation, landmarks, furniture, ferry routes and spatial indices
belong to each World. Candidate generation does not overwrite active scenery.
Only a ready candidate can be published after a nonblocking scenery-reader drain.

The ordinary campaign keeps its authored Solace chart and existing save format.
Free Flight never serializes procedural airport indices into a career. Returning
to any career/research/menu path restores the authored world before reading those
indices. Existing and pending career saves are tested for byte and mtime stability.

## Travel model

Procedural charts are nonrepeating: flying beyond the old 50 km seam continues
through ocean instead of revealing a duplicate island. Surface-chart coordinates
map to double-precision geographic positions; y remains altitude because that is
what the existing flight model simulates. Neighboring chart changes must preserve
geographic position, altitude, speed, attitude, fuel and elapsed flight state.
They are not rigid ECEF rebases and do not establish native spherical/orbital physics.

The in-flight implementation starts heading/route-based neighbor generation from
takeoff, stages candidate GPU data with a 2 MiB frame cap and 256 KiB calls, and
publishes only a ready candidate after an asynchronous scenery-reader drain.
Obsolete work is retired off-thread rather than joined in a flight frame. The
handoff uses ownership hysteresis and a conservative ocean-clearance condition;
plane, camera, weather and flight state are transported rather than restarted.
The render path keeps far geometry for the active, candidate and previous region,
with a dithered near/far transition. A multi-second terrain-shadow bake found in
real EGL testing has been changed to incremental eight-row updates on streaming
publication. End-to-end smoothness remains a measured acceptance gate, not a
consequence of passing the upload-budget unit test.

If full detail arrives late, the drawable coarse proxy has a paired immutable
collision grid. Only the active Earth Free Flight World can consult these
foreign surfaces; candidate generation and authored career/research cannot.
Height and geometric normals use the same projected grid triangles and clamped
shoreline as rendering. A low/contacting aircraft pins the accepted version;
GPU completion alone cannot replace it. A newly arriving surface is withheld
from both rendering and collision if revealing it would put terrain under the
aircraft. Detailed terrain and airport facilities are promoted only after the
protected footprint is safely above both surfaces or bounded open ocean.

This fallback is deliberately coarse terrain without local buildings or usable
airport services. It prevents the former visible-land/ocean-collision mismatch,
but does not guarantee full scenery readiness on arbitrarily slow hardware.
The numerical contact tests cover adjacent and prolonged 260/1,200 km chart
separations. Shared-edge coverage has a bounded floating-point tolerance; this
does not change the authored terrain or its saved geometry.

The default visual curvature uses the same 6,371 km Earth radius as the catalog.
The existing ocean mesh reaches beyond the horizon. Physics remains local-chart
physics, and the debug flat-world radius override does not describe the normal
horizon-coverage contract.

At address level 5, neighboring centers are 226.52–312.73 km apart. With the
53.74 km conservative circular generated land extent, the minimum coast-to-coast ocean gap
is 119.04 km between generated groups. Solace is an authored exception: its
nearest generated center is 196.94 km away, with a conservative 86.63 km ocean
gap using Solace's entire terrain-square diagonal as its land bound. These are
useful generation lead times, but are not proof that land
is out of sight at high altitude. Distant rendering needs to bridge publication.
A separately authorized jet-fuel calibration is included on this review
branch. Route warnings use the flight model's nominal remaining-range API, with
no reserve claimed; shorter-range aircraft still need suitable route planning.

## Distant coverage and memory

At conventional light/research-analogue jet heights (about 14–16 km), the measured
conservative 360-degree neighborhood is typically17–19 groups with hypothetical
3 km summits, with a sampled maximum 24. At fictional XR mission/stress heights
25/28/30 km, that sampled maximum becomes 32/34/36. These are envelope candidates,
not promises that every group is visually resolvable through fog or in one camera
view. The generated land envelope is a square; using 38 km as its disk radius
would undercount corner cases. See the reproducible visibility report in
`docs/validation/procedural-regions/visibility-28km-summary.md`.

The lightweight horizon module independently samples analytic regional recipes
and the raw authored Solace base into 129-square silhouette meshes, without full
World heightmaps, roads, trees or settlements. Its immutable center catalog is
under 320 KiB. Selection is all-directional, includes 3 km of altitude lookahead
and a 3 km terrain-height allowance, and reports incomplete coverage when its
hard 64-group cap truncates a request. Approximate far silhouettes omit detailed
terrain smoothing and grading; the authoritative near mesh still comes from the
full World. A coarse mesh is at most 3.75 MiB of expanded GPU triangle payload,
with submerged triangles omitted in practice. Full nearby Worlds and driver/
texture resources are separate memory costs.

Exact collision counterparts retain 16-byte position/height grid vertices:
64 full 129-square grids consume at most 17,040,384 bytes (16.25 MiB), separate
from GPU triangles. At most two pending grids add 532,512 bytes, alongside the
bounded two-mesh worker result. Construction temporarily uses the ordinary
vertex grid. The older CPU-only proxy-memory measurements predate this retention
and must not be used as the final combined allocation total.

The paired renderer checkpoint completed actual generated and Solace crossings
on llvmpipe with 278/391 ms handoff frames against its roughly 300–420 ms normal
scene envelope. Those are diagnostic software-renderer timings, not hardware
FPS targets. A 30 km camera selected and populated 28 groups; the three-scene
process peaked at 1.80 GiB RSS. See `validation/procedural-regions/renderer-acceptance.md`.
Air traffic was explicitly disabled for those measurements: earlier mixed-fleet
shader compilation exceeded this host's process memory limit. Production traffic
is unchanged and needs hardware-GPU validation. Do not quote World allocations
or coarse vertex counts as total game RAM/VRAM.

## Checks and review tools

- `procedural_regions_test`: deterministic recipes, canonical IDs, seam/pole
  discovery, distinct terrain families, protected airport/town footprints and
  dry routes under strict, fast and sanitizer math.
- `region_scenery_isolation_test`: concurrent active reads/candidate scenery
  bakes, deterministic parity, copy detachment and move publication.
- `distant_regions_test`: independent horizon enumeration, seam/pole/date-line
  coverage, all five coarse landforms plus Solace, bounded allocations, invalid
  input handling, deterministic concurrency and strict/fast-math parity.
- `world_upload_budget_test`: staged GPU handle isolation, byte/allocation
  budgets, commit/cancel/failure recovery and bounded repeated resources.
- `region_scenery_test`: per-world settlement isolation, airport cache A/B/A
  replacement, drained worker jobs, and no authored landmark/code leakage.
- `procedural_world_test`: actual generated terrain/roads/lots/facilities,
  flatness, approaches, production collisions, world memory, nonrepeating ocean
  queries and real Plane crossings. `--recipe-only --region all` is a cheap check.
- `tools/validation/procedural_world_report.py`: isolated-process fresh/replay/
  repeat/cache-byte comparisons, wrong-descriptor rejection and optional full
  authored Solace preservation. It does not modify its baseline cache.
- `procedural_region_runtime_test`: real Free Flight region loading, flights,
  cancellation, repeated requests and restoration with existing/pending saves.
- `ui_review_harness region-<canonical-id> OUTPUT W H SCALE scene`: production
  EGL scene capture. `freeflight0earth` captures the actual destination picker.
- `tools/validation/procedural_render_report.py`: explicitly builds the optional
  `ui_review_harness` target, records source/executable hashes and UTC capture
  times, and refuses mixed-provenance output. The harness is excluded from the
  default build; rebuilding default tests alone does not refresh it. This
  diagnostic runner disables airborne traffic and labels that coverage limit.

Validation results and limitations belong with the final review evidence. A
passing analytic recipe test alone is not proof of a rendered or playable world.

## Integration boundaries

This is a local review branch, not a release. It touches Game setup/pause routing
and loading telemetry plus renderer world ownership. A concurrent UI/branding
branch must integrate these functional paths deliberately rather than replacing
whole Game or renderer files. No branding or visual-style redesign is included.
The current career save schema and authored airport IDs remain unchanged; runtime
procedural airport identity is the region ID plus local airport index.

## Landscape content revision 2

The address/seed version remains 1 (`pr1-...`). A separate recipe-content
revision invalidates generated terrain caches when landscape formulas change;
it does not move addresses, rename destinations, change the layout seed or enter
the authored Solace cache stamp. Before/after metadata digests match across all
6,143 generated addresses with the default seed and eight alternate seeds.

Coastlines use immutable 64-knot periodic profiles with broad asymmetric
headlands, medium bays and smaller coves. Coastal slopes vary to reuse the
existing beach and rock materials. Atolls have uneven lagoon boundaries, broad
inhabited islands, narrow reef sections, seeded passages and submerged breaks
between uninhabited motu. Their eastern airport-to-town arc stays continuously
dry. Fjords have unequal inland ends, curved trunks, differently widened mouths
and three connected tributaries. Narrow protected coast sectors and the
inhabited fjord band retain access between the existing airport and town sites.

Sampling remains allocation-free and thread-safe; shape profiles are generated
from a separate salted random stream after the original layout. The existing
runway/apron grading, town pads, approach-clearance calculation and exact 38 km
square ocean cutoff still apply last. Recipe memory remains below 16 KiB.
Analytic coast maps are diagnostics, not game screenshots or evidence of full
World road/collision behavior; production captures and generation checks remain
separate acceptance gates.

`procedural_landforms_test` checks measurable atoll asymmetry and rim-width
variety, lagoon passages, dry inhabited reef arcs, curved/branched
ocean-connected fjords, periodic-profile continuity, exact ocean edges, memory
and concurrent deterministic sampling. The fast-math target applies the same
shape checks. Existing `procedural_regions_test` continues to cover the airport,
town and dry-route invariants across its alternate-seed fixtures.
