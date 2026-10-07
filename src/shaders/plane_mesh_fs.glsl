//! kPlaneMeshFS
//! The aircraft mesh pass: the baked static airframe into the G-buffer with the same materials and lighting classes as
//! the march (plane_gb.glsl), from the mesh's position, normal and material id; the field is asked only on the few
//! triangles that straddle two materials.
in vec3 vW; in vec3 vN; flat in float vId; in float vIdS; in float vAo;
uniform int uMeshThin;   // the cockpit's thin patch (laid out 4.7 mm fat: every pixel of it traced)
uniform int uMeshTraffic;   // -1 the player's aircraft, else the traffic aircraft whose airframe this is
uniform sampler2D uScrDepth; uniform int uScrSkip; uniform float uLogC; uniform float uScrNear;   // uScrNear: how far behind a pane the trace still decides (0: none)   // the research craft's screens' depth: a fragment at or behind a screen is dropped (the screens are holes to the world)
void main(){
  // behind a research cockpit's screen or window pane: dropped - but within 3 cm of it (the frame round the pane: the
  // pane's mesh outline is the lattice's, and cut teeth out of the frame) the trace below decides, frame or pane
  bool nearScr = false; float behind = 0.0;   // (how far behind the pane, along the ray)
  if (uScrSkip == 1) {
    float zs = texelFetch(uScrDepth, ivec2(gl_FragCoord.xy), 0).r;
    if (gl_FragCoord.z >= zs - 2e-7) {
      float wf = exp2(gl_FragCoord.z*2.0/uLogC) - 1.0, ws = exp2(zs*2.0/uLogC) - 1.0;
      int m0 = int(vId + 0.5);   // (a pane's own fragments: dropped as ever)
      if (uScrNear <= 0.0 || (m0 >= 41 && m0 <= 43) || (m0 >= 61 && m0 <= 63) || wf - ws > uScrNear) discard;
      nearScr = true; behind = (wf - ws)*length(vW - uCamPos)/max(wf, 1e-4);
    }
  }
  gZero = min(uQuality, 0);
  bool traf = uMeshTraffic >= 0;
  if (traf) { loadTraffic(uMeshTraffic); trafficXf(uMeshTraffic); } else { loadMain(); pieceXf(-1); }
  vec3 p = vW;
  // the cloaked part of the XR-40 (behind the cloak's sweeping front) is see-through: the effects pass draws it over the
  // lit frame, so the G-buffer keeps what lies behind it
  if (RESEARCH_ON && !traf && uWr[4].w > 0.001 && int(gM[0].z + 0.5) == 6 && gPS.w < 0.5 && uWreck == 0 && (transpose(uPlaneRot)*(p - uPlanePos)).z < uWr[6].y) discard;
  vec3 d = p - uCamPos; float t = length(d); vec3 rd = d/max(t, 1e-6);
  vec3 ln = normalize(vN);
  int mid = int(vId + 0.5);
  bool pod = !traf && uPlaneOn == 1 && gPS.w > 0.5 && uWreck == 0;
  bool straddle = abs(vIdS - vId) > 1e-3;
  // The cockpit's small details within arm's reach (bezels, frames, knobs, lenses, the edges between two materials) are
  // finer than the bake's 1.56 cm lattice: there the true surface is found along this pixel's ray, from 2.5 cm before
  // the mesh to 3 cm past it, with its own normal and material - the edge lands where the shape has it, whichever side
  // of it the mesh fell (a screen's outline too). The broad smooth surfaces (the panel face, the shell, the seats) keep the mesh, except
  // seen edge-on: their outlines (the roof and the window frames against the sky) are refined the same way, and where
  // the ray clears the shape by 3 mm the mesh's overhang is dropped (the world drawn before it shows).
  mat3 inv = transpose(gPR);
  vec3 lo = gPC + inv*(uCamPos - gPP), ld = inv*rd;
  bool jetPod = RESEARCH_ON && int(gM[0].z + 0.5) >= 5;   // (the research jets' sealed cockpits: larger, their frames further off)
  bool scrId = (mid >= 41 && mid <= 43) || (mid >= 61 && mid <= 63);
  bool broad = jetPod ? scrId || int(gM[0].z + 0.5) == 6 && mid == 64 : mid >= 10 && mid <= 14 || mid == 69;   // (panel face, shell, seats, controls, glareshield, webbing; a research jet's screens and panes - their outlines are the straddling triangles' - and the XR-40's shell and ribs)
  float reach = jetPod ? 2.4 : 1.3;
  bool edgeOn = abs(dot(ln, ld)) < 0.2;
  bool thinP = uMeshThin == 1;
  bool refine = pod && (t < reach && !broad || (straddle || nearScr) && t < reach || (edgeOn || thinP) && t < 5.0) && (uDbg & 4096) == 0;
  // (an outline's and the thin patch's own normals are right: the shape's is taken only for the details and the edges between materials)
  bool needN = t < reach && !broad || straddle || nearScr;
  if (refine || straddle) {   // (one loop, one copy of the field: the march, then the normal's four taps; or the id alone)
    float s = refine ? t - 0.025 - behind : t, sEnd = t + (edgeOn || thinP ? 0.12 : 0.03), e = 0.0012, hid = vId;   // (an edge-on ray runs on further to clear the outline)
    vec3 nAcc = vec3(0.0); int phase = 0; float lastD = 0.0; bool cleared = false;
    for (int i = gZero; i < 24; i++) {
      vec3 k = 2.0*vec3(float(((phase + 2) >> 1) & 1), float(((phase - 1) >> 1) & 1), float((phase - 1) & 1)) - 1.0;
      vec2 r = mapPiece(lo + ld*s + (phase > 0 ? k*e : vec3(0.0)));
      if (!refine) { hid = r.y; break; }
      if (phase == 0) {
        if (i == 0 && r.x < 0.0) break;   // (begun inside another part: the mesh stays)
        if (r.x < 0.0004) { hid = r.y; phase = needN ? 1 : 5; if (needN) continue; break; }
        s += r.x; lastD = r.x; if (s > sEnd) { cleared = thinP || lastD > 0.003; break; }
      } else { nAcc += k*r.x; if (++phase == 5) break; }
    }
    if (nearScr && phase != 5) discard;   // (no surface of its own found in front of the pane: behind it, as before)
    if (!refine || phase == 0) mid = straddle ? int(hid + 0.5) : mid;
    if (refine && cleared && (uScrSkip == 0 || thinP)) discard;   // (the thin patch is drawn after the rest: what lies behind its halo is there)
    if (refine && phase == 5) { t = s; p = uCamPos + rd*s; if (needN) ln = normalize(nAcc); mid = int(hid + 0.5); }
    if (nearScr && mid >= 80) discard;   // (through the pane onto the XR-40's outer hull: the world's, as ever)
  }
  planeToGB(p, rd, t, mid, ln, pod, traf, !traf && gPS.w > 0.5 ? vAo : -1.0);
}
