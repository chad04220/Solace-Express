//! kWater
//! The sea surface shading.
uniform sampler2DArray uWaves;   // the wave bands (Renderer::genWaves): slope x, slope z (along / across the wind), height
uniform vec3 uWaveRms;           // each band's share of the sea's slope (rms, sums to 1 in squares)

// one wave band at xz: its waves run down the wind d at phase speed c (m/s); returns the slope in world xz in units of
// the band's rms, h its height (in rms). L the band's tile (m), off a fixed offset (the crossing sample's)
// (the direction is turned a fraction of a degree, so a whole number of the band's tiles fits in the map's 100 km each
// way: the sea joins itself seamlessly at the map's wrap, world.h WRAP_HALF)
vec2 waveBand(vec2 xz, int b, float L, vec2 d, float c, vec2 off, out float h){
  d = round(d*(100000.0/L))*(L/100000.0);
  vec2 u = vec2(dot(xz, d) - c*uTime, dot(xz, vec2(-d.y, d.x)))/L + off;
  vec4 w = texture(uWaves, vec3(u, float(b)));
  vec2 s = (w.rg*2.0 - 1.0)*4.0;
  h = (w.b*2.0 - 1.0)*4.0;
  return d*s.x + vec2(-d.y, d.x)*s.y;
}

// value noise that repeats every n lattice cells (the sea's patches and swell: a whole number of cells in the map's
// 100 km, so they join at its wrap)
float hashP(ivec2 i, int n){ return hash2i(ivec2(((i.x % n) + n) % n, ((i.y % n) + n) % n)); }
float vnoiseP(vec2 x, int n){ vec2 i = floor(x), f = fract(x); f = f*f*(3.0 - 2.0*f); ivec2 k = ivec2(i);
  return mix(mix(hashP(k, n), hashP(k + ivec2(1, 0), n), f.x), mix(hashP(k + ivec2(0, 1), n), hashP(k + ivec2(1, 1), n), f.x), f.y); }
vec3 noisedP(vec2 x, int n){
  vec2 f0 = floor(x); ivec2 i = ivec2(f0); vec2 f = x - f0;
  vec2 u = f*f*f*(f*(f*6.0-15.0)+10.0); vec2 du = 30.0*f*f*(f*(f-2.0)+1.0);
  float a = hashP(i, n), b = hashP(i+ivec2(1,0), n), c = hashP(i+ivec2(0,1), n), d = hashP(i+ivec2(1,1), n);
  float k1 = b-a, k2 = c-a, k4 = a-b-c+d;
  return vec3(-1.0+2.0*(a+k1*u.x+k2*u.y+k4*u.x*u.y), 2.0*du*vec2(k1+k4*u.y, k2+k4*u.x));
}

// Beer–Lambert transmission: absorption grows with the refracted optical path, not a
// screen-space alpha. Red disappears first; cold/turbid water has a shorter clear range.
vec3 waterTransmission(float distanceInWater, vec3 extinction){
  return exp(-max(distanceInWater, 0.0)*extinction);
}
vec2 waterDetailFrame(vec2 p){
  return vec2(0.8*p.x - 0.6*p.y, 0.6*p.x + 0.8*p.y);
}
vec2 waterWarpFootprint(vec2 footprint, vec2 gradientA, vec2 gradientB){
  return footprint + vec2(dot(gradientA, footprint)/19.0, dot(gradientB, footprint)/31.0)*2.4;
}
vec3 waterBottomColor(vec2 p, vec2 pixelDx, vec2 pixelDy, float rock){
  // Two decorrelated views of each existing terrain scan, blended in linear space by
  // a broad, continuous patch field. Rotation/offset/scale break the repeating beach
  // tile without random per-cell seams, animated noise or another texture allocation.
  // Smooth bounded domain warp breaks the repeated pock lattice, not just its tile seams.
  // noised returns analytic derivatives, so the identical Jacobian transforms both
  // texture gradients before the rotation. No screen derivatives inside this branch.
  vec3 warpA = noised(p/19.0 + vec2(6.7, -3.4));
  vec3 warpB = noised(p/31.0 + vec2(-8.1, 12.6));
  vec2 warped = p + vec2(warpA.x, warpB.x)*2.4;
  vec2 dx0 = waterWarpFootprint(pixelDx, warpA.yz, warpB.yz);
  vec2 dy0 = waterWarpFootprint(pixelDy, warpA.yz, warpB.yz);
  vec2 q = waterDetailFrame(warped)/1.13 + vec2(17.3, -29.1);
  vec2 dx = waterDetailFrame(dx0)/1.13, dy = waterDetailFrame(dy0)/1.13;
  float sedimentPatch = warpA.x*0.5 + 0.5;
  // Neither repeated view takes over an entire patch by itself.
  float blend = 0.24 + 0.52*smoothstep(0.18, 0.82, sedimentPatch);
  vec3 sand0 = textureGrad(uAlb, vec3(warped/6.0, float(M_SAND)), dx0/6.0, dy0/6.0).rgb;
  vec3 sand1 = textureGrad(uAlb, vec3(q/6.0, float(M_SAND)), dx/6.0, dy/6.0).rgb;
  vec3 stone0 = textureGrad(uAlb, vec3(warped/18.0, float(M_ROCK)), dx0/18.0, dy0/18.0).rgb;
  vec3 stone1 = textureGrad(uAlb, vec3(q/18.0 + vec2(0.41, 0.73), float(M_ROCK)), dx/18.0, dy/18.0).rgb;
  vec3 sand = mix(sand0*sand0, sand1*sand1, blend);
  vec3 stone = mix(stone0*stone0, stone1*stone1, 1.0 - blend);
  // Restrained natural sediment variation lives in metre-scale patches, never a tile grid.
  sand *= mix(vec3(0.86, 0.91, 0.86), vec3(0.98, 0.96, 0.86), sedimentPatch);
  return mix(sand, stone*vec3(0.65, 0.72, 0.7), rock);
}

