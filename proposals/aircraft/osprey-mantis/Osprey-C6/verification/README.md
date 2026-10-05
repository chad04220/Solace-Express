# Reproduce against a298771

The delivered aircraft consists only of the two .inc rows. Files here are isolated verification fixtures, not requested production changes. The original baseline source was never changed.

1. Run `python3 validate.py` from the package root.
2. Run `python3 verification/prepare_test_copy.py /path/to/a298771 /tmp/osprey-check`. The destination must not exist and must be outside the baseline.
3. In `/tmp/osprey-check`, run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure -j2
CORE='src/world.cpp src/scenery.cpp src/entities.cpp src/airport_scenery.cpp src/aircraft.cpp src/aircraft_perf.cpp src/aircraft_stunt.cpp src/aero.cpp src/career.cpp'
g++ -std=c++17 -O2 -pthread -I src tests/osprey_max_weight_test.cpp $CORE -o build/osprey_max_weight_test
build/osprey_max_weight_test
build/flight_test --table
g++ -std=c++17 -O2 -pthread -I src -I build/gen tests/aircraft_visual_test.cpp src/models.cpp src/gl.cpp $CORE -ldl -o build/aircraft_visual_test
MESA_SHADER_CACHE_DIR=/tmp/osprey-mesa LIBGL_ALWAYS_SOFTWARE=1 AVT_VIEWS=front,side,rear,top,belly,retracted build/aircraft_visual_test /tmp/osprey-previews 1200 750 7 7
MESA_SHADER_CACHE_DIR=/tmp/osprey-mesa LIBGL_ALWAYS_SOFTWARE=1 AVT_VIEWS=cockpit,down,left,gearfront,gearside,gearbelly,transition,controls,controls-negative build/aircraft_visual_test /tmp/osprey-qa 960 600 7 7
```

Requires C++17 compiler, CMake, EGL/OpenGL Mesa software rendering libraries; no GL window or physical GPU is needed. Do not install packages or use another machine without the applicable authorization.

## Fixture scope

- `osprey_max_weight_test.cpp` derives from baseline `flight_test.cpp`. Civilian loops target index7 and reset with full220kg fuel plus810kg payload, giving2450kg initial total. Fuel burns normally during each sortie; this is not a constant-mass test. Original assertions are retained, including26-degree comfortable-route bank ceiling. Ancillary XR-9 checks remain in this fixture; they are not Osprey coverage.
- The visual fixture includes the production model field, modelProps and shaders. Its numerical conventional probe loop extends through index7 instead of silently stopping at6. Headrests are intentionally suppressed by production cabin fitting for this cabin: the raw1.0000m probe value is a sentinel, not a measured clearance. Endcap seam probe actually samples Osprey.
- Static propeller blades in the final hero previews use an inspection-only adaptation of the existing production prop-disc compositor, with body coordinates already aligned and identical modelProps hub/radius inputs. They are composited flat blades, not new aircraft meshes or SDF changes. This also replaces a reversed-edge smoothstep with its defined equivalent. Airframe and gear are the unmodified production SDF.
- Inspection lighting and cockpit material colors are simplified. The harness does not draw instrument textures or the world/runway. Blank panel faces in retained cockpit QA are an inspection limitation. The hero contact sheet intentionally uses exterior views only.
- CPU/Linux CTest passes do not establish Windows rendering, the future rasterizer, a physical GPU, every weather/airport combination, or visual correctness of full-game GAV/CKV shots. Those remain integration checks.
- Baseline tests contain some hard-coded nine-aircraft loops. The inserted Osprey at7 is covered; preserving baseline source/tests means this suite is not proof that shifted index9 Wraith is covered in every generic loop. Dedicated Wraith checks still use its adjusted constant. For the combined research/civilian pack, the integrator should audit its own test roster bounds separately.


## Revision audit commands

Use the same build recipe above with the delivered updated fixture. Add:

```sh
AVT_SYMMETRY=1 AVT_VIEWS=none LIBGL_ALWAYS_SOFTWARE=1 build/aircraft_visual_test /tmp/osprey-audit 64 64 7 7
AVT_VIEWS=ortho-front,ortho-rear,ortho-top,ortho-underside LIBGL_ALWAYS_SOFTWARE=1 build/aircraft_visual_test /tmp/osprey-ortho 1200 1000 7 7
AVT_SILHOUETTE=1 AVT_VIEWS=ortho-front,ortho-rear,ortho-top,ortho-underside LIBGL_ALWAYS_SOFTWARE=1 build/aircraft_visual_test /tmp/osprey-silhouette 960 800 7 7
```

AVT_GEAR and AVT_FLAPS may override the displayed gear/flap state in [0,1]. The mirror probe compares production fields at x and −x, with pitch/flaps unchanged and roll/yaw/steering signs reflected. It samples 6 gear positions, both flap endpoints and 3 control endpoints through 48 vertical slices. The single left-wing pitot is a documented production feature; only samples within its exact production bounding sphere may have unequal field distances. Raw error and exception counts are printed alongside structural error, so the exception cannot silently hide a new mismatch elsewhere. Existing headrest/endcap checks remain unchanged. No production or baseline test assertions were removed or relaxed.
