//! kHullBakeMain
//! Aircraft hull and mesh bakes (aircraft_hull.cpp, aircraft_mesh.cpp): the airframe's field at each listed point
//! (aircraft space). uHMode 0: the least distance over every listed state of the gear, flaps, steering and controls
//! (a hull built from it holds the airframe in any of them); 4: the greatest; 1: the distance in state uHState; 2: in
//! that state the distance, the material id and the cabin's ambient occlusion; 3: in that state the surface normal.
//! uHPart picks what is evaluated (plane_parts.glsl gPartMode).

uniform sampler2D uHPts; uniform int uHStN; uniform vec4 uHStPS[128]; uniform vec4 uHStCtl[128]; uniform vec4 uHStWr[128]; uniform vec4 uHStWr2[128];
uniform int uHMode; uniform int uHState;
uniform int uHPart; uniform vec2 uHPartSide;   // -1 the whole aircraft, -2 without its rigid parts, >= 0 that part alone in its own frame (plane_parts.glsl); the side: its rest instance, for the ambient occlusion
// one of the listed states: the gear / flaps / steering / cabin, the controls, and the XR-40's pods, vanes, fan, bay,
// turrets and bomb (its surfaces follow the controls)
void hullState(int s){
  gPS = uHStPS[s]; gCtl = uHStCtl[s];
  vec4 w = uHStWr[s], w2 = uHStWr2[s];
  gWr[0] = vec4(w.x); gWr[1] = vec4(w.y); gWr[2] = vec4(w.z); gWr[3] = vec4(w.w);
  gWr[4] = vec4(w2.x, w2.y, w2.z, 0.0); gWr[5] = vec4(gCtl.x, gCtl.z, gCtl.y, 0.0); gWr[6] = vec4(w2.w, 0.0, 0.0, 0.0);
  // the XR-30's 2D nozzles read gFlame.z (the vectoring angle: the hover setting less half the stick): swept here, so the
  // bake sees them move (they were classed static, baked at the first frame's angle)
  if (int(gM[0].z + 0.5) == 5) gFlame.z = w.x - gCtl.x*0.5;
#if HAS_CABIN
  if (gPS.w > 0.5) loadCabinFit();
#endif
}
void main(){
  vec3 p = texelFetch(uHPts, ivec2(gl_FragCoord.xy), 0).xyz;
  loadMain(); pieceXf(-1);
  gPartMode = uHPart;
  gZero = min(uQuality, 0);
  // one call each of the field, its normal and the occlusion, whatever the mode (each call written out is another
  // copy of the airframe's distance in the program): the field in the loop over the states (modes 0 and 4 all of
  // them, the others uHState alone)
  bool all = uHMode == 0 || uHMode == 4;
  int s0 = all ? 0 : uHState, sn = all ? uHStN : 1;
  float d = uHMode == 4 ? -1e9 : 1e9; vec2 r = vec2(0.0);
  for (int i = gZero; i < 128; i++) {
    if (i >= sn) break;
    hullState(s0 + i);
    if (uHMode == 3) break;   // (the normal alone)
    r = mapPlane(p);
    d = uHMode == 4 ? max(d, r.x) : min(d, r.x);
  }
  vec4 o = vec4(d, 0.0, 0.0, 0.0);
  if (uHMode == 2 || uHMode == 3) {
    vec3 n = planeNormal(p);
    if (uHMode == 3) o = vec4(n, 0.0);
    else {
      float ao = 1.0;
#if HAS_CABIN
      if (gPS.w > 0.5) {
        vec3 ap = p, an = n;
        if (uHPart >= 0) {   // a part's occlusion in the cabin about it, at its rest pose
          Pose X = partPose(uHPart, uHPartSide); gPartMode = -1;
          ap = X.R*p + X.T; an = X.R*n;
        }
        ao = interiorAO(ap, an);
      }
#endif
      o = vec4(r.x, r.y, ao, 0.0);
    }
  }
  oColor = o; oDepth = o.x; oCloudMask = 0.0;
}
