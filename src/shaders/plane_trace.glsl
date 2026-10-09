//! kPlaneTrace
//! Marching the aircraft distance fields: the player (with the hull start), wreck pieces, traffic; their normals, shadows
//! and the cabin ambient occlusion.
vec2 mapPiece(vec3 p){ vec2 d = mapPlane(p); if (gPI >= 0) d.x = max(d.x, sdBox(p - uPcC[gPI], uPcH[gPI])); return d; }
// (The airframe's distance is a very large function: every call written out is another inlined copy in the shader.
// Its multi-tap users loop with a bound the compiler can't see through (gZero, 0 at run time), so they keep one copy.)
int gZero = 0;
vec3 planeNormal(vec3 p){
  // Keep one field call site: large SDF functions must not be duplicated by fallbacks.
  for (int attempt = gZero; attempt < 3; attempt++) {
    float e = attempt == 0 ? 0.0025 : attempt == 1 ? 0.00125 : 0.005;
    vec3 n = vec3(0.0);
    for (int i = gZero; i < 6; i++) {
      if (attempt == 0 && i >= 4) break;
      vec3 k;
      if (attempt == 0) k = 2.0*vec3(float(((i + 3) >> 1) & 1), float((i >> 1) & 1), float(i & 1)) - 1.0;
      else { k = i/2 == 0 ? vec3(1,0,0) : i/2 == 1 ? vec3(0,1,0) : vec3(0,0,1); if ((i & 1) != 0) k = -k; }
      n += k*mapPiece(p + k*e).x;
    }
    float n2 = dot(n,n);
    if (n2 > 1e-20 && n2 < 1e30) return n*inversesqrt(n2);
  }
  // Unresolved gradients do not move a crossing/QEF vertex. The bake reconstructs
  // its normal from oriented adjacent faces before simplification, with a finite gate.
  return vec3(0.0);
}

