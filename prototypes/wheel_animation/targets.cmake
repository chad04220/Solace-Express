# Optional native rolling integration and mesh coverage test, appended only to an isolated laboratory.
add_executable(wheel_animation_test prototypes/wheel_animation/wheel_test.cpp ${GAME_SOURCES} src/radio_stub.cpp)
target_include_directories(wheel_animation_test PRIVATE src prototypes/aircraft prototypes/wheel_animation)
add_dependencies(wheel_animation_test shaders_gen)
if(UNIX AND NOT APPLE)
  target_link_libraries(wheel_animation_test PRIVATE ${CMAKE_DL_LIBS})
endif()
add_test(NAME wheel_animation COMMAND wheel_animation_test)

if(TARGET aircraft_candidate_render)
  target_compile_definitions(aircraft_candidate_render PRIVATE CANDIDATE_WHEEL_ANIMATION)
endif()
