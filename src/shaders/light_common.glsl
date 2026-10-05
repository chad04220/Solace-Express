//! kLightCommon
//! Lighting shared by the ray tracer and the raster passes: the baked terrain sun shadow, the scenery shadow cascades,
//! PBR, ambient, the cockpit fixture lights, surface shading with the point lights, and the aerial perspective.
uniform sampler2D uTSh; uniform int uTShOn;   // baked terrain sun shadow (see kTShBakeMain)
float terrainShadow(vec3 ro, vec3 rd, float camT){
  if ((uDbg & 2) != 0) return 1.0;
  if (uTShOn == 1) {   // one lookup instead of a march: the height a point here needs to see the sun, and how far away
    vec2 hd = texture(uTSh, ro.xz/(2.0*WH) + 0.5).xy;   // the terrain that blocks it is (that sets the penumbra)
    return clamp(12.0*length(rd.xz)*(ro.y - hd.x)/max(hd.y, 1.0), 0.0, 1.0);
  }
  float res = 1.0, t = 2.0;
  // distant pixels cover many metres each: fewer steps and octaves are enough there (the profile showed terrain
  // shadows among the most expensive features)
  int n = camT > 6000.0 ? 16 : (camT > 2000.0 ? 26 : 40), oct = camT > 2000.0 ? 3 : 4;
  for (int i=0;i<40;i++){
    if (i >= n) break;
    vec3 p = ro + rd*t;
    if (p.y > uMaxH) break;
    float h = p.y - terrainH(p.xz, oct);
    res = min(res, 12.0*h/t);
    if (res < 0.0) return 0.0;
    t += clamp(h*0.6, 6.0, 450.0);
  }
  return clamp(res, 0.0, 1.0);
}

// ---------------------------------------------------------------- environment entities (G-buffer from the raster pass)
uniform sampler2D uGB0; uniform sampler2D uGB1; uniform sampler2D uGB2;
uniform int uShOn; uniform sampler2DShadow uShMap0; uniform sampler2DShadow uShMap1; uniform mat4 uShM0; uniform mat4 uShM1; uniform vec2 uShTexel;
uniform vec4 uShFade; uniform vec4 uShFadeR;   // camera-anchored fade centres (xz, per cascade) and fade radii
vec3 octDec(vec2 e){ vec3 n = vec3(e.x, 1.0 - abs(e.x) - abs(e.y), e.y); if (n.y < 0.0) n.xz = (1.0 - abs(n.zx))*vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0); return normalize(n); }
// sun shadow of trees, rocks and buildings (two cascades, 2x2 PCF in the comparison sampler; normal offset against acne)
float shTap(sampler2DShadow m, vec3 q, float bias){ return texture(m, vec3(q.xy, q.z - bias)); }
float shCascade(int c, vec3 p, vec3 n){
  vec3 pp = p + n*(c == 0 ? uShTexel.x : uShTexel.y)*1.5;
  vec4 h = (c == 0 ? uShM0 : uShM1)*vec4(pp, 1.0);
  vec3 q = h.xyz*0.5 + 0.5;
  vec2 edge = abs(q.xy - 0.5);
  if (max(edge.x, edge.y) > 0.48 || q.z > 1.0) return 1.0;   // (only steep terrain far above or below the centre)
  return c == 0 ? shTap(uShMap0, q, 0.0004) : shTap(uShMap1, q, 0.0006);
}
// Scenery shadows from two cached sun cascades. Each fades out with distance from a point that moves smoothly with
// the camera, well inside the area its map covers: the near cascade hands over to the far one, the far one to none,
// and a map re-rendering as the camera moves on never makes shadows appear or vanish.
float entShadow(vec3 p, vec3 n){
  if (uShOn == 0 || (uDbg & 4) != 0) return 1.0;
  float w0 = 1.0 - smoothstep(uShFadeR.x, uShFadeR.y, length(p.xz - uShFade.xy));
  float w1 = uShOn > 1 ? 1.0 - smoothstep(uShFadeR.z, uShFadeR.w, length(p.xz - uShFade.zw)) : 0.0;
  float s = 1.0;
  if (w0 < 1.0 && w1 > 0.0) s = mix(1.0, shCascade(1, p, n), w1);
  if (w0 > 0.0) s = mix(s, shCascade(0, p, n), w0);
  return s;
}

