# Reproduce the native portable-mesh review

These are source-only diagnostic adapters and exact comparison scripts. They do not modify production files. They insert read-only GPU capture and timers into a separately compiled copy of the production renderer; geometry, materials, pose and draw code still come from the selected source tree.

## Requirements and workspace

Use an existing Linux C++17/GNU g++ toolchain, CMake, Python 3 with NumPy and Pillow, and EGL/OpenGL with sufficient texture units. No script installs dependencies. Keep builds and full bakes serial; the original run used Mesa llvmpipe LLVM 19.1.7 / Mesa 25.0.7-2+deb13u1. Reproducing on another driver is useful, but its fresh procedural bytes and image pixels need not match the recorded driver. Compare its original and candidate runs on that same driver.

Copy this directory into a **new external workspace**. It generates hundreds of megabytes of meshes and GPU buffers, plus images, build objects and shader caches. Do not run it in the tracked documentation directory. Preserve existing review artifacts. The commands below assume the repository checkout is the intended candidate and that the original commit is available locally.

```sh
REPO=$(pwd)
REVIEW=$(mktemp -d /tmp/prebuilt-aircraft-review.XXXXXX)
cp -R docs/validation/prebuilt-aircraft/reproduce/. "$REVIEW/"
mkdir -p "$REVIEW/source-snapshots/baseline-72d0af0"
git archive 72d0af0b23defb8067dd697f87d94384b39a412b | \
  tar -x -C "$REVIEW/source-snapshots/baseline-72d0af0"
cd "$REVIEW"
```

Keep the candidate checkout unchanged throughout the review, or make a separate source snapshot before building. The build records every source/material hash and refuses unstable source inputs. `CMAKE=/path/to/cmake` or `--cmake /path/to/cmake` selects a non-default CMake executable.

## Build adapters and establish current identities

```sh
python3 build_review.py --source source-snapshots/baseline-72d0af0 --name baseline --jobs 1
python3 build_review.py --source "$REPO" --name candidate-startup --jobs 1
export LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless LP_NUM_THREADS=1
export MESA_SHADER_CACHE_DIR="$REVIEW/identity-mesa-cache"
adapters/baseline/aircraft_review --identity baseline-identities
adapters/candidate-startup/aircraft_review --identity candidate-startup-identities
```

The identity mode creates the legacy keys, packed parameters, actual GL driver string, 60 pruned builder programs, and candidate portable identities. Check corresponding original/candidate files exactly before drawing; the candidate-only `portable-identities.csv` has no original equivalent. Source changes after this recorded review may legitimately change keys or geometry, but then require a new acceptance review. Never relabel an old cache or edit an identity to force it to pass.

## Fresh original geometry and native reference

```sh
python3 run_fixture_review.py --adapter baseline --out baseline-cold \
  --cache baseline-cache --mesa-cache baseline-mesa-cache --frames 4 --threads 1
python3 verify_uploads.py baseline-cold --report validation/baseline-cache-gpu-exact.json
python3 summarize_run.py baseline-cold --report validation/baseline-cold-summary.json
```

The fixture plan has 44 fixed camera/animation states at native 1920×1080, Medium, scale1. Each state is drawn four times. This is articulated-state coverage, not a continuous-motion video. Logs retain separate world/init, body and shader-stage times. Timers overlap; do not add them to infer a whole-game launch.

Atlas cockpit needs an **independently baked original actual-startup reference**. Its original PT46 fan AO has tiny phase-dependent float differences. A diagnostic phase1.1 mesh is not the same byte reference as real startup phase754.166687. Build the supplied alternate harness, which invokes the original `Game::update(1/60)` and `buildFrame` prewarm state. Copy only non-mesh shader/world caches; there must be no old mesh in the startup cache.

