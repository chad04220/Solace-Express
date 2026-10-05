//! kClouds
//! Volumetric clouds: density field, the march with multiple scattering, and the cloud shadow lookup.
// ---------------------------------------------------------------- clouds
// Volumetric cumulus: a coverage field (2D) gives each cloud its footprint; a flat base and a billowing, rounded top
// come from the height profile, two scales of 3D noise carve the billows, and fine 3D detail erodes only the thin
// edges into wisps (the dense cores stay solid).
float cloudDensity(vec3 p, int detail){
  float thick = 900.0 + 900.0*uCloudCover;
  float hf = (p.y - uCloudBase) / thick;
  if (hf < 0.0 || hf > 1.0) return 0.0;
  vec2 q = (p.xz + uWindOff) / 5200.0;
  float cov = textureLod(uCloudCov, q*(1.0/16.0), 0.0).r*0.9375;
  float shape = smoothstep(0.0, 0.07, hf) * smoothstep(1.0, 0.4 - 0.22*uCloudCover, hf);
  float d = cov - (1.05 - uCloudCover*0.75) + shape*0.45 - 0.45;
  if (d < -0.2) return 0.0;
  vec3 w = p + vec3(uWindOff.x, 0.0, uWindOff.y);
  float bill = cn3(w/760.0)*0.6 + cn3(w/270.0 + vec3(11.3, 4.1, 7.7))*0.4;
  d += (bill - 0.55)*0.42*(0.55 + hf);                                       // billows, deeper towards the tops
  if (detail > 0) {
    float e = (cn3(w/95.0 + vec3(3.7, uTime*0.015, 1.9)) - 0.5)*0.17 + (cn3(w/36.0 + vec3(17.1, 9.3, 5.5)) - 0.5)*0.06;
    d += e*(1.0 - smoothstep(0.0, 0.3, d));                                   // wispy edges, solid cores
  }
  return clamp(d*4.5, 0.0, 1.0);
}
float hgPhase(float c, float g){ float g2 = g*g; return (1.0 - g2)/(12.566*pow(max(1.0 + g2 - 2.0*g*c, 1e-4), 1.5)); }
int gCloudLite = 0;   // reflections: half the steps
vec4 traceClouds(vec3 ro, vec3 rd, float tmax, float jitter){
  if (uCloudCover < 0.02 || (uDbg & 1) != 0) return vec4(0.0,0.0,0.0,1.0);
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
  float t = t0 + dt*jitter;
  for (int i=0;i<84;i++){
    if (t > t1) break;
    COST(2);
    vec3 p = ro + rd*t;
    float d = cloudDensity(p, 1);
    if (d <= 0.01) { t += dt*1.5; continue; }   // clear air between clouds: longer strides
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
      float a = 1.0 - exp(-d*dt*0.016);
      float fogT = exp(-uFogB*t*0.6);
      L += T*a*mix(skyH, c, fogT);
      T *= 1.0 - a;
      if (T < 0.02) break;
    }
    t += dt;
  }
  return vec4(L, T);
}
float cloudShadow(vec3 p){
  if (uCloudCover < 0.05 || (uDbg & 32) != 0) return 1.0;
  vec3 c = p + uSunDir * ((uCloudBase + 500.0 - p.y)/max(uSunDir.y, 0.1));
  float d = cloudDensity(vec3(c.x, uCloudBase + 400.0, c.z), 0);
  return mix(1.0, 0.25, smoothstep(0.0, 0.5, d));
}
