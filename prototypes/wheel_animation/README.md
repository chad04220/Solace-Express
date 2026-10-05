# Common rotating-wheel integration for review

Prepared on R0 `db08f7aa334cf3e7c04ed99c6f5caa5bdca63ca6`, independently of the two new-aircraft definitions. The production source in this branch includes the shared gear/canopy work; **wheel animation is a separate, tested optional patch** for Claude to integrate or port to the upcoming mesh renderer. No release or aircraft catalogue has been changed.

The audit found no wheel-phase state in `Plane`, `TrafficCraft`, their visual packets or the four wheeled entity meshes. Propeller angles animate only propellers. `Ent` airport furniture has no movement driver, so parked cars/trucks/planes should remain stationary.

| Rendered vehicle | Wheels / phase channels | Source of motion |
|---|---|---|
| Every player aircraft, including the candidates | Main left, main right, nose/tail; paired tyres share their axle phase | Existing per-wheel contact velocity, including angular motion and steering |
| AI airport aircraft | Same three phases; stored in unused traffic rotation-column `.w` components | Actual contact-point displacement over the simulation tick |
| Parked GA-plane entity | 3 independently tagged wheels | Zero for scenery; explicit dynamic packet for a future driver |
| Parked airliner entity | 6 independently tagged wheels | Same |
| Car entity | 4 independently tagged wheels | Same |
| Airport fuel truck/bowser entity | 6 independently tagged wheels | Same |

`WheelMotion` retains signed travel in double precision and resolves radians at the **visual tyre radius**. This avoids adding ModelDef dependencies to the flight-model core and keeps the guide's two-row authoring contract intact. The wheels rotate faster when smaller, reverse with reverse ground travel, follow left/right differential travel in a turn, and stop on a stationary contact. Main-wheel brakes already slow ground velocity through the existing physics; the animation follows that velocity. Airborne wheels coast with an eight-second cosmetic time constant, or spin down rapidly under main-wheel brakes. They never spin from airspeed or `uTime`. Reset and paused simulation leave deterministic state.

Aircraft face -Z, while scenery meshes face +Z: the adapter resolves the corresponding sign around +X. The aircraft shader applies inverse SPIN only after reconstructing the wheel's deployment/steering frame. XR-9 and XR-11 retain their distinct fold paths; the original research nose wheels have no steering geometry. The SDF tyres/rims are axisymmetric, so their distance envelope stays unchanged while their hub/bolt finish rotates. For the upcoming mesh renderer, use these phases as `rigSPIN` children of the existing gear/steer parents, with the rim and brake rotor rotating and the caliper/leg remaining fixed.

Ground wheel IDs 27..32 distinguish rotating wheels from other dark surfaces. Their auxiliary fields contain radius and pivot Y/Z; `Ent` remains 32 bytes and `EVert` remains 40 bytes. The vertex shader rotates both vertices and normals **before instance scale and yaw**, in the view and shadow passes. The rest-space material shows rotating hub spokes. Ground motion uses the supplied world velocity and yaw rate; tyres use world-scaled radii. Existing furniture uses uniform scale. A future driver should keep wheel Y/Z scale equal, register moving collision/raycast behaviour, and remove any duplicate static furniture instance.

`FrameParams::groundVehicles` is an explicit draw packet. The renderer does not invent airport routes or move scenery. It uses small individual draws for supplied moving vehicles and refreshes only shadow cascades affected by a changed/removed packet. A real dynamic-vehicle system should measure that shadow-refresh cost and may later use a separate dynamic-caster pass. Empty packets preserve static batching/caching; parked packets preserve their phase. Camera feeds and shadow passes never advance motion. The current entity TAA path does not provide per-wheel motion vectors; validate fast moving vehicles/aliasing on the target renderer before adding a driver.

## Reproduce in an isolated laboratory

From the review branch:

```sh
python3 prototypes/aircraft/prepare_lab.py --source . --output ../solace-wheel-lab
python3 prototypes/wheel_animation/apply_wheel_animation.py --source ../solace-wheel-lab
cmake -S ../solace-wheel-lab -B ../solace-wheel-build -DCMAKE_BUILD_TYPE=Release
cmake --build ../solace-wheel-build -j 2
ctest --test-dir ../solace-wheel-build --output-on-failure
```

The wheel adapter accepts only a fresh aircraft lab, checks pinned parent file hashes and all anchors before writing, and installs the common headers there. It does not edit the production checkout. Add `--career-insertion` to the aircraft preparer for the guide's production slots; the wheel adapter accepts that layout too. The checked-in `integration.patch` is ordinary source, applies against this branch, and contains the corresponding production C++/GLSL/header edits without laboratory aircraft rows or count overrides:

```sh
git apply --check prototypes/wheel_animation/integration.patch
# After Claude's review:
git apply prototypes/wheel_animation/integration.patch
```

The normal root CMake is unchanged. For the independent wheel probe, append `targets.cmake` to an isolated CMake copy as the adapter does. The patch neither inserts aircraft rows nor modifies contact/friction forces, save records, static instance/vertex strides or the 32-texel traffic texture width.

Linux EGL fixtures use the full production renderer and actual shaders:

```sh
GALLIVM_PERF=nopt MALLOC_ARENA_MAX=2 LP_NUM_THREADS=1 LIBGL_ALWAYS_SOFTWARE=1 \
  SHADERCACHE=../solace-shader-cache \
  ../solace-wheel-build/aircraft_candidate_render ../wheel-images 640 400 \
  wheel-main-zero,wheel-main-quarter swift-s6,xr14-nightjar,xr9-specter

# Ground-driver fixture: actual distance moves each vehicle and its following camera.
GALLIVM_PERF=nopt MALLOC_ARENA_MAX=2 LP_NUM_THREADS=1 LIBGL_ALWAYS_SOFTWARE=1 \
  SHADERCACHE=../solace-shader-cache \
  ../solace-wheel-build/aircraft_candidate_render ../ground-wheel-images 640 400 unused ground
```

The `nopt` flag is a software-driver memory workaround for this container, not a hardware optimization. Wheel views can also use `wheel-nose-half-zero`, `wheel-nose-half-quarter`, and `wheel-main-frame-N` (N × 5 degrees), with frozen state and cleared temporal history for each pose. These are animation/rig fixtures; native rollout and AI travel are checked independently.

Evidence: [validation](../../docs/design/additional-aircraft/validation.md), [138 native checks](../../docs/design/additional-aircraft/evidence/wheel-test-r0.log), [sanitizers](../../docs/design/additional-aircraft/evidence/wheel-test-sanitize-r0.log), and [12-suite regression](../../docs/design/additional-aircraft/evidence/ctest-wheel-r0.log). The 110 candidate flight CSV is byte-identical before/after cosmetic wheel integration. Windows/controller/GPU timing validation belongs to the integrated target build.

[Open actual game-rendered wheel and ground-vehicle previews](../../docs/design/additional-aircraft/README.md#prepared-wheel-animation).
