//! kEntCommon
//! The scenery programs' class switches (both stages): ENT_CLASS n, one class's own build - 0 the trees and bushes, 1
//! the rocks, 2 the buildings, the airport's fittings and the vehicles (entities.h entClass) - each draw drawn with its
//! kind's (entity_render.cpp). The code only another class runs is cut from the source (shader_prune.h); without
//! ENT_CLASS, every class's.
#ifdef ENT_CLASS
#define ENT_TREES (ENT_CLASS == 0)
#define ENT_ROCKS (ENT_CLASS == 1)
#define ENT_BUILDINGS (ENT_CLASS == 2)
#else
#define ENT_TREES 1
#define ENT_ROCKS 1
#define ENT_BUILDINGS 1
#endif
