// The landmarks (landmarks.cpp; docs/LIVING_ISLANDS_PLAN.md, Phase C), each where the world has room for it. A stone
// circle on a bare summit, its stones standing (two fallen) round a clearing no tree grows in; a wreck on a reef in
// warm shallows, open water round her; an offshore platform in deep water out of sight of no land; three observatory
// domes on Mount Kaleo's rim; a star fort on a cape; Lighthouse Key's cottages and landing stage; the cities' stadium and
// container terminal; the racetrack, its circuit and pit lane, its buildings and its grounds; the Fjordhaven dam and its
// lake; the ferries' routes across open water between the islands, their timetables, and the yachts off the towns. The wind
// farm: a row of turbines along a ridge's crest, 300 m and more apart, on the hills under the snow, out of the settlements, off the
// roads and 6 km or more from every runway; each in its own chunk's scenery; found the same every time.
#include "../src/scenery.h"
#include "../src/entities.h"
#include "../src/settlements.h"
#include "test_world.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

float airportInfluence(float x, float z);   // world.cpp

static int failures = 0, checks = 0;
static void check(bool ok, const char* what) { checks++; if (!ok) { if (failures < 20) std::printf("FAIL: %s\n", what); failures++; } }

int main() {
  buildTestWorld();
  World& w = g_world;
  const std::vector<PlacedEnt> items = landmarks();
  std::vector<Ent> turbines;
  for (const PlacedEnt& p : items) if (p.kind == EK_WINDTURBINE) turbines.push_back(p.e);
  check(turbines.size() >= 8 && turbines.size() <= 20, "a wind farm of eight turbines or more, two farms at the most");
  for (size_t i = 0; i < turbines.size(); i++) {
    const Ent& t = turbines[i];
    float b[4]; w.sampleBase(t.x, t.z, b);
    check(std::isfinite(t.x + t.y + t.z + t.yaw) && b[0] >= 80.f && b[0] <= 700.f && b[3] <= 0.3f && t.y > 50.f, "a turbine on the green hills, where it's mild");
    check(fabsf(t.y - w.groundHeight(t.x, t.z, 8)) < 1.f, "a turbine on its ground");
    for (const Airport& a : w.airports) check(hypotf(t.x - a.x, t.z - a.z) >= 6000.f, "a turbine 6 km or more from every runway");
    check(settlementShare(w.settlements, t.x, t.z) >= 2.f, "a turbine out of the settlements");
    check(roadEdgeDistance(w.roadGrid, t.x, t.z) > 40.f, "a turbine off the roads");
    // on the crest: higher than the ground 250 m either side across its row (its front, local +z, faces across it)
    const float fx = sinf(t.yaw), fz = cosf(t.yaw);
    float s1[4], s2[4]; w.sampleBase(t.x + fx * 250.f, t.z + fz * 250.f, s1); w.sampleBase(t.x - fx * 250.f, t.z - fz * 250.f, s2);
    check(b[0] > 0.5f * (s1[0] + s2[0]) + 4.f, "a turbine on a ridge's crest");
    for (size_t j = i + 1; j < turbines.size(); j++) check(hypotf(turbines[j].x - t.x, turbines[j].z - t.z) >= 290.f, "the turbines 300 m or so apart");
  }
  // the stone circle: its standing stones round a centre (its flat stone), on a summit, in a clearing
  {
    std::vector<Ent> stones; const Ent* centre = nullptr;
    for (const PlacedEnt& p : items) if (p.kind == EK_BLOCK || p.kind == EK_SLAB) stones.push_back(p.e);
    for (const PlacedEnt& p : items) if (p.kind == EK_SLAB && p.e.sx > 1.f) centre = &p.e;
    check(centre != nullptr && stones.size() >= 15, "a stone circle: its stones and its centre");
    if (centre) {
      int standing = 0;
      for (const Ent& e : stones) {
        const float d = hypotf(e.x - centre->x, e.z - centre->z);
        check(d < 17.f, "the circle's stones round its centre");
        if (d > 11.f && d < 15.f && e.sy > 2.f) standing++;
        check(fabsf(e.y - w.groundHeight(e.x, e.z)) < 1.f, "a stone on its ground, its foot sunk");
      }
      check(standing >= 12, "most of the circle's stones standing, 3 m and more tall");
      float b[4]; w.sampleBase(centre->x, centre->z, b);
      bool summit = true;
      for (int k = 0; k < 8; k++) { float r[4]; w.sampleBase(centre->x + 400.f * cosf(k * 0.785f), centre->z + 400.f * sinf(k * 0.785f), r); summit = summit && b[0] > r[0] + 11.f; }
      check(summit, "the circle on a summit");
      int trees = 0;
      for (int cz = Scenery::chunkOf(centre->z - 30.f); cz <= Scenery::chunkOf(centre->z + 30.f); cz++)
        for (int cx = Scenery::chunkOf(centre->x - 30.f); cx <= Scenery::chunkOf(centre->x + 30.f); cx++) {
          const Scenery::Chunk* ch = g_scenery.ensure(cx, cz, 2);
          for (int k = 0; k <= EK_BUSH; k++)
            for (uint32_t n = ch->off[k]; n < ch->off[k + 1]; n++) trees += hypotf(ch->ents[n].x - centre->x, ch->ents[n].z - centre->z) < 25.f;
          int found = 0;
          for (int k = EK_BLOCK; k <= EK_SLAB; k++)
            for (uint32_t n = ch->off[k]; n < ch->off[k + 1]; n++) found += hypotf(ch->ents[n].x - centre->x, ch->ents[n].z - centre->z) < 17.f;
          (void)found;
        }
      check(trees == 0, "no tree or bush in the circle's clearing");
      g_scenery.clear();
    }
  }
  // the wreck: on a reef 1.5 to 4 m down, open water round her; the platform: in deep water 5 km and more from land
  int wrecks = 0, platforms = 0;
  for (const PlacedEnt& p : items) {
    if (p.kind == EK_SHIPWRECK) {
      wrecks++;
      const float floor = w.groundHeight(p.e.x, p.e.z);
      check(floor < -1.f && floor > -4.5f && fabsf(p.e.y - floor - 3.6f) < 0.01f, "the wreck aground on a reef in the shallows");
      for (int k = 0; k < 8; k++) check(w.groundHeight(p.e.x + 150.f * cosf(k * 0.785f), p.e.z + 150.f * sinf(k * 0.785f)) < 0.f, "open water round the wreck");
    }
    if (p.kind == EK_PLATFORM) {
      platforms++;
      check(p.e.y == 0.f && w.groundHeight(p.e.x, p.e.z) < -60.f, "the platform in deep water, at the sea");
      for (int k = 0; k < 24; k++) check(w.groundHeight(p.e.x + 5000.f * cosf(k * 0.26f), p.e.z + 5000.f * sinf(k * 0.26f)) < 0.f, "no land within 5 km of the platform");
      for (const Airport& a : w.airports) check(hypotf(p.e.x - a.x, p.e.z - a.z) >= 8000.f, "the platform 8 km and more from every runway");
    }
  }
  check(wrecks == 1 && platforms == 1, "a wreck and a platform");
  // the observatory's domes on Mount Kaleo's rim, clear of the crater field's runway; the star fort on a cape, its site
  // level; Lighthouse Key's cottages behind the light, its landing stage from the land out over the water
  int domes = 0, forts = 0, jetties = 0, cottages = 0;
  float lx = 0, lz = 0, ly = 0; const bool light = findLighthouseSite(w, lx, lz, ly);
  for (const PlacedEnt& p : items) {
    const Ent& e = p.e;
    if (p.kind == EK_OBSERVATORY) {
      domes++;
      const float r = hypotf(e.x - 25000.f, e.z + 9000.f);
      check(r > 250.f && r < 1700.f && e.y > 1500.f, "a dome on Mount Kaleo's rim");
      for (const Airport& a : w.airports) check(hypotf(e.x - a.x, e.z - a.z) >= 2500.f, "a dome clear of the runways");
      check(e.y <= w.groundHeight(e.x, e.z) + 0.01f, "a dome on its ground (its drum's foot at the lowest)");
    }
    if (p.kind == EK_STARFORT) {
      forts++;
      int sea = 0; float lo = 1e9f, hi = -1e9f;
      for (int k = 0; k < 16; k++) { float b[4]; w.sampleBase(e.x + 350.f * cosf(k * 0.3927f), e.z + 350.f * sinf(k * 0.3927f), b); sea += b[0] < 0.f; }
      for (int k = 0; k < 12; k++) { const float h = w.groundHeight(e.x + 66.f * cosf(k * 0.5236f), e.z + 66.f * sinf(k * 0.5236f)); lo = std::min(lo, h); hi = std::max(hi, h); }
      check(sea >= 10 && lo >= 2.f && hi - lo <= 6.f, "the star fort on a cape, its site dry and level");
      check(roadEdgeDistance(w.roadGrid, e.x, e.z) >= 90.f && settlementShare(w.settlements, e.x, e.z) >= 1.3f, "the star fort off the roads, out of the settlements");
    }
    if (p.kind == EK_HOUSE_HIP && light && hypotf(e.x - lx, e.z - lz) < 60.f) cottages++;
    if (p.kind == EK_JETTY) {
      jetties++;
      const float dx = sinf(e.yaw), dz = cosf(e.yaw);
      check(w.groundHeight(e.x - dx * 21.f, e.z - dz * 21.f) > 0.f && w.groundHeight(e.x + dx * 24.f, e.z + dz * 24.f) < -1.f, "the landing stage from the land out over the water");
      check(light && hypotf(e.x - lx, e.z - lz) < 500.f, "the landing stage by the light");
    }
  }
  check(domes == 3 && forts == 1 && jetties == 1 && cottages == 2, "the observatory's three domes, a star fort, Lighthouse Key's cottages and landing stage");
  // the cities': Solace Capital's stadium in the city on level ground no road crosses, no building in it; Port Verde's
  // terminal - its quay's wall at 8 m of water, its apron reclaimed from the shallows back to the land, four cranes on
  // it, a ship alongside in the water
  {
    RoadIndex roads;
    for (size_t pi = 0; pi < w.roads.paths.size(); pi++) roads.addPath(w.roads.paths[pi], (int)pi);
    int stadiums = 0, quays = 0, cranes = 0, stacks = 0, ships = 0;
    const Ent* quay = nullptr;
    for (const PlacedEnt& p : items) if (p.kind == EK_QUAY) quay = &p.e;
    for (const PlacedEnt& p : items) {
      const Ent& e = p.e; const float c = cosf(e.yaw), s = sinf(e.yaw);
      auto at = [&](float x, float z) { return vec2(e.x + c * x + s * z, e.z - s * x + c * z); };   // (local to world, as ent_vs)
      if (p.kind == EK_STADIUM) {
        stadiums++;
        int town = -1; settlementShare(w.settlements, e.x, e.z, &town);
        check(town >= 0 && !strcmp(kTowns[town].name, "Solace Capital"), "the stadium in Solace Capital");
        float lo = 1e9f, hi = -1e9f; bool clear = true;
        for (float a = -92.f; a <= 92.f; a += 23.f) for (float b = -74.f; b <= 74.f; b += 18.5f) {
          const vec2 q = at(a, b); const float g = w.groundHeight(q.x, q.y);
          lo = std::min(lo, g); hi = std::max(hi, g);
          clear = clear && roads.nearest(q, 3.f, -1, nullptr, nullptr, true) >= 1.f;
        }
        check(hi - lo <= 5.5f && clear, "the stadium on level ground no road crosses");
        std::vector<int> near; lotsIn(e.x - 120.f, e.z - 120.f, e.x + 120.f, e.z + 120.f, near);
        int inside = 0;
        for (int id : near) { const Lot& L = settlementLots()[id]; const float dx = L.cx - e.x, dz = L.cz - e.z;
          const float lx = dx * c - dz * s, lz = dx * s + dz * c; inside += fabsf(lx) < 92.f && fabsf(lz) < 74.f; }
        check(inside == 0, "no building in the stadium");
      }
      if (p.kind == EK_QUAY) {
        quays++;
        int landed = 0;
        for (float x = -200.f; x <= 200.f; x += 50.f) {
          const vec2 sea = at(x, 30.f), apron = at(x, -60.f), back = at(x, -124.f);
          check(w.groundHeight(sea.x, sea.y) < -8.f && w.groundHeight(apron.x, apron.y) <= e.y + 0.3f && w.groundHeight(apron.x, apron.y) > -12.5f, "the quay at deep water, its apron over the land and the shallows");
          landed += w.groundHeight(back.x, back.y) > 0.f;
        }
        check(landed >= 5 && e.y >= 3.f, "the quay's apron reaching back to the land, 3 m and more over the sea");
      }
      if (p.kind == EK_GANTRY) { cranes++; check(quay && fabsf(e.y - quay->y) < 0.01f, "a crane on the quay"); }
      if (p.kind == EK_CONTAINERS) stacks++;
      if (p.kind == EK_CONTAINERSHIP) {
        ships++;
        check(e.y == 0.f && w.groundHeight(e.x, e.z) < -8.f, "the container ship afloat alongside, in deep water");
      }
    }
    check(stadiums == 1 && quays == 1 && cranes == 4 && stacks >= 6 && ships == 1, "the stadium; the terminal's quay, four cranes, its containers and a ship");
  }
  // ---- the racetrack: its circuit closed, 3.9 km, on dry ground out of the settlements and away from the runways, never
  // more than 5 m off the ground nor steeper than 6%, no other road on it; its pit lane beside the main straight at its
  // height, out of it and back; a road from the paddock to the network; its buildings each where they belong, off the
  // roads' paving; its grounds clear of trees
  {
    const Racetrack R = racetrackOf(w.roads);
    check(R.found && R.pitLane >= 0, "a racetrack, its circuit and pit lane");
    if (R.found && R.pitLane >= 0) {
      const RoadPath &C = w.roads.paths[R.circuit], &P = w.roads.paths[R.pitLane];
      float len = 0.f; bool dry = true, out = true, onGround = true, gentle = true;
      for (size_t k = 0; k < C.pts.size(); k++) {
        const RoadPoint& q = C.pts[k];
        if (k) {
          const float run = hypotf(q.x - C.pts[k - 1].x, q.z - C.pts[k - 1].z); len += run;
          gentle = gentle && fabsf(q.h - C.pts[k - 1].h) <= 0.06f * run + 1e-3f;
        }
        dry = dry && w.groundHeight(q.x, q.z) >= 3.f;
        out = out && settlementShare(w.settlements, q.x, q.z) >= 1.3f && airportInfluence(q.x, q.z) <= 0.f;
        for (const Airport& a : w.airports) out = out && hypotf(q.x - a.x, q.z - a.z) >= 3000.f;
        onGround = onGround && fabsf(q.h - w.naturalHeight(q.x, q.z)) <= 5.f;
      }
      check(hypotf(C.pts.front().x - C.pts.back().x, C.pts.front().z - C.pts.back().z) < 0.01f && len > 3800.f && len < 4000.f, "the circuit closed, 3.9 km round");
      check(dry && out, "the circuit on dry land, out of the settlements, 3 km and more from the runways");
      check(onGround && gentle, "the circuit within 5 m of the ground, nowhere steeper than 6%");
      // (the main straight straight, from the pit lane's mouth past the start line to turn 1)
      bool straight = true;
      for (const RoadPoint& q : C.pts) {
        const float sv = dot2(vec2(q.x, q.z) - R.o, R.u), vv = dot2(vec2(q.x, q.z) - R.o, vec2(-R.u.y, R.u.x));
        if (sv > 1.f && sv < 890.f && fabsf(vv) < 30.f) straight = straight && fabsf(vv) < 0.3f;
      }
      check(straight, "the main straight straight, its start line on it");
      int crossing = 0;
      for (const RoadSegment& sg : w.roadGrid.segs) {
        if (sg.path == R.circuit || sg.path == R.pitLane) continue;
        for (size_t k = 0; k < C.pts.size(); k += 2) {
          const vec2 a(sg.ax, sg.az), b(sg.bx, sg.bz), q(C.pts[k].x, C.pts[k].z), d = b - a;
          if (fabsf(q.x - a.x) > 600.f && fabsf(q.x - b.x) > 600.f) continue;
          const float t = std::clamp(dot2(q - a, d) / std::max(dot2(d, d), 1e-6f), 0.f, 1.f);
          crossing += length(a + d * t - q) < roadSpec(RC_CIRCUIT).halfPlatform + roadSpec(sg.cls).halfPlatform;
        }
      }
      check(crossing == 0, "no other road on the circuit");
      bool beside = true;
      for (const RoadPoint& q : P.pts) {
        const float sv = dot2(vec2(q.x, q.z) - R.o, R.u), vv = dot2(vec2(q.x, q.z) - R.o, vec2(-R.u.y, R.u.x));
        if (sv > 150.f && sv < 620.f) beside = beside && fabsf(vv - RACE_PITLANE_V) < 0.5f;
        float best = 1e9f, hb = 0.f;
        for (const RoadPoint& c : C.pts) { const float d = hypotf(c.x - q.x, c.z - q.z); if (d < best) { best = d; hb = c.h; } }
        beside = beside && fabsf(q.h - hb) < 0.3f;
      }
      const float in = hypotf(P.pts.front().x - R.at(10.f, 0.f).x, P.pts.front().z - R.at(10.f, 0.f).y), back = hypotf(P.pts.back().x - R.at(780.f, 0.f).x, P.pts.back().z - R.at(780.f, 0.f).y);
      check(beside && in < 1.f && back < 1.f, "the pit lane beside the main straight at its height, out of the circuit and back into it");
      bool reached = false;   // (its far end on a highway, a road or a lane of the network's)
      for (size_t pi = 0; pi < w.roads.paths.size(); pi++) {
        const RoadPath& p = w.roads.paths[pi];
        if (p.cls != RC_ROAD || p.from >= 0 || p.pts.size() < 2) continue;
        const vec2 f(p.pts.front().x, p.pts.front().z), b(p.pts.back().x, p.pts.back().z);
        if (length(f - R.at(170.f, RACE_PADDOCK_V)) > 5.f && length(f - R.at(640.f, RACE_PADDOCK_V)) > 5.f) continue;
        for (const RoadSegment& sg : w.roadGrid.segs) {
          if (sg.path == pi || sg.cls > RC_LANE) continue;
          const vec2 a(sg.ax, sg.az), d = vec2(sg.bx, sg.bz) - a;
          const float t = std::clamp(dot2(b - a, d) / std::max(dot2(d, d), 1e-6f), 0.f, 1.f);
          reached = reached || length(a + d * t - b) < 1.f;
        }
      }
      check(reached, "a road from the paddock to the network");
      int pits = 0, walls = 0, stands = 0, vans = 0, cars = 0, rails = 0, onPaving = 0, railsOff = 0;
      for (const PlacedEnt& p : items) {
        const Ent& e = p.e; const float c = cosf(e.yaw), s = sinf(e.yaw);
        auto at = [&](float x, float z) { return vec2(e.x + c * x + s * z, e.z - s * x + c * z); };
        auto paved = [&](vec2 q) { int sg = -1; const float d = roadEdgeDistance(w.roadGrid, q.x, q.y, &sg);
                                   return sg >= 0 && d + roadSpec(w.roadGrid.segs[sg].cls).halfPlatform - roadSpec(w.roadGrid.segs[sg].cls).halfPaved < 0.f; };
        const EntKindInfo& I = kEntInfo[p.kind];
        if (p.kind == EK_PITS || p.kind == EK_GRANDSTAND || p.kind == EK_TRANSPORTER || p.kind == EK_CAR) {
          if (p.kind == EK_CAR && hypotf(e.x - R.at(430.f, 90.f).x, e.z - R.at(430.f, 90.f).y) > 400.f) continue;   // (the car park's)
          pits += p.kind == EK_PITS; stands += p.kind == EK_GRANDSTAND; vans += p.kind == EK_TRANSPORTER; cars += p.kind == EK_CAR;
          for (float a : {-1.f, 0.f, 1.f}) for (float b : {-1.f, 1.f}) onPaving += paved(at(a * I.hx * e.sx * 0.98f, b * I.hz * e.sz * 0.98f));
        }
        if (p.kind == EK_PITWALL) walls++;
        if (p.kind == EK_GUARDRAIL) {
          float best = 1e9f;
          for (const RoadPoint& q : C.pts) best = std::min(best, hypotf(q.x - e.x, q.z - e.z));
          if (best > 100.f) continue;   // (the network's own)
          rails++; railsOff += best < 13.5f || best > 17.f;
        }
      }
      std::printf("racetrack at %.0f %.0f heading %.0f: %.0f m round; %d garages blocks, %d of pit wall, %d grandstands, %d transporters, %d cars, %d guardrails\n",
                  R.o.x, R.o.y, atan2f(R.u.x, R.u.y) * 57.3f, len, pits, walls, stands, vans, cars, rails);
      check(pits == 4 && walls == 10 && stands >= 3 && vans == 32 && cars >= 300 && rails >= 40, "the racetrack's garages, pit wall, grandstands, transporters, car park and guardrails");
      check(onPaving == 0, "none of the racetrack's buildings, transporters or cars on a road's paving");
      int furnished = 0;   // (no pole, sign or lamp in a building, a transporter or a grandstand)
      for (const RoadFurniture& f : roadFurniture())
        for (const PlacedEnt& p : items) {
          if (p.kind != EK_PITS && p.kind != EK_GRANDSTAND && p.kind != EK_TRANSPORTER && p.kind != EK_PITWALL) continue;
          const Ent& e = p.e; const float c = cosf(e.yaw), s = sinf(e.yaw), dx = f.e.x - e.x, dz = f.e.z - e.z;
          const float lx = c * dx - s * dz, lz = s * dx + c * dz;
          furnished += fabsf(lx) < kEntInfo[p.kind].hx * e.sx + 1.f && fabsf(lz) < kEntInfo[p.kind].hz * e.sz + 1.f;
        }
      check(furnished == 0, "no road furniture in the racetrack's buildings or among its transporters");
      check(railsOff == 0, "the circuit's guardrails at the run-off's edge");
      int trees = 0;
      for (size_t k = 0; k < C.pts.size(); k += 25) {
        const Scenery::Chunk* ch = g_scenery.ensure(Scenery::chunkOf(C.pts[k].x), Scenery::chunkOf(C.pts[k].z), 2);
        for (int kk = 0; kk <= EK_BUSH; kk++)
          for (uint32_t n = ch->off[kk]; n < ch->off[kk + 1]; n++) {
            float best = 1e9f;
            for (size_t q = 0; q < C.pts.size(); q++) best = std::min(best, hypotf(C.pts[q].x - ch->ents[n].x, C.pts[q].z - ch->ents[n].z));
            trees += best < 20.f;
          }
      }
      check(trees == 0, "no tree or bush on the circuit's run-off");
      g_scenery.clear();
    }
  }
  // ---- the Fjordhaven dam and its lake: the dam across a gorge in the northern mountains, its abutments in ground as
  // high as its crest, the valley below it dry; the lake flat at its level wherever the ground lies under it, held by
  // the ground all round (but at the dam), drowning no road, no settlement, no runway; nothing growing in it
  {
    const Reservoir& r = w.reservoir; const LakeMask& L = w.lake;
    int dams = 0; const Ent* dam = nullptr;
    for (const PlacedEnt& p : items) if (p.kind == EK_DAM) { dams++; dam = &p.e; }
    check(r.on > 0.f && L.n > 0 && dams == 1, "the Fjordhaven dam and its lake");
    if (r.on > 0.f && L.n > 0 && dam) {
      int fj = -1; for (int t = 0; t < kNumTowns; t++) if (!strcmp(kTowns[t].name, "Fjordhaven")) fj = t;
      check(fj >= 0 && hypotf(r.x - kTowns[fj].x, r.z - kTowns[fj].z) < 14000.f, "the dam within 14 km of Fjordhaven");
      const vec2 P(r.x, r.z), v(r.vx, r.vz), u(-v.y, v.x), A = r.archCentre();
      for (float s : {-0.5f, 0.5f}) { const vec2 q = P + u * (s * r.chord); check(w.naturalHeight(q.x, q.y) >= r.floor + r.crest - 1.f, "the dam's abutments as high as its crest"); }
      const vec2 below = P - v * 150.f;
      check(w.naturalHeight(below.x, below.y) < r.level - 10.f && fabsf(w.height(below.x, below.y) - w.naturalHeight(below.x, below.y)) < 0.01f, "the valley below the dam dry, its ground as it was");
      check(fabsf(dam->y - (r.floor - 4.f)) < 0.01f && fabsf(dam->y + 67.f * dam->sy - r.level) < 0.05f, "the dam's foot in the floor, its water line at the lake's level");
      int wet = 0, notFlat = 0, roads = 0, town = 0, runway = 0, leaks = 0;
      for (int j = 0; j < L.n; j++)
        for (int i = 0; i < L.n; i++) {
          const float x = L.x0 + (i + 0.5f) * L.cell, z = L.z0 + (j + 0.5f) * L.cell;
          const bool inMask = L.m[(size_t)j * L.n + i] > 0.5f, under = w.naturalHeight(x, z) < r.level;
          if (inMask && under) {
            wet++;
            notFlat += fabsf(w.height(x, z) - r.level) > 0.01f;
            roads += roadEdgeDistance(w.roadGrid, x, z) < 0.f;
            town += settlementShare(w.settlements, x, z) < 1.5f;
            runway += airportInfluence(x, z) > 0.f;
          }
          // (where the mask ends, the ground holds the water in - but at the dam)
          if (inMask && i > 0 && j > 0 && i < L.n - 1 && j < L.n - 1)
            for (int d = 0; d < 4; d++) {
              const int ni = i + (d == 0) - (d == 1), nj = j + (d == 2) - (d == 3);
              if (L.m[(size_t)nj * L.n + ni] > 0.5f) continue;
              const vec2 q(L.x0 + (ni + 0.5f) * L.cell, L.z0 + (nj + 0.5f) * L.cell);
              leaks += w.naturalHeight(q.x, q.y) < r.level && length(q - A) > r.archR() + 12.f;
            }
        }
      std::printf("Fjordhaven dam at %.0f %.0f: chord %.0f m, crest %.0f m over its floor (%.0f m); lake %.2f km2 at %.1f m\n",
                  r.x, r.z, r.chord, r.crest, r.floor, wet * L.cell * L.cell / 1e6f, r.level);
      check(wet * L.cell * L.cell > 1e5f, "a lake of 0.1 km2 and more");
      check(notFlat == 0, "the lake flat at its level");
      check(roads == 0 && town == 0 && runway == 0, "the lake drowning no road, no settlement, no runway");
      check(leaks == 0, "the lake held by the ground all round but at the dam");
      int growing = 0;
      for (int cz = Scenery::chunkOf(L.z0); cz <= Scenery::chunkOf(L.z0 + L.n * L.cell); cz++)
        for (int cx = Scenery::chunkOf(L.x0); cx <= Scenery::chunkOf(L.x0 + L.n * L.cell); cx++) {
          const Scenery::Chunk* ch = g_scenery.ensure(cx, cz, 2);
          for (int k = 0; k <= EK_SEASTACK; k++)
            for (uint32_t n = ch->off[k]; n < ch->off[k + 1]; n++)
              if (w.underLake(ch->ents[n].x, ch->ents[n].z)) { if (growing++ < 5) std::printf("  %s in the lake at %.0f %.0f\n", kEntInfo[k].name, ch->ents[n].x, ch->ents[n].z); }
        }
      check(growing == 0, "no tree, bush or rock in the lake");
      g_scenery.clear();
    }
  }
  // the sea's traffic: the ferries, harbour to harbour across open water, joining the islands; the yachts off the towns
  {
    const std::vector<FerryRoute>& F = ferryRoutes();
    check(!F.empty(), "ferries between the islands");
    std::vector<int> root(64); for (int i = 0; i < 64; i++) root[i] = i;
    auto find = [&](int i) { while (root[i] != i) i = root[i] = root[root[i]]; return i; };
    int shallow = 0, lowDeck = 0, offRoute = 0, jumps = 0, dry = 0, docks = 0;
    for (const FerryRoute& r : F) {
      const int ia = w.roads.nodes[r.townA].island, ib = w.roads.nodes[r.townB].island;
      check(ia != ib && find(ia & 63) != find(ib & 63), "each ferry joining two islands no other route has joined");
      root[find(ia & 63)] = find(ib & 63);
      // her terminals: a causeway from the land out to the linkspan, her berth beyond it in water deep enough for her, and
      // her way out straight from it
      for (int e = 0; e < 2; e++) {
        const int t = e ? r.townB : r.townA; const vec2 h = e ? r.pts.back() : r.pts.front(), out = e ? r.pts[r.pts.size() - 2] : r.pts[1];
        const FerryPier& P = r.pier[e];
        check(P.lengths >= 1 && P.lengths <= 8 && fabsf(length(P.dir) - 1.f) < 1e-3f, "a terminal's causeway, 40 to 320 m long");
        check(hypotf(h.x - kTowns[t].x, h.y - kTowns[t].z) <= 5000.f && w.naturalHeight(h.x, h.y) < -4.5f, "a ferry's berth off her town, 4.5 m deep");
        check(length(P.root + P.dir * (40.f * P.lengths + kFerryBerth) - h) < 0.01f, "her berth beyond the causeway's linkspan");
        check(w.naturalHeight(P.root.x, P.root.y) > 0.f && w.naturalHeight(h.x - P.dir.x * 60.f, h.y - P.dir.y * 60.f) < 0.f, "the causeway from the land out over the water");
        const vec2 way = out - h;
        check(length(way) >= 240.f && (way.x * P.dir.x + way.y * P.dir.y) / length(way) > 0.999f, "her way out straight from the linkspan, 250 m");
        int segs = 0, spans = 0;
        for (const PlacedEnt& p : items) {
          const vec2 q(p.e.x - P.root.x, p.e.z - P.root.y); const float along = q.x * P.dir.x + q.y * P.dir.y, off = fabsf(q.x * P.dir.y - q.y * P.dir.x);
          if (p.kind == EK_FERRYPIER && off < 0.01f && fabsf(fmodf(along - 20.f + 0.005f, 40.f)) < 0.01f && along < 40.f * P.lengths) segs++;
          if (p.kind == EK_LINKSPAN && off < 0.01f && fabsf(along - 40.f * P.lengths - 5.f) < 0.01f) spans++;
        }
        check(segs == P.lengths && spans == 1, "the causeway's lengths and the linkspan at its head");
        // nothing built on it
        int built = 0;
        for (float a = 0.f; a <= 40.f * P.lengths; a += 40.f) {
          const vec2 q = P.root + P.dir * a;
          const Scenery::Chunk* ch = g_scenery.ensure(Scenery::chunkOf(q.x), Scenery::chunkOf(q.y), 2);
          for (int k = EK_HOUSE; k <= EK_GASSTATION; k++)
            for (uint32_t n = ch->off[k]; n < ch->off[k + 1]; n++) {
              const Ent& b = ch->ents[n]; const vec2 v(b.x - P.root.x, b.z - P.root.y);
              const float al = v.x * P.dir.x + v.y * P.dir.y, of = fabsf(v.x * P.dir.y - v.y * P.dir.x);
              if (al > -15.f && al < 40.f * P.lengths + 15.f && of < 14.f + std::max(kEntInfo[k].hx * b.sx, kEntInfo[k].hz * b.sz)) built++;
            }
        }
        check(built == 0, "no building on a terminal's causeway");
        // docked end-on to the linkspan
        vec2 at, hd; bool dk = false; int ends = 0;
        for (float tt = 0.f; tt < 2.f * (r.along.back() / kFerrySpeed + kFerryDwell); tt += 10.f)
          if (ferryAt(r, tt, at, hd, &dk) && dk && length(at - h) < 1.f) { ends++; check(fabsf(hd.x * P.dir.x + hd.y * P.dir.y) > 0.999f, "a ferry alongside end-on to the linkspan"); }
        check(ends > 0, "a ferry at her berth for her wait");
        std::printf("  %s's terminal: a causeway of %d lengths, her berth %.0f m out, %.1f m deep\n", kTowns[t].name, P.lengths, 40.f * P.lengths + kFerryBerth - 12.f, -w.naturalHeight(h.x, h.y));
      }
      check(r.along.size() == r.pts.size() && r.along.back() > 1000.f, "a ferry's route measured along");
      for (size_t k = 0; k + 1 < r.pts.size(); k++) {
        const vec2 a = r.pts[k], b = r.pts[k + 1];
        const int n = std::max(1, (int)(length(b - a) / 10.f));
        for (int j = 0; j <= n; j++) { const vec2 p = a + (b - a) * ((float)j / n); if (w.naturalHeight(p.x, p.y) > -2.f) shallow++; }
        // under a bridge only where the girders clear her mast
        for (const Bridge& br : w.bridges)
          for (size_t d = 0; d + 1 < br.deck.size(); d++) {
            const vec2 c(br.deck[d].x, br.deck[d].z), e2(br.deck[d + 1].x, br.deck[d + 1].z);
            const vec2 r1 = b - a, r2 = e2 - c; const float den = r1.x * r2.y - r1.y * r2.x;
            if (fabsf(den) < 1e-6f) continue;
            const float u = ((c.x - a.x) * r2.y - (c.y - a.y) * r2.x) / den, v = ((c.x - a.x) * r1.y - (c.y - a.y) * r1.x) / den;
            if (u < 0.f || u > 1.f || v < 0.f || v > 1.f) continue;
            const float under = br.deck[d].h + (br.deck[d + 1].h - br.deck[d].h) * v - br.depth;
            if (under < kEntInfo[EK_FERRY].h + 3.f) { if (lowDeck++ < 3) std::printf("  ferry %s-%s under a bridge %.0f m up at %.0f %.0f\n", kTowns[r.townA].name, kTowns[r.townB].name, under, a.x + r1.x * u, a.y + r1.y * u); }
          }
      }
      // her timetable: out, a wait, back, a wait; on her route, over the water, never faster than her speed
      const float T = 2.f * (r.along.back() / kFerrySpeed + kFerryDwell);
      vec2 last; bool first = true; int docked = 0;
      for (float t = 0.f; t < T; t += 2.f) {
        vec2 at, hd; bool dk = false;
        check(ferryAt(r, t, at, hd, &dk), "a ferry somewhere at every time");
        docked += dk;
        if (fabsf(length(hd) - 1.f) > 1e-3f) jumps++;
        if (!first && length(at - last) > 2.f * kFerrySpeed * 1.01f + 0.01f) jumps++;
        if (w.naturalHeight(at.x, at.y) > -2.f) dry++;
        float best = 1e9f;
        for (size_t k = 0; k + 1 < r.pts.size(); k++) {
          const vec2 a = r.pts[k], ab = r.pts[k + 1] - a; const float u = std::clamp(((at.x - a.x) * ab.x + (at.y - a.y) * ab.y) / std::max(ab.x * ab.x + ab.y * ab.y, 1e-6f), 0.f, 1.f);
          best = std::min(best, length(a + ab * u - at));
        }
        if (best > 0.5f) offRoute++;
        last = at; first = false;
      }
      if (fabsf(docked * 2.f - 2.f * kFerryDwell) > 6.f) docks++;
      vec2 a0, a1, hd;
      ferryAt(r, 123.f, a0, hd); ferryAt(r, 123.f + T, a1, hd);
      check(length(a0 - a1) < 0.5f, "a ferry's timetable repeating every round trip");
      std::printf("ferry %s - %s: %.1f km, %zu legs, round trip %.0f min\n", kTowns[r.townA].name, kTowns[r.townB].name, r.along.back() / 1000.f, r.pts.size() - 1, T / 60.f);
    }
    check(shallow == 0, "the ferries' routes over open water, 2 m deep and more all the way");
    check(lowDeck == 0, "no ferry under a bridge too low for her");
    check(offRoute == 0 && jumps == 0 && dry == 0, "each ferry on her route, at her speed, afloat");
    check(docks == 0, "each ferry alongside at each end for her wait");
    // every island a coastal town is on reached by them (32 km of sea and less between)
    std::vector<int> coastal;
    for (const FerryRoute& r : F) for (int t : {r.townA, r.townB}) coastal.push_back(find(w.roads.nodes[t].island & 63));
    std::sort(coastal.begin(), coastal.end()); coastal.erase(std::unique(coastal.begin(), coastal.end()), coastal.end());
    check(coastal.size() == 1, "the ferries joining their islands into one network");
    // the yachts: afloat at the sea's level in the shallows off a town, 16 m and more apart, clear of the quay and the
    // ferries' harbours; a few off each coastal town and no more than its share
    vec2 quay(1e9f, 1e9f);
    for (const PlacedEnt& p : items) if (p.kind == EK_QUAY) quay = vec2(p.e.x, p.e.z);
    std::vector<Ent> yachts; std::vector<int> per(kNumTowns, 0);
    for (const PlacedEnt& p : items) if (p.kind == EK_BOAT) yachts.push_back(p.e);
    int badYacht = 0, crowded = 0, inHarbour = 0;
    for (size_t i = 0; i < yachts.size(); i++) {
      const Ent& e = yachts[i]; const float g = w.naturalHeight(e.x, e.z);
      int town = -1; float best = 2500.f;
      for (int t = 0; t < kNumTowns; t++) { const float d = hypotf(e.x - kTowns[t].x, e.z - kTowns[t].z); if (d <= best) { best = d; town = t; } }
      if (town >= 0) per[town]++;
      if (e.y != 0.f || g > -1.2f || g < -7.f || town < 0 || hypotf(e.x - quay.x, e.z - quay.y) < 450.f) badYacht++;
      for (const FerryRoute& r : F) for (const vec2& h : {r.pts.front(), r.pts.back()}) if (hypotf(e.x - h.x, e.z - h.y) < 250.f) inHarbour++;
      for (const FerryRoute& r : F)
        for (const FerryPier& P : r.pier) {
          const vec2 v(e.x - P.root.x, e.z - P.root.y); const float al = v.x * P.dir.x + v.y * P.dir.y;
          if (al > -20.f && al < 40.f * P.lengths + kFerryBerth + 60.f && fabsf(v.x * P.dir.y - v.y * P.dir.x) < 60.f) inHarbour++;
        }
      for (size_t j = i + 1; j < yachts.size(); j++) if (hypotf(yachts[j].x - e.x, yachts[j].z - e.z) < 16.f) crowded++;
    }
    int towns = 0, over = 0;
    for (int t = 0; t < kNumTowns; t++) { towns += per[t] > 0; if (per[t] > (kTowns[t].kind == 2 ? 24 : kTowns[t].kind == 1 ? 14 : 8) * 2) over++; }
    std::printf("yachts: %zu off %d towns\n", yachts.size(), towns);
    check(towns >= 4 && yachts.size() >= 40, "yachts moored off the coastal towns");
    check(badYacht == 0, "each yacht afloat in 1.2 to 7 m of water off a town, clear of the terminal's quay");
    check(crowded == 0 && inHarbour == 0 && over == 0, "the yachts 16 m apart, clear of the ferries' terminals and berths, a few to each town");
  }
  // the sea stacks: off the cliffed shores alone - none out in the open bay south of Port Verde, a few at the foot of
  // the steep eastern coast
  {
    int stacks = 0, offCliffs = 0, inBay = 0, east = 0;
    auto scan = [&](float x0, float z0, float x1, float z1, bool eastern) {
      for (int cz = Scenery::chunkOf(z0); cz <= Scenery::chunkOf(z1); cz++)
        for (int cx = Scenery::chunkOf(x0); cx <= Scenery::chunkOf(x1); cx++) {
          const Scenery::Chunk* ch = g_scenery.ensure(cx, cz, 2);
          for (uint32_t n = ch->off[EK_SEASTACK]; n < ch->off[EK_SEASTACK + 1]; n++) {
            const Ent& e = ch->ents[n]; stacks++; east += eastern;
            offCliffs += seaStackShore(e.x, e.z);
            inBay += e.x > -32000.f && e.x < -29000.f && e.z > 14800.f && e.z < 18000.f;
          }
        }
    };
    scan(-34000.f, 8000.f, -27000.f, 22000.f, false);
    scan(25000.f, -28000.f, 30000.f, -12000.f, true);
    std::printf("sea stacks: %d off Port Verde, %d off the eastern cliffs\n", stacks - east, east);
    check(offCliffs == stacks && inBay == 0 && east >= 3, "the sea stacks off the cliffed shores, none out in an open bay");
    g_scenery.clear();
  }
  // in their chunks' scenery, as generated
  for (size_t i = 0; i < turbines.size(); i += 3) {
    const Ent& t = turbines[i];
    const Scenery::Chunk* ch = g_scenery.ensure(Scenery::chunkOf(t.x), Scenery::chunkOf(t.z), 1);
    bool found = false;
    for (uint32_t k = ch->off[EK_WINDTURBINE]; k < ch->off[EK_WINDTURBINE + 1]; k++) found = found || !memcmp(&ch->ents[k], &t, sizeof(Ent));
    check(found, "a turbine in its chunk's scenery");
  }
  g_scenery.clear();
  sceneryBakeLandmarks(w);
  const std::vector<PlacedEnt>& again = landmarks();
  bool same = again.size() == items.size();
  for (size_t i = 0; same && i < items.size(); i++) same = again[i].kind == items[i].kind && !memcmp(&again[i].e, &items[i].e, sizeof(Ent));
  check(same, "the landmarks found the same every time");
  if (!turbines.empty()) std::printf("wind farm: %zu turbines, from %.0f %.0f (%.0f m) to %.0f %.0f, facing %.0f deg\n", turbines.size(), turbines.front().x, turbines.front().z,
                                     turbines.front().y, turbines.back().x, turbines.back().z, turbines.front().yaw * 57.2958f);
  for (const PlacedEnt& p : items)
    if (p.kind == EK_SHIPWRECK || p.kind == EK_PLATFORM || (p.kind == EK_SLAB && p.e.sx > 1.f) || p.kind >= EK_OBSERVATORY || p.kind == EK_HOUSE_HIP)
      std::printf("%s at %.0f %.0f (%.1f m), facing %.0f\n", kEntInfo[p.kind].name, p.e.x, p.e.z, p.e.y, p.e.yaw * 57.3f);
  std::printf("landmarks: %zu placed; %d checks, %d failures\n", items.size(), checks, failures);
  return failures ? 1 : 0;
}
