# CPU renderer regression checks

From the repository root:

```sh
python3 tools/validation/run_cpu.py
```

Requires Python 3 and a C++17 compiler using libstdc++ (`g++` by default, or set `CXX`). AddressSanitizer and UndefinedBehaviorSanitizer are enabled by default. If the compiler lacks them, use `--no-sanitize` and identify that limitation when reporting results. No OpenGL library, driver, display, context, shader compilation or GPU work is performed.

The default run builds in a temporary directory and cleans it afterward. To retain logs, generated includes, source fingerprints and executables:

```sh
python3 tools/validation/run_cpu.py --output build/renderer-validation
```

Leak detection defaults off because LeakSanitizer is unsupported in some traced/sandboxed environments; ASan bounds/use-after-free checks and UBSan remain enabled. You may set `ASAN_OPTIONS` to enable leak checking where supported.

## What runs

`extract.py` reads the **current checkout's actual production methods** and writes temporary includes. It does not use stale copies of the implementation. `reference_hull_bake_main.glsl` is the small, unchanged upstream bake entry point from `ce435bba`; it is a comparison fixture, not a production shader. Keeping that fixture makes the tests usable from source archives and shallow clones.

- `bake_dispatch_test.cpp` mocks GL calls and executes actual renderer bake methods, `setRT`, feed measurement and paired NVIDIA fallback logic. It checks common inputs, model/layout/fit forwarding, current cloud/wake arrays, optimized-out selectors, all bake modes, partial normal rows, a 524,305-point multi-batch query, hostile UI yields, invalid input rejection, feed reset, coherent full/safe variants and cleanup. Source checks verify the unchanged field/AO body, immediate normal reuse, batching bounds and cache identities.
- `shader_lifetime_test.cpp` executes the current compile/link/binary-cache implementation with mock shader/program objects. It checks successful detachment/deletion, failed vertex/fragment/link cleanup, cold cache writes, warm binary loads and rejected-binary rebuilds.

The mock bake output deliberately uses identifiable synthetic values to validate transfer and state. It does **not** evaluate the distance field or prove numeric GLSL equivalence, completed mesh integrity, final images, vendor-driver behavior or frame rate. See [the renderer review](../../docs/RENDERER_REVIEW.md) for the separate historical GPU comparison and outstanding target-renderer checks.

These focused checks are supplemental to CMake/CTest and are not automatically registered as tests requiring a compiler at CTest runtime. A production-method signature or shader-contract change may require a deliberate update to this narrow harness; extraction or assertions should fail visibly rather than silently using an old implementation.
