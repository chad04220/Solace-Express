# Validation evidence

The deliverable is two native Tier A aircraft definitions, shared gear/wheel finish for the fleet, an XR-9 exterior canopy change, and a separately prepared rotating-wheel integration. No production catalogue or release was changed. Current publication parent: R0 `db08f7aa334cf3e7c04ed99c6f5caa5bdca63ca6`. Historical flight/authoring captures were run on `3bde64c6ecbe45e1c755459ec93119207ff8e02b`; R0 changed shader layout/assembly, with identical aircraft tables and flight-model source. Evidence filenames distinguish the baselines.

## Native flight and authoring

- Final candidate matrix: **110/110 passing cases**, 55 per aircraft. The 96 autolands cover PVI/CAP/NPT/CDR, calm and gusting crosswind, direct and NAV entries, and light/max-guide payloads. Four departures start on the ground with full fuel; six control probes verify pitch/roll/yaw direction at sensible control-test speed; four comfortable HOLD/NAV cases constrain bank/g/load and approach behaviour. No airborne resets or forced touchdown are used to pass departures/landings.
- All **11 suites passed** on the historical isolated append roster. ASan+UBSan candidate run also passed all 110 cases, with no reported address/undefined-behaviour errors. Leak checking was disabled because of the container's process-inspection limitation; these are not leak-clean claims.
- The guide's actual insertion order was separately rehearsed: Swift index 7, Nightjar 8, XR-9 9, XR-11 10, automatic career count 9. All **11 suites passed**, including the entire candidate matrix, career progression, saves and gameplay. That rehearsal remains explicitly labeled 3b, and the optional adapter reproduces it on the current source.
- Current R0 with optional wheel integration: **12/12 suites pass**, including all 110 flight cases and 138 wheel checks. The entire candidate flight CSV is byte-for-byte identical to the pre-animation output, corroborating that cosmetic wheel state does not change flight forces/results.

| Result | Swift S6 | XR-8 Nightjar |
|---|---:|---:|
| Guide maximum mass | 1,940 kg | 7,190 kg |
| Guide Vs1 / Vs0 | 33.185 / 27.345 m/s | 44.113 / 37.898 m/s |
| Full-fuel light liftoff distance | 158.6 m | 598.1 m |
| Guide maximum-weight liftoff distance | 415.5 m | 624.8 m |
| Maximum touchdown descent across landing cases | 1.7761 m/s | 2.5996 m/s |
| Roof above eye | 9.1 cm | 8.7 cm |
| Spar-to-floor clearance | 5.6 cm | 6.7 cm |

Values are measured/computed at the test configuration, not performance guarantees in arbitrary weather or terrain. The authoring deliverable contains conservative guide mass, field calculations, ranges, inertia checks, clearance checks and exact rows. Source/guide coordinate discrepancies are stated there rather than hidden by renderer hooks.

## Wheel animation

The current native probe passes **138/138 checks**. It covers all eleven aircraft, both candidates' rendered-radius packing, independent left/right/nose channels, native ground contacts, air-start behaviour, AI takeoff/traffic texture transport, all wheel slots/pivots/radii in the four scenery kinds at both detailed LODs, signed reverse travel, stationary parking, pause/reset, radius scaling, turn travel, analytic airborne spin-down, airborne braking, invalid input guards and affected/distant shadow-cache keys. `Ent` and `EVert` sizes and the traffic texture stride remain unchanged. The current wheel probe also passes under **ASan+UBSan**, with leak checking disabled for the same container limitation.

`integration.patch` passes `git apply --check` on the current review source. It does not contain the lab's candidate insertions/count override. Both integration scripts fail before writes when parent hashes or anchors differ. Shared gear/canopy reproduction against pristine R0 modules is byte-exact. A manifest verifies all 124 current source/test/tool/CMake files against the R0 Git tree; exactly the three claimed shared aircraft shader modules differ in the production checkout. Wheel hooks live in the optional patch/lab until Claude integrates them.

## Visual evidence and limits

The capture tool uses the real game renderer, source distance fields, procedural entity meshes, materials, camera and controls through surfaceless EGL OpenGL. Renderer: Mesa llvmpipe, LLVM 20.1.2, 256-bit SIMD. No GPU or generated concept imagery is used. Quality 0, fixed daytime sun/weather, frozen four-frame poses, stopped propeller and cleared temporal history make geometry comparisons repeatable. Hull guides are disabled as an acceleration aid; visible geometry remains the actual model.

The historical fleet gallery contains **56 successful gear/deployment/steering/flight captures** across all eleven aircraft. Both candidates also have six guide/interior views each, plus front shots. XR-9 canopy before/after uses matched side/front cameras: crown 11 cm lower, shoulder blend 0.16 m, sealed interior unchanged. The complete historical render manifest records 74 original images; the branch includes curated comparison sheets/previews, while the capture commands regenerate full images. Wheel animation evidence adds **37 current R0 native captures**, all with `GL errors=0`: 15 aircraft/canopy/phase images, 8 ground-driver images, 12 Swift loop frames and 2 Wraith half-retracted nose poses. All production scene/UI programs initialize and link successfully (21 program-cache events); no new shader errors were reported.

The parked photography fixture does not settle suspension, so a tailwheel can float slightly above the runway in the fixed reset pose. That is a capture-fixture limitation; the native contact rollout/departure tests simulate settling. Current wheel material coordinates use Wraith's own 0.19 m retracted-belly offset, rather than XR-9's 0.50 m offset.

llvmpipe's optimized LLVM compilation exceeded this container's 8 GiB limit during the initial full-scene attempt. `GALLIVM_PERF=nopt`, one heavy renderer process and one llvmpipe worker produced the actual captures. This is a diagnostic environment setting and does not establish Windows compatibility, GPU frame cost, controller feel or release readiness. The owner/Claude should check those on the integrated Windows/RTX 3070 build, especially moving gear, high-speed wheel aliasing/TAA, camera feeds, nighttime interiors, the future mesh SPIN hierarchy and dynamic-caster cost if moving ground vehicles are added.
