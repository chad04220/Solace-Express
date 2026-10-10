# Immutable runway baseline

Source commit: `52317bb1f74a5ee9e66e063e2f17d5be786955a0`.

`runways_52317bb.golden` was captured from an independent `git archive` of that
commit, before accepting any community/world redesign changes. The capture tool
is `generate_runway_golden.cpp`; it is deliberately not a build target or a test
update mode. Never regenerate the fixture against the revised checkout to make a
failing preservation test pass.

Fixture SHA-256 (LF endings):
`a0b1c619f3ed21945e099390d23500f10dee9ea2336e201c02771eaac06d3656`.

The test also hard-codes its FNV-1a fingerprint, normalizing CRLF for Windows.
Its fixture identity, sample count and sixteen-airport roster are checked.

## Captured contract

- Exact airport codes, names, descriptions, center coordinates, elevation,
  heading, length, width, surface, size, hospital flags and runway designators.
- Both three-dimensional thresholds/endpoints and each runway's direction.
- 193,625 actual `g_world.height` samples across all sixteen runways. Sampling is
  at most 5 m along the runway and 2 m across it, extended 60 m past each threshold
  and 25 m beyond each side. Explicit thresholds, centerlines, edges and points
  25 cm inside the edges are included.
- Capture verified that 7-, 8- and 11-octave terrain heights agree at every sample.
  Every recorded height is exactly its runway elevation and every normal is
  exactly vertical. The repeated values are stored losslessly as IEEE-754 bits
  with run-length encoding; sample axes are captured, not reconstructed from the
  revised airport data.
- 1,506 runway-edge, threshold, approach-light and PAPI instances, including
  positions, terrain-relative elevation, yaw, scale and light/glide-slope seed.
  Taxiway blue lights and unrelated airport furniture are intentionally not frozen.

## Current-code assertions

The test builds the new world from scratch; it cannot pass by loading an old cache.
It checks exact descriptors and compares terrain/contact heights and normals at
frozen coordinates. Longitudinal and transverse gradients, aircraft contact
classification at margins 0/2/4/30 m, and scenery/physics height agreement are tested.

It generates level-2 scenery in an envelope extending 600 m from runway ends and
sides and performs oriented-footprint overlap tests against the protected runway
and graded strip. This includes community buildings, farmsteads, garden trees,
rocks and airport furniture, including objects centered in neighboring chunks.
Existing lights/PAPI are frangible fixtures; fences and windsocks still must stay
outside the runway proper. Production collision and obstacle-height queries are
also sampled along three runway lanes. Runway/approach/PAPI records must match the
captured records.

Descriptors are exact. Float-derived positions allow 6 mm (world-coordinate ULPs),
height comparisons allow 0.5 mm, and gradients allow 0.00015. Actual verified
current-versus-baseline runway height and gradient differences are zero.
These tolerances allow compiler arithmetic differences, not design alterations.

## Reproduce the original capture for auditing only

Run in the repository root, keeping the archived sources outside the worktree:

```sh
mkdir -p /tmp/solace-runway-audit
git archive 52317bb1f74a5ee9e66e063e2f17d5be786955a0 src | tar -x -C /tmp/solace-runway-audit
g++ -std=c++17 -O2 -pthread -I/tmp/solace-runway-audit/src \
  tests/fixtures/generate_runway_golden.cpp \
  /tmp/solace-runway-audit/src/world.cpp \
  /tmp/solace-runway-audit/src/scenery.cpp \
  /tmp/solace-runway-audit/src/entities.cpp \
  /tmp/solace-runway-audit/src/airport_scenery.cpp \
  -o /tmp/solace-runway-audit/capture
/tmp/solace-runway-audit/capture /tmp/solace-runway-audit/runways.golden
cmp tests/fixtures/runways_52317bb.golden /tmp/solace-runway-audit/runways.golden
```

## Verification boundaries

This is a CPU contract, not a screenshot or GPU throughput result. It does not
claim a complete flight-model retest or judge environment appearance. The existing
`airport_layouts`, `entity_raycast`, `world_cache`, terrain-material/shader checks
and actual renderer previews remain complementary acceptance tests.

