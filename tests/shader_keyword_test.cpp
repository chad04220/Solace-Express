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

// NVIDIA's compiler failed on two forms that every other driver takes (v3.35.0 would not start: "fatal error C9999:
// Unhandled expr op assign/(182) in CreateDag" in the shadow proxy): struct variables declared together with
// initializers ("Pose M = f(), N = g();") and arrays declared in a list after other names ("float a = 1.0, b[3];").
// One struct to a declaration, local arrays on their own. Returns the number of such declarations.
static int scanDecl(const char* name, const std::string& src) {
  std::string s; s.reserve(src.size());   // the source without comments (newlines kept for the line numbers)
  for (size_t i = 0; i < src.size();) {
    if (src.compare(i, 2, "//") == 0) { while (i < src.size() && src[i] != '\n') i++; continue; }
    if (src.compare(i, 2, "/*") == 0) { i += 2; while (i + 1 < src.size() && src.compare(i, 2, "*/") != 0) { if (src[i] == '\n') s += '\n'; i++; } i += 2; continue; }
    s += src[i++];
  }
  std::set<std::string> structs;
  for (size_t at = s.find("struct "); at != std::string::npos; at = s.find("struct ", at + 7)) {
    size_t j = at + 7; while (j < s.size() && s[j] == ' ') j++;
    size_t k = j; while (k < s.size() && (isalnum((unsigned char)s[k]) || s[k] == '_')) k++;
    if (k > j) structs.insert(s.substr(j, k - j));
  }
  static const std::set<std::string> kTypes = {"float", "int", "uint", "bool", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "mat2", "mat3", "mat4"};
  int bad = 0, line = 1, depth = 0; size_t st = 0;
  for (size_t i = 0; i <= s.size(); i++) {
    char c = i < s.size() ? s[i] : ';';
    if (c == '(') depth++; else if (c == ')') depth--;
    if (depth == 0 && (c == ';' || c == '{' || c == '}')) {
      std::string t = s.substr(st, i - st); st = i + 1;
      size_t a = t.find_first_not_of(" \t\n"); int ln = line;
      for (size_t q = 0; q < a && a != std::string::npos; q++) if (t[q] == '\n') ln++;
      if (a != std::string::npos) {
        size_t b = a; while (b < t.size() && (isalnum((unsigned char)t[b]) || t[b] == '_')) b++;
        std::string ty = t.substr(a, b - a);
        bool isStruct = structs.count(ty) > 0;
        if (isStruct || kTypes.count(ty)) {
          int d = 0, decl = 0, inits = 0; bool arrLater = false, seenEq = false;
          for (size_t q = b; q < t.size(); q++) {
            char e = t[q];
            if (e == '(' || e == '[') { if (e == '[' && d == 0 && decl > 0 && !seenEq) arrLater = true; d++; }
            else if (e == ')' || e == ']') d--;
            else if (d == 0 && e == ',') { decl++; seenEq = false; }
            else if (d == 0 && e == '=' && q + 1 < t.size() && t[q + 1] != '=' && t[q - 1] != '=' && t[q - 1] != '<' && t[q - 1] != '>' && t[q - 1] != '!') { seenEq = true; inits++; }
          }
          if ((isStruct && decl > 0 && inits > 0) || arrLater) { printf("%s:%d: '%s' declared %s (one to a declaration: NVIDIA)\n", name, ln, ty.c_str(), arrLater ? "as an array in a list" : "with others and initialized"); bad++; }
        }
      }
    }
    if (i < s.size() && s[i] == '\n') line++;
  }
  return bad;
}

int main() {
  // the same assemblies as Renderer::compilePrograms and the raster passes: line numbers match the driver's error log
  std::string lib = worldLibAssembly("");
  std::string h = "#version 330 core\n";
  int bad = scan("scene_lib.frag", lib) + scan("map.frag", kMapMain) + scan("disp.frag", kDispMain);
  bad += scan("terrain.vert", terrainVSAssembly("")) + scan("terrain.frag", terrainFSAssembly("")) + scan("water.vert", waterVSAssembly("")) + scan("water.frag", waterFSAssembly("")) + scan("light.frag", lightFSAssembly("")) + scan("objects.frag", objectsFSAssembly("")) + scan("shadow_proxy.frag", shadowProxyFSAssembly("")) + scan("effects.frag", effectsFSAssembly("")) + scan("plane_mesh.vert", planeMeshVSAssembly("")) + scan("plane_mesh.frag", planeMeshFSAssembly("")) + scan("objects_light.frag", objectsFSAssembly("#define AF_LIGHT\n")) + scan("plane_mesh_light.frag", planeMeshFSAssembly("#define AF_LIGHT\n"));
  bad += scan("fullscreen.vert", kFullscreenVS) + scan("sprite.vert", kSpriteVS) + scan("sprite.frag", kSpriteFS);
  bad += scan("down.frag", kDownFS) + scan("up.frag", kUpFS) + scan("raymask.frag", kRayMaskFS) + scan("ray.frag", kRayFS);
  bad += scan("taa.frag", kTaaFS) + scan("post.frag", kPostFS);
  bad += scan("ui.vert", kUIVS) + scan("ui.frag", kUIFS);
  bad += scan("entity.vert", h + kEntVS) + scan("entity.frag", h + kEntFS1 + kEntFS2) + scan("entity_shadow.frag", h + kEntFS1 + kEntShadowFS);
  if (bad) { printf("FAIL: %d reserved word(s) used as identifiers\n", bad); return 1; }
  int forms = scanDecl("objects.frag", objectsFSAssembly("")) + scanDecl("shadow_proxy.frag", shadowProxyFSAssembly("")) + scanDecl("effects.frag", effectsFSAssembly(""))
            + scanDecl("plane_mesh.frag", planeMeshFSAssembly("")) + scanDecl("scene_lib.frag", lib) + scanDecl("light.frag", lightFSAssembly(""));
  if (forms) { printf("FAIL: %d declaration(s) NVIDIA's compiler refuses\n", forms); return 1; }
  printf("PASS: no reserved GLSL words in the shaders\n");
  return 0;
}