// ---------------------------------------------------------------- lighting
vec3 fresnelSchlick(float c, vec3 f0){ float k = clamp(1.0-c, 0.0, 1.0); return f0 + (1.0-f0)*(k*k*k*k*k); }
vec3 pbr(vec3 n, vec3 v, vec3 l, vec3 alb, float rough, float metal, vec3 lightCol){
  vec3 h = normalize(v+l); float nl = max(dot(n,l),0.0), nv = max(dot(n,v),1e-3), nh = max(dot(n,h),0.0), vh = max(dot(v,h),0.0);
  float a = rough*rough, a2 = a*a; float dd = nh*nh*(a2-1.0)+1.0; float D = a2/(PI*dd*dd);
  float k = (rough+1.0)*(rough+1.0)/8.0; float G = nv/(nv*(1.0-k)+k) * nl/(nl*(1.0-k)+k);
  vec3 f0 = mix(vec3(0.04), alb, metal); vec3 F = fresnelSchlick(vh, f0);
  vec3 spec = min(D*G*F/(4.0*nv*max(nl,1e-3)+1e-3), vec3(60.0));  // bounded: tiny glossy parts must not overflow fp16
  vec3 kd = (1.0-F)*(1.0-metal);
  return (kd*alb/PI + spec)*lightCol*nl;
}
vec3 ambientLight(vec3 n){
  vec3 skyUp = skyColor(normalize(vec3(0.3,1.0,0.2)));
  vec3 ground = vec3(0.12,0.11,0.08)*(uSunCol.g + 0.02);
  return mix(ground, skyUp*2.2, n.y*0.5+0.5) + vec3(0.03,0.04,0.07)*uNight*0.4;
}

// ---------------------------------------------------------------- cockpit lighting
// Every interior light comes from a modelled fixture (a lens, LED strip or display): it is treated as a line light
// along that fixture, lighting from the nearest point on it with PBR shading and a soft falloff, so light never
// appears to float in mid-air or pool into a hot spot. Body-space positions, normals and view vector.
vec3 fixtureLight(vec3 p, vec3 n, vec3 v, Mat m, vec3 a, vec3 b, vec3 c, float k, vec3 dir){
  vec3 ab = b - a; float h = clamp(dot(p - a, ab)/max(dot(ab, ab), 1e-6), 0.0, 1.0);
  vec3 d = a + ab*h - p; float dl = max(length(d), 1e-3);
  float beam = dot(dir, dir) > 0.5 ? smoothstep(0.2, 0.75, dot(-d/dl, dir)) : 1.0;   // recessed: lights only the way its lens faces
  return pbr(n, v, d/dl, m.alb, max(m.rough, 0.18), m.metal, c*(9.4*beam/(1.0 + dl*dl*k)));
}
// XR-30 sealed pod: panoramic display, two warm ceiling light bars, cyan spine and console strips, amber footwell, MFDs
vec3 podLight(vec3 p, vec3 n, vec3 v, Mat m, vec3 E){
  vec3 L = m.alb*vec3(0.03, 0.04, 0.055);                                       // faint bounce
  L += fixtureLight(p, n, v, m, E + vec3(-0.45, 0.02, -0.47), E + vec3(0.45, 0.02, -0.47), vec3(0.42, 0.55, 0.68)*0.55, 6.0, vec3(0.0, 0.0, 1.0));
  for (int i = -1; i <= 1; i += 2) {
    float sx = float(i);
    L += fixtureLight(p, n, v, m, E + vec3(0.2*sx, 0.545, -0.04), E + vec3(0.2*sx, 0.545, 0.34), vec3(1.0, 0.8, 0.58)*0.2, 9.0, vec3(0.0, -1.0, 0.0));
    L += fixtureLight(p, n, v, m, E + vec3(0.39*sx, -0.395, -0.25), E + vec3(0.39*sx, -0.395, 0.42), gColStripe*0.05, 14.0, vec3(-0.6*sx, 0.8, 0.0));
  }
  L += fixtureLight(p, n, v, m, E + vec3(0.0, 0.50, -0.55), E + vec3(0.0, 0.60, 0.65), gColStripe*0.06, 10.0, vec3(0.0, -1.0, 0.0));
  L += fixtureLight(p, n, v, m, E + vec3(-0.24, -0.60, -0.42), E + vec3(0.24, -0.60, -0.42), vec3(1.0, 0.5, 0.15)*0.12, 10.0, vec3(0.0, 0.7, 0.7));
  L += fixtureLight(p, n, v, m, E + vec3(-0.42, -0.33, -0.42), E + vec3(0.42, -0.33, -0.42), vec3(0.3, 0.75, 0.6)*0.1, 12.0, vec3(0.0, 0.6, 0.8));
  return L;
}
// Light-aircraft / airliner cabin at night: glareshield LED strip floods the panel, dim amber dome light
vec3 cabinLight(vec3 p, vec3 n, vec3 v, Mat m, vec3 E, float pz, float phw){
  vec3 L = fixtureLight(p, n, v, m, vec3(-phw*0.88, E.y - 0.13, pz + 0.07), vec3(phw*0.88, E.y - 0.13, pz + 0.07), vec3(1.0, 0.84, 0.62)*(0.015 + 0.16*uNight), 18.0, normalize(vec3(0.0, -1.0, 0.35)));
  L += fixtureLight(p, n, v, m, vec3(0.0, gCab0.z - 0.024, E.z - 0.09), vec3(0.0, gCab0.z - 0.024, E.z - 0.05), vec3(1.0, 0.72, 0.45)*0.12*uNight, 8.0, vec3(0.0, -1.0, 0.0));
  return L;
}

