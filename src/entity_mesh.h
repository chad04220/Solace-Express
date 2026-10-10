// Solace Express - procedural meshes for the environment entities (four levels of detail per kind)
#pragma once
#include "entities.h"

// mesh vertex: position, normal, (part, ambient occlusion, u, v)
struct EVert { float px, py, pz, nx, ny, nz, part, ao, u, v; };
// surface parts (see the entity G-buffer shader)
enum EntPart { P_BARK = 0, P_LEAF, P_FROND, P_NEEDLE, P_ROCK, P_WALL, P_ROOF, P_TRIM, P_GLASS, P_METAL, P_DOOR, P_BRICK,
               P_AWNING, P_WOOD, P_DARK, P_LAMP, P_SIGN, P_CANOPY, P_LEAFCARD, P_RLAMP, P_PAPI,
               P_PAINT, P_STRIPE, P_SOCK, P_BEACON, P_FENCE, P_OBST, P_WHEEL0, P_WHEEL1, P_WHEEL2, P_WHEEL3, P_WHEEL4, P_WHEEL5 };
// Slots 0/1/2 retain established near/mid/far tiers; 3 is close inspection detail.
static const int ENT_LODS = 4;
struct EntMeshRange { int first[ENT_LODS], count[ENT_LODS]; };
void buildEntityMeshes(std::vector<EVert>& out, EntMeshRange ranges[EK_COUNT]);
