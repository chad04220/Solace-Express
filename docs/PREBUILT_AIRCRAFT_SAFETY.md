# Prebuilt aircraft safety audit

Audit snapshot: 2026-10-11. Scope: portable format, source identity, runtime load
ordering, strict exporter, release packaging, and focused CPU tests. This is not
a claim that native Windows, RTX 3070, or complete fleet visual verification has
finished.

## Verified behavior

- A validated packaged body is loaded before the driver-local cache. A hit
  bypasses the bake shader pair, field evaluation, extraction and simplification.
  Existing GPU upload and moving-part pose paths consume the unchanged arrays.
- Portable data retains IEEE float32/uint32 bits, ordered rigid parts, the
  coarse/fine index boundary, moving hull, and its eye flag. No added
  quantization, mesh simplification, normal repair or geometry transformation is
  performed by export/load.
- Decode checks version, algorithm, full-quality profile, model/view/source
  identity, reserved metadata, exact section lengths, SHA256, finite attributes,
  normals/AO, index bounds, duplicate part types, and hull/partition structure.
  The 512 MiB file limit precedes allocation. Rejected data leaves the output
  unchanged and never reaches GPU upload or modifies a packaged file.
- Export always invokes a fresh production bake. Legacy caches cannot establish
  full-quality provenance and are never promoted. Either `NV_SAFE_GEAR` fallback
  or a shared builder prevents canonical export. The ordinary runtime fallback
  behavior remains available.
- The exporter uses one serial subprocess per body. The coordinator verifies
  each worker's source, model/view, full-quality and GL receipt, then decodes its
  file. Only all 30 successful bodies produce a complete bundle report.
- Packaging revalidates the archived bytes and the consumer build's source
  manifest. It rejects incomplete rosters, duplicate identities, unsafe archive
  names, links, legacy caches and unexpected files. Temporary publication keeps
  failed exports out of the release destination.

## Portability defects found and fixed

1. Host-computed model/fit floats initially participated in asset names. A real
   GCC `-O2` versus `-O2 -ffast-math` probe changed 17 values across 12 of 15
   aircraft, affecting gear stations, panel width and foot fitting. The largest
   absolute change was approximately 0.596 micrometre; even one changed bit
   invalidated an entire asset identity. Source-semantic identities now avoid
   host-evaluated float bytes and shader constants. The exact local canonical
   model comparison still excludes custom geometry. Mesh payloads were not
   rounded or changed to fix identity.
2. LF/CRLF checkout differences could change otherwise identical source hashes.
   `.gitattributes` now enforces LF for source/tool inputs and CMakeLists. The
   isolated Git checkout regression proves the contract under
   `core.autocrlf=true`, including initially CRLF and future nested inputs.
3. The source contract now includes mesher/hull/part/simplification dependencies,
   model/layout computation, aerodynamic dependencies of gear stations, and the
   exporter. Source edits regenerate the manifest as build dependencies.
4. Python validation was aligned with the producer-record digest contract and
   C++ duplicate-part rejection.

## Commands and observed results

Run from the repository root; `CXX` and `CMAKE` identify installed tools. The
focused audit used GCC 14.2.0 and an existing CMake installation, without installing
dependencies.

```sh
mkdir -p /tmp/portable-mesh-audit
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  tests/portable_mesh_adversarial_test.cpp src/aircraft_mesh_asset.cpp \
  -o /tmp/portable-mesh-audit/adversarial
/tmp/portable-mesh-audit/adversarial
# The optimized run also passed with -ffast-math added.
g++ -std=c++17 -O1 -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -fno-sanitize-recover=undefined \
  tests/portable_mesh_adversarial_test.cpp src/aircraft_mesh_asset.cpp \
  -o /tmp/portable-mesh-audit/adversarial-sanitize
ASAN_OPTIONS=detect_leaks=0 /tmp/portable-mesh-audit/adversarial-sanitize

python3 tests/portable_mesh_checkout_test.py
python3 tests/portable_mesh_identity_test.py "$CXX" "$CMAKE"
python3 tools/mesh_assets/test_mesh_assets.py
python3 tests/aircraft_geometry_source_test.py "$CMAKE"
python3 tests/aircraft_mesh_workflow_test.py
git diff --check
```

- Codec: **1,341 checks passed** in optimized, fast-math, and ASan/UBSan builds.
  Includes every truncated prefix and single-byte corruption of the fixture,
  recomputed-checksum semantic corruption, overflow counts, a sparse oversized
  file, failed-write preservation, and negative-zero/subnormal bit preservation.
- Git checkout: **19 checks passed**.
- Actual extracted production identity functions, linked to the production
  codec/pruner/generated shaders/source contract/roster: **all 30 filenames
  matched strict and fast builds**, including cache and invalid-ID guards.
- Packaging: **16 tests passed**. Source generator: **880 checks passed**.
  Release wiring: **6 tests passed**. Whitespace check passed.
- Linux Release (GCC 14.2.0) and Windows LLVM-MinGW (Clang 23.1.3)
  cross-builds separately completed.
  Their generated source manifests were byte-identical, with build digest
  `bf82d00316c079de750e6812b19825f83a4a67ff055821aa1ab20d402a7d7460`.
  The clean final Release CTest run passed **90/90 tests in 307.61 seconds**.
  All 90 unique passed names match the current CTest JSON inventory, with no
  missing or obsolete tests. This supersedes the earlier separate-run union;
  it is not a complete fleet sanitizer run.

## Remaining limits and concerns

- These results do not establish native MSVC/Windows execution, RTX 3070 startup
  timings, cross-driver visual equivalence, or complete fresh-bake fleet parity.
  Same-producer byte-preserving transport and cross-compiler identity are
  distinct claims. GPU/runtime evidence must be recorded separately.
- Only focused codec/mocked-dispatch sanitizer checks were run, not a complete
  fleet sanitizer bake. LeakSanitizer could not run under executor ptrace;
  AddressSanitizer and UndefinedBehaviorSanitizer passed with leak detection off.
- SHA256 establishes integrity and compatibility, not authorship. Structurally
  valid geometry is not a proof of the intended shape. The loader retains the
  production topology contract rather than introducing new geometric repairs or
  degenerate-area thresholds; trusted fresh export and parity evidence remain
  important.
- The legacy driver-local cache's pre-existing allocation and provenance limits
  are unchanged. The new portable format does not repair that older format.
- Directory publication uses an existence check followed by rename, rather than
  a platform-specific atomic no-replace primitive. Use isolated staging and a
  new destination; concurrent publishers to the same destination are unsupported.
