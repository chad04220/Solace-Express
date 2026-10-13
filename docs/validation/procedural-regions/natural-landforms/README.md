# Natural landforms: revision 2

Local review branch only. Catalog IDs/seeds remain version 1. The naturalism worker performed no commit, push or merge; the parent checkpoints the integrated review separately.

## Verified

- Strict original analytic suite: 200,795 checks, zero failures.
- New shape suite, strict and fast-math: 99,597 checks each, zero failures.
- New shape suite under AddressSanitizer + UndefinedBehaviorSanitizer: 99,597 checks, zero failures. `ASAN_OPTIONS=detect_leaks=0` was required: LeakSanitizer reports that it cannot work under the environment's ptrace wrapper. This does not claim a leak-sanitizer pass.
- Every default-seed generated region (6,143): 2,397,110 airport, town, approach and dry-connection checks, zero failures.
- Metadata before/after: identical for 6,143 regions × nine seeds (55,287 recipes). Digests include canonical IDs, names, seeds, archetypes, geographic origins and all airport/town metadata fields. See matching metadata-before.txt and metadata-after.txt.
- Recipe memory: 15,308 bytes for both comparison fixtures, under the established 16 KiB cap.
- Atoll fixture: unprotected radial spans 17.5–33%, narrow-to-broad dry rim spans 2.5–30% of nominal radius, multiple open/submerged gaps and continuous inhabited arcs.
- Fjord fixture: a 6.35 km inland trunk shift, distinct fork legs separated by headlands, and confirmed ocean connectivity.

## Evidence boundaries

`*-comparison.png`, `*-before.png` and `*-after.png` are analytic base-height diagnostics sampled from Recipe::sample. They are not rendered game screenshots. Before images were sampled from the saved pre-change generator source/header; after images were sampled from the frozen revision-2 source. Both show exactly the same 76 km square and colour thresholds. The original production views remain in ../readable-landforms.

Actual full-World smoothing, airport facilities, roads, collision, coarse-far visibility, cache invalidation/replay and oblique game screenshots are separate integration checks owned by the parent task. None are implied by these maps.

Sampling timing files produced during the shared rebuild are provisional and affected by CPU contention. They are superseded by sampling-quiet-pairs.txt: three sequential old/new repetitions in a reserved quiet window. Median atoll time was 154.2 → 157.8 ns/sample (+2.3%); median fjord time was 255.5 → 332.4 ns/sample (+30.1%). Both remain below the 1.5× cost target. The benchmark source samples 2,097,152 positions per fixture at -O2, single-threaded, without constructing a full World. These are analytic CPU costs, not game-frame or full-generation timings.

The additional content revision belongs only in generated-world cache stamps. It must not feed catalog IDs, metadata seed streams or the authored Solace cache stamp. Full authored-world cache byte preservation remains an integration gate.

## Reproduction

The test is CPU-only and can be linked directly from tests/procedural_landforms_test.cpp, src/procedural_regions.cpp and src/geo.cpp with C++17 and thread support. `catalog-placement-audit.cpp`, `metadata-digest.cpp`, `sampling-benchmark.cpp`, `sample-analytic-map.cpp` and `draw-analytic-maps.py` reproduce the additional checks and diagnostic maps. `source-sha256.txt` identifies the frozen generator and shape-test source.
