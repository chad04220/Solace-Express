// Air Xpress - procedural meshes for the environment entities (three levels of detail per kind)
#pragma once
#include "entities.h"

// mesh vertex: position, normal, (part, ambient occlusion, u, v)
struct EVert { float px, py, pz, nx, ny, nz, part, ao, u, v; };
// surface parts (see the entity G-buffer shader)
enum EntPart { P_BARK = 0, P_LEAF, P_FROND, P_NEEDLE, P_ROCK, P_WALL, P_ROOF, P_TRIM, P_GLASS, P_METAL, P_DOOR, P_BRICK,
               P_AWNING, P_WOOD, P_DARK, P_LAMP, P_SIGN, P_CANOPY, P_LEAFCARD, P_RLAMP };
static const int ENT_LODS = 3;
struct EntMeshRange { int first[ENT_LODS], count[ENT_LODS]; };
void buildEntityMeshes(std::vector<EVert>& out, EntMeshRange ranges[EK_COUNT]);
