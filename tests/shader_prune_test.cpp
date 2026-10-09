// shaderPrune::prune (src/shader_prune.h): what a program can run stays, byte for byte where it matters; what it
// can't goes; anything it can't read plainly is kept.
#include <cstdio>
#include <string>
#include "../src/shader_prune.h"

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
    check(has(o, "struct Mat") && has(o, "layout(location=0) out vec4 oColor;") && has(o, "const vec3 K") && has(o, "uniform float uA;"), "declarations kept", o);
    check(!has(o, "//") && !has(o, "/*"), "comments stripped", o);
    check(has(o, "void main()"), "main kept", o);
  }
  {   // a function a macro names is kept; one inside #ifdef'd text that names it is kept too
    const std::string src =
      "#version 330 core\n"
      "float viaMacro(){ return 1.0; }\n"
      "#define CALL viaMacro()\n"
      "float inIfdef(){ return 2.0; }\n"
      "#ifdef FEATURE\n"
      "float feature(){ return inIfdef(); }\n"
      "#endif\n"
      "out vec4 o;\n"
      "void main(){ o = vec4(CALL);\n"
      "#ifdef FEATURE\n"
      "  o.x += feature();\n"
      "#endif\n"
      "}\n";
    const std::string o = shaderPrune::prune(src);
    check(has(o, "float viaMacro()"), "macro-named function kept", o);
    check(has(o, "float feature()") && has(o, "float inIfdef()"), "functions named in conditional code kept", o);
    check(has(o, "#ifdef FEATURE") && has(o, "#endif"), "preprocessor lines kept", o);
  }
  {   // an unused function inside a conditional block goes; the directives round it stay balanced
    const std::string src =
      "#version 330 core\n"
      "#ifdef X\n"
      "float unusedX(){ return 1.0; }\n"
      "#else\n"
      "float unusedX(){ return 2.0; }\n"
      "#endif\n"
      "out vec4 o;\n"
      "void main(){ o = vec4(0.0); }\n";
    const std::string o = shaderPrune::prune(src);
    check(!has(o, "unusedX"), "unused function in both branches removed", o);
    check(has(o, "#ifdef X") && has(o, "#else") && has(o, "#endif"), "directives kept", o);
  }
  {   // a header a preprocessor line cuts through, or a body with an unbalanced conditional, is kept whole
    const std::string src =
      "#version 330 core\n"
      "float split(\n"
      "#ifdef X\n"
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
  {   // nothing to prune: the source comes back as it was, comments aside
    const std::string src = "#version 330 core\nout vec4 o;\nvoid main(){ o = vec4(1.0); }\n";
    check(shaderPrune::prune(src) == src, "untouched", shaderPrune::prune(src));
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
  return fails ? 1 : 0;
}
