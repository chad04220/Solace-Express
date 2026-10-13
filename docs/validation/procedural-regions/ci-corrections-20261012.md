# First published CI: scoped corrections

Date: 2026-10-12. Initial published commit:
[`94546c98aee144bfa13687903f005fbdc9c516d1`](https://github.com/chad04220/Solace-Express/commit/94546c98aee144bfa13687903f005fbdc9c516d1).
Its tree `f1b3d55795e7d15890eab28047ac1510d4da27c8` and sole parent `d1b2c99`
matched the locally reviewed source; all 2,338 remote file hashes/modes matched.

## Initial result

[Build run 38310219382](https://github.com/chad04220/Solace-Express/actions/runs/38310219382)
compiled the Windows executable but failed tests. Windows failed `gameplay_loop_1`
(access violation), `jet_fuel`, `region_travel_fastmath`, and `shader_prune`.
Linux sanitizer shard 2 failed `gameplay_loop_1` and `shader_prune`; shard 3
failed one `procedural_region_runtime` assertion. Sanitizer shard 1 and the
complete aircraft export/package job passed. No Windows distributable was produced.
[Offline Earth sampling](https://github.com/chad04220/Solace-Express/actions/runs/38310219363)
passed on Windows and Ubuntu.

## Corrections and preserved contracts

1. **Free Flight fixture synchronization.** Returning from live, unwrapped Solace
   restores the authored World asynchronously. The older direct-call fixture
   assumed an immediate menu return, then re-entered setup while `g_world` was
   being rebuilt. A loader can assign airport metadata before filling the terrain
   array; the fixture could therefore call `Plane::reset`/height sampling against
   incomplete terrain. Four exact-save assertions reproduced on Linux. Windows
   supplied no crash backtrace, so that interleaving is a source-grounded crash
   explanation rather than a captured Windows stack.

   `free_flight_regression.inc` now checks the loading gate and drains the real
   loader to verified authored-world readiness before the original menu/save
   assertions or another launch. A private authored cache survives all Game/job
   lifetimes. All 17 original assertion expressions are unchanged. The corrected
   coherent Release binary passes all 352 launch combinations and the exact
   active/recovery-career and pending-transaction checks. No production Game
   lifetime or save behavior was changed.

2. **Portable fuel-save fixture.** `jet_fuel_test.cpp` used a hard-coded `/tmp`
   filename. On Windows that parent is not guaranteed to exist. A bounded Linux
   fault injector returning `ENOENT` for only that filename reproduced exactly
   the four save/load/quantity/billing failures. The test now uses
   `std::filesystem::temp_directory_path()` and cleans the save, `.bak`, and `.tmp`
   files. Its complete Linux run passes both normally and with a temporary root
   containing spaces while the obsolete path remains fault-injected. All 34
   assertion-bearing lines and the production fuel model are unchanged.

3. **Horizontal transport precision.** A float quaternion's imperfect unit norm
   and intermediate rounding produced real horizontal speed drift. MSVC fast
   math exceeded the existing tolerance at `0.000132893448 m/s`. Horizontal
   vector transport now uses double sine/cosine and a planar rotation with one
   final float narrowing, retaining vertical velocity exactly. Attitude still
   uses the existing normalized float quaternion. No existing tolerance changed.

   The Linux fast-math sweep's prior speed error `4.53124538e-05 m/s` becomes
   `1.64259598e-05 m/s`. A new independent polar-coordinate oracle covers 2,882
   chart rotations and fails the previous code. Its maximum corrected speed and
   vector-oracle errors are `1.61304168e-05` and `1.61649774e-05 m/s`.
   Strict, fast-math, native-optimized, and ASan+UBSan math-only suites each pass
   76,479 checks. Leak detection was disabled because executor ptrace blocks
   LeakSanitizer; this is not a leak-sanitizer pass. The linked real-Plane strict
   and fast-math suites each pass 78,185 checks.

4. **Aircraft-only shader identity.** The added world-topology uniforms leaked
   into aircraft bake declarations. Their unused declarations changed the bake
   cache key despite unchanged aircraft geometry. `PART_BAKE` now excludes those
   declarations and uses the old non-world helper forms; the bake-only road stub
   is restored. The pinned shared, `AF_LIGHT`, and `AF_JET` fixtures are unchanged
   and pass. Across 168 pruned programs, 104 are byte-identical to the original
   published source; the other 64 are aircraft bakes with only those two unused
   uniform declarations removed. No other program tokens or declarations changed.
   All 280 assembled/pruned vertex and fragment shader files pass glslang linking.

5. **Deterministic stale-candidate fixture.** The uncooperative fixture performed
   an entire cold procedural build only after releasing its gate, inside the
   five-minute completion wait. An unchanged local Release run passed, with
   24.103 seconds spent in that one cold build. Sanitizer contention plausibly
   exhausted the bound, but the original combined assertion log cannot distinguish
   timeout from a generation exception. No production retirement race was found.

   The same populated World/cache is now prepared during fixture setup, while the
   async completion/ownership transfer remains genuinely gated and uncooperative.
   The original five-minute limit, retirement predicate, and separate real
   in-progress generation-cancellation test remain. Added diagnostics and explicit
   future/error checks make future failures distinguishable. No production
   scheduling or cancellation limit was changed.

## Verification scope

The corrective commit includes focused regression output, the eight changed
source-file SHA-256 values, the shader comparison summary, and fresh Windows
cross-build results in the adjacent `ci-corrections/` directory. The local
regression selection passed 16/16 in 103.82 seconds from 139 registered tests, including all
three gameplay shards, saves, jet fuel, runtime, transport, collision, and
retirement coverage. It is not the entire Linux project suite. The exact final
source also cross-linked the Windows game and six affected test executables;
a repeated build reported no work remaining. The gated obsolete-candidate
retirement completed in 4.259 ms; the independent real cancellation request and
worker retirement measured 0.025 and 31.168 ms in this Release run.

A new full GitHub CI run is required for the corrective commit, especially native
MSVC fast math and the Windows gameplay crash. Check the exact commit's terminal
result in issue #2. Cross-linking is not a Windows runtime test. Hardware-GPU
mixed-fleet performance and deliberate integration with upstream's rebrand and
foveation remain separate acceptance work. No upstream merge, release, numerical
tolerance relaxation, assertion deletion, or aircraft fixture update is included.
