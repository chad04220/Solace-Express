# Reproduce

Start with an isolated test copy produced by the Osprey aircraft-row package. It must contain Osprey at index7; that hard-coded index belongs only to this fixture. No baseline files need edits.

Run `python3 prepare_fixture.py /absolute/isolated-copy --isolated-copy`.

From that isolated copy:

```sh
cmake -DSRC_DIR="$PWD/src/shaders" -DOUT="$PWD/build-cmake/gen/shaders_gen.h" -P tools/embed_shaders.cmake
CORE='src/world.cpp src/scenery.cpp src/entities.cpp src/airport_scenery.cpp src/aircraft.cpp src/aircraft_perf.cpp src/aircraft_stunt.cpp src/aero.cpp src/career.cpp'
g++ -std=c++17 -O2 -pthread -I src -I build-cmake/gen tests/aircraft_visual_test.cpp src/models.cpp src/gl.cpp $CORE -ldl -o interior_test
MESA_SHADER_CACHE_DIR=/tmp/osprey-interior-mesa LIBGL_ALWAYS_SOFTWARE=1 AVT_VIEWS=cockpit,down,left,right,aft ./interior_test previews 960 600 7 7
```

This uses Mesa/EGL, no display window. The delivery was checked with llvmpipe LLVM19.1.7. All geometry and numerical-probe fields are GLSL, not approximated CPU shapes. Font glyphs are the existing `font_data.h` atlas. Pose probe includes yokes and pedals; throttle and flap levers remain untouched baseline and sit in the centre forward pedestal, outside the supplemental trim envelope. The custom part's floor begins z=-1.36; centre levers lie forward of it. Visual material overlays are Osprey-scoped aesthetic recommendations; baseline IDs and animations retain their meaning.
