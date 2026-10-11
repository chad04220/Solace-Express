// Solace Express - the road network's bridges (Phase A item 5): a structure under every span the network flags as a
// bridge (road_network.h RoadPath::bridge - over water, or the road standing well clear of the ground), its deck the
// road's own graded centreline, carried on piers down to the ground or the sea floor between abutments at its ends.
// Derived from the road network whenever the world is built or loaded (World::bridges): nothing of it is cached. The
// renderer draws them (bridge_mesh.h), the scenery collision makes them solid (Scenery::collide / raycast).
#pragma once
#include "road_network.h"
#include <vector>

class World;

struct BridgePier {
  float x, z;          // centre
  float top, bottom;   // its cap (the girders' underside) and its foot (the ground, or the sea floor and a little into it)
  float ux, uz;        // the deck's direction over it (unit, horizontal): the pier's thickness is along it
  float halfWidth;     // across the deck (m)
  float halfThick;     // along the deck (m)
};

struct Bridge {
  int path = -1, first = 0, last = 0;   // the road and its points first..last (RoadPath::pts) the span runs between
  RoadClass cls = RC_ROAD;
  bool water = false;                   // over the sea (else a viaduct over a valley or a hillside)
  std::vector<RoadPoint> deck;          // the centreline, the abutments' ends included: the road's own points and heights
  std::vector<float> along;             // the distance along the road at each deck point (m, from the path's start)
  float halfDeck = 0;                   // the deck's half width, parapets included (m)
  float depth = 0;                      // the structure's depth under the road surface (slab and girders, m)
  float length = 0;                     // between the abutments (m)
  std::vector<BridgePier> piers;
  float minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;   // the bounds of all of it
};

// Every bridge span of the network, in path order. Deterministic for a given world.
std::vector<Bridge> buildBridges(const World& world);

// The parapet's height above the road surface (m): a concrete barrier on the highways and roads, a steel rail on the
// lanes and tracks
inline float bridgeParapet(int cls) { return cls <= RC_ROAD ? 1.05f : 1.1f; }

// Whether (x, z) lies within r of a bridge's deck (under its footprint), and the girders' underside there (the lowest,
// should two overlap): the scenery keeps what stands there below it (Scenery::generate)
bool bridgeOver(const std::vector<Bridge>& bridges, float x, float z, float r, float* underside = nullptr);

// Collision (the scenery's, entities.h Scenery::collide / raycast): whether a sphere touches any bridge's deck,
// parapets, piers or abutments; the first bridge a segment (a, unit d, length L) meets: its distance, or -1
bool bridgeCollide(const std::vector<Bridge>& bridges, vec3 p, float r);
float bridgeRaycast(const std::vector<Bridge>& bridges, vec3 a, vec3 d, float L);