// cabin light: the big displays light it with what they show (sky ahead and above, ground below), plus the emitter
// strips, headrest slits and a soft key from the overhead rail
vec3 wraithPodLight(vec3 p, vec3 n, vec3 v, Mat m, vec3 E){
  vec3 fw = uPlaneRot*vec3(0.0, 0.0, -1.0), upw = uPlaneRot*vec3(0.0, 1.0, 0.0);
  float day = smoothstep(-0.08, 0.15, uSunDir.y);
  vec3 skyF = skyColor(normalize(fw + upw*0.15))*0.9, skyU = skyColor(normalize(upw + fw*0.2))*0.9;
  vec3 ground = vec3(0.16, 0.2, 0.12)*(uSunCol*max(uSunDir.y, 0.0)*1.5 + vec3(0.03))*(0.5 + 0.5*day);
  vec3 L = m.alb*vec3(0.02, 0.025, 0.035);
  L += fixtureLight(p, n, v, m, E + vec3(-0.38, 0.07, -1.18), E + vec3(0.38, 0.07, -1.18), skyF*0.32, 2.2, vec3(0.0, 0.0, 1.0));
  for (int i = -1; i <= 1; i += 2) {
    float s = float(i);
    L += fixtureLight(p, n, v, m, E + vec3(0.79*s, -0.03, -0.7), E + vec3(0.79*s, -0.03, -0.05), mix(skyF, ground, 0.5)*0.18, 2.5, vec3(-s, 0.0, 0.0));
    L += fixtureLight(p, n, v, m, E + vec3(0.8*s, 0.315, -1.2), E + vec3(0.8*s, 0.315, 0.7), vec3(0.25, 0.85, 1.0)*0.035, 9.0, vec3(0.0));
    L += fixtureLight(p, n, v, m, E + vec3(0.8*s, -0.675, -1.2), E + vec3(0.8*s, -0.675, 0.7), vec3(0.65, 0.35, 1.0)*0.04, 9.0, vec3(0.0));
  }
  L += fixtureLight(p, n, v, m, E + vec3(-0.35, 0.405, -0.55), E + vec3(0.35, 0.405, -0.55), skyU*0.22, 2.5, vec3(0.0, -1.0, 0.0));
  L += fixtureLight(p, n, v, m, E + vec3(-0.3, -0.77, -0.66), E + vec3(0.3, -0.77, -0.66), ground*0.6 + vec3(0.65, 0.35, 1.0)*0.015, 3.0, vec3(0.0, 1.0, 0.0));
  L += fixtureLight(p, n, v, m, E + vec3(-0.15, 0.37, 0.15), E + vec3(0.15, 0.37, 0.35), vec3(0.85, 0.9, 1.0)*0.07, 7.0, vec3(0.0, -1.0, 0.0));
  return L;
}

