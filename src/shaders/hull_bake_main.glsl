//! kHullBakeMain
//! Aircraft hull and mesh bakes (aircraft_hull.cpp, aircraft_mesh.cpp): the airframe's field at each listed point
//! (aircraft space). uHMode 0: the least distance over every listed state of the gear, flaps, steering and controls
//! (a hull built from it holds the airframe in any of them); 4: the greatest; 1: the distance in state uHState; 2: in
//! that state the distance, the material id and the cabin's ambient occlusion; 3: in that state the surface normal.

uniform sampler2D uHPts; uniform int uHStN; uniform vec4 uHStPS[128]; uniform vec4 uHStCtl[128]; uniform vec4 uHStWr[128]; uniform vec4 uHStWr2[128];
uniform int uHMode; uniform int uHState;
// one of the listed states: the gear / flaps / steering / cabin, the controls, and the XR-40's pods, vanes, fan, bay,
// turrets and bomb (its surfaces follow the controls)
void hullState(int s){
  gPS = uHStPS[s]; gCtl = uHStCtl[s];
  vec4 w = uHStWr[s], w2 = uHStWr2[s];
  gWr[0] = vec4(w.x); gWr[1] = vec4(w.y); gWr[2] = vec4(w.z); gWr[3] = vec4(w.w);
  gWr[4] = vec4(w2.x, w2.y, w2.z, 0.0); gWr[5] = vec4(gCtl.x, gCtl.z, gCtl.y, 0.0); gWr[6] = vec4(w2.w, 0.0, 0.0, 0.0);
  if (int(gM[0].z + 0.5) == 8) { gWr[0] = w; gWr[1] = w2; }   // FX-27 Gatling: doors, carrier and rotor
  if (gPS.w > 0.5) loadCabinFit();
}
void main(){
  vec3 p = texelFetch(uHPts, ivec2(gl_FragCoord.xy), 0).xyz;
  loadMain(); pieceXf(-1);
  vec4 o = vec4(0.0);
  if (uHMode == 0 || uHMode == 4) {   // (4: the greatest distance over the states)
    float d = uHMode == 0 ? 1e9 : -1e9;
    for (int s = 0; s < 128; s++) {
      if (s >= uHStN) break;
      hullState(s);
      float ds = mapPlane(p).x;
      d = uHMode == 0 ? min(d, ds) : max(d, ds);
    }
    o = vec4(d, 0.0, 0.0, 0.0);
  } else {
    hullState(uHState);
    if (uHMode == 1) o = vec4(mapPlane(p).x, 0.0, 0.0, 0.0);
    else if (uHMode == 2) { vec2 r = mapPlane(p); vec3 n = planeNormal(p); o = vec4(r.x, r.y, gPS.w > 0.5 ? interiorAO(p, n) : 1.0, 0.0); }
    else o = vec4(planeNormal(p), 0.0);
  }
  oColor = o; oDepth = o.x; oCloudMask = 0.0;
}
