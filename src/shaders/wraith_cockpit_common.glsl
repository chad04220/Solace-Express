//! kWraithCockpitCommon
//! The XR-40 cockpit's display panes: their frames, shapes and edges, shared by its distance field and its materials.
// ---------------------------------------------------------------- XR-40 cockpit
uniform vec4 uPip;   // bomb impact prediction (world) + valid flag
uniform vec4 uFeed;     // bomb camera look-at point (world) + active flag
// a flat display pane: c centre, n facing the pilot, up hint, half size, corner chamfer. x = pane, y = raised bezel
vec3 wrFrame(vec3 q, vec3 c, vec3 n, vec3 up){ vec3 t = normalize(cross(up, n)), b = cross(n, t); vec3 d = q - c; return vec3(dot(d, t), dot(d, b), dot(d, n)); }
float wrShape(vec2 l, vec2 hs, float ch){ vec2 a = abs(l); return max(max(a.x - hs.x, a.y - hs.y), (a.x + a.y - (hs.x + hs.y - ch))*0.70711); }
vec2 wrPane(vec3 q, vec3 c, vec3 n, vec3 up, vec2 hs, float ch){
  vec3 l = wrFrame(q, c, n, up);
  float s = wrShape(l.xy, hs, ch);
  return vec2(max(s, abs(l.z) - 0.004), max(max(s - 0.034, -s), abs(l.z - 0.006) - 0.013));
}
// pane layout (cabin frame: eye at the origin, -z forward)
// the front display: one seamless panorama on a vertical cylinder ahead of the pilot (centre WF_C, radius WF_R), its
// outline in (arc length, height): half width WF_S.x, half height WF_S.y, the upper corners cut along the cabin's
// octagonal section. Fed by one wide camera at the nose (feed_cameras.h).
const vec3 WF_C = vec3(0.0, 0.07, -0.15);   const float WF_R = 1.0;  const vec2 WF_S = vec2(0.84, 0.33);
vec3 wrFront(vec3 q){ vec2 d = q.xz - WF_C.xz; return vec3(WF_R*atan(d.x, -d.y), q.y - WF_C.y, WF_R - length(d)); }   // arc, height, depth
float wrFrontShape(vec3 q, vec2 l){ return max(max(abs(l.x) - WF_S.x, abs(l.y) - WF_S.y), abs(q.x) + q.y + 0.18 - 1.2); }
const vec3 WS_C = vec3(0.8, -0.075, -0.4);  const vec3 WS_N = vec3(-1.0, 0.0, 0.0);      const vec2 WS_S = vec2(0.34, 0.345);
const vec3 WA_C = vec3(0.8, -0.075, 0.34);  const vec2 WA_S = vec2(0.25, 0.345);   // aft side displays (same facing as WS)
const vec3 WO_C = vec3(0.0, 0.403, -0.55);  const vec3 WO_N = vec3(0.0, -1.0, 0.0);       const vec2 WO_S = vec2(0.42, 0.36);
const vec3 WC_C = vec3(0.0, -0.5, -1.0);    const vec3 WC_N = vec3(0.0, 0.7509, 0.6604);  const vec2 WC_S = vec2(0.38, 0.2);
const vec3 WL_C = vec3(0.0, -0.775, -0.66); const vec3 WL_N = vec3(0.0, 1.0, 0.0);       const vec2 WL_S = vec2(0.4, 0.34);
const vec3 WD_C = vec3(0.0, -0.34, -0.99);  const vec3 WD_N = vec3(0.0, 0.6, 0.8);
const vec3 WB_C = vec3(0.47, -0.705, 0.4);   const vec3 WB_N = vec3(-0.3714, 0.9285, 0.0); const vec2 WB_S = vec2(0.14, 0.2);
// distance (negative inside) to the edge of the display a point lies on, for the HUD frame and vignette
float wrScreenEdge(vec3 q, int id){
  if (id == 41) return wrFrontShape(q, wrFront(q).xy);
  if (id == 42 || id == 43) { vec3 aq = vec3(abs(q.x), q.y, q.z);
    return min(wrShape(wrFrame(aq, WS_C, WS_N, vec3(0,1,0)).xy, WS_S, 0.13), wrShape(wrFrame(aq, WA_C, WS_N, vec3(0,1,0)).xy, WA_S, 0.11)); }
  if (id == 61) return q.z > 0.1 ? wrShape(wrFrame(vec3(abs(q.x), q.y, q.z), WB_C, WB_N, vec3(0,0,-1)).xy, WB_S, 0.07) : wrShape(wrFrame(q, WL_C, WL_N, vec3(0,0,-1)).xy, WL_S, 0.1);
  if (id == 62) return wrShape(wrFrame(q, WO_C, WO_N, vec3(0,0,-1)).xy, WO_S, 0.12);
  return wrShape(wrFrame(q, WC_C, WC_N, vec3(0,1,0)).xy, WC_S, 0.07);
}
