// Guards the GLSL sources against identifiers that later GLSL versions (or the reserved-word list) claim.
// The shaders are #version 330, where e.g. "patch" and "sample" are still legal names, but some Windows drivers
// apply the newer keyword list regardless and refuse to compile them.
#include <cctype>
#include <cstdio>
#include <set>
#include <string>
#include "../src/shaders.h"
#include "../src/shaders_wraith_cockpit.h"
#include "../src/entity_shaders.h"

static const std::set<std::string> kReserved = {
  // keywords added after GLSL 3.30
  "patch", "sample", "subroutine", "buffer", "shared", "coherent", "volatile", "restrict", "readonly", "writeonly",
  "precise", "atomic_uint", "dvec2", "dvec3", "dvec4", "dmat2", "dmat3", "dmat4",
  "image1D", "image2D", "image3D", "imageCube", "image2DRect", "image1DArray", "image2DArray", "imageBuffer",
  "iimage2D", "uimage2D", "samplerCubeArray", "isamplerCubeArray", "usamplerCubeArray",
  // reserved for future use
  "common", "partition", "active", "asm", "class", "union", "enum", "typedef", "template", "this", "resource",
  "goto", "inline", "noinline", "public", "static", "extern", "external", "interface", "long", "short", "half",
  "fixed", "unsigned", "superp", "input", "output", "hvec2", "hvec3", "hvec4", "fvec2", "fvec3", "fvec4",
  "sampler3DRect", "filter", "sizeof", "cast", "namespace", "using",
};

// Reports every reserved word used in the source (comments skipped); returns the number found.
static int scan(const char* name, const std::string& src) {
  int bad = 0, line = 1;
  for (size_t i = 0; i < src.size();) {
    char c = src[i];
    if (c == '\n') { line++; i++; continue; }
    if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') { while (i < src.size() && src[i] != '\n') i++; continue; }
    if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) { if (src[i] == '\n') line++; i++; }
      i += 2; continue;
    }
    if (isalpha((unsigned char)c) || c == '_') {
      size_t j = i;
      while (j < src.size() && (isalnum((unsigned char)src[j]) || src[j] == '_')) j++;
      std::string w = src.substr(i, j - i);
      if (kReserved.count(w)) { printf("%s:%d: reserved GLSL word '%s'\n", name, line, w.c_str()); bad++; }
      i = j; continue;
    }
    if (isdigit((unsigned char)c)) { while (i < src.size() && (isalnum((unsigned char)src[i]) || src[i] == '.')) i++; continue; }
    i++;
  }
  return bad;
}

int main() {
  // same assembly as Renderer::init: line numbers match the driver's error log
  std::string rt = rtAssembly("");
  std::string h = "#version 330 core\n";
  int bad = scan("raytrace.frag", rt) + scan("map.frag", kMapMain) + scan("disp.frag", kDispMain);
  bad += scan("terrain.vert", terrainVSAssembly("")) + scan("terrain.frag", terrainFSAssembly("")) + scan("water.vert", waterVSAssembly("")) + scan("water.frag", waterFSAssembly("")) + scan("light.frag", lightFSAssembly("")) + scan("objects.frag", objectsFSAssembly("")) + scan("shadow_proxy.frag", shadowProxyFSAssembly("")) + scan("effects.frag", effectsFSAssembly("")) + scan("plane_mesh.vert", planeMeshVSAssembly("")) + scan("plane_mesh.frag", planeMeshFSAssembly(""));
  bad += scan("fullscreen.vert", kFullscreenVS) + scan("sprite.vert", kSpriteVS) + scan("sprite.frag", kSpriteFS);
  bad += scan("down.frag", kDownFS) + scan("up.frag", kUpFS) + scan("raymask.frag", kRayMaskFS) + scan("ray.frag", kRayFS);
  bad += scan("taa.frag", kTaaFS) + scan("post.frag", kPostFS);
  bad += scan("ui.vert", kUIVS) + scan("ui.frag", kUIFS);
  bad += scan("entity.vert", h + kEntVS) + scan("entity.frag", h + kEntFS1 + kEntFS2) + scan("entity_shadow.frag", h + kEntFS1 + kEntShadowFS);
  if (bad) { printf("FAIL: %d reserved word(s) used as identifiers\n", bad); return 1; }
  printf("PASS: no reserved GLSL words in the shaders\n");
  return 0;
}
