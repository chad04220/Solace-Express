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
// The aircraft's wake through the cloud (game.cpp updateCloudWake, aero_wake.h): its path, kept drifting with the
// cloud, and what the airframe's flow has done to the air along it. Its surfaces' vortices (uWakeV: the wing's tips
// and flap edges winding together into the pair, the tailplane's and the fin's dying away) wind the cloud round their
// cores; the pair sinks under its own downwash carrying the air in its oval with it (uWakeP.y: how far, away from
// the lift - towards the airframe's -y, or +y under negative g); the propellers'
// slipstreams twist it, a jet's hot exhaust clears it (uWakeE). Each point of the path keeps the pair as it was there
// (uWakeG: circulation, spacing, the bank the airframe had, the pair's height on it). The air that went through the
// wake - round the path and the curtain the pair left sinking from it - is cleared, and turbulence widens that channel,
// roughens its walls and fills it in again over the next minute; after a while the pair's Crow instability pinches it
// into a chain of bulges (uWakeP.z: the distance along the path in their wavelengths). The cloud pushed aside piles up
// a little round a young channel.
// wakeAt: how much of the cloud at p to take away (up to the segment's strength uWakeP.x; a little below nothing at
// the rim), and where the air now at p was before the aircraft went by (src: the cloud there is the cloud here).
uniform vec4 uWake[20]; uniform vec4 uWakeP[20]; uniform vec4 uWakeG[20]; uniform int uWakeN; uniform vec4 uWakeB;
uniform vec4 uWakeV[10]; uniform int uWakeVN; uniform vec4 uWakeE[4]; uniform int uWakeEN;
uniform vec4 uWakeA;   // the airframe's pair now (circulation, spacing: uWakeV's scale) and how long a flap's vortex takes to wind into its tip's (s)
// q turned back round a vortex core c (circulation gam, core radius rc) by what it has turned it in age seconds: a
// Lamb-Oseen vortex turns the air near its core fastest (a few turns at most: the core's own spreading smears the rest)
vec2 wakeUnwind(vec2 q, vec2 c, float gam, float age, float rc){
  vec2 d = q - c; float r2 = dot(d, d) + 1e-4;
  float a = clamp(gam*age/(6.2831853*r2)*(1.0 - exp(-r2/(rc*rc))), -12.0, 12.0);
  float cs = cos(a), sn = sin(a);
  return c + vec2(d.x*cs + d.y*sn, d.y*cs - d.x*sn);
}
float wakeAt(vec3 p, out vec3 src){
  src = p;
  vec3 b = p - uWakeB.xyz;
  if (dot(b, b) > uWakeB.w*uWakeB.w) return 0.0;
  // the segment this point is most inside (the walls' turbulence below can move it out to 1.5 radii)
  float best = 0.0, bh = 0.0; int bi = -1;
  for (int i = 0; i < 19; i++) {
    if (i + 1 >= uWakeN) break;
    vec4 PA = uWakeP[i];
    if (PA.x <= 0.0) continue;
    vec4 A = uWake[i], B = uWake[i + 1], PB = uWakeP[i + 1];
    vec3 ab = B.xyz - A.xyz, ap = p - A.xyz;
    float h = clamp(dot(ap, ab)/max(dot(ab, ab), 1e-3), 0.0, 1.0);
    vec3 dv = ap - ab*h;
    float r = mix(A.w, B.w, h), D = abs(mix(PA.y, PB.y, h)), reach = 2.2*r + D;   // (1.5 radii, swollen by a Crow bulge and moved by its meander)
    if (dot(dv, dv) >= reach*reach) continue;
    float y = dv.y;
    y -= clamp(y, -D, D);   // (the pair sinks away from the lift: down, or the bank's way)
    float pot = PA.x*(1.0 - smoothstep(0.3, 1.5, length(vec2(length(dv.xz), y))/r));
    if (pot > best) { best = pot; bi = i; bh = h; }
  }
  if (bi < 0) return 0.0;
  vec4 A = uWake[bi], B = uWake[bi + 1], PA = uWakeP[bi], PB = uWakeP[bi + 1], GA = uWakeG[bi], GB = uWakeG[bi + 1];
  vec3 ab = B.xyz - A.xyz, c0 = A.xyz + ab*bh, dv = p - c0;
  vec3 ax = ab*inversesqrt(max(dot(ab, ab), 1e-3)), side = cross(ax, vec3(0.0, 1.0, 0.0));
  side = dot(side, side) > 1e-6 ? normalize(side) : vec3(1.0, 0.0, 0.0);
  vec3 up = cross(side, ax);
  float r = mix(A.w, B.w, bh), D = mix(PA.y, PB.y, bh), age = mix(PA.w, PB.w, bh), K = PA.x;
  float gam = mix(GA.x, GB.x, bh), b0 = max(mix(GA.y, GB.y, bh), 0.5), tilt = mix(GA.z, GB.z, bh), py = mix(GA.w, GB.w, bh);
  // in the airframe's axes as it passed (banked by tilt; across, up), from the pair's height
  float ct = cos(tilt), st = sin(tilt), along = dot(dv, ax);
  vec2 xy = vec2(dot(dv, side), dot(dv, up));
  vec2 q = vec2(xy.x*ct + xy.y*st, xy.y*ct - xy.x*st) - vec2(0.0, py);
  // the air wound back round the vortices, each where it has sunk to, the circulation scaled to the pair's here. Each
  // turns the air on its own side of the pair (between them their shared downwash is the oval's sinking, below); a
  // wing vortex spins the cloud's droplets out of its core, clearing a thin tube along it
  float rc = sqrt(pow(max(0.05*b0, 0.3), 2.0) + 8e-4*abs(gam)*age);
  float kg = abs(uWakeA.x) > 1e-3 ? gam/uWakeA.x : 0.0, kb = b0/max(uWakeA.y, 0.5), m = smoothstep(0.0, max(uWakeA.z, 0.1), age);
  vec2 dsp = vec2(0.0); float core = 0.0;
  for (int k = 0; k < 10; k++) {
    if (k >= max(uWakeVN, 2)) break;
    vec4 v = uWakeVN > 0 ? uWakeV[k] : vec4((k == 0 ? -0.5 : 0.5)*b0, 0.0, (k == 0 ? -1.0 : 1.0)*gam, 1.0);
    float kgv = uWakeVN > 0 ? kg : 1.0, kbv = uWakeVN > 0 ? kb : 1.0;
    vec2 c = vec2(v.x*kbv, v.y);
    float g = v.z*kgv;
    if (v.w > 0.5) c = mix(c, vec2(sign(v.x)*0.5*b0, 0.0), m); else g *= exp(-age/2.5);
    c.y -= D;
    float own = smoothstep(-0.2*b0, 0.2*b0, q.x*(v.x >= 0.0 ? 1.0 : -1.0));
    dsp += (wakeUnwind(q, c, g, age, rc) - q)*own;
    if (v.w > 0.5) { vec2 dc = q - c; core = max(core, exp(-dot(dc, dc)/(4.0*rc*rc))); }
  }
  vec2 s = q + dsp;
  // the pair's oval: the air in it came down with the pair
  float ov = 1.0 - smoothstep(0.85, 1.15, length(vec2(q.x/(1.045*b0), (q.y + D)/(0.865*b0))));
  // the engines: a propeller's slipstream twisted back by its swirl (dying away in a second or two), a jet's hot exhaust
  float hot = 0.0;
  for (int e = 0; e < 4; e++) {
    if (e >= uWakeEN) break;
    vec4 E = uWakeE[e];
    vec2 c = vec2(E.x, E.y - py);
    c.y -= D*(1.0 - smoothstep(0.85, 1.15, length(vec2(c.x/(1.045*b0), c.y/(0.865*b0)))));
    float R = abs(E.z), d = length(s - c);
    if (E.z < 0.0) { float Rh = R*(2.0 + 4.0*sqrt(age)); hot = max(hot, exp(-age/12.0)*exp(-d*d/(Rh*Rh))); }
    else if (E.w != 0.0) {
      float Rw = R*(1.0 + 0.8*sqrt(age)), a = E.w/R*1.5*(1.0 - exp(-age/1.5))*(1.0 - smoothstep(0.8*Rw, 1.2*Rw, d));
      float cs = cos(a), sn = sin(a); vec2 dd = s - c;
      s = c + vec2(dd.x*cs + dd.y*sn, dd.y*cs - dd.x*sn);
    }
  }
  s.y += D*ov;
  // where that air was: back in the world's axes
  vec2 sp = s + vec2(0.0, py), sw = vec2(sp.x*ct - sp.y*st, sp.x*st + sp.y*ct);
  src = c0 + ax*along + side*sw.x + up*sw.y;
  // what the passing cleared: the air round the path and in the curtain the pair left sinking from it (in the
  // airframe's axes), a wider channel as it ages (pinched and bulged by the Crow instability), its walls moved in and
  // out by two scales of noise drifting with the cloud and churning upward - more as it ages, leaving patches of cloud
  float crow = 0.35*smoothstep(15.0, 45.0, age), ph = 6.2831853*mix(PA.z, PB.z, bh);
  float yc = sp.y; yc -= clamp(yc, min(-D, 0.0), max(-D, 0.0));
  float qc = length(vec2(sp.x - 0.35*r*crow*sin(ph + 1.3), yc/0.83))/(r*(1.0 + crow*sin(ph)));   // (the oval: 0.83 as tall as wide)
  float af = smoothstep(0.0, 50.0, age);
  vec3 pw = src + vec3(uWindOff.x, age*0.8, uWindOff.y);
  float n = cn3(pw/26.0)*0.65 + cn3(pw/9.0 + vec3(5.2, 1.3, 7.7))*0.35;
  float qn = qc + (n - 0.5)*(0.3 + 0.5*af);
  float c = K*(1.0 - smoothstep(mix(0.5, 0.15, af), 1.0, qn))*(1.0 - 0.4*af*(1.0 - smoothstep(0.3, 0.55, n)));
  c = max(c, K*max(hot, core));
  return c - 0.3*K*(1.0 - af)*smoothstep(0.95, 1.1, qn)*(1.0 - smoothstep(1.15, 1.45, qn));
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
// how far the light seen along this ray was scattered, weighted by how much each step gave (kCloudMain: the clouds'
// own accumulation reprojects each texel's last value from where its cloud was, not from the sky or the ground behind)
float gCloudW = 0.0, gCloudWT = 0.0;
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
    vec3 ps = p; float wk = 0.0;
    if (uWakeN > 1 && gCloudLite == 0) wk = wakeAt(p, ps);   // (the cloud here is the cloud where the wake's flow brought this air from)
    float d = cloudDensity(ps, 1)*(1.0 - wk);
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
      gCloudW += T*a; gCloudWT += T*a*t;
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
    gCloudW += T*a; gCloudWT += T*a*t;
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
