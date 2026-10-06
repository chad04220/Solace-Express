// Solace Express - shader sources and the programs assembled from them
#pragma once
#include <string>
#include "shaders_gen.h"   // the sources: src/shaders/*.glsl, embedded at build time (tools/embed_shaders.cmake)

// The scene programs are put together from the GLSL modules (one constant per file in src/shaders), in an order that
// defines every function before its first use. Everything that builds them - the game, the shader checker, the
// keyword test, the geometry previewer - takes the list from here, so they always check what the game runs.

// The shared library of scene functions (terrain heights and materials, the clouds, the aircraft fields and
// materials, the cockpit displays, the lights): no main. The GPS map, the terrain-shadow bake, the cloud pass, the
// hull bake and the display atlas each add their own main to it.
inline std::string worldLibAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kNoiseTex + kRtIO + kViewUniforms + kSceneUniforms + kPlaneCommon + kPlaneParts + kPlaneSDF + kPlaneTrace + kTerrainTrace +
         kMaterialCommon + kLightCommon + kClouds + kTerrainMaterial + kRaytraceUfo + kRaytraceText + kRaytraceDisplays + kRtPrims + kPlaneScreens +
         kFeeds + kPlaneFx + kWraithSDF + kWraithMaterial + kWraithFx + kWraithCockpitCommon + kWraithCockpitSDF + kWraithCockpitMaterial +
         kPlaneMaterial + kWater + kAfShMap + kPlaneLight;
}
// The aircraft distance fields alone (tests/aircraft_visual_test.cpp adds its own main)
inline std::string sdfAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kRtIO + kViewUniforms + kSceneUniforms + kPlaneCommon + kPlaneParts + kPlaneSDF + kWraithSDF + kWraithCockpitCommon + kWraithCockpitSDF;
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
// the objects pass: the ray tracer's aircraft, traffic, debris and UFO code, writing the G-buffer (no terrain march, no clouds)
inline std::string objectsFSAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kNoiseTex + kViewUniforms + kSceneUniforms + kPlaneCommon + kPlaneParts + kPlaneSDF + kPlaneTrace +
         kMaterialCommon + kLightCommon + kClouds + kRaytraceUfo + kRaytraceText + kRaytraceDisplays + kRtPrims + kPlaneScreens +
         kFeeds + kPlaneFx + kWraithSDF + kWraithMaterial + kWraithFx + kWraithCockpitCommon + kWraithCockpitSDF + kWraithCockpitMaterial +
         kPlaneMaterial + kAfShMap + kPlaneLight + kGBuffer + kGBWrite + kPlaneGB + kObjectsFS;
}
// the rigid parts' poses (plane_parts.glsl partPose), once a frame into a small texture the part draws read
inline std::string partPoseFSAssembly() {
  return std::string("#version 330 core\n#define PART_POSE_ONLY\n") + kCommonGLSL + kRtIO + kViewUniforms + kSceneUniforms + kPlaneCommon + kPlaneParts +
         "uniform sampler2D uPPInfo;   // per instance: its part type, its owner (0 the player's aircraft, k + 1 traffic k), its side\n"
         "void main(){\n"
         "  int x = int(gl_FragCoord.x), i = x/4, c = x - i*4;\n"
         "  vec4 info = texelFetch(uPPInfo, ivec2(i, 0), 0);\n"
         "  int owner = int(info.y + 0.5);\n"
         "  if (owner > 0) loadTraffic(owner - 1); else loadMain();\n"
         "  Pose X = partPose(int(info.x + 0.5), info.zw);\n"
         "  oColor = vec4(c == 0 ? X.R[0] : c == 1 ? X.R[1] : c == 2 ? X.R[2] : X.T, 1.0); oDepth = 0.0; oCloudMask = 0.0;\n"
         "}\n";
}
// the aircraft mesh pass: the objects pass's materials and lighting classes on the baked static airframe
inline std::string planeMeshVSAssembly(const std::string& defines) { return std::string("#version 330 core\n") + defines + kPlaneMeshVS; }
inline std::string planeMeshFSAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kNoiseTex + kViewUniforms + kSceneUniforms + kPlaneCommon + kPlaneParts + kPlaneSDF + kPlaneTrace +
         kMaterialCommon + kLightCommon + kClouds + kRaytraceUfo + kRaytraceText + kRaytraceDisplays + kRtPrims + kPlaneScreens +
         kFeeds + kPlaneFx + kWraithSDF + kWraithMaterial + kWraithFx + kWraithCockpitCommon + kWraithCockpitSDF + kWraithCockpitMaterial +
         kPlaneMaterial + kAfShMap + kPlaneLight + kGBuffer + kGBWrite + kPlaneGB + kPlaneMeshFS;
}
// the shadow proxy: the airframe fields' shadows on what is in the G-buffer, for the lighting pass
inline std::string shadowProxyFSAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kNoiseTex + kViewUniforms + kSceneUniforms + kPlaneCommon + kPlaneParts + kPlaneSDF + kPlaneTrace +
         kWraithSDF + kWraithCockpitCommon + kWraithCockpitSDF + kMaterialCommon + kLightCommon + kGBuffer + kAfShMap + kShadowProxyFS;
}
// the effects pass: the ray tracer's effects over the lit frame (its cloak needs the airframe's field)
inline std::string effectsFSAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kNoiseTex + kViewUniforms + kSceneUniforms + kPlaneCommon + kPlaneParts + kPlaneSDF + kPlaneTrace +
         kMaterialCommon + kLightCommon + kClouds + kRaytraceUfo + kRaytraceText + kRaytraceDisplays + kRtPrims + kPlaneScreens +
         kFeeds + kPlaneFx + kWraithSDF + kWraithMaterial + kWraithFx + kWraithCockpitCommon + kWraithCockpitSDF + kWraithCockpitMaterial +
         kGBuffer + kEffectsFS;
}
inline std::string lightFSAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kNoiseTex + kRtIO + kViewUniforms + kSceneUniforms + kMaterialCommon + kLightCommon + kClouds + kGBuffer + kLightFS;
}
