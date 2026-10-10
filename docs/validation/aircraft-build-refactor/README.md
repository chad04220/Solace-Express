# Aircraft build-interface parity, 2026-10-10

## Result

The common aircraft build interface is applied. Conventional fleet, XR-30 Specter and XR-40 Wraith retain separate CPU builders and shader specializations. The refactor changes ownership/dispatch only; it does not change authored geometry, materials, physics, game state, shader switches, part IDs, cache algorithm 24, or the bake/render lifecycle.

The baseline was refreshed **after** the intentional Atlas bay/flap/fairing, conventional cockpit, winding/planar, Atlas door-edge, and Specter overhead-toggle extraction repairs. Those repairs are part of the baseline, not changes introduced by this refactor.

- **7,329 exact legacy-versus-live CPU/shader-assembly checks pass**, covering 3,594 part plans, every one of the 15 aircraft, exterior/cockpit/player/traffic lists, ordered hull states, and perturbed packed inputs.
- **1,459 durable contract checks pass**, optimized and under ASan/UBSan. Leak detection was disabled because the execution environment prevents LSan operation under ptrace; this is focused sanitizer evidence, not an integrated sanitizer-suite result.
- **8/8 native 1920×1080 fixtures match exactly** before and after: all 16,588,800 decoded pixels, packed frame-state CSVs, uploaded geometry bounds and topology. Every RGB mean absolute error is zero.
- CPU/GLSL part ABI and separate-family module ownership, aircraft specialization, all 37 gear/debris rendering contracts, 8,074 covered-wheel checks, and all 21,923,754 final Atlas geometry checks pass.
- C++17 syntax checks pass for aircraft_mesh.cpp, aircraft_hull.cpp and shader_check.cpp.

## New ownership

- `src/aircraft_build_family.h`: packed family selector and unchanged shader defines.
- `src/aircraft_mesh_build_types.h`: shared stable CPU part IDs, ordered instances, sampling plan, hull state and builder contract.
- `src/aircraft_mesh_build_fleet.h`: conventional part lists and surface/gear bounds, including final Atlas flap and bay bounds.
- `src/aircraft_mesh_build_specter.h`: Specter parts, nozzle/surface plans and authored hull-sweep extension.
- `src/aircraft_mesh_build_wraith.h`: Wraith pods, weapons, actuators, surfaces and authored hull-sweep extension.
- `src/aircraft_mesh_build_research_gear.h`: only the pre-existing gear bounds legitimately shared by both research craft.
- `src/aircraft_mesh_build.h`: registry and common dispatch.

`aircraft_mesh.cpp`, `hull_mesh.h` and `shaders.h` consume these contracts. Three existing source-bound tests now read the relocated fleet functions and their named PT_* constants; their geometry assertions and negative controls remain intact. The temporary duplicate legacy implementation used for one-off exact parity is deliberately not shipped. The production regression fixture is `tests/aircraft_build_golden.inc`, captured from the pre-refactor implementation.

## Native fixtures and reproducibility

The complete native PNG and CSV evidence is included in `before/` and `after/`:

1. Specter neutral exterior, gear extended
2. Specter vectoring/deflected controls with gear in transit
3. Specter cockpit with deflected controls
4. Wraith hovering pods, vanes and surfaces
5. Wraith weapons deployed, bay open, bomb loaded
6. Wraith weapons deployed with the bomb absent
7. Wraith partial cloak wave
8. Wraith cockpit in the hover state

Each side includes frozen build provenance and its actual fixture harness. The original renderer comparison is `native-parity-original.json`; `native-parity.json` was independently regenerated against the copies in this folder. Recheck from the repository root with Python and Pillow installed:

```sh
python3 docs/validation/aircraft-build-refactor/compare_native.py \
  docs/validation/aircraft-build-refactor/before \
  docs/validation/aircraft-build-refactor/after \
  --report /tmp/aircraft-build-native-parity.json
```

`research-snapshot-compatibility.json` proves that the last Atlas-only leading-fairing shader edit was absent from all 22 checked XR-30/XR-40 pruned programs. The associated mesh, pose, model, game and cache-stamp sources were unchanged. This permits the preserved XR fixtures from the final round-2 snapshot to serve as the exact before baseline while the final Atlas-only snapshot rendered independently.

These native captures use the real **cached production mesh and renderer**. `native-integrity.json` records cache hits and zero body builds in the after captures. They verify cached drawing, part poses, frame state and topology; they are not independent post-refactor cold bakes of the entire fleet. Exact CPU/build-plan/source preservation supplies the separate build-interface evidence.

## Evidence index

- `focused-validation.txt`: exact parity, durable tests, focused sanitizer, contracts, geometry and syntax results.
- `native-parity.json`: 8-fixture exact comparison against the included files.
- `native-integrity.json`: resolution, binary identity, cache status and capture integrity.
- `before/build_provenance.json`, `after/build_provenance.json`: immutable renderer builds and source identities.
- `pre-refactor-source-sha256.json`, `post-refactor-source-sha256.json`: 101-source before/after guards. Only the expected dispatch/document/test files changed; GLSL, physics/model/aero, geometry helpers and cache stamp did not.
- `interface-source-sha256.json`: new headers and durable test identities.
- `wheel-cover-contract.json`: all actual covered-wheel users and geometry/steering checks.
- `specter-toggle-native-measurements.json`: the **preceding intentional repair**, separated from the no-visual-change refactor. Its local 3.90625 mm patch adds about 201 KiB of final vertex/index data; peak tracked CPU working vectors were 37.2 MiB, excluding output mesh and GPU staging.
- `evidence-sha256.json`: hashes of the persisted evidence files.

## Remaining release gates

This report establishes the interface refactor's focused CPU/source/native parity. The final whole-project build/CTest and research regressions, whole-fleet visual sweep, current-source flight/rollout validation, integrated sanitizers, Windows native launch, real-controller testing and target-hardware performance remain separate parent-owned gates. No commit, push, merge or publication was performed as part of this work.