## Runway shader preservation

`runway_material_52317bb.glsl` freezes the original `airportAt`, `seg7`,
`rwyDigits`, and `runwayMaterial` functions directly from the same git commit.
The test requires them to remain identical after removing comments and whitespace.
The sole normalized difference is forwarding the pixel-footprint argument to
`aptGround`, which is called only outside the runway surface and supports the
separately authorized apron/taxiway redesign. Directly changing these frozen
functions' paint arithmetic, number shapes, dimensions or shoulder/blast-pad
logic fails. The frozen shader fixture has its own hard-coded fingerprint.

The user's later photographic-material request intentionally permits realistic
grass, asphalt and concrete inputs and ambient-light refinements, including their
appearance on runways. The source contract does not freeze shared texture sampling
or lighting and does not imply identical rendered runway appearance. Physical
position, elevation, dimensions, heading, surface classification, paint layout
and navigation fixtures remain the strict preservation boundary.

## Full-world terrain preservation

`world_height_52317bb.golden` adds 17,523 fixed locations at 7, 8 and 11 octaves:
a 625 m lattice over the entire 80 km archipelago, plus 49 extra probes around
each original named settlement. The current world is compared at those frozen
coordinates, independent of relocated community definitions. Unlike flat runway
samples, mountainous procedural terrain permits a small compiler/libm allowance
(10 mm plus 3 ppm of height). Matched GCC builds have zero sampled difference.

The independent full-array audit also found **bit-identical** current/baseline
heightmap RGBA (16,777,216 floats), all height-bound mip levels, all terrain-envelope
vertices (4,198,401 floats), and all seven envelope mip levels. Scenery masks and
road IDs are deliberately excluded from this invariant because those changed.

Full-world fixture SHA-256:
`2be01e9c00f8a585dbee8e99f3d9dbb29422a239acd04c7d83701f1d97767990`.
The test locks a separate FNV-1a fingerprint as well. Its audit-only generator is
`generate_world_height_golden.cpp`, compiled with the same archived baseline
world.cpp/scenery.cpp as the runway capture, never the revised source.

### Explicit volcanic-crater exception

The requested landscape redesign subsequently authorized an actual summit crater.
Only full-world samples strictly inside the 600 m disk centered on `(25000, -9000)`
may differ from baseline; all outputs must still be finite. This never exempts
runway terrain, fixtures, airport grounds, or approach corridors. The immutable
fixture is unchanged, and the test reports the number of exempt samples separately.
The bit-identical whole-array result above describes the redesigned communities
before adding that explicit crater, not an assertion that the crater changes no
terrain. Future audits must compare height channels outside this bounded disk.

### Explicit road-grading exception

The owner's brief (`docs/LIVING_ISLANDS_PLAN.md`, Phase A item 4) builds the road
network into the ground: each road's platform is levelled across its width, with
cut-and-fill banks blending back to the natural ground. A frozen full-world location
may differ from baseline only where all of these hold:

- it lies within a road's reach (`ROAD_BANK_MAX` of a road's edge, plus 1 m);
- it is not on any airfield's grounds;
- under an approach funnel, it is no higher than the funnel's cap.

Bridges and the stretches beside an airfield leave the ground alone. The test
counts these samples separately and fails if they exceed a twentieth of the
frozen locations; v3.45 grades 47 of 17,523. Runway samples, runway fixtures and
airport grounds have no road exception. The immutable fixture is unchanged.

### The open sea beyond the archipelago (map wrap)

From v3.45 the 80 km square sits in 10 km of open sea, and the map wraps every
100 km (`src/world.h`, `WRAP_HALF`). Every frozen location lies inside the square,
where the wrap changes nothing: a lookup is wrapped into [-50, 50) km, which leaves
coordinates inside the square exactly as they were. The sea fades to 80 m deep only
outside the square. `world_cache_test` checks that heights repeat exactly every
100 km, and that the band is open sea with no cliff at the square's edge.
