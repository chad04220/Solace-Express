//! kWater
//! The sea surface shading.

// the sea surface: band-limited waves, Fresnel sky and cloud reflection, depth colour, foam and the sun glint
vec3 waterShade(vec3 p, vec3 rd, float t){
  float sunVis = smoothstep(-0.05, 0.05, uSunDir.y);
  // ocean
  float depth = max(-groundH(p.xz, 5), 0.0);
  // wave normals band-limited to the pixel footprint (fades each octave before it can alias into sparkle)
  float foot = t*2.0*uTanHalf/uRes.y;                         // metres per pixel at this distance
  vec2 w = p.xz*0.05 + uTime*vec2(0.3, 0.2);
  float a1 = 1.0 - smoothstep(2.5, 7.0, foot), a2 = 1.0 - smoothstep(0.8, 2.2, foot), a3 = 1.0 - smoothstep(0.15, 0.45, foot), a4 = 1.0 - smoothstep(0.05, 0.14, foot);
  vec3 n1 = noised(w*1.0); vec3 n2 = noised(w*3.1 + 5.0); vec3 n3 = noised(p.xz*0.9 + uTime*vec2(-0.9, 0.7));
  vec3 n4 = a4 > 0.0 ? noised(p.xz*3.3 + uTime*vec2(1.3, -1.1)) : vec3(0.0);
  vec3 n0 = noised(p.xz*0.004 + uTime*vec2(0.02, 0.013));      // long swell, never aliases
  float amp = 0.12 + 0.12*uStorm + 0.04*uWet;
  vec2 sl = (n1.yz*0.6*a1 + n2.yz*0.3*a2)*amp + n3.yz*0.035*a3 + n4.yz*0.012*a4 + n0.yz*0.02;
  vec3 n = normalize(vec3(-sl.x, 1.0, -sl.y));
  float rough = clamp(foot*0.02 + (1.0 - a2)*0.25, 0.0, 0.6);   // lost wave detail becomes statistical roughness
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
  vec3 lit = water*(uSunCol*max(uSunDir.y,0.0)*1.0*sh + ambientLight(vec3(0,1,0))*0.35);
  float foam = smoothstep(0.7, 0.0, depth) * smoothstep(0.55, 0.85, vnoise(p.xz*0.15 + uTime*0.4) + 0.3*sin(depth*4.0 - uTime*1.5));
  lit = mix(lit, vec3(0.85)*(uSunCol*max(uSunDir.y,0.0)*1.5 + ambientLight(vec3(0,1,0))), foam*0.8);
  vec3 h = normalize(v + uSunDir);
  float ex = mix(900.0, 40.0, rough/0.6);
  float spec = pow(max(dot(n, h), 0.0), ex)*(ex + 8.0)/(900.0 + 8.0)*120.0 + pow(max(dot(n,h),0.0), 90.0*(1.0 - rough))*1.5;
  return mix(lit, refl, fres) + uSunCol*spec*sh*(1.0 - smoothstep(0.5, 1.0, uCloudCover));
  // night: runway/town light reflections handled by overlay pass glow
}
