// Solace Express - the bridges' meshes (bridges.h): each bridge built where it stands, in world coordinates, drawn by
// the buildings' programs as one instance at the origin (entity_render.cpp) and lit and shadowed like them
#pragma once
#include "entity_mesh.h"
#include "bridges.h"

// the deck's road surface, one part per road class (u: across the road from its centreline, v: along it, metres)
static const int P_DECK = 33;   // P_DECK + RoadClass (after the wheels, 27..32)

struct BridgeMeshRange { int first, count; };
// Every bridge's triangles appended to out (EVert, entity_mesh.h); ranges[i] is bridges[i]'s
void buildBridgeMeshes(const std::vector<Bridge>& bridges, std::vector<EVert>& out, std::vector<BridgeMeshRange>& ranges);
