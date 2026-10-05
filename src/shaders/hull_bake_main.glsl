//! kHullBakeMain
//! Aircraft hull and mesh bakes (aircraft_hull.cpp, aircraft_mesh.cpp): the airframe's field at each listed point
//! (aircraft space). uHMode 0: the least distance over every listed state of the gear, flaps, steering and controls
//! (a hull built from it holds the airframe in any of them); 4: the greatest; 1: the distance in state uHState; 2: in
//! that state the distance, the material id and the cabin's ambient occlusion; 3: in that state the surface normal.

uniform sampler2D uHPts; uniform int uHStN; uniform vec4 uHStPS[128]; uniform vec4 uHStCtl[128];
uniform int uHMode; uniform int uHState;
void main(){
  vec3 p = texelFetch(uHPts, ivec2(gl_FragCoord.xy), 0).xyz;
  loadMain(); pieceXf(-1);
  vec4 o = vec4(0.0);
  if (uHMode == 0 || uHMode == 4) {   // (4: the greatest distance over the states)
    float d = uHMode == 0 ? 1e9 : -1e9;
    for (int s = 0; s < 128; s++) {
      if (s >= uHStN) break;
      gPS = uHStPS[s]; gCtl = uHStCtl[s];
      if (gPS.w > 0.5) loadCabinFit();
      float ds = mapPlane(p).x;
      d = uHMode == 0 ? min(d, ds) : max(d, ds);
    }
    o = vec4(d, 0.0, 0.0, 0.0);
  } else {
    gPS = uHStPS[uHState]; gCtl = uHStCtl[uHState];
    if (gPS.w > 0.5) loadCabinFit();
    if (uHMode == 1) o = vec4(mapPlane(p).x, 0.0, 0.0, 0.0);
    else if (uHMode == 2) { vec2 r = mapPlane(p); vec3 n = planeNormal(p); o = vec4(r.x, r.y, gPS.w > 0.5 ? interiorAO(p, n) : 1.0, 0.0); }
    else o = vec4(planeNormal(p), 0.0);
  }
  oColor = o; oDepth = o.x; oCloudMask = 0.0;
}
