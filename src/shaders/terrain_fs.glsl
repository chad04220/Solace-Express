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
  vec2 pixelDx = dFdx(vW.xz), pixelDy = dFdy(vW.xz);   // before the divergent LOD discard
#ifdef FAR_REGION_DITHER
  // (a program of its own, for while an Earth region's far blend is on: without a discard the card tests the depth
  // before it shades - the sea floor is cut away by kTerrainVS's clip plane - Renderer::drawTerrainMesh)
  if (uFarRegionBlend >= 1.0 || regionLodDither() < uFarRegionBlend) discard;
#endif
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
