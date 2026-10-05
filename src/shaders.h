// Solace Express - shader sources and the programs assembled from them
#pragma once
#include <string>
#include "shaders_gen.h"   // the sources: src/shaders/*.glsl, embedded at build time (tools/embed_shaders.cmake)

// The scene programs are put together from the GLSL modules (one constant per file in src/shaders), in an order that
// defines every function before its first use. Everything that builds them - the game, the shader checker, the
// keyword test, the geometry previewer - takes the list from here, so they always check what the game runs.

// The ray tracer: the whole scene in one fragment program. The GPS map, the terrain-shadow bake, the cloud pass,
// the hull bake and the display atlas are built from the same source with main() renamed and their own main added.
inline std::string rtAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kNoiseTex + kRtIO + kViewUniforms + kSceneUniforms + kPlaneCommon + kPlaneSDF + kPlaneTrace + kTerrainTrace +
         kMaterialCommon + kLightCommon + kClouds + kTerrainMaterial + kRaytraceUfo + kRaytraceText + kRaytraceDisplays + kRtPrims + kPlaneScreens +
         kFeeds + kPlaneFx + kWraithSDF + kWraithMaterial + kWraithFx + kWraithCockpitCommon + kWraithCockpitSDF + kWraithCockpitMaterial +
         kPlaneMaterial + kWater + kRtShade + kRtMain;
}
// The same with main() renamed, for a program that adds its own main
inline std::string rtAssemblyNoMain(const std::string& defines) {
  std::string s = rtAssembly(defines); size_t m = s.find("void main(");
  if (m != std::string::npos) s.replace(m, 10, "void mainRT(");
  return s;
}
// The aircraft distance fields alone (tests/aircraft_visual_test.cpp adds its own main)
inline std::string sdfAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kRtIO + kViewUniforms + kSceneUniforms + kPlaneCommon + kPlaneSDF + kWraithSDF + kWraithCockpitCommon + kWraithCockpitSDF;
}

// ---- the raster renderer's programs (raster_renderer.cpp, terrain_mesh.cpp)
inline std::string terrainVSAssembly(const std::string& defines) { return std::string("#version 330 core\n") + defines + kCommonGLSL + kTerrainVS; }
inline std::string terrainFSAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kNoiseTex + kViewUniforms + kSceneUniforms + kMaterialCommon + kTerrainMaterial + kGBuffer + kGBWrite + kTerrainFS;
}
inline std::string waterVSAssembly(const std::string& defines) { return std::string("#version 330 core\n") + defines + kWaterVS; }
inline std::string waterFSAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kNoiseTex + kViewUniforms + kSceneUniforms + kMaterialCommon + kLightCommon + kClouds + kWater + kGBuffer + kGBWrite + kWaterFS;
}
inline std::string lightFSAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kNoiseTex + kRtIO + kViewUniforms + kSceneUniforms + kMaterialCommon + kLightCommon + kClouds + kGBuffer + kLightFS;
}
