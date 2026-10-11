// Solace Express - shader sources and the programs assembled from them
#pragma once
#include <cstdio>
#include <string>
#include "aircraft_build_family.h"
#include "shaders_gen.h"   // the sources: src/shaders/*.glsl, embedded at build time (tools/embed_shaders.cmake)

// The scene programs are put together from the GLSL modules (one constant per file in src/shaders), in an order that
// defines every function before its first use. Everything that builds them - the game, the shader checker, the
// keyword test, the geometry previewer - takes the list from here, so they always check what the game runs.

// One aircraft's own build of the airframe programs: its index in kAircraft (AF_MODEL), its family's switch (AF_LIGHT,
// AF_JET for the XR-30, AF_WRAITH for the XR-40) and its packed model (models.cpp packModel, the same for the player's
// aircraft and every traffic aircraft of the type) as the constant gM. Every branch on another aircraft's type or
// shape is settled as the program compiles, and the code only another aircraft runs is cut from its source
// (plane_common.glsl HAS_*, shader_prune.h): the program holds this aircraft and nothing else.
inline std::string aircraftDefines(int model, const float packed[96]) {
  std::string d = "#define AF_MODEL " + std::to_string(model) + "\n" + aircraftBuild::shaderDefine(aircraftBuild::familyOf(packed));
  d += "#define AF_PACKED_MODEL vec4[24](";
  for (int i = 0; i < 96; i++) {
    char b[32]; snprintf(b, sizeof b, "%.9g", packed[i]);   // (9 digits: the float itself, exactly)
    std::string v = b;
    if (v.find_first_of(".en") == std::string::npos) v += ".0";   // (a float literal, never an int)
    d += (i % 4 == 0 ? (i ? "), vec4(" : "vec4(") : ", ") + v;
  }
  d += "))\n";
  return d;
}

// The shared library of scene functions (terrain heights and materials, the clouds, the aircraft fields and
// materials, the cockpit displays, the lights): no main. The GPS map, the terrain-shadow bake, the cloud pass, the
// hull bake and the display atlas each add their own main to it.
inline std::string worldLibAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kNoiseTex + kRtIO + kViewUniforms + kSceneUniforms + kRoads + kPlaneCommon + kCockpitLayout + kPlaneParts + kCockpitFittings + kResearchCockpitLayout + kPlaneSDF + kPlaneTrace + kTerrainTrace +
         kMaterialCommon + kLightCommon + kClouds + kTerrainMaterial + kRaytraceUfo + kRaytraceText + kRaytraceDisplays + kRtPrims + kPlaneScreens +
         kFeeds + kPlaneFx + kWraithWeaponSDF + kWraithSDF + kWraithStoreMaterial + kWraithMaterial + kWraithFx + kWraithCockpitCommon + kWraithCockpitSDF + kWraithCockpitMaterial +
         kCockpitMaterial + kPlaneMaterial + kWater + kAfShMap + kPlaneLight;
}
// the aircraft bodies' bake (aircraft_mesh.cpp, aircraft_hull.cpp): the fields evaluated part by part (PART_BAKE), and
// with HULL_BAKE_NORMALS their normals and cabin occlusion
inline std::string hullBakeFSAssembly(const std::string& defines) { return worldLibAssembly(defines + "#define PART_BAKE\n") + kHullBakeMain; }
// The aircraft distance fields alone (tests/aircraft_visual_test.cpp adds its own main)
inline std::string sdfAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kRtIO + kViewUniforms + kSceneUniforms + kRoads + kPlaneCommon + kCockpitLayout + kPlaneParts + kCockpitFittings + kResearchCockpitLayout + kPlaneSDF + kWraithWeaponSDF + kWraithSDF + kWraithCockpitCommon + kWraithCockpitSDF;
}

// ---- the scenery's instanced meshes (entity_render.cpp): a build for each class (ENT_CLASS, ent_common.glsl: the trees,
// the rocks, the buildings and vehicles), each with its class's code alone; each draw takes its kind's class's
inline std::string entVSAssembly(const std::string& defines) { return std::string("#version 330 core\n") + defines + kEntCommon + kEntVS; }
inline std::string entFSAssembly(const std::string& defines) { return std::string("#version 330 core\n") + defines + kEntCommon + kEntFS1 + kEntFS2; }
inline std::string entShadowFSAssembly(const std::string& defines) { return std::string("#version 330 core\n") + defines + kEntCommon + kEntFS1 + kEntShadowFS; }
inline std::string entClassDefines(int c) { return "#define ENT_CLASS " + std::to_string(c) + "\n"; }

