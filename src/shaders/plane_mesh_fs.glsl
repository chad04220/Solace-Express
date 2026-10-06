//! kPlaneMeshFS
//! The aircraft mesh pass: the baked static airframe into the G-buffer with the same materials and lighting classes as
//! the march (plane_gb.glsl), from the mesh's position, normal and material id; the field is asked only on the few
//! triangles that straddle two materials.
in vec3 vW; in vec3 vN; flat in float vId; in float vIdS; in float vAo;
uniform int uMeshTraffic;   // -1 the player's aircraft, else the traffic aircraft whose airframe this is
uniform float uLogC;   // the raster passes' logarithmic depth (the fine patch writes the refined surface's)
#if defined(MESH_REFINE) && defined(GL_ARB_conservative_depth)
layout(depth_greater) out float gl_FragDepth;
#endif
uniform sampler2D uScrDepth; uniform int uScrSkip;   // the research craft's screens' depth: a fragment at or behind a screen is dropped (the screens are holes to the world)
void main(){
  if (uScrSkip == 1 && gl_FragCoord.z >= texelFetch(uScrDepth, ivec2(gl_FragCoord.xy), 0).r - 2e-7) discard;
  gZero = min(uQuality, 0);
  bool traf = uMeshTraffic >= 0;
  if (traf) { loadTraffic(uMeshTraffic); trafficXf(uMeshTraffic); } else { loadMain(); pieceXf(-1); }
  vec3 p = vW;
  vec3 d = p - uCamPos; float t = length(d); vec3 rd = d/max(t, 1e-6);
  vec3 ln = normalize(vN);
  int mid = int(vId + 0.5);
#ifdef MESH_REFINE
  // the fine patch over the cabin's thin parts (aircraft_mesh.cpp): this surface is a shell 4 mm outside the field's,
  // and the field is marched from it along the view ray to the surface itself - a plate's edge is then the field's
  // own at any resolution, not the lattice's. The first step is the shell's own thickness along the ray (a flat part:
  // one step lands; held to twice the safe step); the rest are sphere-tracing steps. Only a ray that has clearly left
  // the part (past the edge, farther from it than the shell) is dropped; one that runs out of steps grazing a face keeps its nearest point
  // (dropping those opened sky-coloured cracks along creases). The depth written is the surface's, never nearer than
  // the shell's (layout depth_greater: the coarse mesh's depth rejects the hidden patch before this runs)
  vec3 lq = gPC + transpose(gPR)*(p - gPP), lrd = transpose(gPR)*rd;   // (body space, the normal's)
  // (no facing test: the shell's far side marches away from the surface and is dropped as a miss, or lies behind the
  // near side; a test by the vertex normal or by the winding, both of which turn over at a sharp rim, dropped near-side
  // fragments there as streaks and bites)
  // (the field is asked in one place, the loop - each call written out is another copy of it in the shader; the
  // nearest point's material comes with it)
  float tt = 0.0, dmin = 1e9, tbest = 0.0, idBest = vId; bool gone = false;
  for (int i = gZero; i < 17; i++) {
    vec2 r = mapPiece(lq + lrd*tt); float dd = r.x;
    if (dd < dmin) { dmin = dd; tbest = tt; idBest = r.y; }
    else if (dd > 6e-3 && dmin > 1.2e-3) { gone = true; break; }   // past the edge, and moving away
    if (dd < 1e-4) break;
    float step = i == 0 ? min(dd/max(-dot(lrd, ln), 0.2), 2.0*dd) : dd;   // (the first step at most twice the safe one: at a plate's rim the shell's normal is the rim's, and a step by its cosine jumped clear past the edge)
    tt += min(step, 0.03);
    if (tt > 0.08) { gone = true; break; }
  }
  if (gone && dmin > 1.2e-3) discard;
  p += rd*tbest; t += tbest;
  if (abs(vIdS - vId) > 1e-3) mid = int(idBest + 0.5);
  float w = uPano.x > 0.0 ? t : -t*dot(rd, uCamRot[2]);
  gl_FragDepth = (log2(max(1e-6, 1.0 + w))*uLogC - 1.0)*0.5 + 0.5;
#else
  if (abs(vIdS - vId) > 1e-3) { vec3 lp = gPC + transpose(gPR)*(p - gPP); mid = int(mapPiece(lp).y + 0.5); }
#endif
  bool pod = !traf && uPlaneOn == 1 && gPS.w > 0.5 && uWreck == 0;
  planeToGB(p, rd, t, mid, ln, pod, traf, !traf && gPS.w > 0.5 ? vAo : -1.0);
}