// the aircraft's shadow in point light i's beam at p (d from the light along l): the ray tracer marches the airframe,
// the raster passes read their shadow maps
float lightShadow(int i, vec3 p, vec3 n, vec3 l, float d);
vec3 shadeSurface(vec3 p, vec3 n, vec3 rd, Mat m, float shadow){
  vec3 v = -rd;
  vec3 col = pbr(n, v, uSunDir, m.alb, m.rough, m.metal, uSunCol*shadow*3.2);
  col += m.alb*ambientLight(n)*(0.55 + 0.45*n.y);
  // moonlight
  vec3 md = normalize(vec3(-0.4, 0.55, 0.6));
  col += pbr(n, v, md, m.alb, m.rough, m.metal, vec3(0.05,0.07,0.12)*uNight);
  // specular environment reflection
  vec3 r = reflect(rd, n);
  vec3 F = fresnelSchlick(max(dot(n, v), 0.0), mix(vec3(0.04), m.alb, m.metal));
  col += skyColor(normalize(vec3(r.x, abs(r.y), r.z)))*F*(1.0-m.rough)*(1.0-m.rough)*0.8;
  // point and spot lights (exhaust flames, landing lights, nav lights, beacon, strobes, blasts) with ray-traced
  // shadows from the aircraft: shadow rays only where a light contributes visibly
  for (int i = 0; i < 12; i++) {
    if (i >= uPLN || (uDbg & 16) != 0) break;
    COST(3);
    vec3 lv = uPLP[i].xyz - p; float d2 = dot(lv, lv), d = sqrt(d2); vec3 l = lv/max(d, 1e-4);
    float ndl = dot(n, l);
    if (ndl <= 0.0) continue;
    vec3 E = uPLC[i].rgb/(d2 + uPLP[i].w*uPLP[i].w);
    if (uPLC[i].w > -1.5) E *= smoothstep(uPLC[i].w, mix(uPLC[i].w, 1.0, 0.3), dot(-l, uPLD[i].xyz));
    float lum = max(E.r, max(E.g, E.b))*ndl;
    if (lum < 0.0015) continue;
    if (uPLD[i].w > 0.0 && lum > 0.004) E *= lightShadow(i, p, n, l, d);
    col += pbr(n, v, l, m.alb, m.rough, m.metal, E);
  }
  col += m.emit;
  col += m.alb*vec3(0.7,0.75,1.0)*uLightning*0.4;
  return col;
}

// optical depth along a ray through an exponential layer of scale height H: integral of exp(-y/H) over the path
float layerDepth(float y0, float dy, float t, float H){
  float a = exp(-max(y0, 0.0)/H), k = dy*t/H;
  return abs(k) > 1e-3 ? a*H*(1.0 - exp(-k))/dy : a*t;
}
// Aerial perspective: Rayleigh scattering (blue light scatters most, so distance turns hills blue and drains their
// contrast) plus a low haze layer whose density follows the weather's visibility and that glows around the sun.
// Both thin out with altitude. The in-scattered light is the horizon sky's, so far terrain melts into the sky.
vec3 applyFog(vec3 col, vec3 ro, vec3 rd, float t){
  if ((uDbg & 256) != 0) return col;
  float odR = layerDepth(ro.y, rd.y, t, 8000.0), odM = layerDepth(ro.y, rd.y, t, 1100.0);
  vec3 bR = vec3(5.8e-6, 13.5e-6, 33.1e-6);
  float bM = 3e-6 + uFogB*0.8;
  vec3 tau = bR*odR + vec3(bM*odM) + uFogB*0.03*t;
  vec3 T = exp(-tau);
  float mu = dot(rd, uSunDir), mp = max(mu, 0.0);
  vec3 fogCol = skyColor(normalize(vec3(rd.x, 0.06, rd.z)))*vec3(0.9, 0.94, 1.0);
  // forward scattering by the haze: a broad warm glow towards the sun, stronger the hazier the air
  float hazeW = clamp(bM*odM/max(dot(tau, vec3(0.333)), 1e-6), 0.0, 1.0);
  fogCol += uSunCol*(pow(mp, 8.0)*0.22 + pow(mp, 2.5)*0.07*hazeW);
  return col*T + fogCol*(1.0 - T);
}
