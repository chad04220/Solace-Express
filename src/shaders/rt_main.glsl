//! kRtMain
//! The ray tracer: one camera ray per pixel through terrain, water, scenery, aircraft, traffic, debris, the UFO, the
//! effects and the clouds, shaded in place.
void main(){
  gZero = min(uQuality, 0);
  loadMain();
  vec2 ndc = (vUV + uJit)*2.0 - 1.0;
  vec3 rd = camRay(ndc);
  vec3 ro = uCamPos;
  float jitter = fract(52.9829189*fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) + uSeed);   // interleaved gradient noise, rotated per frame
  float tmax = 80000.0;
  // research jet cockpit: display screens show the outside world (re-traced without the airframe); the rest of
  // the sealed pod hides everything beyond it
  bool onScr = false; int scrId = 0; vec3 scrL = vec3(0.0);
  // pod = a pixel on the sealed cockpit's interior: it is shaded from the cockpit alone, and nothing outside the
  // aircraft (terrain, water, buildings, clouds, shadows) is traced for it. Only the display screens see out.
  // In any cockpit view the aircraft is traced first: a pixel that lands on the cabin (or on a wing seen through a
  // window) needs nothing from outside, so only rays leaving through the windows trace the world.
  bool pod = false, cockpitView = uPlaneOn == 1 && gPS.w > 0.5 && uWreck == 0; vec2 h0 = vec2(-1.0);
  // the aircraft hull was rasterized along exactly this ray: start the airframe march where it is (0: on its inside)
  // (0: march from the camera; the first uHullNear metres are always marched, the hull's faces there are ignored)
  float hullT = 0.0;
  if (uHullOn == 1 && uWreck == 0) { float hv = texelFetch(uEnv, ivec2(gl_FragCoord.xy), 0).g; hullT = hv > 1e29 ? hv : (hv > 0.0 ? max(uHullNear, hv*0.999 - 0.1) : 0.0); }
  if (uFeedSkip > 0.0 && hullT == 0.0) hullT = uFeedSkip;
  // The airframe along this camera ray, traced once for every use below (the cockpit, the cloak, the outside view):
  // each call site would be another inlined copy of the march and the airframe's distance.
  bool jetC = int(gM[0].z + 0.5) >= 5;
  // (outside the cockpit, no further than the nearest rasterized tree / rock / building: past it the airframe is hidden,
  // the cloak's bent ray included)
  float tOpq = tmax;
  if (!cockpitView) { float gb = texelFetch(uGB0, ivec2(gl_FragCoord.xy), 0).x; if (gb > 0.0) tOpq = min(tmax, gb + 0.5); }
  vec2 hTop = tracePlaneHull(ro, rd, cockpitView ? (jetC ? 6.0 : planeBound()*2.0) : tOpq, hullT);
  int hTopPiece = gPI;   // (traffic tracing moves the piece transform; restored before shading)
  if (cockpitView) {
    bool jet = jetC;
    h0 = hTop;
#ifdef HULL_DEBUG
    if ((uDbg & 1024) != 0) {   // (debug: the hull's start against a march from the camera - red: the hull skipped a hit,
      vec2 hf = tracePlane(ro, rd, jet ? 6.0 : planeBound()*2.0);   // blue: a hit moved, green: the hull found one the camera's march didn't)
      vec3 dc = vec3(0.0);
      if (hf.x > 0.0 && h0.x < 0.0) dc = hullT > 1e29 ? vec3(1.0, 0.0, 0.0) : vec3(1.0, 1.0, 0.0);   // (yellow: skipped from a start)
      else if (hf.x < 0.0 && h0.x > 0.0) dc = vec3(0.0, 1.0, 0.0);
      else if (hf.x > 0.0 && abs(hf.x - h0.x) > 0.01) dc = vec3(0.0, 0.3, 1.0)*clamp(abs(hf.x - h0.x)*10.0, 0.3, 1.0);
      oColor = vec4(dc*50.0, 0.0); oDepth = 1.0; oCloudMask = 0.0;
      if ((uDbg & 2048) != 0) oColor = vec4(hullT, hf.x, h0.x, texelFetch(uEnv, ivec2(gl_FragCoord.xy), 0).g);   // (raw, for a readback)
      if ((uDbg & 4096) != 0 && hf.x > 0.0) {   // (the 6.25 cm hull voxel holding the hit: its centre's distance, how far the hit is from it)
        pieceXf(-1); vec3 lp = transpose(gPR)*(ro + rd*hf.x - gPP);
        float org = -ceil(planeBound())*1.0; vec3 c = vec3(org) + (floor((lp - org)/0.0625) + 0.5)*0.0625;
        oColor = vec4(mapPlane(c).x, length(c - lp), mapPlane(lp).x, float(int(mapPlane(lp).y + 0.5)));
        oDepth = lp.x; oCloudMask = lp.y;
      }
      return;
    }
#endif
    if (h0.x > 0.0) {
      int id0 = int(h0.y + 0.5);
      if (jet && ((id0 >= 41 && id0 <= 43) || (id0 >= 61 && id0 <= 63))) { onScr = true; scrId = id0; scrL = transpose(uPlaneRot)*(ro + rd*h0.x - uPlanePos); }
      else { pod = true; tmax = h0.x + 0.05; }
    }
  }
  if (onScr) {   // a display: its camera's picture, with the display's own look and symbology over it
    bool bomb; vec3 rdc;
    vec3 col = feedScreen(scrId, scrL, rdc, bomb);
    bool wr = int(gM[0].z + 0.5) == 6;
    col = bomb ? wrFeedOverlay(col, scrL) : wr ? wraithScreen(col, rdc, scrId, scrL) : jetScreen(col, rdc, scrId, scrL);
    if (wr) col += wrHolo(ro, rd, h0.x);   // the hologram floats inside the cabin, in front of the displays
    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    oColor = vec4(clamp(col, vec3(0.0), vec3(3e4)), 0.0); oDepth = h0.x; oCloudMask = 0.0;
#ifdef COST_MAP
    oColor = gCost;
#endif
    return;
  }
  // XR-40 cloak: a pixel on the cloaked craft sees the world behind it along a slightly bent ray
  bool cloak = false; vec3 ckN = vec3(0.0), ckLp = vec3(0.0), rd0 = rd, ro0 = ro; float ckT = 0.0;
  if (uWr[4].w > 0.001 && uPlaneOn == 1 && uWreck == 0 && !cockpitView && int(gM[0].z + 0.5) == 6) {
    vec2 hc = hTop;
    if (hc.x > 0.0) {
      vec3 hp = ro + rd*hc.x; ckLp = transpose(uPlaneRot)*(hp - uPlanePos);
      if (ckLp.z < uWr[6].y) {
        cloak = true; ckT = hc.x; ckN = uPlaneRot*planeNormal(ckLp);
        ro = hp + rd*0.05; rd = normalize(rd - (ckN - rd*dot(ckN, rd))*0.035);
      }
    }
  }
  vec3 roV = ro, rdV = rd;
  // environment entities: the raster pass already found the nearest tree / rock / building on this pixel
  vec4 g0 = vec4(0.0);
  if (!pod) g0 = texelFetch(uGB0, ivec2(gl_FragCoord.xy), 0);
  if (cloak) g0.x = g0.x > ckT ? g0.x - ckT : 0.0;   // seen through the cloak (the ray now starts on its skin)
  float tE = g0.x > 0.0 && g0.x < tmax ? g0.x : -1.0;
  if (uEnvOn == 1 && !pod && !cloak) {   // the envelope was rasterized along exactly this pixel's ray
    float te = texelFetch(uEnv, ivec2(gl_FragCoord.xy), 0).r;
    gTStart = te > 1e29 ? te : (te > 0.0 ? max(1.0, te*0.999 - 1.0) : 1.0);
  }
  float tT = pod ? -1.0 : traceTerrain(ro, rd, tE > 0.0 ? tE + 1.0 : tmax);
  gTStart = 1.0;
  float tW = (!pod && rd.y < 0.0 && ro.y > 0.0) ? -ro.y/rd.y : -1.0;
  vec3 bn; float bkind = 0.0; vec3 bl;
  vec2 bh = pod ? vec2(-1.0) : traceBoxes(ro, rd, tT > 0.0 ? tT : tmax, bn, bkind, bl);
  int trafK = -1; vec2 trafH = vec2(-1.0);
  vec2 ph = onScr || cloak || (cockpitView && !pod) ? vec2(-1.0) : (pod ? h0 : hTop);
  float t = 1e9; int hit = 0;
  if (tT > 0.0) { t = tT; hit = 1; }
  if (tW > 0.0 && tW < t) { t = tW; hit = 2; }
  if (bh.x > 0.0 && bh.x < t) { t = bh.x; hit = 3; }
  if (tE > 0.0 && tE < t) { t = tE; hit = 5; }
  if (ph.x > 0.0 && ph.x < t) { t = ph.x; hit = 4; }
  // traffic: only as far as the nearest opaque hit found so far
  if (!pod && uTrafficN > 0) { gTrafCamRay = !cloak; trafH = traceTraffic(ro, rd, t < 1e8 ? t : tmax, trafK); gTrafCamRay = false; loadMain(); pieceXf(hTopPiece); }
  float tU = (uUfoOn == 1 && !pod) ? traceUfo(ro, rd, t < 1e8 ? t : tmax) : -1.0;
  bool ufoHit = tU > 0.0 && tU < t;
  if (ufoHit) { t = tU; hit = 8; }
  bool trafHit = false;
  if (trafH.x > 0.0 && trafH.x < t) { t = trafH.x; hit = 4; ph = trafH; trafHit = true; loadTraffic(trafK); trafficXf(trafK); }
  vec3 dn; float dChar = 0.0;
  float tD = uDebN > 0 && !pod ? traceDebris(ro, rd, t < 1e8 ? t : tmax, dn, dChar) : -1.0;
  if (tD > 0.0 && tD < t) { t = tD; hit = 6; }
  vec3 col;
  float taaFlag = hit == 8 ? 0.2 : hit == 4 ? (uWreck > 0 || trafHit ? 0.2 : 0.5) : (hit == 6 ? 0.2 : 1.0);   // 1 world, 0.5 rigid with the aircraft, 0.2 moving, 0 no history
  if (onScr) taaFlag = 0.0;
  if (hit == 0) { col = skyColor(rd); t = 1e6; }
  else if (hit == 8) { col = applyFog(shadeUfo(ro + rd*t, rd, t), ro, rd, t); }
  else {
    vec3 p = ro + rd*t;
    // (one call site for the ground and the scenery: each is another inlined copy of the aircraft's shape)
    float trafSh = uTrafficN > 0 && (hit == 1 || hit == 5) && uSunDir.y > -0.05 ? trafficShadow(p) : 1.0;
    if (hit == 1) col = terrainShade(p, rd, t, trafSh);
    else if (hit == 2) col = waterShade(p, rd, t);
    else if (hit == 5) col = entityShade(p, rd, t, g0, trafSh);
    else if (hit == 3) col = boxShade(p, rd, t, bn, bkind, bl, bh);
    else if (hit == 6) col = debrisShade(p, rd, t, dn, dChar);
    else {   // the aircraft, a traffic aircraft or a wreck piece (gP* hold the transform of the piece that was hit)
      int mid = int(ph.y + 0.5);
      Mat m; vec3 n, lp, ln; bool interior, podMat;
      planeMaterial(p, rd, t, mid, trafHit, m, n, lp, ln, interior, podMat);
      col = planeLight(p, rd, t, mid, m, n, lp, ln, interior, podMat, trafHit);
    }
    if (!pod) col = applyFog(col, ro, rd, t);
    if (trafHit) loadMain();
  }
  // propeller discs (motion-blurred), composited over scene
  if (uPlaneOn == 1 && uWreck == 0) {
    mat3 inv = transpose(uPlaneRot);
    vec3 lo = inv*(ro - uPlanePos), ld = inv*rd;
    for (int i=0;i<2;i++){
      if (i >= uPropCount) break;
      vec4 pr = uProp[i];
      if (abs(ld.z) < 1e-4) continue;
      float tp = (pr.z - lo.z)/ld.z;
      if (tp < 0.0 || tp > t) continue;
      vec3 hp = lo + ld*tp - pr.xyz;
      float r = length(hp.xy);
      if (r > pr.w) continue;
      float blades = uPr.z; float blur = uPr.y;
      float ang = atan(hp.y, hp.x) - uPr.x;
      float bl = smoothstep(0.86, 0.95, cos(blades*ang*0.5*2.0)) * (1.0 - smoothstep(pr.w*0.9, pr.w, r));
      float a = mix(bl, 0.10 + 0.08*smoothstep(0.6, 1.0, cos(blades*ang)) + 0.25*smoothstep(pr.w*0.95, pr.w, r), blur);
      vec3 pc = vec3(0.04)*(uSunCol*max(uSunDir.y,0.0) + 0.2) + vec3(0.6,0.6,0.1)*smoothstep(pr.w*0.9, pr.w, r)*0.3;
      col = mix(col, pc, clamp(a, 0.0, 1.0)*0.85);
    }
  }
  // research jet exhaust plumes (additive, depth-limited by the scene)
  if (uPlaneOn == 1 && uWreck == 0 && uVapor.x > 0.01) { vec3 c0 = col; col = vaporCone(col, ro, rd, t, jitter); if (dot(abs(col - c0), vec3(1.0)) > 0.02) taaFlag = min(taaFlag, 0.2); }
  // (the research jets' flames are laid over the clouds below, not under them: the clouds on this ray lie mostly
  // beyond the aircraft, and composited afterwards they covered the reheat and the pods' plasma)
  vec3 plE = vec3(0.0); float plT = 1.0;
  if (uPlaneOn == 1 && uWreck == 0 && gPS.w < 0.5 && int(gM[0].z + 0.5) == 5) { plE = jetPlumes(ro, rd, t, jitter); plT = gPlumeT; }
  if (uPlaneOn == 1 && uWreck == 0 && gPS.w < 0.5 && int(gM[0].z + 0.5) == 6) { plE = wraithPlumes(ro, rd, t, jitter); plT = gPlumeT; }
  bool plume = plE.r + plE.g + plE.b > 0.01 || plT < 0.99;
  if (plE.r + plE.g + plE.b > 0.03) taaFlag = min(taaFlag, 0.2);
  if (!pod && uFxBeams + uFxBombs + uFxBlasts > 0) col = weaponsFx(col, ro, rd, t);
  // clouds (a cloaked craft: the skin first, then one cloud march along the whole camera ray through it)
  if (cloak) { col = cloakSkin(col, ckN, rd0, ckLp, ckLp.z - uWr[6].y + 0.8); col = applyFog(col, ro0, rd0, ckT); }
  // ordinary world pixels leave their clouds to the quarter-resolution cloud pass (composited before the TAA); the
  // cabin marches them here
  bool wrCk = cockpitView && int(gM[0].z + 0.5) == 6;
  bool cloudLater = uCloudSplit == 1 && !pod && !wrCk && !plume;   // (a plume pixel marches its own, to go under the flame)
  vec4 cl = pod || cloudLater ? vec4(0.0, 0.0, 0.0, 1.0) : traceClouds(cloak ? ro0 : ro, cloak ? rd0 : rd, cloak ? t + ckT : t, jitter);
  col = col*cl.a + cl.rgb;
  if (plume) col = col*plT + plE;
  oCloudMask = cloudLater ? 1.0 : 0.0;
  if (wrCk) col += wrHolo(roV, rdV, pod ? t : h0.x);   // the hologram floats inside the cabin, in front of everything
  if (cloak) { t += ckT; taaFlag = 0.5; }
#ifdef WR_CLIPATLAS
  col = wrClipAtlas(vUV); t = 1.0;
#endif
#ifdef WR_CLIPDEBUG
  if (wrCk && (onScr || pod)) {   // red: a display or gauge surface inside other cockpit geometry
    vec3 hp = transpose(uPlaneRot)*(uCamPos + rdV*h0.x - uPlanePos);
    int m0 = int(h0.y + 0.5);
    if ((m0 >= 41 && m0 <= 43) || (m0 >= 61 && m0 <= 63) || m0 == 68 || m0 == 69 || m0 == 76) {
      if (wrClip(hp, m0) < -0.0015) col = vec3(1e3, 0.0, 0.0); else col = vec3(0.0, 0.05, 0.0) + col*0.3;
    }
  }
#endif
  if (any(isnan(col)) || any(isinf(col)) || !(col.r + col.g + col.b < 1e7)) col = vec3(0.0);
  if (gDispPx && taaFlag > 0.4 && taaFlag < 0.6) taaFlag = 0.55;
  oColor = vec4(clamp(col, vec3(0.0), vec3(3e4)), taaFlag);
  oDepth = t;
#ifdef COST_MAP
  oColor = gCost;   // analysis build: the work counters instead of the colour
#endif
}
