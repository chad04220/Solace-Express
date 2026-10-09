// Writes the scene programs (as Renderer::compilePrograms and the raster passes assemble them) to files, so a
// GLSL validator can check them in seconds: g++ -std=c++17 -Isrc tools/shader_check.cpp -o shader_check &&
// ./shader_check outdir && glslangValidator outdir/*.frag
#include <cstdio>
#include <string>
#include "../src/shaders.h"
#include "../src/shaders_wraith_cockpit.h"

static void put(const std::string& dir, const char* name, const std::string& src) {
  FILE* f = fopen((dir + "/" + name).c_str(), "w"); if (!f) return;
  fputs(src.c_str(), f); fclose(f);
}

int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : ".";
  // Environment coverage includes the alpha-cutout path shared by colour and shadow draws.
  put(dir, "entities.vert", std::string("#version 330 core\n") + kEntVS);
  put(dir, "entities.frag", std::string("#version 330 core\n") + kEntFS1 + kEntFS2);
  put(dir, "entities_shadow.frag", std::string("#version 330 core\n") + kEntFS1 + kEntShadowFS);
  {
    std::string ms = worldLibAssembly("");
    put(dir, "map.frag", ms + kMapMain);
    put(dir, "tshbake.frag", ms + kTShBakeMain);
    put(dir, "clouds.frag", ms + kCloudMain);
    put(dir, "hullbake.frag", worldLibAssembly("#define PART_BAKE\n") + kHullBakeMain);
    put(dir, "hullbake_normals.frag", worldLibAssembly("#define PART_BAKE\n#define HULL_BAKE_NORMALS\n") + kHullBakeMain);
    put(dir, "displays.frag", ms + kDispMain);
    put(dir, "terrain.vert", terrainVSAssembly("")); put(dir, "terrain.frag", terrainFSAssembly(""));
    put(dir, "water.vert", waterVSAssembly("")); put(dir, "water.frag", waterFSAssembly(""));
    put(dir, "light.frag", lightFSAssembly(""));
    put(dir, "objects.frag", objectsFSAssembly(""));
    put(dir, "shadow_proxy.frag", shadowProxyFSAssembly(""));
    put(dir, "effects.frag", effectsFSAssembly(""));
    put(dir, "prop_disc.vert", kPropDiscVS); put(dir, "prop_disc.frag", kPropDiscFS);
    put(dir, "plane_mesh.vert", planeMeshVSAssembly(""));
    put(dir, "plane_mesh.frag", planeMeshFSAssembly(""));
    put(dir, "plane_mesh_depth.frag", planeMeshDepthFSAssembly());
    put(dir, "part_pose.frag", partPoseFSAssembly());
    // the light-aircraft builds (AF_LIGHT: Renderer::pickAfPrograms)
    put(dir, "objects_light.frag", objectsFSAssembly("#define AF_LIGHT\n"));
    put(dir, "shadow_proxy_light.frag", shadowProxyFSAssembly("#define AF_LIGHT\n"));
    put(dir, "effects_light.frag", effectsFSAssembly("#define AF_LIGHT\n"));
    put(dir, "plane_mesh_light.frag", planeMeshFSAssembly("#define AF_LIGHT\n"));
    // each research jet's own mesh build (Renderer::compilePlaneMesh)
    put(dir, "plane_mesh_jet.frag", planeMeshFSAssembly("#define AF_JET\n"));
    put(dir, "plane_mesh_wraith.frag", planeMeshFSAssembly("#define AF_WRAITH\n"));
    put(dir, "objects_noaf.frag", objectsFSAssembly("#define AF_LIGHT\n#define OBJ_NO_AF\n"));
    put(dir, "shadow_proxy_maps.frag", shadowProxyFSAssembly("#define AF_LIGHT\n#define PROXY_MAPS_ONLY\n"));
    // the reduced builds for a driver whose compiler fails on the whole (raster_renderer.cpp compileRaster)
    put(dir, "shadow_proxy_less.frag", shadowProxyFSAssembly("#define PROXY_NO_TRAFFIC\n#define PROXY_NO_LIGHTS\n"));
    put(dir, "effects_less.frag", effectsFSAssembly("#define FX_NO_CLOAK\n#define FX_NO_PLUMES\n"));
    // and the builds without the retractable gear in the airframe's field (renderer.cpp linkProgramCached: NV_SAFE_GEAR)
    put(dir, "shadow_proxy_safegear.frag", shadowProxyFSAssembly("#define NV_SAFE_GEAR\n"));
    put(dir, "effects_safegear.frag", effectsFSAssembly("#define NV_SAFE_GEAR\n"));
    put(dir, "plane_mesh_safegear.frag", planeMeshFSAssembly("#define NV_SAFE_GEAR\n"));
    put(dir, "hullbake_safegear.frag", worldLibAssembly("#define NV_SAFE_GEAR\n#define PART_BAKE\n") + kHullBakeMain);
    put(dir, "hullbake_normals_safegear.frag", worldLibAssembly("#define NV_SAFE_GEAR\n#define PART_BAKE\n#define HULL_BAKE_NORMALS\n") + kHullBakeMain);
  }
  return 0;
}
