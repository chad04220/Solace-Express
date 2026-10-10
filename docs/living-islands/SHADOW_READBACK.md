# Close-shadow readback diagnosis

## Final production decision

Retain the original quality-dependent cascade coverage and allocation:

| Quality | Near radius | Far radius | Each depth map |
|---|---:|---:|---:|
| Low | 300 m | 1,600 m | 2,048² |
| Medium | 420 m | 2,600 m | 2,048² |
| High | 520 m | 3,600 m | 4,096² |

The environment-only receiver-plane shader remains in production. Its close
contact response is improved, but the eave/balcony aliasing gate is **not solved**.
The 96/160/256 m fixed-radius experiments and adaptive-radius controller are
rejected as production defaults. They must not be represented as shipped shadow
improvements. No runtime adaptive radius, extra map or aircraft-helper edit is
part of the final decision.

The office-night emission correction is separately accepted from matched native
city and office/skyscraper captures. It does not change these daytime shadows.

## Evidence and outcome

The v6 native house/apartment captures still show periodic eave/balcony lobes.
Passing shader arithmetic tests did **not** establish visual acceptance. The
diagnostic compares actual float32 GPU depth data with the exact compiled caster
mesh and recorded camera/light matrices, rather than inferring geometry from a PNG.

| Fixture | Occupied depth texels | Coverage mismatches | Mean depth error | Maximum error |
|---|---:|---:|---:|---:|
| House | 1,014 | 0 | 0.148 mm | 1.327 mm |
| Apartment | 3,324 | 0 | 0.183 mm | 1.890 mm |

These results reproduce the LOD0 caster, its transform and the existing OpenGL
polygon offset. The house's main roof triangles are copied unchanged into hero
LOD3, as are the apartment balcony slabs. The periodic main-roof lobes therefore
are not a mismatched hero roof or incorrect light-space transform. Small added
hero details can still differ from their cheaper shadow caster.

Medium's near map is 2,048² over 840 m: **0.41015625 m per texel**. A 0.5 m
overhang is only 1.22 texels wide. On the captured front wall, receiver depth
changes +0.789/−0.441 m per texel. Existing caster polygon offset adds 0.228 m on
the relevant roof and 1.184 m on its thin fascia. Such coarse edge coverage and
depth offsets limit close inspection even with correct receiver-plane sampling.

There should be a real, continuous diagonal shadow below the overhang. At this
sun angle its approximate depth is 1.3 m, obtained by intersecting solar rays with
the actual roof plane. A fully bright wall from the cascade-off debug view is not
the physical target. The defect is the repeating bright/dark lobes in that band.

The fixed-radius experiments used only the review harness with the same map
allocation and far cascade. No successful numerical audit establishes visual
acceptance or native RTX 3070 performance.

## Radius96 diagnostic result

The exact-camera v7 images visibly restore a coherent house eave band and
rectangular apartment balcony shadows, replacing the large isolated lobes and
diamonds. Fine edge serration remains. Sedan ground contact is tighter. Actual
metadata confirms 96/2,600 m radii at the unchanged 2,048² allocation, with 19,414,
63,410 and 809 occupied house/apartment/car texels respectively (about 19× the
v6 coverage). Small reports are included alongside v6. CPU/GPU raster edge
rounding creates one house coverage disagreement and rare larger depth outliers
where a different triangle wins a shared edge; mean error remains submillimetre.

The existing caster offset still ranges up to about 0.52 m on some visible tyre
facets. However, probing the actual four tyre tangent positions finds the two
sunward points already 49%/62% occluded and the other two fully occluded. A CPU
re-raster with slope factor 0.5 or 0.25 instead of 1.5 gives only 0–7 percentage
points of improvement at the sunward tangent points. The compiled LOD0 and hero
axles/tyre bottoms agree. These results do not justify another caster-bias change;
small tangent footprints and remaining edge aliasing limit contact detail.

The later fixed-unit v8 sweep **rejects unconditional radius 96 as the ground
default**: at 120–180 m, houses and pump/tank fixtures lose distinct ground shadows
that radius 420 retains. The unchanged far map cannot preserve this readable
mid-distance detail. The earlier wider rural overview obscured that loss; the
isolated same-object distance pairs are the stronger acceptance evidence.

Radius 160 m improves close sampling but retains visible eave serration. Its
0.15625 m texels, 152 m full forward coverage and 196.8 m fade endpoint remain a
coverage/detail tradeoff. Radius 256 m was included in the bounded diagnostic
set, not accepted as a new default. The final choice preserves the original
coverage rather than silently sacrificing readable middle-distance shadows.

The adaptive prototype used discrete radii, threshold hysteresis and a near-to-far
handover to avoid changing the map radius every frame. Its arithmetic/controller
tests cannot establish that the far-only interval is visually acceptable. It is
not enabled in production. A smaller radius would also reduce the existing
movement refresh threshold (11.52 m at radius 96, versus 50.4 m at radius 420),
which can raise near-map refresh frequency despite fewer casters. No performance
win is asserted.

## Reproduction

Capture an isolated, yaw-zero, unit-scale fixture with
`ASSET_REVIEW_SHADOW_DUMP=1`. The asset harness writes GB0, near depth and JSON
metadata in little-endian float32, OpenGL bottom row first. The metadata records
actual lighting-pass matrices and camera uniforms. Keep the immutable executable
and source manifest corresponding to the capture.

Export the caster from the matching built objects (no GL or new game build):

```sh
BUILD=/path/to/matching-build
CXX=${CXX:-c++}
$CXX -std=c++17 -O2 -Isrc tools/validation/export_shadow_fixture_mesh.cpp \
  "$BUILD"/CMakeFiles/environment_asset_mesh_test.dir/src/*.o \
  -o /tmp/export_shadow_fixture_mesh
/tmp/export_shadow_fixture_mesh 13 0 /tmp/house-caster.f32
python3 tools/validation/audit_shadow_readback.py \
  /path/to/captures/kind_13_front_oblique --mesh /tmp/house-caster.f32 \
  --origin -577 20 -4413 --output /tmp/house-shadow-audit.json
```

Use kind 19 for apartment or 45 for sedan; slot 0 is the near caster and slot 3
can quantify hero differences. The exporter deliberately rejects unsupported
kinds/slots. The Python audit requires NumPy and never edits input or production
files. It uses actual triangles, per-triangle float32 depth units and the current
polygon-offset factor 1.5, units 2. It reports errors rather than assigning a
visual pass. It does not implement foliage deformation, alpha cutouts, arbitrary
instance rotation/scale, clipped light-depth triangles or all exact hardware fill
and coplanar tie rules.

Small reproducibility records are in
[`performance/shadow-diagnostics`](performance/shadow-diagnostics): v6 production
source hashes, native screenshot/GB0/depth/metadata hashes, and house/apartment
audit summaries. Large raw buffers remain outside the repository. The later
office-night emission edit is not part of this v6 source identity; daytime shadow
helpers are unchanged by that edit.