// the sea surface: wind-driven wave bands, Fresnel sky and cloud reflection, depth colour, light through the crests,
// whitecaps, shore foam and the sun glint
vec3 waterShade(vec3 p, vec3 rd, float t, vec2 pixelDx, vec2 pixelDy){
  float sunVis = smoothstep(-0.05, 0.05, uSunDir.y);
  float floorHeight = groundH(p.xz, 5);
  float depth = max(-floorHeight, 0.0);
  // the wind: its direction sets the waves' (calm: a light westerly's), its speed how steep they are - the sea's mean
  // square slope after Cox and Munk, 0.003 + 0.00512 U - and how much of it breaks white
  float U = length(uWindV.xz) + 8.0*uStorm;
  vec2 d = length(uWindV.xz) > 0.3 ? normalize(uWindV.xz) : vec2(0.8, 0.6);
  vec2 d2 = vec2(d.x*0.825 - d.y*0.565, d.x*0.565 + d.y*0.825), d3 = vec2(d.x*0.9 + d.y*0.436, -d.x*0.436 + d.y*0.9);   // (+-34 / 26 deg)
  float sigma = sqrt(0.003 + 0.00512*U);
  // three bands (16-128 m swell, 2-16 m wind waves, 25 cm-2 m ripples), each at its waves' phase speed and twice, at
  // crossing angles (0.8 and 0.6: the band's variance kept); the texture's mipmaps average away what a pixel can't hold
  // The crossing sample's tile is 0.79 of the main one's, so the two never line up again within sight (the same tile
  // twice read as one pattern stamped across the sea from the air: the review of v3.33.0), and the wind waves and
  // ripples come and go in patches a kilometre or so across, as gusts roughen the sea in some places and not others
  float h0, h0b, h1, h1b, h2, h2b;
  vec2 pw = wrapW(p.xz);   // (every pattern here repeats with the map: its own place keeps the precision)
  vec2 s0 = waveBand(pw, 0, 256.0, d, 7.9, vec2(0.0), h0)*0.8 + waveBand(pw, 0, 202.0, d2, 6.0, vec2(0.37, 0.61), h0b)*0.6;
  vec2 s1 = waveBand(pw, 1, 32.0, d, 3.1, vec2(0.0), h1)*0.8 + waveBand(pw, 1, 25.3, d3, 2.3, vec2(0.71, 0.13), h1b)*0.6;
  vec2 s2 = waveBand(pw, 2, 4.0, d, 1.1, vec2(0.0), h2)*0.8 + waveBand(pw, 2, 3.16, d2, 0.85, vec2(0.29, 0.83), h2b)*0.6;
  float gust = vnoiseP(pw/(100000.0/91.0) + d*uTime*0.004, 91)*0.65 + vnoiseP(pw/(100000.0/270.0) - d*uTime*0.009, 270)*0.35;
  float patchK = mix(0.6, 1.3, smoothstep(0.2, 0.8, gust));
  s1 *= patchK; s2 *= patchK*patchK; h1 *= patchK; h2 *= patchK;   // (whitecaps gather where it gusts)
  // what the pixel can't resolve of each band (its waves under ~3 pixels) leaves the surface as roughness instead
  float foot = max(length(pixelDx), length(pixelDy));                         // metres per pixel at this distance
  float a0 = 1.0 - smoothstep(16.0/3.0, 64.0/3.0, foot), a1 = 1.0 - smoothstep(2.0/3.0, 8.0/3.0, foot), a2 = 1.0 - smoothstep(0.25/3.0, 1.0/3.0, foot);
  vec2 sl = (s0*uWaveRms.x*a0 + s1*uWaveRms.y*a1 + s2*uWaveRms.z*a2)*sigma;
  vec3 n0 = noisedP(pw*0.004 + uTime*vec2(0.02, 0.013), 400);   // long swell from afar, never aliases
  sl += n0.yz*0.02;
  vec3 n = normalize(vec3(-sl.x, 1.0, -sl.y));
  float lost = sigma*sigma*(uWaveRms.x*uWaveRms.x*(1.0 - a0*a0) + uWaveRms.y*uWaveRms.y*patchK*patchK*(1.0 - a1*a1) + uWaveRms.z*uWaveRms.z*patchK*patchK*patchK*patchK*(1.0 - a2*a2)) + 0.0004;
  vec3 v = -rd;
  float fk = clamp(1.0 - dot(n, v), 0.0, 1.0); float fres = 0.02 + 0.98*fk*fk*fk*fk*fk;
  vec3 r = reflect(rd, n); r.y = abs(r.y);
  vec3 refl = skyColor(r);
  // reflected clouds (cheap)
  if (uCloudCover > 0.05 && uQuality > 0) { gCloudLite = 1; vec4 cl = traceClouds(p, r, 30000.0, 0.5); gCloudLite = 0; refl = refl*cl.a + cl.rgb; }
  float sh = sunVis > 0.0 ? terrainShadow(p + vec3(0,1,0), uSunDir, t) * cloudShadow(p) * entShadow(p, vec3(0,1,0)) : 0.0;
  vec4 base = baseAt(p.xz);
  vec3 deep = mix(vec3(0.004,0.03,0.06), vec3(0.003,0.02,0.035), base.w);
  vec3 extinction = mix(vec3(0.22, 0.072, 0.045), vec3(0.29, 0.13, 0.095), base.w);
  extinction *= 1.0 + 0.8*uStorm;
  vec3 sunI = uSunCol*max(uSunDir.y, 0.0)*sh;
  vec3 ambient = ambientLight(vec3(0,1,0));
  vec3 lit = deep*(sunI + ambient*0.35);
  if (depth < 32.0 && uCamPos.y > 0.0 && rd.y < -0.015) {
    // One bounded correction onto the real heightfield gives parallax through clear
    // shallows. No screen-space G-buffer read, so above-water buildings cannot leak in.
    vec3 refracted = refract(rd, n, 0.7501875);
    float rayDown = max(-refracted.y, 0.2);
    float path = depth/rayDown;
    vec2 bottom = p.xz + refracted.xz*path;
    float hitHeight = groundH(bottom, 5);
    path = clamp(-hitHeight/rayDown, 0.0, 48.0);
    bottom = p.xz + refracted.xz*path;
    // Continental shelves are sand with subdued rocky pockets; the world heightfield
    // determines depth and visibility while the existing terrain scans supply detail.
    bottom = wrapW(bottom);   // (the sea floor's own place: across the seam, the far side's shallows)
    float rock = smoothstep(0.56, 0.76, vnoise(bottom/42.0))*(0.25 + 0.55*smoothstep(3.0, 14.0, depth));
    vec3 bed = waterBottomColor(bottom, pixelDx, pixelDy, rock);
    vec3 toBed = waterTransmission(depth/max(uSunDir.y, 0.25), extinction);
    vec3 through = waterTransmission(path, extinction);
    vec3 floorLit = bed*(sunI*toBed + ambient*0.28);
    // Reject projected dry land at a steep coast rather than drawing it under the sea.
    float seabed = 1.0 - smoothstep(-0.05, 0.2, hitHeight);
    float clearRange = 1.0 - smoothstep(24.0, 32.0, depth);
    lit += (floorLit - lit)*through*(seabed*clearRange);
  }
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
  vec3 glint = uSunCol*spec*sh;
#ifdef SKY_BODIES
  // the moon's: the same lobe in its light (its phase, its height) - its path across the sea at night
  vec3 hm = normalize(v + uMoonDir);
  float ndm = max(dot(n, hm), 0.0), mspec = pow(ndm, ex)*(ex + 8.0)/(900.0 + 8.0)*120.0 + pow(ndm, 90.0)*1.5;
  glint += vec3(0.055, 0.06, 0.07)*mspec*uMoonLit*smoothstep(-0.02, 0.05, uMoonDir.y)*max(uNight, 0.25);
#endif
  return mix(lit, refl, fres) + glint*(1.0 - foam)*(1.0 - smoothstep(0.5, 1.0, uCloudCover));
  // night: runway/town light reflections handled by overlay pass glow
}
