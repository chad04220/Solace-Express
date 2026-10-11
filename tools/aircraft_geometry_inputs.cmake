# Conservative CPU dependency contract for portable full-quality aircraft assets.
# Keep paths repository-relative so build directories and hosts do not affect identity.
if(CMAKE_SCRIPT_MODE_FILE)
  file(GLOB AIRCRAFT_GEOMETRY_HEADERS "${CMAKE_SOURCE_DIR}/src/aircraft_mesh*.h" "${CMAKE_SOURCE_DIR}/src/aircraft_build*.h")
  file(GLOB AIRCRAFT_GEOMETRY_PRODUCER_INPUTS "${CMAKE_SOURCE_DIR}/tools/mesh_assets/*.cpp")
else()
  file(GLOB AIRCRAFT_GEOMETRY_HEADERS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/aircraft_mesh*.h" "${CMAKE_SOURCE_DIR}/src/aircraft_build*.h")
  file(GLOB AIRCRAFT_GEOMETRY_PRODUCER_INPUTS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tools/mesh_assets/*.cpp")
endif()
set(AIRCRAFT_GEOMETRY_CPU_INPUTS
  ${AIRCRAFT_GEOMETRY_HEADERS}
  "${CMAKE_SOURCE_DIR}/src/aircraft_mesh.cpp"
  "${CMAKE_SOURCE_DIR}/src/aircraft_mesh_asset.cpp"
  "${CMAKE_SOURCE_DIR}/src/aircraft.cpp"
  "${CMAKE_SOURCE_DIR}/src/aero_strips.cpp"
  "${CMAKE_SOURCE_DIR}/src/aero.h"
  "${CMAKE_SOURCE_DIR}/src/aero.cpp"
  "${CMAKE_SOURCE_DIR}/src/weather.cpp"
  "${CMAKE_SOURCE_DIR}/src/weather.h"
  "${CMAKE_SOURCE_DIR}/src/world.h"
  "${CMAKE_SOURCE_DIR}/src/wheel_motion.h"
  "${CMAKE_SOURCE_DIR}/src/aero_wake.h"
  "${CMAKE_SOURCE_DIR}/src/aircraft_hull.cpp"
  "${CMAKE_SOURCE_DIR}/src/renderer.cpp"
  "${CMAKE_SOURCE_DIR}/src/renderer.h"
  "${CMAKE_SOURCE_DIR}/src/common.h"
  "${CMAKE_SOURCE_DIR}/src/gl.h"
  "${CMAKE_SOURCE_DIR}/src/models.cpp"
  "${CMAKE_SOURCE_DIR}/src/models.h"
  "${CMAKE_SOURCE_DIR}/src/aircraft.h"
  "${CMAKE_SOURCE_DIR}/src/cockpit_layout_data.h"
  "${CMAKE_SOURCE_DIR}/src/cockpit_focus_zoom.h"
  "${CMAKE_SOURCE_DIR}/src/hull_mesh.h"
  "${CMAKE_SOURCE_DIR}/src/mesh_simplify.h"
  "${CMAKE_SOURCE_DIR}/src/mesh_validation.h"
  "${CMAKE_SOURCE_DIR}/src/shaders.h"
  "${CMAKE_SOURCE_DIR}/src/shader_prune.h"
  "${CMAKE_SOURCE_DIR}/tools/aircraft_geometry_inputs.cmake"
  "${CMAKE_SOURCE_DIR}/tools/generate_aircraft_geometry_source.cmake")
list(REMOVE_DUPLICATES AIRCRAFT_GEOMETRY_CPU_INPUTS)
list(SORT AIRCRAFT_GEOMETRY_CPU_INPUTS)
list(SORT AIRCRAFT_GEOMETRY_PRODUCER_INPUTS)