// ---- the raster renderer's programs (raster_renderer.cpp, terrain_mesh.cpp)
inline std::string terrainVSAssembly(const std::string& defines) { return std::string("#version 330 core\n") + defines + kCommonGLSL + kSceneUniforms + kRoads + kTerrainVS; }
inline std::string terrainFSAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + "#define ENV_MATERIALS\n" + kCommonGLSL + kNoiseTex + kViewUniforms + kSceneUniforms + kRoads + kMaterialCommon + kTerrainMaterial + kGBuffer + kGBWrite + kTerrainFS;
}
inline std::string waterVSAssembly(const std::string& defines) { return std::string("#version 330 core\n") + defines + kWaterVS; }
inline std::string waterFSAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + "#define ENV_MATERIALS\n" + kCommonGLSL + kNoiseTex + kViewUniforms + kSceneUniforms + kRoads + kMaterialCommon + kLightCommon + kClouds + kWater + kGBuffer + kGBWrite + kWaterFS;
}
// the objects pass: the aircraft, traffic, debris and UFO fields marched into the G-buffer (no terrain, no clouds)
inline std::string objectsFSAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kNoiseTex + kViewUniforms + kSceneUniforms + kRoads + kPlaneCommon + kCockpitLayout + kPlaneParts + kCockpitFittings + kResearchCockpitLayout + kPlaneSDF + kPlaneTrace +
         kMaterialCommon + kLightCommon + kClouds + kRaytraceUfo + kRaytraceText + kRaytraceDisplays + kRtPrims + kPlaneScreens +
         kFeeds + kPlaneFx + kWraithWeaponSDF + kWraithSDF + kWraithStoreMaterial + kWraithMaterial + kWraithFx + kWraithCockpitCommon + kWraithCockpitSDF + kWraithCockpitMaterial +
         kCockpitMaterial + kPlaneMaterial + kAfShMap + kPlaneLight + kGBuffer + kGBWrite + kPlaneGB + kObjectsFS;
}
// the rigid parts' poses (plane_parts.glsl partPose), once a frame into a small texture the part draws read
inline std::string partPoseFSAssembly() {
  return std::string("#version 330 core\n#define PART_POSE_ONLY\n") + kCommonGLSL + kRtIO + kViewUniforms + kSceneUniforms + kRoads + kPlaneCommon + kCockpitLayout + kPlaneParts +
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
inline std::string planeMeshVSAssembly(const std::string& defines) { return std::string("#version 330 core\n") + defines + "uniform int uWreck;\n" + kWreckClip + kPlaneMeshVS; }
// the aircraft mesh's depth pre-pass: the cut of the research cockpits' windows (cabin_windows.glsl) and the cloak's front
inline std::string planeMeshDepthFSAssembly() {
  return std::string("#version 330 core\n") + kResearchCockpitLayout + kWraithCockpitCommon + kCabinWindows +
         "flat in float vId; in vec3 vW; in vec3 vN; in float vIdS; in float vAo; in vec3 vB; flat in int vPc;\n"
         "uniform int uScrSkip; uniform vec3 uScrEye; uniform int uScrModel; uniform int uBombPane; uniform int uPartInst;\n"
         "uniform float uCloakZ; uniform int uWreck;\n" + kWreckClip +
         "void main(){\n"
         "  if (uWreck > 0 && vPc < 0 && brkOwner(vB) != uPcK) discard;\n"
         "  if (uScrSkip == 1 && cabinWindowCut(vB - uScrEye, uScrModel, uBombPane == 1, uPartInst >= 0)) discard;\n"
         "  if (uCloakZ > -1e8 && vB.z < uCloakZ) discard;\n"
         "}\n";
}
inline std::string planeMeshFSAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + "#define AF_MESH\n" + kCommonGLSL + kNoiseTex + kViewUniforms + kSceneUniforms + kRoads + kWreckClip + kPlaneCommon + kCockpitLayout + kPlaneParts + kCockpitFittings + kResearchCockpitLayout + kPlaneSDF + kPlaneTrace +
         kMaterialCommon + kLightCommon + kClouds + kRaytraceUfo + kRaytraceText + kRaytraceDisplays + kRtPrims + kPlaneScreens +
         kFeeds + kPlaneFx + kWraithWeaponSDF + kWraithSDF + kWraithStoreMaterial + kWraithMaterial + kWraithFx + kWraithCockpitCommon + kCabinWindows + kWraithCockpitSDF + kWraithCockpitMaterial +
         kCockpitMaterial + kPlaneMaterial + kAfShMap + kPlaneLight + kGBuffer + kGBWrite + kPlaneGB + kPlaneMeshFS;
}
// the shadow proxy: the airframe fields' shadows on what is in the G-buffer, for the lighting pass
inline std::string shadowProxyFSAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kNoiseTex + kViewUniforms + kSceneUniforms + kRoads + kPlaneCommon + kCockpitLayout + kPlaneParts + kCockpitFittings + kResearchCockpitLayout + kPlaneSDF + kPlaneTrace +
         kWraithWeaponSDF + kWraithSDF + kWraithCockpitCommon + kWraithCockpitSDF + kMaterialCommon + kLightCommon + kGBuffer + kAfShMap + kEnemyShadow + kShadowProxyFS;
}
// the effects pass: the effects over the lit frame (its cloak needs the airframe's field)
inline std::string effectsFSAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kNoiseTex + kViewUniforms + kSceneUniforms + kRoads + kPlaneCommon + kCockpitLayout + kPlaneParts + kCockpitFittings + kResearchCockpitLayout + kPlaneSDF + kPlaneTrace +
         kMaterialCommon + kLightCommon + kClouds + kRaytraceUfo + kRaytraceText + kRaytraceDisplays + kRtPrims + kPlaneScreens +
         kFeeds + kPlaneFx + kWraithWeaponSDF + kWraithSDF + kWraithStoreMaterial + kWraithMaterial + kWraithFx + kWraithCockpitCommon + kWraithCockpitSDF + kWraithCockpitMaterial +
         kGBuffer + kPropellerGLSL + kEffectsFS;
}
// the composite and the interface, each after the g-force lens they draw (g_lens.glsl: over the scene and the flight HUD)
inline std::string postFSAssembly() { return std::string("#version 330 core\n") + kGLens + kPostFS; }
inline std::string uiFSAssembly() { return std::string("#version 330 core\n") + kGLens + kUIFS; }
inline std::string propDiscFSAssembly() { return std::string("#version 330 core\n") + kPropellerGLSL + kPropDiscFS; }
inline std::string lightFSAssembly(const std::string& defines) {
  return std::string("#version 330 core\n") + defines + kCommonGLSL + kNoiseTex + kRtIO + kViewUniforms + kSceneUniforms + kRoads + kMaterialCommon + kLightCommon + kClouds + kGBuffer + kLightFS;
}

