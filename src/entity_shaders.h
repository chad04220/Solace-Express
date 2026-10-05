// Solace Express - environment entity shaders: instanced meshes rasterised into a G-buffer (lit later by the ray
// tracer, which also traces their shadows on the terrain) and into the sun's shadow cascades.
#pragma once
#include "shaders_gen.h"   // the sources: src/shaders/*.glsl, embedded at build time (tools/embed_shaders.cmake)
