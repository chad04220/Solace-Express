//! kRoads
//! The road network (road_network.h) as the terrain grades it in and the ground material paints it, and the forest noise
//! baked beside it. Assembled after the scene uniforms (uData). Never part of the aircraft bodies' bake (PART_BAKE): its
//! declarations would change their cache key, and the bake reads no ground.
#ifndef PART_BAKE
// uRoadGrid: R32UI per mask texel - the baked forest noise (top 7 bits), how many road entries reach the texel (next
// 6), the first of them (low 19); the entries are uData's rows from 1 on, two texels each (Renderer's upload):
// (a.x, a.z, b.x, b.z), (a.h, b.h, distance along the road at a, class + 8 x flags + 128 x banks-only + 256 x path);
// flags 1 a bridge's span (the ground below left alone, no paint), 2 by an airfield's grounds (painted on the ground as
// it is), 4 and 8 its end a or b at a bridge (the platform stopping square at the joint); banks-only: none from this
// entry on in the texel's list paves the texel (each texel's paving entries listed first: the paint reads no further)
uniform usampler2D uRoadGrid;
const float ROAD_BANK_MAX = 50.0;   // the widest cut or fill bank beyond a platform's edge
const float ROAD_BANK_RUN = 2.0;    // a bank's run for its rise, on average
float circuitStartLine(){ return 430.0; }   // the circuit's start line, along it from its pit lane's mouth (road_network.h)
// The Fjordhaven reservoir's lake (world.h Reservoir, LakeMask), from row 0 of uData (Renderer's upload): its level
// (texel 0's w), its mask's square (texel 1: x0, z0, cell, cells a side; 0 cells: no lake) and the row its cells start on
// (texel 2's x: four to a texel); the ground with it built in, exactly World::lakeGround
float lakeLevel(){ return texelFetch(uData, ivec2(0, 0), 0).w; }
float lakeMaskAt(vec2 p){
  vec4 L = texelFetch(uData, ivec2(1, 0), 0);
  int n = int(L.w);
  if (n <= 0) return 0.0;
  vec2 f = (p - L.xy)/L.z - 0.5;
  if (f.x < -1.0 || f.y < -1.0 || f.x > float(n) || f.y > float(n)) return 0.0;
  int row0 = int(texelFetch(uData, ivec2(2, 0), 0).x);
  ivec2 i = ivec2(floor(f)); vec2 t = f - vec2(i);
  float m[4];
  for (int k = 0; k < 4; k++) {
    ivec2 c = i + ivec2(k & 1, k >> 1);
    if (c.x < 0 || c.y < 0 || c.x >= n || c.y >= n) { m[k] = 0.0; continue; }
    int idx = c.y*n + c.x, tx = idx >> 2;
    m[k] = texelFetch(uData, ivec2(tx & 4095, row0 + (tx >> 12)), 0)[idx & 3];
  }
  return (m[0]*(1.0 - t.x) + m[1]*t.x)*(1.0 - t.y) + (m[2]*(1.0 - t.x) + m[3]*t.x)*t.y;
}
float lakeGround(vec2 p, float g){ return lakeMaskAt(p) > 0.5 ? max(g, lakeLevel()) : g; }
float forestTexel(ivec2 i){ return float(texelFetch(uRoadGrid, clamp(i, ivec2(0), ivec2(MASKN-1)), 0).r >> 25u)/127.0; }
float forestAt(vec2 p){
  vec2 f = (p + WH)/MTEX - 0.5; vec2 fl = floor(f); ivec2 i = ivec2(fl); vec2 t = f - fl;
  float a = forestTexel(i), b = forestTexel(i + ivec2(1,0)), c = forestTexel(i + ivec2(0,1)), d = forestTexel(i + ivec2(1,1));
  return ((a*(1.0-t.x)+b*t.x)*(1.0-t.y) + (c*(1.0-t.x)+d*t.x)*t.y)*0.875;
}
vec4 roadTexel(int i){ return texelFetch(uData, ivec2(i & 4095, 1 + (i >> 12)), 0); }
uint roadHead(vec2 p){ return texelFetch(uRoadGrid, clamp(ivec2(floor((wrapW(p) + WH)/MTEX)), ivec2(0), ivec2(MASKN-1)), 0).r; }
float roadHalfPlatform(int c){ return c == 0 ? 16.0 : c == 1 ? 6.5 : c == 2 ? 4.0 : c == 3 ? 2.8 : c == 4 ? 6.0 : c == 5 ? 13.0 : c == 6 ? 16.0 : 7.5; }   // (road_network.h roadSpec)
float roadHalfPaved(int c){ return c == 0 ? 12.4 : c == 1 ? 4.6 : c == 2 ? 2.9 : c == 3 ? 1.9 : c == 4 ? 3.5 : c == 5 ? 11.0 : c == 6 ? 7.5 : 6.0; }
float roadBankMax(int c){ return c == 4 || c == 6 ? 20.0 : c == 3 || c == 5 || c == 7 ? 12.0 : ROAD_BANK_MAX; }   // (road_network.h)
// The ground with the roads built into it, from the natural ground g: each graded road's platform level across at its
// height, its banks blending back to g (bridges and the stretches by an airfield leave the ground alone; an end at a
// bridge stops square, carried on under the deck's slab, then falling away). Mirrors roadGrade in road_network.cpp
// exactly.
float roadGrade(vec2 p, float g){
  uint h = roadHead(p);
  int n = int((h >> 19u) & 63u);
  if (n == 0) return g;
  int e0 = int(h & 0x7FFFFu);
  float W = 0.0, hs = 0.0, ws = 0.0;
  for (int k = 0; k < 63; k++) {
    if (k >= n) break;
    vec4 a = roadTexel(2*(e0 + k)), b = roadTexel(2*(e0 + k) + 1);
    int code = int(b.w);
    if ((code & 24) != 0) continue;
    vec2 ab = a.zw - a.xy; float L2 = max(dot(ab, ab), 1e-3), L = sqrt(L2), tr = dot(p - a.xy, ab)/L2, t = clamp(tr, 0.0, 1.0);
    float d = length(a.xy + ab*t - p), P = roadHalfPlatform(code & 7);
    float hr = b.x + (b.y - b.x)*clamp(tr, -P/L, 1.0 + P/L);   // (past an end: on along its grade)
    float past = max(max(-tr, tr - 1.0), 0.0)*L;
    bool bridgeEnd = (tr < 0.0 && (code & 32) != 0) || (tr > 1.0 && (code & 64) != 0);
    if (bridgeEnd) hr -= 0.3;
    float w = 1.0 - smoothstep(P, P + clamp(abs(g - hr)*ROAD_BANK_RUN, 4.0, roadBankMax(code & 7)), d);
    if (bridgeEnd) w *= 1.0 - smoothstep(2.0, 2.0 + clamp(abs(g - hr)*1.5, 1.0, 20.0), past);
    if (w <= 0.0) continue;
    float w4 = w*w; w4 *= w4;
    float top = smoothstep(0.9, 1.0, w), wt = w4*(1.0 + 200.0*top*top)*(1.0 - 0.9*smoothstep(0.0, P, past));   // (a platform outweighs banks; past its ends a segment gives way)
    W = max(W, w); hs += wt*hr; ws += wt;
  }
  return W > 0.0 ? g + (hs/ws - g)*W : g;
}
// The road at a point (the network, road_network.h), once for the ground's normal and its paint: the nearest platform's
// signed distance across from its centreline (+ right of its direction), the distance along it, its direction and class;
// on: on its paving or verge (bridge decks are their own meshes: never painted here); junction: a second road's paving
// within a metre - its markings stop. And the graded roads' own surface (roadNormal): the nearest of their platforms -
// how far its edge (edge), its normal, level across and tilted along by its grade (nr). Only the texel's entries paving
// it are read: listed first, the rest only bank the ground (marked 128 from the first of them, roadEntryRows).
struct RoadHit { bool on; float across, along; vec2 dir; int cls; bool junction; float edge; vec3 nr; };
RoadHit noRoad(){ RoadHit r; r.on = false; r.across = 1e9; r.along = 0.0; r.dir = vec2(1.0, 0.0); r.cls = 1; r.junction = false; r.edge = 1e9; r.nr = vec3(0.0, 1.0, 0.0); return r; }
RoadHit roadQuery(vec2 p){
  RoadHit r = noRoad();
  uint h = roadHead(p); int n = int((h >> 19u) & 63u);
  if (n == 0) return r;
  int e0 = int(h & 0x7FFFFu), path = -1;
  float best = 1e9, other = 1e9;   // (other: the nearest platform of any road but the nearest's)
  for (int k = 0; k < 63; k++) {
    if (k >= n) break;
    vec4 b = roadTexel(2*(e0 + k) + 1);
    int code = int(b.w);
    if ((code & 128) != 0) break;   // (the rest only bank the ground here)
    if ((code & 8) != 0) continue;   // (a bridge's span)
    vec4 a = roadTexel(2*(e0 + k));
    int c = code & 7, pid = code >> 8;
    vec2 ab = a.zw - a.xy; float L = max(length(ab), 1e-3), tr = dot(p - a.xy, ab)/(L*L), t = clamp(tr, 0.0, 1.0);
    if (((code & 32) != 0 && tr < 0.0) || ((code & 64) != 0 && tr > 1.0)) continue;   // (past an end at a bridge: under its deck)
    vec2 q = p - (a.xy + ab*t), dir = ab/L;
    float e = length(q) - roadHalfPlatform(c);   // (outside the verge: > 0)
    if ((code & 16) == 0 && e < r.edge) { r.edge = e; float gr = (b.y - b.x)/L; r.nr = normalize(vec3(-gr*dir.x, 1.0, -gr*dir.y)); }   // (an airfield's road: on the ground as it is)
    if (e < best) {
      if (pid != path) { other = best; path = pid; }   // (the nearest so far: every other road's nearest)
      best = e; r.cls = c; r.dir = dir; r.along = b.z + t*L;
      r.across = length(q)*(dir.x*q.y - dir.y*q.x < 0.0 ? -1.0 : 1.0);
    } else if (pid != path) other = min(other, e);
  }
  r.on = best < 0.0; r.junction = r.on && other < 1.0;
  return r;
}
// The ground's normal n with a graded road's platform in it: its own, blended back into n over the platform's last metre.
// (A far chunk's cell spans a road and its banks: its normal, their average, lit the paving as though it lay on the
// hillside, and laid rock on it where the banks were steep)
vec3 roadNormal(RoadHit r, vec3 n){ return normalize(mix(n, r.nr, 1.0 - smoothstep(-1.0, 0.0, r.edge))); }
#else
// (the bake: inert stand-ins, so the world library still compiles as assembled; nothing it runs calls them, and the
// pruner drops them with everything else unreachable)
float forestAt(vec2 p){ return 0.0; }
vec4 roadTexel(int i){ return vec4(0.0); }
uint roadHead(vec2 p){ return 0u; }
float roadHalfPlatform(int c){ return 0.0; }
float roadHalfPaved(int c){ return 0.0; }
float roadBankMax(int c){ return 0.0; }
float roadGrade(vec2 p, float g){ return g; }
float circuitStartLine(){ return 0.0; }
float lakeLevel(){ return 0.0; }
float lakeMaskAt(vec2 p){ return 0.0; }
float lakeGround(vec2 p, float g){ return g; }
#endif
