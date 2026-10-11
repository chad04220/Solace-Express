# Prebuilt aircraft verification: PASS

Original reference: pristine release `72d0af0`. Tested build digest: `25b0ebe6af771a254ed9011d43a42b59cb21625ece22a53bcc84340c04be062c`; geometry digest: `12e3710931e791adb24b9531501db3e7a5132e4866249f36e9a5837ee9d75743`.

## Completed checks

- All 30 original/candidate body payloads and fine splits are exact: **293,786,120 bytes**, preserving positions, normals, material/AO values, static/part index order, moving hull and eye flags. No precision reduction. [Payload evidence](payload-parity.json)
- All **44 native 1920×1080 images are pixel-identical**, with 1,620 exact GPU/pose-buffer comparisons. Both views of all 15 aircraft and the selected Atlas, Wraith, Specter and Larkspur articulation states pass. Each fixed state was drawn four times; all 176 frames per sweep had GL0. [Native evidence](native-parity.json)
- Original cold and extracted-package uploads each pass 1,080 exact checks. Original driver-local loads pass 684 GPU checks plus 282 part-centre checks. [Upload evidence](gpu-upload-parity.json)
- Runtime loaded the actual ZIP extraction with empty writable shader/Mesa caches: **30 package hits, zero misses, zero body bakes**. Installed bytes remained unchanged. [Metrics](runtime-validation.json)
- Five representative Kestrel exterior precedence/fallback cases pass, with exact native output and unchanged read-only package contents. Valid package wins over local cache; missing/stale/corrupt package uses the original cache; missing both uses the original full bake. [Recovery evidence](runtime-paths.json)

The unchanged original Atlas fan AO has tiny phase-dependent float variation. The corrected producer matches the independently measured original actual-startup state, preserving all **51,172,628 Atlas cockpit payload bytes** exactly. The alternative diagnostic-phase reference remains preserved; its three cockpit images are pixel-identical too. No numerical tolerance conceals the AO difference. [Startup finding](atlas-startup.json)

## Measured times and limits

Matched single-thread body-only medians across five new EGL processes: **1.847 s packaged**, versus **0.953 s existing native cache** for all 30 bodies. Original cold procedural body work totaled **746.113 s**. The packaged path's extra 0.894 s includes identity/checksum/validation work; checksum cost was not isolated. No SDF or shader work occurred in the load-only trials. [Raw trial metrics](load-benchmarks.json)

The OS file cache was warm, not flushed. New processes created fresh GPU allocations. The quality sweep used three llvmpipe threads and separately recorded 9.811 s of body work and 4.083 s of cockpit display-shader work across 30 processes. These are not one complete game startup, and overlapping shader/body/init times must not be added.

Mesa llvmpipe establishes the recorded same-driver fresh-bake, transport and integration parity. It does not establish Windows GPU behavior, RTX 3070 speed/FPS, cold-disk startup, all cameras or continuous animation. Windows LLVM-MinGW cross-build/source-manifest parity and the final 90/90 CTest pass (312.95 s) are separate build/test evidence, not hardware rendering evidence.

The ZIP is 149,316,793 bytes, SHA256 `dea672b5b6861ab1ff404aa2c9f45b2c2e7cbf795525ece5dc5730b79780f65b`. Raw binaries, images and buffers stay outside Git. See the [evidence index](README.md), [source invariants](source-invariants.json), [reproduction instructions](reproduce/README.md), and [complete handoff/remaining acceptance](../../CLAUDE_PREBUILT_AIRCRAFT_REVIEW.md).
