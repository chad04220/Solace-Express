//! kTerrainTrace
//! Ray marching the terrain heightfield with the max-height mip chain and the envelope start.
// ---------------------------------------------------------------- terrain
uniform sampler2D uHMax;  // conservative max height per cell, mip L = 256>>L cells per side
const int HMAXN = 256; const int HMAXL = 5;
// Ray march the heightfield. Cells the ray passes entirely above (per the max-height mip chain) are skipped,
// which keeps grazing rays over lowlands from running out of steps (they used to fall through to the sea).
// gTStart: where the march may begin (the terrain envelope mesh on this pixel - see terrain_envelope.cpp); 1e30 means
// the ray meets no terrain in the world. A start that turns out to be under the ground (it never should) is ignored.
uniform int uEnvOn; uniform int uHullOn;
float gTStart = 1.0;
float traceTerrain(vec3 ro, vec3 rd, float tmax){
  float t = 1.0;
  if (gTStart > 1e29) return -1.0;
  if (gTStart > t && gTStart < tmax) { vec3 ps = ro + rd*gTStart; if (ps.y > terrainH(ps.xz, 7)) t = gTStart; }
  else if (gTStart >= tmax) return -1.0;
  if (ro.y > uMaxH) { if (rd.y >= 0.0) return -1.0; t = max(t, (ro.y - uMaxH)/(-rd.y)); }
  float lt = t, ldh = 0.0; bool skipped = true;
  int maxSteps = uQuality > 1 ? 360 : (uQuality > 0 ? 270 : 190);
  if ((uDbg & 64) != 0) maxSteps /= 2;
  vec2 ird = vec2(abs(rd.x) > 1e-6 ? 1.0/rd.x : 1e9, abs(rd.z) > 1e-6 ? 1.0/rd.z : 1e9);
  for (int i=0;i<360;i++){
    if (i >= maxSteps || t > tmax) break;
    vec3 p = ro + rd*t;
    if (p.y > uMaxH && rd.y > 0.0) return -1.0;
    bool sk = false;
    for (int L = HMAXL - 1; L >= 0; L--) {
      int n = HMAXN >> L; float cs = 2.0*WH/float(n);
      ivec2 ci = clamp(ivec2(floor((p.xz + WH)/cs)), ivec2(0), ivec2(n - 1));
      float mh = texelFetch(uHMax, ci, L).r;
      if (p.y <= mh) continue;
      vec2 c0 = vec2(ci)*cs - WH;
      vec2 te2 = (mix(c0, c0 + cs, step(0.0, rd.xz)) - ro.xz)*ird;
      float te = min(te2.x, te2.y);
      float ty = rd.y < 0.0 ? (mh - ro.y)/rd.y : 1e9;
      float tn = min(te + 0.05 + t*1e-5, ty);
      if (tn > t + 0.01) { t = tn; sk = true; break; }
    }
    if (sk) { skipped = true; continue; }
    int oct = t < 1500.0 ? 7 : (t < 6000.0 ? 6 : 5);
    float h = terrainH(p.xz, oct);
    float dh = p.y - h;
    if (dh < 0.0015*t) {
      if (skipped) return t;
      return lt + (t - lt) * ldh / max(ldh - dh, 1e-4);
    }
    skipped = false; lt = t; ldh = dh;
    float k = t > 4500.0 ? 0.6 : 0.42;
    t += max(dh*k, 0.2 + 0.0015*t);
  }
  // out of steps while skimming just above the ground: count it as a hit rather than showing the sea through hills
  if (t <= tmax) { vec3 p = ro + rd*t; if (p.y - terrainH(p.xz, 5) < 0.02*t) return t; }
  return -1.0;
}
