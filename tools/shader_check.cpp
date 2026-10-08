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
  {
    std::string ms = worldLibAssembly("");
    put(dir, "map.frag", ms + kMapMain);
    put(dir, "tshbake.frag", ms + kTShBakeMain);
    put(dir, "clouds.frag", ms + kCloudMain);
    put(dir, "hullbake.frag", worldLibAssembly("#define PART_BAKE\n") + kHullBakeMain);
    put(dir, "displays.frag", ms + kDispMain);
    put(dir, "terrain.vert", terrainVSAssembly("")); put(dir, "terrain.frag", terrainFSAssembly(""));
    put(dir, "water.vert", waterVSAssembly("")); put(dir, "water.frag", waterFSAssembly(""));
    put(dir, "light.frag", lightFSAssembly(""));
    put(dir, "objects.frag", objectsFSAssembly(""));
    put(dir, "shadow_proxy.frag", shadowProxyFSAssembly(""));
    put(dir, "effects.frag", effectsFSAssembly(""));
    put(dir, "plane_mesh.vert", planeMeshVSAssembly(""));
    put(dir, "plane_mesh.frag", planeMeshFSAssembly(""));
    put(dir, "plane_mesh_depth.frag", planeMeshDepthFSAssembly());
    put(dir, "part_pose.frag", partPoseFSAssembly());
    // the light-aircraft builds (AF_LIGHT: Renderer::pickAfPrograms)
    put(dir, "objects_light.frag", objectsFSAssembly("#define AF_LIGHT\n"));
    put(dir, "shadow_proxy_light.frag", shadowProxyFSAssembly("#define AF_LIGHT\n"));
    put(dir, "effects_light.frag", effectsFSAssembly("#define AF_LIGHT\n"));
    put(dir, "plane_mesh_light.frag", planeMeshFSAssembly("#define AF_LIGHT\n"));
    put(dir, "objects_noaf.frag", objectsFSAssembly("#define AF_LIGHT\n#define OBJ_NO_AF\n"));
    put(dir, "shadow_proxy_maps.frag", shadowProxyFSAssembly("#define AF_LIGHT\n#define PROXY_MAPS_ONLY\n"));
    // the reduced builds for a driver whose compiler fails on the whole (raster_renderer.cpp compileRaster)
    put(dir, "shadow_proxy_less.frag", shadowProxyFSAssembly("#define PROXY_NO_TRAFFIC\n#define PROXY_NO_LIGHTS\n"));
    put(dir, "effects_less.frag", effectsFSAssembly("#define FX_NO_CLOAK\n#define FX_NO_PLUMES\n"));
    // and a build with NVIDIA's compiler options (renderer.cpp linkProgramCached), which other compilers must ignore
    { std::string fs = effectsFSAssembly(""); size_t at = fs.find('\n') + 1;
      put(dir, "effects_nvopt.frag", fs.substr(0, at) + "#pragma optionNV(inline all)\n#pragma optionNV(ifcvt none)\n#pragma optionNV(unroll none)\n" + fs.substr(at)); }
  }
  return 0;
}
