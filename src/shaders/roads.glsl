//! kRoads
//! The road network (road_network.h) as the terrain grades it in and the ground material paints it, and the forest noise
//! baked beside it. Assembled after the scene uniforms (uData). Never part of the aircraft bodies' bake (PART_BAKE): its
//! declarations would change their cache key, and the bake reads no ground.
#ifndef PART_BAKE
// uRoadGrid: R32UI per mask texel - the baked forest noise (top 7 bits), how many road entries reach the texel (next
// 6), the first of them (low 19); the entries are uData's rows from 1 on, two texels each (Renderer's upload):
// (a.x, a.z, b.x, b.z), (a.h, b.h, distance along the road at a, class + 8 x flags + 128 x path); flags 1 a bridge's
// span (the ground below left alone, no paint), 2 by an airfield's grounds (painted on the ground as it is), 4 and 8
// its end a or b at a bridge (the platform stopping square at the joint)
uniform usampler2D uRoadGrid;
const float ROAD_BANK_MAX = 50.0;   // the widest cut or fill bank beyond a platform's edge
const float ROAD_BANK_RUN = 2.0;    // a bank's run for its rise, on average
float forestTexel(ivec2 i){ return float(texelFetch(uRoadGrid, clamp(i, ivec2(0), ivec2(MASKN-1)), 0).r >> 25u)/127.0; }
float forestAt(vec2 p){
  vec2 f = (p + WH)/MTEX - 0.5; vec2 fl = floor(f); ivec2 i = ivec2(fl); vec2 t = f - fl;
  float a = forestTexel(i), b = forestTexel(i + ivec2(1,0)), c = forestTexel(i + ivec2(0,1)), d = forestTexel(i + ivec2(1,1));
  return ((a*(1.0-t.x)+b*t.x)*(1.0-t.y) + (c*(1.0-t.x)+d*t.x)*t.y)*0.875;
}
vec4 roadTexel(int i){ return texelFetch(uData, ivec2(i & 4095, 1 + (i >> 12)), 0); }
uint roadHead(vec2 p){ return texelFetch(uRoadGrid, clamp(ivec2(floor((wrapW(p) + WH)/MTEX)), ivec2(0), ivec2(MASKN-1)), 0).r; }
float roadHalfPlatform(int c){ return c == 0 ? 16.0 : c == 1 ? 6.5 : c == 2 ? 4.0 : c == 3 ? 2.8 : 6.0; }   // (road_network.h roadSpec)
float roadHalfPaved(int c){ return c == 0 ? 12.4 : c == 1 ? 4.6 : c == 2 ? 2.9 : c == 3 ? 1.9 : 3.5; }
float roadBankMax(int c){ return c == 4 ? 20.0 : ROAD_BANK_MAX; }
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
#endif
