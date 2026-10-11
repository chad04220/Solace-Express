// Solace Express - the road network's bridges (Phase A item 5): a structure under every span the network flags as a
// bridge (road_network.h RoadPath::bridge - over water, or the road standing well clear of the ground), its deck the
// road's own graded centreline, carried on piers down to the ground or the sea floor between abutments at its ends.
// A long crossing of the water takes its deepest straight stretch in one main span on two heavier piers: a steel
// through truss on a highway (two spans, continuous over a pier between), a tied arch on a road. A viaduct carrying a
// mountain lane is masonry: an arch between each pair of supports, the spandrels solid up to the road.
// Derived from the road network whenever the world is built or loaded (World::bridges): nothing of it is cached. The
// renderer draws them (bridge_mesh.h), the scenery collision makes them solid (Scenery::collide / raycast).
#pragma once
#include "road_network.h"
#include <algorithm>
#include <cmath>
#include <vector>

class World;

struct BridgePier {
  float x, z;          // centre
  float s;             // its distance along the road (m)
  float top, bottom;   // its cap (the girders' underside) and its foot (the ground, or the sea floor and a little into it)
  float ux, uz;        // the deck's direction over it (unit, horizontal): the pier's thickness is along it
  float halfWidth;     // across the deck (m)
  float halfThick;     // along the deck (m)
  bool main = false;   // a main span's (heavier)
};

enum BridgeMain : uint8_t { BM_NONE = 0, BM_TRUSS, BM_ARCH };
// a masonry arch between two supports (distances along the road, m): its intrados springs `rise` under its crown,
// which is kBridgeCrown under the deck's underside
struct BridgeArch { float s0, s1, rise; };
static const float kBridgeCrown = 0.15f;
// a steel member of a main span's structure: a box section from a to b, its half width (level, across it) and half
// depth; colour 0 a truss's grey-green, 1 an arch's white
struct BridgeMember { vec3 a, b; float hw, hh; int colour; };

struct Bridge {
  int path = -1, first = 0, last = 0;   // the road and its points first..last (RoadPath::pts) the span runs between
  RoadClass cls = RC_ROAD;
  bool water = false;                   // over the sea (else a viaduct over a valley or a hillside)
  std::vector<RoadPoint> deck;          // the centreline, the abutments' ends included: the road's own points and heights
  std::vector<float> along;             // the distance along the road at each deck point (m, from the path's start)
  float halfDeck = 0;                   // the deck's half width, parapets included: the road's platform's (m)
  float inX = 0, inZ = 0, outX = 0, outZ = 0;   // the road's direction onto its first point and off its last (unit,
                                                // level; zero where the road starts or ends on it): its ends square to
                                                // the road there, as the road's graded bed ends square to it (roadGrade)
  float depth = 0;                      // the structure's depth under the road surface (slab and girders, m)
  float length = 0;                     // between the abutments (m)
  std::vector<BridgePier> piers;
  BridgeMain main = BM_NONE;            // a main span: its kind, its ends (distances along the road), its structure's
  float main0 = 0, main1 = 0, mainRise = 0;   // height over the road (a truss's) or rise (an arch's crown over the tie)
  bool stone = false;                   // a masonry viaduct, its arches
  std::vector<BridgeArch> arches;
  float minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;   // the bounds of all of it
};

// Every bridge span of the network, in path order. Deterministic for a given world.
std::vector<Bridge> buildBridges(const World& world);

// The deck at distance s along the road (clamped to the bridge): its centreline point (at the road surface), the
// road's level direction and the level direction across it (to the right of the traffic going along it)
void bridgeFrame(const Bridge& b, float s, vec3& centre, vec3& dir, vec3& across);
// The underside of a masonry viaduct's arch at distance s along the road (+1e9 where no arch is)
float bridgeIntrados(const Bridge& b, float s);
// A main span's steel members, in order (the mesh draws them, the collision is them)
template <class F> void forEachBridgeMember(const Bridge& b, F&& f);

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

