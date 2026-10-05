//! kRtShade
//! The ray tracer's shading of each kind of hit: the material and the lighting in one place (the raster passes split them).

// a terrain hit: its material, the sun shadow (terrain, scenery, aircraft, traffic, clouds) and the surface shading
vec3 terrainShade(vec3 p, vec3 rd, float t, float trafSh){
  float sunVis = smoothstep(-0.05, 0.05, uSunDir.y);
  vec3 n = terrainNormal(p.xz, t);
  vec4 base = baseAt(p.xz);
  Mat m = terrainMaterial(p, n, t, base);
  vec3 ns = applyTS(n, m.nrm, t < 2000.0 ? 0.6 : 0.25);
  float sh = sunVis > 0.0 ? terrainShadow(p + n*0.5, uSunDir, t) : 0.0;
  if (sh > 0.0) sh *= entShadow(p, n);
  if (t < 3000.0) sh *= planeShadow(p + n*0.2, uSunDir);
  sh *= trafSh;
  sh *= cloudShadow(p);
  return shadeSurface(p, ns, rd, m, sh);
}

// a tree, rock or building from the scenery G-buffer (g0: distance, normal, class)
vec3 entityShade(vec3 p, vec3 rd, float t, vec4 g0, float trafSh){
  float sunVis = smoothstep(-0.05, 0.05, uSunDir.y);
  // tree, rock or building from the G-buffer
  vec3 n = octDec(g0.yz);
  vec4 g1 = texelFetch(uGB1, ivec2(gl_FragCoord.xy), 0), g2 = texelFetch(uGB2, ivec2(gl_FragCoord.xy), 0);
  Mat m; m.alb = g1.rgb*g1.rgb; m.rough = g1.a; m.metal = g2.a; m.emit = g2.rgb; m.nrm = vec3(0,0,1);
  int cls = int(g0.w + 0.5);
  float sh = sunVis > 0.0 ? terrainShadow(p + n*0.5 + vec3(0.0, 0.5, 0.0), uSunDir, t) : 0.0;
  if (sh > 0.0) sh *= entShadow(p, n)*cloudShadow(p);
  if (sh > 0.0 && t < 3000.0) sh *= planeShadow(p + n*0.2, uSunDir);
  sh *= trafSh;
  vec3 col = shadeSurface(p, n, rd, m, sh);
  if (cls == 4) {   // foliage: light through the leaves when the sun is behind them, and a soft wrap
    float back = pow(max(dot(rd, uSunDir), 0.0), 3.0)*0.9 + 0.12*max(dot(-n, uSunDir), 0.0);
    col += m.alb*vec3(0.85, 1.0, 0.55)*uSunCol*sh*back*1.6;
  }
  return col;
}