```sh
python3 build_review.py --source source-snapshots/baseline-72d0af0 \
  --name baseline-prewarm --harness aircraft_review_prewarm.cpp --jobs 1
python3 - <<'PY'
from pathlib import Path
import shutil
out=Path('prewarm-cache'); out.mkdir()
for f in Path('baseline-cache').glob('*.bin'):
    if not f.name.startswith('mesh_'): shutil.copy2(f,out/f.name)
PY
python3 run_fixture_review.py --adapter baseline-prewarm --out baseline-prewarm \
  --cache prewarm-cache --mesa-cache baseline-mesa-cache --plan prewarm-plan.json \
  --frames 1 --threads 1
python3 run_fixture_review.py --adapter baseline --out baseline-startup-cockpit-cameras \
  --cache prewarm-cache --mesa-cache baseline-mesa-cache --plan atlas-cockpit-plan.json \
  --frames 4 --threads 3
```

`prewarm_state_probe.cpp` is the source of the separate original all-30 startup-state measurement recorded in `atlas-startup.json`. The alternate harness above logs the actual original Atlas startup state while producing the byte reference. Never change the original reference baker to match candidate output.

## Strict export, actual ZIP round trip and packaged runtime

Use the repository's current `tools/mesh_assets/README.md` commands to build the standalone `aircraft_mesh_export`, bake a **new complete** fleet, bundle it and validate/extract the actual ZIP to `$REVIEW/staged-release/aircraft`. The strict exporter must not import baseline caches. Record its build and geometry digests, driver and ZIP hash. Do not use the raw bake directory for the final runtime test.

```sh
python3 validate_runtime_identities.py candidate-startup-identities/portable-identities.csv \
  staged-release/aircraft --report validation/candidate-runtime-identities.json
python3 compare_corpus.py staged-release/aircraft --atlas-startup \
  --report validation/candidate-full-corpus-exact.json
python3 run_fixture_review.py --adapter candidate-startup --out candidate-packaged-fresh \
  --cache candidate-fresh-cache --mesa-cache candidate-fresh-mesa-cache \
  --package staged-release/aircraft --frames 4 --threads 3
python3 verify_uploads.py candidate-packaged-fresh --portable-dir staged-release/aircraft \
  --report validation/candidate-package-gpu-exact.json
python3 compare_fixture_captures.py baseline-cold candidate-packaged-fresh \
  --atlas-startup-reference baseline-startup-cockpit-cameras \
  --report validation/native44-exact.json
python3 summarize_run.py candidate-packaged-fresh --report validation/candidate-package-summary.json
```

Require 30 package hits, zero misses/body bakes, all GL errors zero and every exact check passing. Record package file hashes before and after testing; they must be unchanged. The comparator records the explicit original-startup reference for Atlas cockpit rather than tolerating an AO difference.

## Load-only timing, original local uploads and fallback routes

These separate adapters return before scene rendering/initialization. They time the unchanged production load/checksum/upload path. The GPU-readback adapter is separate from the timed binaries so readback costs are not included.

```sh
python3 build_body_benchmark.py --adapter baseline
python3 build_body_benchmark.py --adapter candidate-startup
python3 run_body_benchmarks.py --mode driver_local --repeats 5
python3 run_body_benchmarks.py --adapter candidate-startup --mode packaged \
  --package staged-release/aircraft --repeats 5
python3 build_local_load_parity.py
LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless LP_NUM_THREADS=1 HULLDBG=1 \
  adapters/baseline/load-parity/load_parity "$REVIEW/baseline-cache" '' \
  "$REVIEW/baseline-local-loaded" > run-local-load-parity.log
python3 verify_uploads.py baseline-local-loaded \
  --report validation/baseline-local-loaded-gpu-exact.json
python3 run_fallback_review.py --adapter candidate-startup --package staged-release/aircraft
```

Each timing trial starts a new EGL process and creates new GPU allocations. OS file cache is warm and is not flushed; label that fact. The benchmark fails on a body bake, shader work or GL error. Its first new-process result is not a cold-disk or full game-startup measurement.

Fallback integration uses only one Kestrel exterior body in each disposable package: valid-over-local, missing/stale/corrupt-to-local, and missing-both-to-original-bake. It checks exact pixels/buffers, route counters and unchanged read-only package/source bytes. It does not establish all-model failure recovery, unwritable writable-cache behavior, every damage/breakup state, Windows GPU parity or hardware FPS. Those require their own tests.

Keep the source digests, adapter provenance, raw logs, exact comparisons and native images together in the external workspace. Do not commit generated meshes, PNGs, binaries, shader caches or GPU readbacks.
