# Additional aircraft and fleet model review

Two fictional aircraft in the native Tier A format: Swift S6, a four-seat PPL touring piston, and XR-14 Nightjar, an ATP civil research jet. Prepared against `db08f7aa334cf3e7c04ed99c6f5caa5bdca63ca6`. The [authoring deliverable](../../docs/design/additional-aircraft/authoring.md) contains the required briefs, computed max-weight self-checks, four exact table rows, insertion indices and harness scenes.

| | Swift S6 | XR-14 Nightjar |
|---|---|---|
| Span / body length | 11.40 / 8.60 m | 16.60 / 16.30 m |
| Wing / engines | Tapered low wing; three-blade nose prop | Near-straight leading edge and forward quarter-chord; twin aft jets |
| Interior | Offset analog cockpit; passenger windows | Offset glass cockpit; no passenger windows |
| Power | 260 kW piston | 2 × 12.5 kN conventional jets |
| Design cruise / approach | 76 / 36 m/s | 150 / 50 m/s |
| Game design range | 140 km | 240 km |
| Nominal runway | 600 m paved | 1,250 m paved |
| Licence / price / rent | PPL / 68,000 / 600 | ATP / 340,000 / 0 |

Both definitions work with the unmodified generic renderer. No special physics flag, new engine/gear code, shader dispatch, display relocation or negative-sweep bound patch is required. They use normal shared aerodynamics and fuel; the flight model does not integrate forces over the rendered mesh or model aeroelasticity.

The owner separately requested detailed landing gear and wheels for both prototypes and the existing fleet, plus a lower, smoother XR-9 exterior canopy. Those shared model changes are isolated to three GLSL files and can be reviewed independently from importing the new rows. Fine tread channels, hub recesses and bolt patterns use procedural materials; the actual oleos, torque links, braces, rims, axle caps and brake discs are model geometry.

## Reproduce

From this branch, create a fresh laboratory outside the checkout. The adapter checks source hashes and refuses to overwrite a directory. It appends the prototype rows at temporary lab slots 9/10 and leaves the original nine rows and seven-aircraft career selection intact. This is test isolation; use the insertion instructions in authoring.md for production.

```sh
python3 prototypes/aircraft/prepare_lab.py --source . --output ../solace-aircraft-lab
cmake -S ../solace-aircraft-lab -B ../solace-aircraft-build -DCMAKE_BUILD_TYPE=Release
cmake --build ../solace-aircraft-build -j 2
ctest --test-dir ../solace-aircraft-build --output-on-failure
```

The full candidate matrix contains 96 autolands (four eligible airports × three weather cases × two entry geometries × two payloads), four full-fuel ground departures, six control-direction probes, two comfortable HOLD turns and two comfortable NAV routes ending in natural landings. The upper payload uses the brief's conservative sum of cargo, every passenger seat at 90 kg, and a 90 kg pilot. The scripted pilot uses the ordinary controls; the test never resets the aircraft airborne or forces touchdown.

```sh
../solace-aircraft-build/aircraft_candidate_test candidate-flights.csv
TRACE_TAKEOFF=1 ../solace-aircraft-build/aircraft_candidate_test takeoff-trace.csv --ground-only
```

On Linux, the optional EGL render probe uses the actual game shaders, geometry and materials:

```sh
GALLIVM_PERF=nopt MALLOC_ARENA_MAX=2 LP_NUM_THREADS=1 LIBGL_ALWAYS_SOFTWARE=1 SHADERCACHE=../solace-shader-cache \
  ../solace-aircraft-build/aircraft_candidate_render ../solace-renders 960 600 front,gear-main,gear-nose
```

The fifth argument selects `all` or comma-separated roster slugs, e.g. `xr9-specter` or `wren,kestrel`. Views include front/rear/top/side, flight, gear-half, controls, cockpit/cockpit-left/cockpit-right/cockpit-down, gear-main/gear-nose, gear-main-half/gear-nose-half, gear-nose-turn, and XR-9 canopy-side/canopy-front. Fixed aircraft skip half-deployment views and retain gear in the flight view. Temporal history resets between views; all four frames in a shot use frozen state. Hull guides are disabled as an acceleration aid only; the visible geometry is the real model.

See [integration](../../docs/design/additional-aircraft/integration.md) and [validation](../../docs/design/additional-aircraft/validation.md). Screenshots from software OpenGL establish visual behaviour, not Windows deployment or RTX 3070 timings.

The Linux `GALLIVM_PERF=nopt` setting is a diagnostic workaround for llvmpipe LLVM compilation memory in this 8 GiB container, not a game setting or hardware-performance result. Use one heavy software-GL process at a time.

To rehearse the actual nine-aircraft career catalogue, add `--career-insertion` to the preparer command. The isolated copy inserts both rows before XR-9 and updates only its research constants, rather than the append/count isolation mode. Both layouts were validated.

Optional rotating wheels are a separately prepared integration: see [wheel_animation/README.md](../wheel_animation/README.md). The native aircraft rows require no aircraft-specific rendering hook.

[Open the rendered aircraft/gear/canopy/wheel review](../../docs/design/additional-aircraft/README.md).