// an airport box building hit (bn normal, bkind kind, bl local hit, bh.y box index)
vec3 boxShade(vec3 p, vec3 rd, float t, vec3 bn, float bkind, vec3 bl, vec2 bh){
  float sunVis = smoothstep(-0.05, 0.05, uSunDir.y);
  Mat m; m.metal = 0.0; m.emit = vec3(0.0); m.nrm = vec3(0,0,1); m.rough = 0.7; m.alb = vec3(0.7);
  vec3 nn = bn;
  vec3 nTS = vec3(0,0,1);
  {
    int k = int(bkind + 0.5); vec3 lh = bl; vec3 H = dataAt(192 + int(bh.y)).xyz;
    if (k == 0) {        // arched hangar: corrugated metal skin, big sliding doors facing the runway
      vec4 tx = triSample(lh*vec3(1.0, 1.0, 1.0), nn, M_CORRUGATED, 2.0, nTS);
      m.alb = tx.rgb*vec3(0.75, 0.78, 0.8); m.rough = tx.a; m.metal = 0.7; m.nrm = nTS;
      if (abs(bn.y) < 0.6 && abs(lh.z) < H.z*0.85 && lh.y < H.y*0.2 && abs(abs(lh.x) - H.x) < 0.3) {
        m.alb = vec3(0.35, 0.4, 0.45); if (fract(lh.z/4.0) < 0.03) m.alb *= 0.5; }
    } else if (k == 1) { // control tower: concrete shaft, glass cab
      float Ht = H.y*2.0, yy = lh.y + H.y;
      vec4 tx = triSample(lh, nn, M_CONCRETE, 3.0, nTS); m.alb = tx.rgb; m.rough = tx.a; m.nrm = nTS;
      if (yy > Ht*0.78 && yy < Ht*0.93 && abs(bn.y) < 0.5) { m.alb = vec3(0.03, 0.06, 0.07); m.rough = 0.04; m.metal = 0.5; m.emit = vec3(0.3, 0.7, 0.45)*uNight*0.6; }
      if (yy > Ht*0.93) m.alb = vec3(0.25);
      if (yy > Ht*0.97) { m.alb = vec3(0.8, 0.1, 0.1); m.emit = vec3(1.0, 0.1, 0.05)*step(0.5, fract(uTime*0.7))*2.0; }
    } else if (k == 2) { // terminal: glass curtain wall over a concrete base
      vec4 tx = triSample(lh, nn, M_CONCRETE, 4.0, nTS); m.alb = tx.rgb*0.95; m.rough = tx.a; m.nrm = nTS;
      if (abs(bn.y) < 0.5 && lh.y > -H.y + 1.0) {
        float mul = step(0.04, fract(lh.z/2.4))*step(0.06, fract((lh.y + H.y)/3.2));
        m.alb = mix(vec3(0.6), vec3(0.04, 0.07, 0.1), mul); m.rough = mix(0.4, 0.04, mul); m.metal = 0.5*mul;
        m.emit = vec3(1.0, 0.88, 0.7)*uNight*0.9*mul;
      }
      if (bn.y > 0.5) { m.alb = vec3(0.5); }
    } else if (k == 3) { // gabled shed / FBO house
      vec4 tx = triSample(lh, nn, abs(bn.y) > 0.3 ? M_TILES : M_PLASTER, 2.5, nTS);
      m.alb = tx.rgb*(abs(bn.y) > 0.3 ? vec3(0.9, 0.6, 0.5) : vec3(0.95, 0.93, 0.88)); m.rough = tx.a; m.nrm = nTS;
      if (abs(bn.y) < 0.3 && fract(lh.z/2.5) > 0.6 && lh.y > -H.y*0.5 && lh.y < 0.0) { m.alb = vec3(0.05); m.rough = 0.08; m.emit = vec3(1.0,0.8,0.5)*uNight*1.5; }
    } else if (k == 4) { // fuel tank
      vec4 tx = triSample(lh, nn, M_METAL, 3.0, nTS); m.alb = tx.rgb*vec3(0.95); m.rough = 0.35; m.metal = 0.6; m.nrm = nTS;
      if (abs(lh.y) < 0.5) m.alb = vec3(0.8, 0.15, 0.1);
    } else {             // radar dome on a pylon
      m.alb = vec3(0.92); m.rough = 0.5;
      if (lh.y < 0.0) { vec4 tx = triSample(lh, nn, M_METAL, 2.0, nTS); m.alb = tx.rgb*0.7; m.metal = 0.6; m.nrm = nTS; }
    }
  }
  vec3 ns = applyTS(nn, m.nrm, 0.5);
  float sh = sunVis > 0.0 ? terrainShadow(p + nn*0.3, uSunDir, t) : 0.0;
  return shadeSurface(p, ns, rd, m, sh*cloudShadow(p));
}

// a debris chunk: torn painted skin or charred metal
vec3 debrisShade(vec3 p, vec3 rd, float t, vec3 dn, float dChar){
  float sunVis = smoothstep(-0.05, 0.05, uSunDir.y);
  // debris chunk: torn painted skin or charred metal
  vec3 nT; vec4 tx = triSample(p*2.0, dn, M_METAL, 1.0, nT);
  Mat m; m.metal = 0.5; m.emit = vec3(0.0); m.nrm = nT;
  float burn = vnoise(p.xz*3.0 + p.y);
  m.alb = dChar > 0.5 ? vec3(0.03, 0.028, 0.026)*(0.6 + burn) : gColBase*tx.rgb*(0.3 + 0.4*burn);
  m.rough = dChar > 0.5 ? 0.9 : 0.45;
  float sh = sunVis > 0.0 ? terrainShadow(p + dn*0.05, uSunDir, t) : 0.0;
  return shadeSurface(p, applyTS(dn, m.nrm, 0.4), rd, m, sh);
}

