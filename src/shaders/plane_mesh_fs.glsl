//! kPlaneMeshFS
//! The aircraft mesh pass: the baked static airframe into the G-buffer with the same materials and lighting classes as
//! the march (plane_gb.glsl), from the mesh's position, normal and material id; the field is asked only on the few
//! triangles that straddle two materials.
in vec3 vW; in vec3 vN; flat in float vId; in float vIdS; in float vAo; in vec3 vB;
uniform int uMeshTraffic;   // -1 the player's aircraft, else the traffic aircraft whose airframe this is
// the research craft's screens are holes to the world (cabin_windows.glsl): uScrSkip, with the eye (body frame), the
// model, whether the XR-40's floor shows the bomb camera, and the part being drawn (-1 the static cabin)
uniform int uScrSkip; uniform vec3 uScrEye; uniform int uScrModel; uniform int uBombPane; uniform int uPartInst;
void main(){
  if (uScrSkip == 1 && cabinWindowCut(vB - uScrEye, uScrModel, uBombPane == 1, uPartInst >= 0)) discard;
  gZero = min(uQuality, 0);
  bool traf = uMeshTraffic >= 0;
  if (traf) { loadTraffic(uMeshTraffic); trafficXf(uMeshTraffic); } else { loadMain(); pieceXf(-1); }
  vec3 d = vW, p = uCamPos + d;   // (from the camera: plane_mesh_vs.glsl; p, in world metres, only for the world's lookups)
  gRelSet = true; gRel = d;
  // the cloaked part of the XR-40 (behind the cloak's sweeping front) is see-through: the effects pass draws it over the
  // lit frame, so the G-buffer keeps what lies behind it
  if (RESEARCH_ON && !traf && uWr[4].w > 0.001 && int(gM[0].z + 0.5) == 6 && gPS.w < 0.5 && uWreck == 0 && vB.z < uWr[6].y) discard;
  float t = length(d); vec3 rd = d/max(t, 1e-6);
  vec3 ln = normalize(vN);
  int mid = int(vId + 0.5);
  if (abs(vIdS - vId) > 1e-3) { vec3 lp = gPC + transpose(gPR)*(d + (uCamPos - gPP)); mid = int(mapPiece(lp).y + 0.5); }
  // (a screen's id outside every outline - the bake's triangles overrunning the glass - is the frame round it; the
  // research cockpits' own display panels against their frames and mounts from their shapes: cabin_windows.glsl)
  if (uScrSkip == 1 && cabinScreenId(mid) && !(uBombPane == 1 && mid == 61)) mid = uScrModel == 6 ? 65 : 44;
  if (RESEARCH_ON && !traf && gPS.w > 0.5 && int(gM[0].z + 0.5) >= 5) mid = cabinPanelId(vB - gM[22].xyz, int(gM[0].z + 0.5), mid);
  vec3 conN;   // (the XR-40's consoles and display mounts: their own field's normal, cabin_windows.glsl)
  if (RESEARCH_ON && !traf && gPS.w > 0.5 && uPartInst < 0 && cabinConsoleNormal(vB - gM[22].xyz, int(gM[0].z + 0.5), mid, conN)) ln = conN;
  vec3 wallN;   // (a window's frame: the surface the eye truly sees there, not the mesh's zigzag one)
  if (uScrSkip == 1 && uPartInst < 0 && cabinWallNormal(vB - uScrEye, uScrModel, mid, wallN)) ln = wallN;
  bool pod = !traf && uPlaneOn == 1 && gPS.w > 0.5 && uWreck == 0;
  planeToGB(p, rd, t, mid, ln, pod, traf, !traf && gPS.w > 0.5 ? vAo : -1.0);
}