float planeBound(){ return max(gM[0].x, gM[9].x*2.0)*0.55 + 1.5; }
void pieceXf(int i){ gPI = i; if (i < 0) { gPP = uPlanePos; gPR = uPlaneRot; gPC = vec3(0.0); } else { gPP = uPcPos[i]; gPR = uPcRot[i]; gPC = uPcC[i]; } }
// gPlStart: where the march along this ray may begin (the aircraft hull mesh - see aircraft_hull.cpp); 1e30: the ray
// misses the hull, so the airframe too
// gPlNear: marched as usual up to there first (the hull's faces nearer than that were ignored), then the jump.
float gPlStart = 0.0, gPlNear = 0.0;
vec2 tracePieceOnce(vec3 ro, vec3 rd, float tmax, float br){
  if (gPlStart > 1e29 && gPlNear <= 0.0) return vec2(-1.0);
  vec3 oc = ro - gPP;
  float b = dot(oc, rd), c = dot(oc,oc) - br*br, h = b*b - c;
  if (h < 0.0) return vec2(-1.0);
  h = sqrt(h); float t0 = max(-b-h, 0.0), t1 = min(-b+h, tmax);
  if (t0 > t1) return vec2(-1.0);
  mat3 inv = transpose(gPR);
  vec3 lo = gPC + inv*(ro - gPP), ld = inv*rd;
  float t = t0;
  bool jet = RESEARCH_ON && int(gM[0].z + 0.5) >= 5;   // XR-30 / XR-40: thin flattened shapes need finer steps
  int steps = gPS.w > 0.5 || jet ? 200 : 120;
  float relax = jet ? 0.65 : 0.8;
  // A hit stops anywhere within the threshold of the surface, by an amount that depends on the steps that led there;
  // on a thin rim or bezel that is enough to take the neighbouring face's normal. So it settles onto the surface with
  // two full steps first (in the same loop: a second call of the airframe's distance would double the shader).
  int settle = -1; float hitId = 0.0;
  for (int i=0;i<202;i++){
    if (i >= steps && settle < 0) break;
    if (settle < 0 && t >= gPlNear && gPlStart > t) { if (gPlStart > 1e29) break; t = gPlStart; if (t > t1) break; }
    vec2 d = mapPiece(lo + ld*t);
    if (settle >= 0) { t += d.x; settle++; if (settle == 2) return vec2(t, hitId); continue; }
    if (d.x < 0.0015*max(1.0, t*0.03)) { hitId = d.y; settle = 1; t += d.x; continue; }
    t += d.x*relax;
    if (t > t1) break;
  }
  if (settle >= 0) return vec2(t, hitId);
  return vec2(-1.0);
}
// Leaves gPI/gPP/gPR/gPC set to the piece that was hit (for shading)
vec2 tracePlane(vec3 ro, vec3 rd, float tmax){
  if (uPlaneOn == 0) return vec2(-1.0);
  vec2 best = vec2(-1.0); int bi = uWreck == 0 ? -1 : 0;
  for (int i = gZero; i < 5; i++) {   // the intact airframe, or each wreck piece (one call of the march for both)
    if (i >= max(uWreck, 1)) break;
    int k = uWreck == 0 ? -1 : i;
    pieceXf(k);
    vec2 h = tracePieceOnce(ro, rd, best.x > 0.0 ? best.x : tmax, k < 0 ? planeBound() : length(uPcH[i]) + 0.3);
    if (h.x > 0.0 && (best.x < 0.0 || h.x < best.x)) { best = h; bi = k; }
  }
  pieceXf(bi);
  return best;
}
// The airframe along a camera ray, using the hull's start (hullT: 0 none, 1e30 no airframe on this ray): the first
// uHullNear metres are marched as usual, then the march resumes where the hull says the airframe can begin.
uniform sampler2D uEnv;   // per pixel: terrain start | airframe hull start | traffic hulls' start (terrain_envelope.cpp, aircraft_hull.cpp)
uniform float uHullNear; uniform int uHullOn; uniform int uHullExitOn;   // (uHullExitOn: uEnv's fourth channel holds the end of the hull volume on this ray)
vec2 tracePlaneHull(vec3 ro, vec3 rd, float tmax, float hullT){
  if (hullT > 0.0) { gPlStart = hullT; gPlNear = uHullNear; }
  vec2 h = tracePlane(ro, rd, tmax);   // (one call: each call site is another copy of the airframe's distance)
  gPlStart = 0.0; gPlNear = 0.0;
  return h;
}
// Other aircraft (AI traffic): bounding-sphere culled, then the same SDF march with that aircraft's data loaded.
// Leaves the globals pointing at the closest hit's aircraft; the caller reloads with loadMain() / loadTraffic().
void trafficXf(int k){
  gPI = -1; gPC = vec3(0.0); gPP = texelFetch(uTraffic, ivec2(24, k), 0).xyz;
  gPR = mat3(texelFetch(uTraffic, ivec2(25, k), 0).xyz, texelFetch(uTraffic, ivec2(26, k), 0).xyz, texelFetch(uTraffic, ivec2(27, k), 0).xyz);
}
uniform int uTrafHullOn;   // the traffic's hulls (third channel of uEnv): where any traffic aircraft can begin on this ray
uniform int uTrafMarch;   // bit k: traffic aircraft k has anything to march (a mesh with an empty moving hull has not)
bool gTrafCamRay = false;   // the ray being traced is this pixel's camera ray
vec2 traceTraffic(vec3 ro, vec3 rd, float tmax, out int idx){
  vec2 best = vec2(-1.0); idx = -1;
  float hs = -1.0;   // where the hulls say traffic can begin on this ray (-1: no hull information)
  if (uTrafHullOn == 1 && gTrafCamRay) {
    float hv = texelFetch(uEnv, ivec2(gl_FragCoord.xy), 0).b;
    hs = hv > 1e29 ? 1e30 : (hv > 0.0 ? max(0.0, hv*0.999 - 0.1) : 0.0);
  }
  for (int k = 0; k < 12; k++) {
    if (k >= uTrafficN) break;
    if ((uTrafMarch & (1 << k)) == 0) continue;
    vec4 P = texelFetch(uTraffic, ivec2(24, k), 0);
    // the hull pass projects to 2 km (aircraft_hull.cpp): it speaks only for aircraft wholly inside that range
    bool hulled = hs >= 0.0 && length(P.xyz - ro) + P.w < 1900.0;
    if (hulled && hs > 1e29) continue;
    gPlStart = hulled ? hs : 0.0;
    vec3 oc = ro - P.xyz; float b = dot(oc, rd), h = b*b - dot(oc, oc) + P.w*P.w;
    float lim = best.x > 0.0 ? best.x : tmax;
    if (h < 0.0 || -b + sqrt(h) < 0.0 || -b - sqrt(h) > lim) continue;
    loadTraffic(k); trafficXf(k);
    vec2 hh = tracePieceOnce(ro, rd, lim, P.w);
    if (hh.x > 0.0 && (best.x < 0.0 || hh.x < best.x)) { best = hh; idx = k; }
  }
  gPlStart = 0.0;
  return best;
}
float gShMax = 1e9;   // shadow rays toward a point light stop at it
float gShK = 10.0;    // penumbra sharpness (sun: 10; a point light: its distance over its size)
float pieceShadow(vec3 ro, vec3 rd, float br, int steps){
  vec3 oc = ro - gPP;
  float b = dot(oc, rd), c = dot(oc,oc) - br*br, h = b*b - c;
  if (h < 0.0 || -b + sqrt(max(h,0.0)) < 0.0) return 1.0;
  h = sqrt(h); float t = max(-b-h, 0.0), t1 = -b+h;
  mat3 inv = transpose(gPR); vec3 lo = gPC + inv*(ro - gPP), ld = inv*rd;
  float res = 1.0;
  for (int i=0;i<40;i++){   // (was 56: the aircraft's self-shadow cost 16 ms a frame in the light aircraft's cockpit)
    if (i >= steps) break;
    float d = mapPiece(lo + ld*t).x;
    res = min(res, gShK*d/max(t,0.1));
    if (res < 0.02) return 0.0;
    t += clamp(d, 0.045, 2.0);
    if (t > min(t1, gShMax)) break;
  }
  return clamp(res, 0.0, 1.0);
}
// the sun's shadow of the traffic: marched through each aircraft's own shape like the player's, but only where the
// ray towards the sun crosses that aircraft's bounding sphere (a few hundred pixels each), so it costs next to nothing
// elsewhere. Leaves the aircraft data loaded as it found it.
uniform int uTrafShOn;   // bit k: traffic aircraft k's sun shadow comes from its shadow map (the shadow proxy), not this march
float trafficShadow(vec3 p){
  float s = 1.0; bool moved = false;
  bool own = gOwn; int tk = gTrafK; int keep = gPI; vec3 kP = gPP; mat3 kR = gPR; vec3 kC = gPC;
  for (int k = 0; k < 12; k++) {
    if (k >= uTrafficN) break;
    if ((uTrafShOn & (1 << k)) != 0) continue;
    vec4 P = texelFetch(uTraffic, ivec2(24, k), 0);
    vec3 oc = p - P.xyz; float b = dot(oc, uSunDir), h = b*b - dot(oc, oc) + P.w*P.w;
    if (h < 0.0 || -b + sqrt(h) < 0.0) continue;
    loadTraffic(k); trafficXf(k); moved = true;
    s *= pieceShadow(p, uSunDir, P.w, 24);   // (traffic is seen from further off than the player's aircraft: a coarser march)
    if (s < 0.02) break;
  }
  if (moved) { if (own) loadMain(); else loadTraffic(tk); gPI = keep; gPP = kP; gPR = kR; gPC = kC; }
  return s;
}
float planeShadow(vec3 ro, vec3 rd){
  if (uPlaneOn == 0 || (uDbg & 8) != 0) return 1.0;
  int keep = gPI; vec3 kP = gPP; mat3 kR = gPR; vec3 kC = gPC;
  float res = 1.0;
  for (int i = gZero; i < 5; i++) {   // the intact airframe, or each wreck piece (one call of the march for both)
    if (i >= max(uWreck, 1) || res < 0.02) break;
    int k = uWreck == 0 ? -1 : i;
    pieceXf(k); res = min(res, pieceShadow(ro, rd, k < 0 ? planeBound() : length(uPcH[i]) + 0.3, 40));
  }
  gPI = keep; gPP = kP; gPR = kR; gPC = kC;
  return mix(res, 1.0, uWr[4].w*0.88);   // a cloaked XR-40 barely darkens the ground
}
// shadeSurface's hook in the aircraft's own passes (a cockpit view lights the airframe there): an aircraft's own lights
// hardly ever shadow it, and every march written out is another copy of the airframe's distance in the shader. The
// airframe's shadow on the world in a light's beam is the shadow proxy's (planeLightShadow)
float lightShadow(int i, vec3 p, vec3 n, vec3 l, float d){ return 1.0; }
float planeLightShadow(int i, vec3 p, vec3 n, vec3 l, float d){
  gShMax = d - uPLD[i].w; gShK = clamp(d/max(uPLP[i].w, 0.02), 6.0, 80.0);
  float s = planeShadow(p + n*0.03, l);
  gShMax = 1e9; gShK = 10.0;
  return s;
}

// Cheap ambient occlusion from the cockpit's own distance field (3 taps along the normal)
float gInteriorAO = -1.0;   // the mesh pass: the cabin's ambient occlusion baked per vertex (-1: tap the field)
float interiorAO(vec3 p, vec3 n){
  if ((uDbg & 512) != 0) return 1.0;
  if (gInteriorAO >= 0.0) return gInteriorAO;
#ifdef AF_MESH
  return 1.0;   // (the mesh pass: the cabin's occlusion is baked per vertex - a mesh without it has no cabin)
#endif
  float occ = 0.0, w = 1.0;
  for (int i = 1 + gZero; i <= 3; i++) { float h = 0.02*float(i*i); occ += (h - mapPlane(p + n*h).x)*w; w *= 0.55; }
  return clamp(1.0 - 3.5*occ, 0.3, 1.0);
}
