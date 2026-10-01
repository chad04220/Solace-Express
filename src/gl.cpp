#include "gl.h"
namespace glf {
#define GL_DEFINE(ret, name, args) PFN_##name name = nullptr;
GL_FUNCS(GL_DEFINE)
#undef GL_DEFINE
}
bool glLoad(void* (*getProc)(const char*), const char** missing) {
#define GL_LOAD(ret, name, args) glf::name = (glf::PFN_##name)getProc(#name); if (!glf::name) { if (missing) *missing = #name; return false; }
  GL_FUNCS(GL_LOAD)
#undef GL_LOAD
  return true;
}
