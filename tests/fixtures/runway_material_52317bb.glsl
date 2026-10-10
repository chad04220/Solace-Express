// Frozen runway-only shader functions from 52317bb1f74a5ee9e66e063e2f17d5be786955a0.
// Off-runway aptGround implementation is intentionally outside this contract.

int airportAt(vec2 p, out vec2 uv, out vec2 along){
  for (int i=0;i<16;i++){
    if (i >= uApCount) break;
    vec4 a = uAp[i]; vec4 d = uApDim[i];
    vec2 dp = p - a.xy; float s = sin(a.w), c = cos(a.w);
    float u = dp.x*s - dp.y*c, v = dp.x*c + dp.y*s;
    if (abs(u) < d.x*0.5 + 600.0 && abs(v) < 600.0) { uv = vec2(u, v); along = vec2(s, -c); return i; }
  }
  along = vec2(1.0, 0.0); return -1;
}

float seg7(vec2 q, int d){
  if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) return 0.0;
  d = clamp(d, 0, 9);
  // a select chain, not a local array: NVIDIA gives every inlined copy of an indexed array its own scratch resource
  int b = d < 5 ? (d == 0 ? 0x3F : d == 1 ? 0x06 : d == 2 ? 0x5B : d == 3 ? 0x4F : 0x66)
                : (d == 5 ? 0x6D : d == 6 ? 0x7D : d == 7 ? 0x07 : d == 8 ? 0x7F : 0x6F);
  float w = 0.16; float on = 0.0;
  // a top, b top-right, c bottom-right, d bottom, e bottom-left, f top-left, g middle
  if ((b & 1) != 0 && q.y > 1.0 - w) on = 1.0;
  if ((b & 2) != 0 && q.x > 1.0 - w && q.y > 0.5) on = 1.0;
  if ((b & 4) != 0 && q.x > 1.0 - w && q.y < 0.5) on = 1.0;
  if ((b & 8) != 0 && q.y < w) on = 1.0;
  if ((b & 16) != 0 && q.x < w && q.y < 0.5) on = 1.0;
  if ((b & 32) != 0 && q.x < w && q.y > 0.5) on = 1.0;
  if ((b & 64) != 0 && abs(q.y - 0.5) < w*0.5) on = 1.0;
  return on;
}

float rwyDigits(vec2 q, int num){
  // two digits side by side, each 7 m wide x 18 m long, 3 m gap; q relative to the block centre (x across, y along)
  int d0 = num/10, d1 = num - d0*10;
  float a = seg7(vec2((q.x + 8.5)/7.0, q.y/18.0 + 0.5), d0);
  float b = seg7(vec2((q.x - 1.5)/7.0, q.y/18.0 + 0.5), d1);
  return max(a, b);
}

