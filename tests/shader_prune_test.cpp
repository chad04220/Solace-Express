// shaderPrune::prune (src/shader_prune.h): what a program can run stays, byte for byte where it matters; what it
// can't goes; anything it can't read plainly is kept.
#include <cstdio>
#include <string>
#include <cstring>
#include <cstdint>
#include "../src/shader_prune.h"
#include "../src/shaders.h"

static int fails = 0;
static bool has(const std::string& s, const std::string& t) { return s.find(t) != std::string::npos; }
static void check(bool ok, const char* what, const std::string& out) {
  if (!ok) { fails++; printf("FAIL: %s\n----\n%s\n----\n", what, out.c_str()); }
}

int main() {
  {   // unused functions go, with what only they called; used ones, their callees and overloads stay
    const std::string src =
      "#version 330 core\n"
      "uniform float uA;   // a uniform\n"
      "struct Mat { vec3 alb; float rough; };\n"
      "layout(location=0) out vec4 oColor;\n"
      "const vec3 K = vec3(1.0, 2.0, 3.0);\n"
      "float helper(float x){ return x*2.0; }\n"
      "float helper(vec2 x){ return x.x; }\n"
      "float onlyDead(float x){ return x + 1.0; }\n"
      "float dead(float x){ return onlyDead(x); }   /* never called */\n"
      "float proto(float x);\n"
      "float used(float x){ return helper(x) + proto(x); }\n"
      "float proto(float x){ return x; }\n"
      "Mat makeMat(){ Mat m; m.alb = K; m.rough = uA; return m; }\n"
      "void main(){ Mat m = makeMat(); oColor = vec4(m.alb*used(1.0), 1.0); }\n";
    const std::string o = shaderPrune::prune(src);
    check(o.compare(0, 18, "#version 330 core\n") == 0, "#version stays first", o);
    check(!has(o, "dead(") && !has(o, "onlyDead"), "dead chain removed", o);
    check(has(o, "float helper(float x)") && has(o, "float helper(vec2 x)"), "both overloads of a used name kept", o);
    check(has(o, "float proto(float x);") && has(o, "float proto(float x){"), "prototype and definition of a used function kept", o);
    check(has(o, "struct Mat") && has(o, "layout(location=0)out vec4 oColor;") && has(o, "const vec3 K") && has(o, "uniform float uA;"), "declarations kept", o);
    check(!has(o, "//") && !has(o, "/*"), "comments stripped", o);
    check(has(o, "void main()"), "main kept", o);
  }
  {   // a function a macro names is kept; one inside conditional text the program can't settle that names it, too
    const std::string src =
      "#version 330 core\n"
      "float viaMacro(){ return 1.0; }\n"
      "#define CALL viaMacro()\n"
      "float inIfdef(){ return 2.0; }\n"
      "#ifdef GL_ARB_FEATURE\n"
      "float feature(){ return inIfdef(); }\n"
      "#endif\n"
      "out vec4 o;\n"
      "void main(){ o = vec4(CALL);\n"
      "#ifdef GL_ARB_FEATURE\n"
      "  o.x += feature();\n"
      "#endif\n"
      "}\n";
    const std::string o = shaderPrune::prune(src);
    check(has(o, "float viaMacro()"), "macro-named function kept", o);
    check(has(o, "float feature()") && has(o, "float inIfdef()"), "functions named in unsettled conditional code kept", o);
    check(has(o, "#ifdef GL_ARB_FEATURE") && has(o, "#endif"), "unsettled preprocessor lines kept (the driver's GL_ names)", o);
  }
  {   // the program's own switches settle its conditionals: the dead branch goes, with what only it called
    const std::string src =
      "#version 330 core\n"
      "#define AF_MODEL 7\n"
      "#define LEVEL (AF_MODEL*2 + 1)\n"
      "float swift(){ return 7.0; }\n"
      "float kestrel(){ return 0.0; }\n"
      "float bake(){ return 3.0; }\n"
      "out vec4 o;\n"
      "void main(){ o = vec4(0.0);\n"
      "#if AF_MODEL == 7\n"
      "  o.x += swift();\n"
      "#elif AF_MODEL == 0\n"
      "  o.x += kestrel();\n"
      "#endif\n"
      "  #  ifdef PART_BAKE\n"
      "  o.y += bake();\n"
      "  #endif\n"
      "#if defined(AF_MODEL) && LEVEL >= 15 && !defined(PART_BAKE)\n"
      "  o.z = 1.0;\n"
      "#else\n"
      "  o.z = 2.0;\n"
      "#endif\n"
      "#ifndef AF_MODEL\n"
      "  o.w = kestrel();\n"
      "#endif\n"
      "}\n";
    const std::string o = shaderPrune::prune(src);
    check(has(o, "o.x+=swift();") && has(o, "float swift()"), "the live branch kept", o);
    check(!has(o, "kestrel") && !has(o, "bake"), "dead branches and the functions only they called removed", o);
    check(has(o, "o.z=1.0;") && !has(o, "o.z=2.0;"), "arithmetic, defined() and nested macros evaluated", o);
    check(!has(o, "#if") && !has(o, "#elif") && !has(o, "#else") && !has(o, "#endif"), "settled directives removed", o);
    check(has(o, "#define AF_MODEL 7"), "#defines kept", o);
  }
  {   // a group it can't settle is written out; its branches' own groups are still settled; a macro they define
      // differently is unknown after it; a later #elif that can't be settled reopens the group as an #if
    const std::string src =
      "#version 330 core\n"
      "#define A 1\n"
      "out vec4 o;\n"
      "void main(){ o = vec4(0.0);\n"
      "#ifdef GL_EXT_thing\n"
      "#define B 1\n"
      "#if A\n"
      "  o.x = 1.0;\n"
      "#else\n"
      "  o.x = 2.0;\n"
      "#endif\n"
      "#else\n"
      "#define B 2\n"
      "#endif\n"
      "#if B == 1\n"
      "  o.y = 1.0;\n"
      "#endif\n"
      "#if A == 0\n"
      "  o.z = 1.0;\n"
      "#elif defined(GL_EXT_other)\n"
      "  o.z = 2.0;\n"
      "#elif A == 1\n"
      "  o.z = 3.0;\n"
      "#else\n"
      "  o.z = 4.0;\n"
      "#endif\n"
      "#if UNDEFINED_NAME\n"
      "  o.w = 1.0;\n"
      "#endif\n"
      "}\n";
    const std::string o = shaderPrune::prune(src);
    check(has(o, "#ifdef GL_EXT_thing") && has(o, "#define B 1") && has(o, "#define B 2"), "unsettled group written out", o);
    check(has(o, "o.x=1.0;") && !has(o, "o.x=2.0;"), "a settled group inside an unsettled one cut", o);
    check(has(o, "#if B == 1") && has(o, "o.y=1.0;"), "a macro defined differently by its branches is unknown after it", o);
    check(!has(o, "o.z=1.0;") && has(o, "#if defined(GL_EXT_other)") && has(o, "o.z=2.0;") && has(o, "#elif A == 1") && has(o, "o.z=3.0;") && !has(o, "o.z=4.0;"),
          "decided-false branches dropped, an undecided one reopens the group, a decided-true one ends it", o);
    check(has(o, "#if UNDEFINED_NAME") && has(o, "o.w=1.0;"), "an undefined name in #if is left for the driver", o);
  }
  {   // an unused function inside a conditional block goes; the directives round it stay balanced
    const std::string src =
      "#version 330 core\n"
      "#ifdef GL_X\n"
      "float unusedX(){ return 1.0; }\n"
      "#else\n"
      "float unusedX(){ return 2.0; }\n"
      "#endif\n"
      "out vec4 o;\n"
      "void main(){ o = vec4(0.0); }\n";
    const std::string o = shaderPrune::prune(src);
    check(!has(o, "unusedX"), "unused function in both branches removed", o);
    check(has(o, "#ifdef GL_X") && has(o, "#else") && has(o, "#endif"), "directives kept", o);
  }
  {   // a header a preprocessor line cuts through, or a body with an unbalanced conditional, is kept whole
    const std::string src =
      "#version 330 core\n"
      "float split(\n"
      "#ifdef GL_X\n"
      "  float a\n"
      "#else\n"
      "  int a\n"
      "#endif\n"
      "){ return 1.0; }\n"
      "out vec4 o;\n"
      "void main(){ o = vec4(0.0); }\n";
    const std::string o = shaderPrune::prune(src);
    check(has(o, "float split("), "straddling declaration kept", o);
  }
  {   // nothing to prune, and in the canonical layout: the source comes back as it was
    const std::string src = "#version 330 core\nout vec4 o;\nvoid main(){o=vec4(1.0);}\n";
    check(shaderPrune::prune(src) == src, "untouched", shaderPrune::prune(src));
  }
  {   // the layout is canonical: re-indenting, re-aligning or spacing code out changes nothing the driver gets
    const std::string a =
      "#version 330 core\n"
      "#define K(x) (x*2.0)\n"
      "#define SUM 1.0 + \\\n"
      "  2.0\n"
      "out vec4 o;\n"
      "float f(float x){return x*.5 + 1. + x .x - -x;}\n"
      "void main(){ o = vec4(K(1.0) - -SUM + f(2.0)); }\n";
    const std::string b =
      "#version   330 core\n"
      "  #define   K(x)  (x*2.0)\n"
      "#define SUM 1.0 +   \\\n"
      "        2.0   \n"
      "\n\n"
      "out    vec4 o;\t\n"
      "float f( float x ) { return x * .5+1.+x.x- - x; }\n"
      "\n    void   main ( ) {o=vec4( K( 1.0 )   -   -SUM+f(2.0)) ;}\n";
    const std::string oa = shaderPrune::prune(a), ob = shaderPrune::prune(b);
    check(oa == ob, "only the layout differs: the same program", oa + "----\n" + ob);
    check(oa.compare(0, 18, "#version 330 core\n") == 0 && has(oa, "#define K(x) (x*2.0)") && has(oa, "#define SUM 1.0 + \\\n 2.0"), "preprocessor lines keep their spaces", oa);
    check(has(oa, "x*.5+1.+x.x- -x") && has(oa, "float f(float x)") && !has(oa, "\n\n"), "the spaces two tokens need, and only those", oa);
    check(shaderPrune::prune(oa) == oa, "canonical: pruned twice, the same", oa);
  }
  {   // a line a '\' continues keeps the space its leading blanks made, and an empty one is kept: each splices as before
    const std::string src =
      "#version 330 core\n"
      "#define Y a\\\n"
      "    b\n"
      "#define F\\\n"
      "  (x)\n"
      "#define X 1 \\\n"
      "   \n"
      "float y;\n"
      "out vec4 o;\n"
      "void main(){ o = vec4(y); }\n";
    const std::string o = shaderPrune::prune(src);
    check(has(o, "#define Y a\\\n b\n"), "a continued line's first token stays apart from the last before", o);
    check(has(o, "#define F\\\n (x)\n"), "an object-like macro whose body opens with '(' stays object-like", o);
    check(has(o, "#define X 1 \\\n\nfloat y;"), "an empty continued line still ends the directive", o);
    check(shaderPrune::prune(o) == o, "canonical with continuations: pruned twice, the same", o);
  }
  {   // a name that is also a variable elsewhere is kept (conservative)
    const std::string src =
      "#version 330 core\n"
      "float t(){ return 1.0; }\n"
      "out vec4 o;\n"
      "void main(){ float t = 2.0; o = vec4(t); }\n";
    check(has(shaderPrune::prune(src), "float t()"), "shadowed name kept", shaderPrune::prune(src));
  }
  printf("%s\n", fails ? "shader_prune: FAILED" : "shader_prune: all checks passed");
  {   // the aircraft bodies' cache key (meshCacheStamp) hashes their bake programs as pruned: those must not change
      // unless an aircraft's shape does. A world-only edit - a top-level declaration in the shared library, the road
      // chunk - would rebuild every player's bodies on their next launch (v3.44 did, for one unused constant). The
      // fixture holds the hash of the shared bake's four builds; it changes on purpose only, with an aircraft edit.
    uint64_t h = 1469598103934665603ull;
    for (const char* d : {"", "#define AF_OUTSIDE\n"}) for (const char* n : {"", "#define HULL_BAKE_NORMALS\n"}) {
      const std::string o = shaderPrune::prune(hullBakeFSAssembly(std::string(d) + n));
      for (unsigned char c : o) h = (h ^ c) * 1099511628211ull;
    }
    char got[24]; snprintf(got, sizeof got, "%016llx", (unsigned long long)h);
    std::string want;
    if (FILE* f = fopen(SOLACE_SOURCE_DIR "/tests/fixtures/aircraft_bake.fnv", "r")) { char b[64] = {}; if (fscanf(f, "%63s", b) == 1) want = b; fclose(f); }
    if (want != got) printf("aircraft bake hash %s, fixture %s\n", got, want.c_str());
    check(want == got, "the aircraft bodies' bake is unchanged (tests/fixtures/aircraft_bake.fnv: update it only with an aircraft edit)", "");
  }
  // New loadout geometry must not invalidate the existing non-Wraith family bakes.
  for (const auto& family : {std::pair<const char*,uint64_t>{"AF_LIGHT",0xc9df3f37c31b4da2ull}, {"AF_JET",0x4c0d9343f2d600b6ull}}) {
    uint64_t h=1469598103934665603ull;
    for(const char* n:{"","#define HULL_BAKE_NORMALS\n"})
      for(unsigned char c:shaderPrune::prune(hullBakeFSAssembly(std::string("#define ")+family.first+"\n"+n)))h=(h^c)*1099511628211ull;
    check(h==family.second,"non-Wraith family bake identity preserved",family.first);
  }
  for(int kind=0;kind<3;kind++) {
    const std::string s=shaderPrune::prune(releasedStoreFSAssembly(kind));
    check(s.size()<14000 && !has(s,"mapWraith(") && !has(s,"wrKineticCartridge(") && !has(s,"wrChargedCartridge("),
      "released hardware program contains one payload, no aircraft or gun fields",s.substr(0,100));
    check(has(s,"shadeWraithStore(") && has(s,"releasedStoreField("),"released store shares detailed field and exact bay materials","");
  }
  return fails ? 1 : 0;
}
