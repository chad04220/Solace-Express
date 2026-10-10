// Writes the scene programs (as Renderer::compilePrograms and the raster passes assemble them) to files, so a
// GLSL validator can check them in seconds: g++ -std=c++17 -Isrc tools/shader_check.cpp -o shader_check &&
// ./shader_check outdir && glslangValidator outdir/*.frag
#include <cstdio>
#include <string>
#include <filesystem>
#include "../src/shaders.h"
#include "../src/shaders_wraith_cockpit.h"
#include "../src/shader_prune.h"
#include "../src/hangar_preview.h"
#include "../src/models.h"
#include "../src/aircraft.h"

// each program as assembled, and in <dir>/pruned as the renderer hands it to the driver (shader_prune.h)
static void put(const std::string& dir, const char* name, const std::string& src) {
  if (FILE* f = fopen((dir + "/" + name).c_str(), "w")) { fputs(src.c_str(), f); fclose(f); }
  if (FILE* f = fopen((dir + "/pruned/" + name).c_str(), "w")) { fputs(shaderPrune::prune(src).c_str(), f); fclose(f); }
}

int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : ".";
  { std::error_code ec; std::filesystem::create_directories(dir + "/pruned", ec); }
  put(dir, "hangar.vert", hangarPreview::kVS);
  put(dir, "hangar.frag", hangarPreview::kFS);
  put(dir, "hangar_classified.frag", hangarPreview::kClassifiedFS);
  // Environment coverage includes the alpha-cutout path shared by colour and shadow draws.
  put(dir, "entities.vert", entVSAssembly(""));
  put(dir, "entities.frag", entFSAssembly(""));
  put(dir, "entities_shadow.frag", entShadowFSAssembly(""));
  for (int c = 0; c < 3; c++) {   // (each class's own build: Renderer::compilePrograms)
    const std::string d = entClassDefines(c), n = std::to_string(c);
    put(dir, ("entities_c" + n + ".vert").c_str(), entVSAssembly(d));
    put(dir, ("entities_c" + n + ".frag").c_str(), entFSAssembly(d));
    put(dir, ("entities_shadow_c" + n + ".frag").c_str(), entShadowFSAssembly(d));
  }
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
    put(dir, "prop_disc.vert", kPropDiscVS); put(dir, "prop_disc.frag", propDiscFSAssembly());
    put(dir, "plane_mesh.vert", planeMeshVSAssembly(""));
    put(dir, "plane_mesh.frag", planeMeshFSAssembly(""));
    put(dir, "plane_mesh_depth.frag", planeMeshDepthFSAssembly());
    put(dir, "part_pose.frag", partPoseFSAssembly());
    // the light-aircraft builds (AF_LIGHT: Renderer::pickAfPrograms)
    put(dir, "objects_light.frag", objectsFSAssembly("#define AF_LIGHT\n"));
    put(dir, "shadow_proxy_light.frag", shadowProxyFSAssembly("#define AF_LIGHT\n"));
    put(dir, "effects_light.frag", effectsFSAssembly("#define AF_LIGHT\n"));
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
    // each aircraft's own builds (shaders.h aircraftDefines): its mesh pass and its bakes
    for (int m = 0; m <= kWraith; m++) {
      float M[96]; packModelOf(m, M);
      const std::string d = aircraftDefines(m, M), n = std::to_string(m);
      put(dir, ("plane_mesh_af" + n + ".frag").c_str(), planeMeshFSAssembly(d));
      put(dir, ("hullbake_af" + n + ".frag").c_str(), hullBakeFSAssembly(d));
      put(dir, ("hullbake_normals_af" + n + ".frag").c_str(), hullBakeFSAssembly(d + "#define HULL_BAKE_NORMALS\n"));
      if (m == kWraith) put(dir, "plane_mesh_af12_probe.frag", planeMeshFSAssembly(d + "#define PROBE_MESH_SHADE\n"));   // (the analysis's probe build)
    }
  }
  // the small programs (Renderer::compilePrograms), as assembled and as pruned
  put(dir, "fullscreen.vert", kFullscreenVS);
  put(dir, "ui.vert", kUIVS); put(dir, "ui.frag", kUIFS);
  put(dir, "sprite.vert", kSpriteVS); put(dir, "sprite.frag", kSpriteFS);
  put(dir, "bloom_down.frag", kDownFS); put(dir, "bloom_up.frag", kUpFS);
  put(dir, "ray_mask.frag", kRayMaskFS); put(dir, "rays.frag", kRayFS); put(dir, "feed_rays.frag", kFeedRaysFS);
  put(dir, "post.frag", kPostFS); put(dir, "taa.frag", kTaaFS);
  put(dir, "cloud_comp.frag", kCloudCompFS); put(dir, "cloud_acc.frag", kCloudAccFS);
  return 0;
}
