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
    put(dir, "hullbake.frag", ms + kHullBakeMain);
    put(dir, "displays.frag", ms + kDispMain);
    put(dir, "terrain.vert", terrainVSAssembly("")); put(dir, "terrain.frag", terrainFSAssembly(""));
    put(dir, "water.vert", waterVSAssembly("")); put(dir, "water.frag", waterFSAssembly(""));
    put(dir, "light.frag", lightFSAssembly(""));
    put(dir, "objects.frag", objectsFSAssembly(""));
    put(dir, "shadow_proxy.frag", shadowProxyFSAssembly(""));
    put(dir, "effects.frag", effectsFSAssembly(""));
    put(dir, "plane_mesh.vert", planeMeshVSAssembly(""));
    put(dir, "plane_mesh.frag", planeMeshFSAssembly(""));
    put(dir, "plane_mesh_fine.frag", planeMeshFSAssembly("#extension GL_ARB_conservative_depth : enable\n#define MESH_REFINE\n"));
    put(dir, "part_pose.frag", partPoseFSAssembly());
  }
  return 0;
}
