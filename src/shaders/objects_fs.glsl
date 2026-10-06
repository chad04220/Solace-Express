//! kObjectsFS
//! The raster renderer's objects pass: the player's aircraft (its march starting on the rasterized hull), the traffic,
//! wreck pieces, debris and the UFO, marched through their distance fields along this pixel's camera ray and written
//! into the G-buffer with depth, so they take their place among the terrain and the scenery and the lighting pass
//! shades them. What only the fields know goes into GB3: ambient occlusion, the airframe's own sun shadow, the
//! terrain's sun shadow at the airframe, and the GBF_* flags. The cockpit (the sealed pod, the cabin seen from the
//! pilot's seat) and the research jets' display screens come out prelit: their light comes from their own fixtures
//! and the cameras' pictures, as in the ray tracer, not from the sun.
in vec2 vUV;
uniform float uLogC;
uniform int uMeshOn;   // the player's aircraft is drawn as a mesh where it never moves: march only where its moving hull says
void main(){
  gZero = min(uQuality, 0);
  loadMain();
  vec2 ndc = (vUV + uJit)*2.0 - 1.0;
  vec3 rd = camRay(ndc), ro = uCamPos;
  float tmax = 80000.0;
  bool onScr = false;   // a research jet's display from the pilot's seat
  // pod: a pixel on the aircraft in a cockpit view - shaded from the cockpit alone, no fog, no clouds (rt_main.glsl)
  bool pod = false, cockpitView = uPlaneOn == 1 && gPS.w > 0.5 && uWreck == 0;
  // the aircraft hull was rasterized along exactly this ray: start the airframe march where it is (0: on its inside)
  float hullT = 0.0;
  if (uHullOn == 1 && uWreck == 0) { float hv = texelFetch(uEnv, ivec2(gl_FragCoord.xy), 0).g; hullT = hv > 1e29 ? hv : (hv > 0.0 ? max(uHullNear, hv*0.999 - 0.1) : 0.0); }
  if (uFeedSkip > 0.0 && hullT == 0.0) hullT = uFeedSkip;
  bool jetC = int(gM[0].z + 0.5) >= 5;
  // (the player's aircraft as a mesh: only a pixel its moving hull covers has anything left to march - and in the
  // cockpit the first uHullNear metres from the eye, whose hull faces the hull pass drops, as the ray tracer does)
  float hullEnd = cockpitView ? (jetC ? 6.0 : planeBound()*2.0) : tmax;
  if (uMeshOn == 1 && uHullOn == 1 && uHullExitOn == 1 && uWreck == 0) { float he = texelFetch(uEnv, ivec2(gl_FragCoord.xy), 0).a; if (he > 0.0) hullEnd = min(hullEnd, he*1.002 + 0.05); }
  vec2 hTop = uMeshOn == 1 && (uHullOn == 0 || (hullT > 1e29 && uHullNear <= 0.0)) ? vec2(-1.0) : tracePlaneHull(ro, rd, hullEnd, hullT);
  int hTopPiece = gPI;   // (traffic tracing moves the piece transform; restored before shading)
  if (cockpitView && hTop.x > 0.0) {
    int id0 = int(hTop.y + 0.5);
    if (jetC && ((id0 >= 41 && id0 <= 43) || (id0 >= 61 && id0 <= 63))) onScr = true;
    else pod = true;
  }
  // the cloaked part of the XR-40 is see-through: its own pass draws it over the lit frame
  vec2 ph = hTop;
  if (uWr[4].w > 0.001 && uPlaneOn == 1 && uWreck == 0 && !cockpitView && int(gM[0].z + 0.5) == 6 && ph.x > 0.0) {
    vec3 lp0 = transpose(uPlaneRot)*(ro + rd*ph.x - uPlanePos);
    if (lp0.z < uWr[6].y) ph = vec2(-1.0);
  }
  float t = 1e9; int hit = 0;
  if (ph.x > 0.0) { t = ph.x; hit = 4; }
  int trafK = -1; vec2 trafH = vec2(-1.0);
  if (!pod && uTrafficN > 0) { gTrafCamRay = true; trafH = traceTraffic(ro, rd, t < 1e8 ? t : tmax, trafK); gTrafCamRay = false; loadMain(); pieceXf(hTopPiece); }
  float tU = (uUfoOn == 1 && !pod) ? traceUfo(ro, rd, t < 1e8 ? t : tmax) : -1.0;
  if (tU > 0.0 && tU < t) { t = tU; hit = 8; }
  bool trafHit = false;
  if (trafH.x > 0.0 && trafH.x < t) { t = trafH.x; hit = 4; ph = trafH; trafHit = true; loadTraffic(trafK); trafficXf(trafK); }
  vec3 dn; float dChar = 0.0;
  float tD = uDebN > 0 && !pod ? traceDebris(ro, rd, t < 1e8 ? t : tmax, dn, dChar) : -1.0;
  if (tD > 0.0 && tD < t) { t = tD; hit = 6; }
  if (hit == 0) discard;
  vec3 p = ro + rd*t;
  // the raster passes' logarithmic depth of the view depth along the camera's axis (a panorama: the distance itself)
  float w = uPano.x > 0.0 ? t : -t*dot(rd, uCamRot[2]);
  gl_FragDepth = (log2(max(1e-6, 1.0 + w))*uLogC - 1.0)*0.5 + 0.5;
  float sunVis = smoothstep(-0.05, 0.05, uSunDir.y);
  if (hit == 8) {   // the UFO
    Mat m; vec3 n; bool cabin; float ao;
    ufoMaterial(p, rd, t, m, n, cabin, ao);
    gbWrite(t, n, GB_UFO, m, ao);
    oG3 = vec4(1.0, 1.0, 1.0, float(GBF_MOVING)/255.0);
    return;
  }
  if (hit == 6) {   // a debris chunk: torn painted skin or charred metal (rt_shade.glsl debrisShade)
    vec3 nT; vec4 tx = triSample(p*2.0, dn, M_METAL, 1.0, nT);
    Mat m; m.metal = 0.5; m.emit = vec3(0.0); m.nrm = nT;
    float burn = vnoise(p.xz*3.0 + p.y);
    m.alb = dChar > 0.5 ? vec3(0.03, 0.028, 0.026)*(0.6 + burn) : gColBase*tx.rgb*(0.3 + 0.4*burn);
    m.rough = dChar > 0.5 ? 0.9 : 0.45;
    gbWrite(t, applyTS(dn, m.nrm, 0.4), GB_DEBRIS, m, 1.0);
    oG3 = vec4(1.0, 1.0, sunVis > 0.0 ? terrainShadow(p + dn*0.05, uSunDir, t) : 0.0, float(GBF_MOVING)/255.0);
    return;
  }
  // the aircraft, a traffic aircraft or a wreck piece (gP* hold the transform of the piece that was hit); a display
  // in a research jet's cockpit shows its camera's picture (plane_gb.glsl)
  vec3 lp0 = gPC + transpose(gPR)*(p - gPP);
  planeToGB(p, rd, t, int(ph.y + 0.5), planeNormal(lp0), pod || onScr, trafHit, -1.0);
}
