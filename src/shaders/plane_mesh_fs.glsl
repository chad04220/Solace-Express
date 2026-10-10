//! kPlaneMeshFS
//! The aircraft mesh pass: the baked static airframe into the G-buffer with the same materials and lighting classes as
//! the march (plane_gb.glsl), from the mesh's position, normal and material id; the field is asked only on the few
//! triangles that straddle two materials.
in vec3 vW; in vec3 vN; flat in float vId; in float vIdS; in float vAo; in vec3 vB;
uniform int uMeshTraffic;   // -1 the player's aircraft, else the traffic aircraft whose airframe this is
// the research craft's screens are holes to the world (cabin_windows.glsl): uScrSkip, with the eye (body frame), the
// model, whether the XR-40's floor shows the bomb camera, and the part being drawn (-1 the static cabin)
uniform int uScrSkip; uniform vec3 uScrEye; uniform int uScrModel; uniform int uBombPane; uniform int uPartInst;
uniform mat3 uRot; uniform vec3 uPos;   // (plane_mesh_vs.glsl: the eye in the body frame is -uRot^T uPos, exact)
// The raised frame round each pilot's instrument cluster (plane_sdf.glsl: 14 mm wide, 12 mm proud of the panel) is
// finer than the bake's lattice, whose triangles there mix the frame and the panel: a strip of panel tilted like the
// frame's wall, a frame edge in steps. Along the eye's sight line instead: the frame's top, one of its walls, or the
// panel face round it. clusterFr: the frame's centre line's distance (x) and its gradient (yz) at a panel point
vec3 clusterFr(vec2 xy, vec4 E, int ck){
  bool co = xy.x*E.x < 0.0;
  float cx = ck == 2 ? 0.02 : (ck == 1 ? 0.09 : 0.05), hx = co ? coCluster(ck).y : (ck == 2 ? 0.2 : (ck == 1 ? 0.255 : 0.215)), hy = ck == 2 ? 0.098 : 0.112;
  vec2 fq = vec2(xy.x - (!co ? E.x + cx : -E.x + coCluster(ck).x + coShift(E, ck)), xy.y - (E.y - 0.32));
  vec2 dq = abs(fq) - vec2(hx, hy) + 0.02;
  return vec3(length(max(dq, 0.0)) + min(max(dq.x, dq.y), 0.0) - 0.02, normalize(max(dq, 1e-6))*sign(fq));
}
bool clusterFrame(vec3 q, vec3 e, inout int mid, out vec3 nB){
  nB = vec3(0.0, 0.0, 1.0);
  vec4 E = gM[22]; float pf = gM[21].w + 0.045; int ck = int(gM[21].z + 0.5);
  if ((mid != 10 && mid != 66) || abs(q.z - pf - 0.006) > 0.02 || abs(q.y - (E.y - 0.32)) > 0.16) return false;
  vec3 v = q - e;
  if (v.z > -1e-4 || abs(clusterFr(q.xy, E, ck).x) > 0.03) return false;
  vec3 ft = clusterFr(e.xy + v.xy*((pf + 0.012 - e.z)/v.z), E, ck);   // where the line crosses the frame's top
  if (abs(ft.x) <= 0.007) { mid = 66; return true; }
  vec3 fb = clusterFr(e.xy + v.xy*((pf - e.z)/v.z), E, ck);           // and the panel face
  if (abs(fb.x) <= 0.007 || (ft.x > 0.0) != (fb.x > 0.0)) { mid = 66; nB = vec3(ft.x > 0.0 ? fb.yz : -fb.yz, 0.0); return true; }
  mid = 10; return true;
}
void main(){
#if HAS_RESEARCH
  if (RESEARCH_ON && uScrSkip == 1 && cabinWindowCut(vB - uScrEye, uScrModel, uBombPane == 1, uPartInst >= 0)) discard;
#endif
  gZero = min(uQuality, 0);
  // (a traffic aircraft is drawn with its type's own build too - the XR-30s of the formations and the escort pair: an own
  // build's gM is its type's constants, the ones afModelOf matched the traffic aircraft's by; loadTraffic reads the rest)
  bool traf = uMeshTraffic >= 0;
  if (traf) { loadTraffic(uMeshTraffic); trafficXf(uMeshTraffic); } else { loadMain(); pieceXf(-1); }
  vec3 d = vW, p = uCamPos + d;   // (from the camera: plane_mesh_vs.glsl; p, in world metres, only for the world's lookups)
  gRelSet = true; gRel = d;
  // the cloaked part of the XR-40 (behind the cloak's sweeping front) is see-through: the effects pass draws it over the
  // lit frame, so the G-buffer keeps what lies behind it
#if HAS_RESEARCH
  if (RESEARCH_ON && !traf && uWr[4].w > 0.001 && int(gM[0].z + 0.5) == 6 && gPS.w < 0.5 && uWreck == 0 && vB.z < uWr[6].y) discard;
#endif
  float t = length(d); vec3 rd = d/max(t, 1e-6);
  vec3 ln = normalize(vN);
  // a face turned from the eye in the cabin - the shell's outer skin seen through a gap in a window opening's lip,
  // where the lip runs thinner than the lattice along the roof - is lit as the cabin side it stands for: lit as the
  // skin outside, facing the sun, it showed as pale shards along the side windows' tops (the light aircraft, looking up)
  if (!traf && gPS.w > 0.5 && uPartInst < 0 && !gl_FrontFacing) ln = -ln;   // (the bake winds each triangle to its outward normal; a part's pose may mirror it)
  int mid = int(vId + 0.5);
  if (abs(vIdS - vId) > 1e-3) { vec3 lp = gPC + transpose(gPR)*(d + (uCamPos - gPP)); mid = int(mapPiece(lp).y + 0.5); }
  // (a screen's id outside every outline - the bake's triangles overrunning the glass - is the frame round it; the
  // research cockpits' own display panels against their frames and mounts from their shapes: cabin_windows.glsl)
#if HAS_RESEARCH
  if (RESEARCH_ON && uScrSkip == 1 && cabinScreenId(mid) && !(uBombPane == 1 && mid == 61)) mid = uScrModel == 6 ? 65 : 44;
#endif
#if HAS_RESEARCH
  if (RESEARCH_ON && !traf && gPS.w > 0.5 && int(gM[0].z + 0.5) >= 5) mid = cabinPanelId(vB - gM[22].xyz, int(gM[0].z + 0.5), mid);
#endif
#if HAS_FLEET_CABIN
  if (FLEET_ON && !traf && gPS.w > .5 && uPartInst < 0) mid = fleetPanelId(vB, mid);
#endif
  vec3 conN;   // (the XR-40's consoles and display mounts: their own field's normal, cabin_windows.glsl)
#if HAS_WRAITH   // (the XR-40's alone: cabin_windows.glsl)
  if (RESEARCH_ON && !traf && gPS.w > 0.5 && uPartInst < 0 && cabinConsoleNormal(vB - gM[22].xyz, int(gM[0].z + 0.5), mid, conN)) ln = conN;
#endif
  vec3 wallN;   // (a window's frame: the surface the eye truly sees there, not the mesh's zigzag one)
#if HAS_WRAITH   // (the XR-40's alone: cabin_windows.glsl)
  if (RESEARCH_ON && uScrSkip == 1 && uPartInst < 0 && cabinWallNormal(vB - uScrEye, uScrModel, mid, wallN)) ln = wallN;
#endif
  vec3 frN;   // (a light aircraft's cluster frames)
#if HAS_FLEET
  if (FLEET_ON && !traf && gPS.w > 0.5 && uPartInst < 0 && int(gM[0].z + 0.5) < 5 && !fleetCabin() && !isMantis() && clusterFrame(vB, -transpose(uRot)*uPos, mid, frN)) ln = frN;
#endif
  bool pod = !traf && uPlaneOn == 1 && gPS.w > 0.5 && uWreck == 0;
#ifdef PROBE_MESH_SHADE
  if (!traf) { gbWritePrelit(t, ln, GB_POD, vec3(0.05)); oG3 = vec4(1.0, 1.0, 1.0, float(GBF_RIGID)/255.0); return; }   // (the analysis's probe build: Renderer::kProbeMeshShade)
#endif
  planeToGB(p, rd, t, mid, ln, pod, traf, !traf && gPS.w > 0.5 ? vAo : -1.0);
}
