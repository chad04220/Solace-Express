//! kTerrainFS
//! The ground's material into the G-buffer (terrainMaterial), lit later by the lighting pass.
in vec3 vW; in vec3 vN; in vec3 vC;
uniform float uFarRegionBlend;
// Kept identical in both terrain fragment passes: the two coverages partition every screen sample.
float regionLodDither() {
  ivec2 q = ivec2(gl_FragCoord.xy) & ivec2(7);
  int b0 = ((q.x & 1) ^ (q.y & 1))*2 + (q.y & 1);
  int b1 = (((q.x >> 1) & 1) ^ ((q.y >> 1) & 1))*2 + ((q.y >> 1) & 1);
  int b2 = (((q.x >> 2) & 1) ^ ((q.y >> 2) & 1))*2 + ((q.y >> 2) & 1);
  return (float(b0*16 + b1*4 + b2) + 0.5)/64.0;
}

void main(){
  vec2 pixelDx = dFdx(vW.xz), pixelDy = dFdy(vW.xz);   // before divergent LOD and sea-floor discards
  if (uFarRegionBlend >= 1.0 || regionLodDither() < uFarRegionBlend) discard;
  // the sea floor, from above the sea: the sea is drawn over it. Left in, a shallow bank a few decimetres down fought
  // the sea's surface for the depth buffer - each pass's logarithmic depth is exact only at the vertices, off by more
  // than that across the far chunks' large triangles - and a few kilometres off it broke through in a lattice of pale
  // sandy dashes laid out like the chunks' grid
  if (vW.y < 0.0 && uCamPos.y > 0.0) discard;
  vec3 p = vW; float t = length(vC - uCamPos);   // (t: the distance to where it is drawn, the lighting pass's)
  vec2 pw = wrapW(p.xz);
  RoadHit road = noRoad();
  if ((uDbg & 131072) == 0) road = roadQuery(pw);   // (dbgOff 131072, renderer.h kDbgRoads: the roads left out, profiling)
  vec3 n = roadNormal(road, normalize(vN));
  vec3 pl = vec3(pw.x, p.y, pw.y);   // (the ground's own place: across the map's seam, the islands on the far side; y the height)
  Mat m = terrainMaterial(pl, n, t, baseAt(pl.xz), pixelDx, pixelDy, road);
  vec3 ns = applyTS(n, m.nrm, terrainBumpStrength(t));
  gbWrite(t, ns, GB_TERRAIN, m, 1.0);
}
