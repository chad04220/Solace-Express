//! kPlaneMeshFS
//! The aircraft mesh pass: the baked static airframe into the G-buffer with the same materials and lighting classes as
//! the march (plane_gb.glsl), from the mesh's position, normal and material id; the field is asked only on the few
//! triangles that straddle two materials.
in vec3 vW; in vec3 vN; flat in float vId; in float vIdS; in float vAo;
uniform int uMeshTraffic;   // -1 the player's aircraft, else the traffic aircraft whose airframe this is
uniform sampler2D uScrDepth; uniform int uScrSkip;   // the research craft's screens' depth: a fragment at or behind a screen is dropped (the screens are holes to the world)
void main(){
  if (uScrSkip == 1 && gl_FragCoord.z >= texelFetch(uScrDepth, ivec2(gl_FragCoord.xy), 0).r - 2e-7) discard;
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
  if (abs(vIdS - vId) > 1e-3) { vec3 lp = gPC + transpose(gPR)*(p - gPP); mid = int(mapPiece(lp).y + 0.5); }
  bool pod = !traf && uPlaneOn == 1 && gPS.w > 0.5 && uWreck == 0;
  planeToGB(p, rd, t, mid, ln, pod, traf, !traf && gPS.w > 0.5 ? vAo : -1.0);
}
