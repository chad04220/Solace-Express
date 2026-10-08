//! kWater
//! The sea surface shading.
uniform sampler2DArray uWaves;   // the wave bands (Renderer::genWaves): slope x, slope z (along / across the wind), height
uniform vec3 uWaveRms;           // each band's share of the sea's slope (rms, sums to 1 in squares)

// one wave band at xz: its waves run down the wind d at phase speed c (m/s); returns the slope in world xz in units of
// the band's rms, h its height (in rms). L the band's tile (m), off a fixed offset (the crossing sample's)
vec2 waveBand(vec2 xz, int b, float L, vec2 d, float c, vec2 off, out float h){
  vec2 u = vec2(dot(xz, d) - c*uTime, dot(xz, vec2(-d.y, d.x)))/L + off;
  vec4 w = texture(uWaves, vec3(u, float(b)));
  vec2 s = (w.rg*2.0 - 1.0)*4.0;
  h = (w.b*2.0 - 1.0)*4.0;
  return d*s.x + vec2(-d.y, d.x)*s.y;
}

// the sea surface: wind-driven wave bands, Fresnel sky and cloud reflection, depth colour, light through the crests,
// whitecaps, shore foam and the sun glint
vec3 waterShade(vec3 p, vec3 rd, float t){
  float sunVis = smoothstep(-0.05, 0.05, uSunDir.y);
  float depth = max(-groundH(p.xz, 5), 0.0);
  // the wind: its direction sets the waves' (calm: a light westerly's), its speed how steep they are - the sea's mean
  // square slope after Cox and Munk, 0.003 + 0.00512 U - and how much of it breaks white
  float U = length(uWindV.xz) + 8.0*uStorm;
  vec2 d = length(uWindV.xz) > 0.3 ? normalize(uWindV.xz) : vec2(0.8, 0.6);
  vec2 d2 = vec2(d.x*0.825 - d.y*0.565, d.x*0.565 + d.y*0.825), d3 = vec2(d.x*0.9 + d.y*0.436, -d.x*0.436 + d.y*0.9);   // (+-34 / 26 deg)
  float sigma = sqrt(0.003 + 0.00512*U);
  // three bands (16-128 m swell, 2-16 m wind waves, 25 cm-2 m ripples), each at its waves' phase speed and twice, at
  // crossing angles (0.8 and 0.6: the band's variance kept); the texture's mipmaps average away what a pixel can't hold
  float h0, h0b, h1, h1b, h2, h2b;
  vec2 s0 = waveBand(p.xz, 0, 256.0, d, 7.9, vec2(0.0), h0)*0.8 + waveBand(p.xz, 0, 256.0, d2, 6.7, vec2(0.37, 0.61), h0b)*0.6;
  vec2 s1 = waveBand(p.xz, 1, 32.0, d, 3.1, vec2(0.0), h1)*0.8 + waveBand(p.xz, 1, 32.0, d3, 2.6, vec2(0.71, 0.13), h1b)*0.6;
  vec2 s2 = waveBand(p.xz, 2, 4.0, d, 1.1, vec2(0.0), h2)*0.8 + waveBand(p.xz, 2, 4.0, d2, 0.95, vec2(0.29, 0.83), h2b)*0.6;
  // what the pixel can't resolve of each band (its waves under ~3 pixels) leaves the surface as roughness instead
  float foot = t*2.0*uTanHalf/uRes.y;                         // metres per pixel at this distance
  float a0 = 1.0 - smoothstep(16.0/3.0, 64.0/3.0, foot), a1 = 1.0 - smoothstep(2.0/3.0, 8.0/3.0, foot), a2 = 1.0 - smoothstep(0.25/3.0, 1.0/3.0, foot);
  vec2 sl = (s0*uWaveRms.x + s1*uWaveRms.y + s2*uWaveRms.z)*sigma;
  vec3 n0 = noised(p.xz*0.004 + uTime*vec2(0.02, 0.013));      // long swell from afar, never aliases
  sl += n0.yz*0.02;
  vec3 n = normalize(vec3(-sl.x, 1.0, -sl.y));
  float lost = sigma*sigma*(uWaveRms.x*uWaveRms.x*(1.0 - a0*a0) + uWaveRms.y*uWaveRms.y*(1.0 - a1*a1) + uWaveRms.z*uWaveRms.z*(1.0 - a2*a2)) + 0.0004;
  vec3 v = -rd;
  float fk = clamp(1.0 - dot(n, v), 0.0, 1.0); float fres = 0.02 + 0.98*fk*fk*fk*fk*fk;
  vec3 r = reflect(rd, n); r.y = abs(r.y);
  vec3 refl = skyColor(r);
  // reflected clouds (cheap)
  if (uCloudCover > 0.05 && uQuality > 0) { gCloudLite = 1; vec4 cl = traceClouds(p, r, 30000.0, 0.5); gCloudLite = 0; refl = refl*cl.a + cl.rgb; }
  float sh = sunVis > 0.0 ? terrainShadow(p + vec3(0,1,0), uSunDir, t) * cloudShadow(p) * entShadow(p, vec3(0,1,0)) : 0.0;
  vec4 base = baseAt(p.xz);
  vec3 deep = mix(vec3(0.004,0.03,0.06), vec3(0.003,0.02,0.035), base.w);
  vec3 shallow = mix(vec3(0.02,0.16,0.17), vec3(0.03,0.30,0.29), base.z) * (1.0 - 0.7*base.w);
  vec3 water = mix(shallow, deep, smoothstep(0.0, 18.0, depth));
  vec3 sunI = uSunCol*max(uSunDir.y, 0.0)*sh;
  vec3 lit = water*(sunI + ambientLight(vec3(0,1,0))*0.35);
  // light through the crests: a wave's thin top lit from behind glows green-blue, looking toward the sun
  float crest = clamp((h0*0.6 + h1*0.4)/0.72, 0.0, 3.0);
  float back = pow(max(dot(rd, normalize(vec3(uSunDir.x, max(uSunDir.y, 0.0)*0.3, uSunDir.z))), 0.0), 4.0);
  lit += vec3(0.02, 0.17, 0.14)*uSunCol*sh*back*crest*0.25*smoothstep(1.0, 6.0, U);
  // whitecaps: the crests that break, as much of the sea as the wind breaks (Monahan: ~1% at 10 m/s, 10% at 20 m/s)
  float z = clamp(3.1 - 0.09*U, 1.2, 3.1);
  float cap = smoothstep(z, z + 0.6, (h0*0.6 + h1*0.4)/0.72 + 0.35*h2) * smoothstep(4.0, 9.0, U);
  // the shore: foam lapping where it is a few decimetres deep, broken by the wind waves
  float shore = smoothstep(0.7, 0.0, depth) * smoothstep(0.35, 0.8, 0.5 + 0.25*h1 + 0.35*sin(depth*4.0 - uTime*1.5 + h0*0.6));
  float foam = clamp(cap*0.9 + shore*0.8, 0.0, 1.0);
  lit = mix(lit, vec3(0.85)*(uSunCol*max(uSunDir.y,0.0)*1.5*mix(1.0, sh, 0.8) + ambientLight(vec3(0,1,0))), foam);
  fres *= 1.0 - foam;
  // the sun's glint: a lobe as wide as the slopes the pixel averages (Beckmann-like, to a Phong exponent), plus a
  // broad sheen
  float ex = clamp(1.0/lost - 2.0, 20.0, 2500.0);
  vec3 hv = normalize(v + uSunDir);
  float ndh = max(dot(n, hv), 0.0);
  float spec = pow(ndh, ex)*(ex + 8.0)/(900.0 + 8.0)*120.0 + pow(ndh, 90.0)*1.5;
  return mix(lit, refl, fres) + uSunCol*spec*sh*(1.0 - foam)*(1.0 - smoothstep(0.5, 1.0, uCloudCover));
  // night: runway/town light reflections handled by overlay pass glow
}
