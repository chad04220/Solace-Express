//! kPlaneGB
//! An airframe pixel into the G-buffer, shared by the objects pass (the march) and the aircraft mesh pass. p: the
//! world point, t: its distance, mid: the material id the field gave, ln: the body-space normal, pod: a cockpit view
//! (the aircraft lit by its own fixtures, no fog, no clouds), trafHit: a traffic aircraft (its data loaded),
//! aoIn: the cabin's ambient occlusion when baked (-1: tap the field).
void planeToGB(vec3 p, vec3 rd, float t, int mid, vec3 ln, bool pod, bool trafHit, float aoIn){
  float sunVis = smoothstep(-0.05, 0.05, uSunDir.y);
  int eng = int(gM[0].z + 0.5);
  bool wr = RESEARCH_ON && eng == 6;
  // a research jet's display from the pilot's seat: its camera's picture, with the display's own look and symbology
  if (RESEARCH_ON && pod && eng >= 5 && ((mid >= 41 && mid <= 43) || (mid >= 61 && mid <= 63))) {
    vec3 scrL = transpose(uPlaneRot)*(gRelSet ? gRel + (uCamPos - uPlanePos) : p - uPlanePos);
    bool bomb; vec3 rdc;
    vec3 col = feedScreen(mid, scrL, rdc, bomb);
    if (uScrWin == 1 && !bomb) discard;   // a window: the world drawn before the airframe stays
    col = bomb ? wrFeedOverlay(col, scrL) : wr ? wraithScreen(col, rdc, mid, scrL) : jetScreen(col, rdc, mid, scrL);
    if (wr) col += wrHolo(uCamPos, rd, t);   // the hologram floats inside the cabin, in front of the displays
    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    gbWritePrelit(t, -rd, GB_DISPLAY, clamp(col, vec3(0.0), vec3(3e4)));
    oG3 = vec4(1.0, 1.0, 1.0, float(GBF_DISPLAY)/255.0);
    return;
  }
  Mat m; vec3 n, lp, lnOut; bool interior, podMat;
  gDispPx = false;
  planeMaterialN(p, rd, t, mid, trafHit, ln, m, n, lp, lnOut, interior, podMat);
  int flags = (trafHit || uWreck > 0) ? GBF_MOVING : GBF_RIGID;
  // the sun's shadow terms: the terrain's is one value for the whole intact airframe (from the CPU); the airframe's own
  // from its sun shadow map, marched only where the map can't say (plane_light.glsl afSunSelf: one call for the
  // exterior and the cabin)
  float tsh = 1.0, self = 1.0;
  if (sunVis > 0.0 && !podMat) {
    tsh = (trafHit || uWreck > 0) ? terrainShadow(p, uSunDir, t) : uPlaneTSh;
    if (tsh > 0.0 && !trafHit) self = afSunSelf(p, n, interior);
  }
  if (pod || interior || podMat) {   // the cockpit: lit by its own fixtures and the sun through the windows (plane_light.glsl)
    gInteriorAO = aoIn;
    vec3 col = planeLight(p, rd, t, mid, m, n, lp, lnOut, interior, podMat, sunVis > 0.0 ? tsh*self : 0.0);
    gInteriorAO = -1.0;
    if (pod && wr) col += wrHolo(uCamPos, rd, t);   // the hologram floats inside the cabin, in front of everything
    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    gbWritePrelit(t, n, pod ? GB_POD : GB_CABIN, clamp(col, vec3(0.0), vec3(3e4)));
    oG3 = vec4(1.0, 1.0, 1.0, float(flags + (gDispPx ? GBF_DISPLAY : 0))/255.0);
    return;
  }
  // the exterior: the lighting pass shades it with the sun's shadow terms it can't compute itself (G3)
  gbWrite(t, n, trafHit ? GB_TRAFFIC : uWreck > 0 ? GB_WRECK : GB_PLANE, m, 1.0);
  oG3 = vec4(1.0, self, tsh, float(flags + (mid <= 5 ? GBF_GLINT : 0) + (gDispPx ? GBF_DISPLAY : 0))/255.0);
}