// Enemy-only builders: each program contains one hull field, never the player model table.
inline std::string enemyFieldAssembly(int type) {
  std::string s = kEnemyCommon;
  switch (type) {
    case 0: return s + kEnemyNeedle + "\nvec2 mapEnemySurface(vec3 p){return ccNeedle(p,vec4(0));}\n";
    case 1: return s + kEnemyBastion + "\nvec2 mapEnemySurface(vec3 p){return ccBastion(p,vec4(0));}\n";
    case 2: return s + kEnemyHeavyCommon + kEnemyCantor + "\nvec2 mapEnemySurface(vec3 p){return ccSupport(p,vec4(0));}\n";
    case 3: return s + kEnemyHeavyCommon + kEnemyArchon + "\nvec2 mapEnemySurface(vec3 p){return ccBoss(p,vec4(0));}\n";
    default: return "vec2 mapEnemySurface(vec3 p){return vec2(1e6,110.);}\n";
  }
}
inline std::string enemyMeshFSAssembly(int type) {
  return std::string("#version 330 core\n") + kCommonGLSL + kViewUniforms + kSceneUniforms +
    "struct Mat { vec3 alb; float rough; float metal; vec3 nrm; vec3 emit; };\nuniform sampler2DArray uAfShMap;\n" +
    enemyFieldAssembly(type) + kEnemyMaterial + kGBuffer + kGBWrite + kEnemyShadow + kEnemyMeshFS;
}
inline std::string enemyBakeFSAssembly(int type) {
  return std::string("#version 330 core\n") + enemyFieldAssembly(type) + R"GLSL(
uniform sampler2D uPoints; uniform int uNormals; out vec4 o;
void main(){
 vec3 p=texelFetch(uPoints,ivec2(gl_FragCoord.xy),0).xyz;
 if(uNormals==0){o=vec4(mapEnemySurface(p),1.,0.);return;}
 const float e=.001;
 vec3 n=vec3(mapEnemySurface(p+vec3(e,0,0)).x-mapEnemySurface(p-vec3(e,0,0)).x,
 mapEnemySurface(p+vec3(0,e,0)).x-mapEnemySurface(p-vec3(0,e,0)).x,
 mapEnemySurface(p+vec3(0,0,e)).x-mapEnemySurface(p-vec3(0,0,e)).x);
 o=vec4(normalize(n),0.);
})GLSL";
}

inline std::string releasedStoreFSAssembly(int kind=0) {
 const char* field=kind==1?"wrPenetratorPayload":kind==2?"wrEmpPayload":"wrPlasmaPayload";
 return std::string("#version 330 core\n#define AF_WRAITH\n#define AF_MESH\n") +
   kCommonGLSL+kViewUniforms+kSceneUniforms+kRoads+kPlaneCommon+
   "const int PT_WR_KINETIC=47,PT_WR_CHARGED=48,PT_WR_PENETRATOR=49,PT_WR_EMP=50;\n"+
   kWraithWeaponSDF+kMaterialCommon+kWraithStoreMaterial+
   "vec2 releasedStoreField(vec3 p){return "+field+"(p); }\nuniform sampler2DArray uAfShMap;\n"+
   kGBuffer+kGBWrite+kEnemyShadow+kReleasedStoreFS;
}