void runwayMaterial(int ai, vec2 uv, inout Mat m, vec3 pw, vec2 footprint, out bool onRw, out bool paved){
  vec4 d = uApDim[ai]; float len = d.x, wid = d.y; int surf = int(d.z); int size = int(d.w);
  onRw = false; paved = false;
  float u = uv.x, v = uv.y;
  vec3 nTS;
  float side = (ai - (ai/2)*2) == 1 ? 1.0 : -1.0;
  float off = wid*0.5 + (size == 2 ? 170.0 : 85.0);
  int padLayer = size == 2 ? M_CONCRETE : M_ASPHALT;
  if (abs(v) > wid*0.5 + 3.0 && aptGround(ai, uv, pw, surf, size, len, wid, m)) { paved = surf == 0; return; }
  if (abs(u) > len*0.5 + 6.0 || abs(v) > wid*0.5 + 3.0) {
    if (abs(u) < len*0.5 + 60.0 && abs(v) < wid*0.5 + 7.5 && surf == 0) {
      // paved blast pad / shoulders with yellow chevrons
      vec4 t = matSample(pw.xz, M_ASPHALT, 7.0, nTS);
      m.alb = t.rgb*0.9; m.rough = t.a; m.nrm = nTS; paved = true;
      float bu = abs(u) - len*0.5;
      if (bu > 6.0 && abs(v) < wid*0.5) m.alb = mix(m.alb, vec3(0.6,0.48,0.05), terrainStripeCoverage((bu + abs(v)*1.2)/14.0, 0.12, (footprint.x + footprint.y*1.2)/14.0));
      return;
    }
    if (abs(u) < len*0.5 + 120.0 && abs(v) < wid*0.5 + 60.0 && (surf == 0 || surf == 1)) {
      vec4 t = matSample(pw.xz, M_GRASS, 5.0, nTS);
      float stripe = 1.0 - terrainStripeCoverage(u/18.0, 0.5, footprint.x/18.0);
      m.alb = t.rgb*(0.85 + 0.15*stripe)*vec3(0.95,1.05,0.9); m.rough = 0.9; m.nrm = nTS;
      if (surf != 0) m.alb *= vec3(0.78, 0.82, 0.7);   // longer, darker rough beside an unpaved strip: the strip stands out
    }
    return;
  }
  onRw = true; paved = surf == 0;
  int layer = surf == 0 ? padLayer : surf == 1 ? M_GRASS : surf == 2 ? M_GRAVEL : surf == 3 ? M_SNOW : M_SAND;
  vec4 t = matSample(pw.xz, layer, surf == 0 ? 7.0 : 5.0, nTS);
  m.alb = t.rgb; m.rough = t.a; m.nrm = nTS; m.metal = 0.0;
  if (surf == 1) {   // mown strip: short, lighter, yellower grass in lengthwise mowing bands, worn wheel tracks
    m.alb *= vec3(1.12, 1.18, 0.82) * (0.84 + 0.16*(1.0 - terrainStripeCoverage((v + wid*0.5)/4.5, 0.5, footprint.y/4.5)));
    float track = smoothstep(1.4, 0.4, abs(abs(v) - 1.3))*(0.7 + 0.3*vnoise(vec2(u*0.08, v)));
    m.alb = mix(m.alb, vec3(0.32, 0.27, 0.17), track*0.55);
    float worn = smoothstep(len*0.5 - 40.0, len*0.5 - 160.0, abs(u))*smoothstep(len*0.5 - 260.0, len*0.5 - 120.0, abs(u));
    m.alb = mix(m.alb, vec3(0.36, 0.3, 0.2), worn*smoothstep(0.55, 0.8, vnoise(pw.xz*0.25))*0.6);   // bare patches at touchdown
  }
  if (surf >= 2) { float rut = smoothstep(1.5, 0.3, abs(abs(v) - 2.2)); m.alb *= 1.0 - 0.18*rut; }
  if (surf != 0) {   // white edge boards every 50 m and a row of them across each threshold, as on real unpaved strips
    float au = abs(u), hl = len*0.5;
    float eb = abs(fract((u + hl)/50.0 + 0.5) - 0.5)*50.0;
    bool board = (eb < 0.9 && abs(abs(v) - wid*0.5) < 0.35)
              || (au > hl - 4.0 && au < hl - 2.6 && abs(fract(v/4.0 + 0.5) - 0.5)*4.0 < 0.9);
    if (board) { m.alb = vec3(0.85); m.rough = 0.6; m.nrm = vec3(0,0,1); }
  }
  if (surf == 0) {
    if (size == 2) {
      float joint = max(terrainStripeCoverage(u/7.5 + 0.01, 0.02, footprint.x/7.5), terrainStripeCoverage(v/7.5 + 0.01, 0.02, footprint.y/7.5));
      m.alb *= 1.0 - 0.22*joint;
    } else {
      float gr = smoothstep(0.42, 0.5, abs(fract(u/0.04) - 0.5));
      m.nrm.y += gr*0.2*terrainDetailWeight(footprint.x/0.04);   // 4 cm grooves only while resolved
    }
    float tz = smoothstep(len*0.5 - 80.0, len*0.5 - 200.0, abs(u)) * smoothstep(len*0.5 - 650.0, len*0.5 - 300.0, abs(u));
    float tyre = tz * smoothstep(wid*0.3, 0.0, abs(abs(v) - 3.5)) * (0.5 + 0.5*vnoise(vec2(u*0.05, v*2.0)));
    m.alb *= 1.0 - 0.55*tyre;
    m.rough = mix(m.rough, 0.45, tyre);
    float paint = 0.0;
    float au = abs(u), hl = len*0.5;
    if (au < hl - 70.0) paint = terrainLineCoverage(v, 0.45, footprint.y)*terrainStripeCoverage(u/50.0, 0.6, footprint.x/50.0);
    paint = max(paint, terrainLineCoverage(abs(v) - (wid*0.5 - 1.0), 0.45, footprint.y));
    if (au > hl - 50.0 && au < hl - 12.0 && abs(v) < wid*0.5 - 3.0) paint = max(paint, terrainStripeCoverage((v + wid*0.5)/3.6, 0.5, footprint.y/3.6));
    if (au > hl - 380.0 && au < hl - 320.0) paint = max(paint, terrainLineCoverage(abs(v) - wid*0.25, 2.5, footprint.y));
    if (au > hl - 300.0 && au < hl - 150.0 && wid > 25.0) paint = max(paint, terrainLineCoverage(abs(v) - wid*0.22, 2.5, footprint.y)*terrainStripeCoverage(au/75.0, 0.3, footprint.x/75.0));
    // runway designators, readable from the approach end
    float hdg = uAp[ai].w*57.29578;
    int n0 = int(floor(mod(hdg, 360.0)/10.0 + 0.5)); if (n0 == 0) n0 = 36;
    int n1 = int(floor(mod(hdg + 180.0, 360.0)/10.0 + 0.5)); if (n1 == 0) n1 = 36;
    if (u < -hl + 100.0 && u > -hl + 55.0) paint = max(paint, rwyDigits(vec2(v, u - (-hl + 78.0)), n0));
    if (u > hl - 100.0 && u < hl - 55.0) paint = max(paint, rwyDigits(vec2(-v, -(u - (hl - 78.0))), n1));
    m.alb = mix(m.alb, vec3(0.86), paint*0.92);
    m.rough = mix(m.rough, 0.55, paint);
  }
  // pools of light from the edge and threshold lights (lamps 0.35 m up on their stalks: E = I h / d^3)
  if (uRwyLights > 0.01) {
    float hl = 0.35;
    float ku = clamp(floor((u + len*0.5)/60.0 + 0.5), 0.0, floor(len/60.0));
    float du = u - (-len*0.5 + ku*60.0), dv = abs(v) - (wid*0.5 + 1.5);
    float d2 = du*du + dv*dv + hl*hl;
    vec3 lc = abs(-len*0.5 + ku*60.0) > len*0.5 - 600.0 && size > 0 ? vec3(1.0, 0.7, 0.25) : vec3(1.0, 0.93, 0.78);
    vec3 E = lc*hl/(d2*sqrt(d2));
    float tu = abs(u) - (len*0.5 + 1.0), tv = v - clamp(floor(v/3.0 + 0.5)*3.0, -wid*0.5, wid*0.5);
    float t2 = tu*tu + tv*tv + hl*hl;
    E += (u < 0.0 ? vec3(0.15, 1.0, 0.35) : vec3(1.0, 0.12, 0.08))*hl/(t2*sqrt(t2))*step(abs(v), wid*0.5 + 1.0);
    m.emit += m.alb*E*uRwyLights*6.0;
  }
}