// ---- the main spans' structure
template <class F> void forEachBridgeMember(const Bridge& b, F&& f) {
  if (b.main == BM_NONE) return;
  const float x = b.halfDeck + 0.4f;   // (outside the deck's edges: the road between them clear)
  auto at = [&](float s, float side, float y) { vec3 c, d, a; bridgeFrame(b, s, c, d, a); return c + a * (side * x) + vec3(0, y, 0); };
  if (b.main == BM_TRUSS) {   // a Warren truss with verticals over both spans, continuous over the pier between them
    const float T = b.mainRise, len = b.main1 - b.main0;
    const int P = std::max(4, 2 * (int)roundf(len / 36.f)), Q = 2 * P;   // (panels of about 9 m, an even number a span)
    auto sp = [&](int i) { return b.main0 + len * (float)i / Q; };
    for (float side : {-1.f, 1.f}) {
      for (int i = 0; i < Q; i++) f(BridgeMember{at(sp(i), side, -0.1f), at(sp(i + 1), side, -0.1f), 0.35f, 0.55f, 0});   // bottom chord
      for (int i = 1; i + 1 < Q; i++) f(BridgeMember{at(sp(i), side, T), at(sp(i + 1), side, T), 0.4f, 0.5f, 0});        // top chord
      f(BridgeMember{at(sp(0), side, 0.f), at(sp(1), side, T), 0.4f, 0.5f, 0});                                          // end posts
      f(BridgeMember{at(sp(Q), side, 0.f), at(sp(Q - 1), side, T), 0.4f, 0.5f, 0});
      for (int i = 1; i < Q; i++) f(BridgeMember{at(sp(i), side, 0.3f), at(sp(i), side, T), i == P ? 0.35f : 0.2f, 0.25f, 0});   // verticals
      for (int i = 1; i + 1 < Q; i++) {   // diagonals, falling towards the middle of each span
        const bool first = (i % P) < P / 2;
        f(first ? BridgeMember{at(sp(i), side, T), at(sp(i + 1), side, 0.3f), 0.2f, 0.25f, 0}
                : BridgeMember{at(sp(i), side, 0.3f), at(sp(i + 1), side, T), 0.2f, 0.25f, 0});
      }
    }
    for (int i = 1; i < Q; i++) f(BridgeMember{at(sp(i), -1.f, T), at(sp(i), 1.f, T), 0.15f, 0.3f, 0});                   // top struts
    for (int i = 1; i + 1 < Q; i += 2) {                                                                                   // and X bracing
      f(BridgeMember{at(sp(i), -1.f, T), at(sp(i + 1), 1.f, T), 0.08f, 0.12f, 0});
      f(BridgeMember{at(sp(i), 1.f, T), at(sp(i + 1), -1.f, T), 0.08f, 0.12f, 0});
    }
    for (int i : {1, Q - 1}) f(BridgeMember{at(sp(i), -1.f, T - 1.8f), at(sp(i), 1.f, T - 1.8f), 0.15f, 0.3f, 0});       // portals
  } else {   // a tied arch: two ribs rising from the ends of the tie, hangers down to it, braced across over the road
    const float R = b.mainRise, len = b.main1 - b.main0;
    const int N = 24;
    auto rib = [&](int i, float side) { const float u = (float)i / N; return at(b.main0 + len * u, side, 0.4f + R * 4.f * u * (1.f - u)); };
    for (float side : {-1.f, 1.f}) {
      for (int i = 0; i < N; i++) f(BridgeMember{rib(i, side), rib(i + 1, side), 0.5f, 0.6f, 1});
      for (int i = 0; i < N; i++) f(BridgeMember{at(b.main0 + len * i / N, side, -0.1f), at(b.main0 + len * (i + 1) / N, side, -0.1f), 0.35f, 0.55f, 1});   // the tie
      const int H = std::max(2, (int)roundf(len / 6.f));
      for (int i = 1; i < H; i++) {   // hangers every 6 m or so
        const float u = (float)i / H; const vec3 top = at(b.main0 + len * u, side, 0.4f + R * 4.f * u * (1.f - u) - 0.6f);
        f(BridgeMember{at(b.main0 + len * u, side, 0.45f), top, 0.05f, 0.05f, 1});
      }
    }
    for (int i = 2; i <= N - 2; i += 2) {   // struts across where the ribs stand high over the road, X bracing between
      const float u = (float)i / N;
      if (R * 4.f * u * (1.f - u) < 7.f) continue;
      f(BridgeMember{rib(i, -1.f), rib(i, 1.f), 0.2f, 0.35f, 1});
      if (i + 2 <= N - 2 && R * 4.f * (u + 2.f / N) * (1.f - u - 2.f / N) >= 7.f) {
        f(BridgeMember{rib(i, -1.f), rib(i + 2, 1.f), 0.08f, 0.12f, 1});
        f(BridgeMember{rib(i, 1.f), rib(i + 2, -1.f), 0.08f, 0.12f, 1});
      }
    }
  }
}
