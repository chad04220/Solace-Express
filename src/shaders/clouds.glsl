//! kClouds
//! Volumetric clouds: density field (shaped by the wind), the aircraft's wake through it, the march with multiple scattering, the rain shafts below, and the cloud shadow lookup.
// ---------------------------------------------------------------- clouds
// Volumetric cumulus: a coverage field (2D) gives each cloud its footprint; a flat base and a billowing, rounded top
// come from the height profile, two scales of 3D noise carve the billows, and fine 3D detail erodes only the thin
// edges into wisps (the dense cores stay solid).
// The wind shapes them (weather.cpp cloudDensity keeps the same field on the CPU): the tops lean downwind, the wind
// being stronger aloft; the fine detail drifts through the cloud bodies (uCloudDet), so wisps stream off their downwind
// edges; the smaller billows rise through the cloud (uCloudBoil), the cumulus boiling.
uniform vec3 uCloudDet; uniform float uCloudBoil;
float cloudDensity(vec3 p, int detail){
  float thick = 900.0 + 900.0*uCloudCover;
  float hf = (p.y - uCloudBase) / thick;
  if (hf < 0.0 || hf > 1.0) return 0.0;
  float U = length(uWindV.xz);
  vec2 lean = (U > 0.1 ? uWindV.xz/U : vec2(0.0))*(hf*min(U*30.0, 450.0));
  vec2 q = (p.xz - lean + uWindOff) / 5200.0;
  float cov = textureLod(uCloudCov, q*(1.0/16.0), 0.0).r*0.9375;
  float shape = smoothstep(0.0, 0.07, hf) * smoothstep(1.0, 0.4 - 0.22*uCloudCover, hf);
  float d = cov - (1.05 - uCloudCover*0.75) + shape*0.45 - 0.45;
  if (d < -0.2) return 0.0;
  vec3 w = p + vec3(uWindOff.x - lean.x, 0.0, uWindOff.y - lean.y);
  float bill = cn3(w/760.0)*0.6 + cn3((w - vec3(0.0, uCloudBoil, 0.0))/270.0 + vec3(11.3, 4.1, 7.7))*0.4;
  d += (bill - 0.55)*0.42*(0.55 + hf);                                       // billows, deeper towards the tops
  if (detail > 0) {
    vec3 wv = w + uCloudDet;
    float e = (cn3(wv/95.0 + vec3(3.7, 0.0, 1.9)) - 0.5)*0.17 + (cn3(wv/36.0 + vec3(17.1, 9.3, 5.5)) - 0.5)*0.06;
    d += e*(1.0 - smoothstep(0.0, 0.3, d));                                   // wispy edges, solid cores
  }
  return clamp(d*4.5, 0.0, 1.0);
}
// The aircraft's wake through the cloud: its path (points with the tunnel's radius there, kept drifting with the
// cloud) carves a tunnel that opens behind it, widens and fills in again over the next minute (game.cpp
// updateCloudWake). 0 outside, up to the segment's strength along its axis.
uniform vec4 uWake[20]; uniform float uWakeK[20]; uniform int uWakeN; uniform vec4 uWakeB;
float wakeCarve(vec3 p){
  vec3 b = p - uWakeB.xyz;
  if (dot(b, b) > uWakeB.w*uWakeB.w) return 0.0;
  float c = 0.0;
  for (int i = 0; i < 19; i++) {
    if (i + 1 >= uWakeN) break;
    if (uWakeK[i] <= 0.0) continue;
    vec4 A = uWake[i], B = uWake[i + 1];
    vec3 ab = B.xyz - A.xyz, ap = p - A.xyz;
    float h = clamp(dot(ap, ab)/max(dot(ab, ab), 1e-3), 0.0, 1.0);
    vec3 dv = ap - ab*h;
    float r = mix(A.w, B.w, h), d2 = dot(dv, dv);
    if (d2 >= r*r) continue;
    c = max(c, uWakeK[i]*(1.0 - smoothstep(0.55*r, r, sqrt(d2))));
  }
  return c;
}
// Rain shafts: the rain under the cloud cells, from the base to the ground and carried downwind as it falls (snow
// further), grey curtains under the heavier clouds - the same field as the rain round the aircraft (weather.cpp rainAt)
float rainColumn(vec3 p){
  float U = length(uWindV.xz);
  vec2 wd = U > 0.1 ? uWindV.xz/U : vec2(0.0);
  float drift = min(max(uCloudBase - p.y, 0.0)*U/(uSnow > 0.0 ? 1.5 : 9.0), 3000.0);
  float cov = textureLod(uCloudCov, (p.xz - wd*drift + uWindOff)/(5200.0*16.0), 0.0).r*0.9375;
  float c = cov - (1.05 - uCloudCover*0.75);
  return smoothstep(-0.25, 0.15, c)*(0.35 + 0.65*smoothstep(0.42, 0.62, cov))*(uStorm > 0.5 ? 1.0 : 0.85);
}
float hgPhase(float c, float g){ float g2 = g*g; return (1.0 - g2)/(12.566*pow(max(1.0 + g2 - 2.0*g*c, 1e-4), 1.5)); }
int gCloudLite = 0;   // reflections: half the steps, no rain shafts, no wake
vec4 cloudLayer(vec3 ro, vec3 rd, float tmax, float jitter){
  float thick = 900.0 + 900.0*uCloudCover;
  float yb = uCloudBase, yt = uCloudBase + thick;
  float t0, t1;
  if (abs(rd.y) < 1e-4) { if (ro.y < yb || ro.y > yt) return vec4(0,0,0,1); t0 = 0.0; t1 = 30000.0; }
  else {
    float ta = (yb - ro.y)/rd.y, tb = (yt - ro.y)/rd.y;
    t0 = max(min(ta,tb), 0.0); t1 = max(ta,tb);
  }
  t1 = min(t1, min(tmax, 45000.0));
  if (t1 <= t0) return vec4(0,0,0,1);
  int N = (uQuality > 1 ? 56 : (uQuality > 0 ? 40 : 24)) >> gCloudLite;
  float dt = (t1 - t0)/float(N);
  float T = 1.0; vec3 L = vec3(0.0);
  float mu = dot(rd, uSunDir);
  vec3 sunC = uSunCol; vec3 amb = skyColor(vec3(0.0,1.0,0.0))*1.4 + vec3(0.05);
  vec3 skyH = skyColor(normalize(vec3(rd.x, max(rd.y,0.02), rd.z)));
  // phase: a strong forward lobe (silver linings towards the sun) plus some back-scatter
  float ph0 = mix(hgPhase(mu, 0.8), hgPhase(mu, -0.25), 0.3);
  float ph1 = mix(hgPhase(mu, 0.4), hgPhase(mu, -0.12), 0.3), ph2 = mix(hgPhase(mu, 0.2), hgPhase(mu, -0.06), 0.3);
  // (from inside the layer a ray can run on for tens of km: there the steps start short and lengthen with distance -
  // the same number of them - so the cloud round the aircraft, and its wake through it, is sampled finely)
  bool inside = t0 <= 0.0 && t1 - t0 > 1500.0;
  float span = t1 - t0;
  float t = inside ? 12.0*jitter : t0 + dt*jitter;
  for (int i=0;i<84;i++){
    if (t > t1) break;
    COST(2);
    float dts = inside ? clamp(2.0*sqrt(max(t, 1.0)*span)/float(N), 8.0, dt*2.5) : dt;
    vec3 p = ro + rd*t;
    float d = cloudDensity(p, 1);
    if (uWakeN > 1 && gCloudLite == 0 && d > 0.01) d *= 1.0 - wakeCarve(p);
    if (d <= 0.01) { t += dts*1.5; continue; }   // clear air between clouds: longer strides
    {
      // light march towards the sun: optical depth through the cloud above this point
      float od = (cloudDensity(p + uSunDir*60.0, 0)*60.0 + cloudDensity(p + uSunDir*160.0, 0)*100.0
                + cloudDensity(p + uSunDir*340.0, 0)*180.0 + cloudDensity(p + uSunDir*650.0, 0)*310.0)*0.012;
      // multiple scattering (three octaves, each less absorbed and less directional) and the powder darkening of
      // thin edges seen side-on to the sun
      vec3 ms = vec3(exp(-od)*ph0 + 0.5*exp(-od*0.5)*ph1 + 0.25*exp(-od*0.25)*ph2);
      float powder = mix(1.0, 1.0 - exp(-d*7.0), 0.6*(1.0 - max(mu, 0.0)));
      float hf = clamp((p.y - uCloudBase)/thick, 0.0, 1.0);
      vec3 c = sunC*ms*powder*11.0 + amb*(0.3 + 0.7*hf)*(1.0 - 0.4*uStorm)*(0.75 + 0.25*exp(-od*0.3));
      c += vec3(0.8,0.85,1.0)*uLightning*2.0;
      float a = 1.0 - exp(-d*dts*0.016);
      float fogT = exp(-uFogB*t*0.6);
      L += T*a*mix(skyH, c, fogT);
      T *= 1.0 - a;
      if (T < 0.02) break;
    }
    t += dts;
  }
  return vec4(L, T);
}
// the rain shafts along the part of the ray below the base: steps closer together near the eye
vec4 rainShafts(vec3 ro, vec3 rd, float tmax, float jitter){
  float yb = uCloudBase, t0 = 0.0, t1 = min(tmax, 24000.0);
  if (ro.y > yb) { if (rd.y > -1e-4) return vec4(0.0, 0.0, 0.0, 1.0); t0 = (yb - ro.y)/rd.y; }
  else if (rd.y > 1e-4) t1 = min(t1, (yb - ro.y)/rd.y);
  if (t1 <= t0) return vec4(0.0, 0.0, 0.0, 1.0);
  vec3 skyH = skyColor(normalize(vec3(rd.x, max(rd.y, 0.02), rd.z)));
  // (the rain under a cloud is in its shade: darker than the haze round it, a snow shower paler)
  vec3 col = (skyColor(vec3(0.0, 1.0, 0.0))*1.1 + vec3(0.02))*(uSnow > 0.0 ? vec3(0.9, 0.92, 0.96) : vec3(0.36, 0.38, 0.42))*(1.0 - 0.3*uStorm)
           + vec3(0.8, 0.85, 1.0)*uLightning*0.6;
  float k = uSnow > 0.0 ? 4.0e-4 : 4.5e-4;   // extinction of the heaviest rain (1/m): a couple of km of it hides what is behind
  float T = 1.0; vec3 L = vec3(0.0);
  const int N = 12;
  for (int i = 0; i < N; i++) {
    float sa = float(i)/float(N), sb = float(i + 1)/float(N), sm = (float(i) + jitter)/float(N);
    float ta = t0 + (t1 - t0)*sa*sa, tb = t0 + (t1 - t0)*sb*sb, t = t0 + (t1 - t0)*sm*sm;
    vec3 p = ro + rd*t;
    float r = rainColumn(p)*smoothstep(yb, yb - 150.0, p.y);
    float a = 1.0 - exp(-k*smoothstep(0.35, 0.95, r)*(tb - ta));
    L += T*a*mix(skyH, col, exp(-uFogB*t*0.6));
    T *= 1.0 - a;
  }
  return vec4(L, T);
}
vec4 traceClouds(vec3 ro, vec3 rd, float tmax, float jitter){
  if (uCloudCover < 0.02 || (uDbg & 1) != 0) return vec4(0.0,0.0,0.0,1.0);
  vec4 c = cloudLayer(ro, rd, tmax, jitter);
  if (uWet + uSnow <= 0.0 || gCloudLite != 0) return c;
  vec4 r = rainShafts(ro, rd, tmax, jitter);   // (below the base: in front of the layer from under it, behind it from above)
  return ro.y < uCloudBase ? vec4(r.rgb + r.a*c.rgb, r.a*c.a) : vec4(c.rgb + c.a*r.rgb, c.a*r.a);
}
float cloudShadow(vec3 p){
  if (uCloudCover < 0.05 || (uDbg & 32) != 0) return 1.0;
  vec3 c = p + uSunDir * ((uCloudBase + 500.0 - p.y)/max(uSunDir.y, 0.1));
  float d = cloudDensity(vec3(c.x, uCloudBase + 400.0, c.z), 0);
  return mix(1.0, 0.25, smoothstep(0.0, 0.5, d));
}
