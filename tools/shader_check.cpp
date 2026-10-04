// Writes the ray tracer and the programs built from it (as Renderer::compilePrograms assembles them) to files, so a
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
  for (int dbg = 0; dbg < 2; dbg++) {
    std::string rt = std::string("#version 330 core\n") + (dbg ? "#define HULL_DEBUG\n" : "") + kCommonGLSL + kRaytraceFS + kRaytraceFS2 + kRaytraceUfo + kRaytraceText +
                     kRaytraceDisplays + kRaytraceFS3 + kRaytraceWraith + kRaytraceWraithCockpit;
    put(dir, dbg ? "raytrace_debug.frag" : "raytrace.frag", rt);
    if (dbg) continue;
    std::string ms = rt; size_t m = ms.find("void main("); if (m != std::string::npos) ms.replace(m, 10, "void mainRT(");
    put(dir, "map.frag", ms + kMapMain);
    put(dir, "tshbake.frag", ms + kTShBakeMain);
    put(dir, "clouds.frag", ms + kCloudMain);
    put(dir, "hullbake.frag", ms + kHullBakeMain);
    put(dir, "displays.frag", ms + kDispMain);
  }
  return 0;
}
