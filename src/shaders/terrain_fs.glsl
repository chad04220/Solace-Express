//! kTerrainFS
//! The ground's material into the G-buffer (terrainMaterial), lit later by the lighting pass.
in vec3 vW; in vec3 vN;
void main(){
  vec2 pixelDx = dFdx(vW.xz), pixelDy = dFdy(vW.xz);   // before the divergent sea-floor discard
  // the sea floor, from above the sea: the sea is drawn over it. Left in, a shallow bank a few decimetres down fought
  // the sea's surface for the depth buffer - each pass's logarithmic depth is exact only at the vertices, off by more
  // than that across the far chunks' large triangles - and a few kilometres off it broke through in a lattice of pale
  // sandy dashes laid out like the chunks' grid
  if (vW.y < 0.0 && uCamPos.y > 0.0) discard;
  vec3 p = vW; vec3 rd = p - uCamPos; float t = length(rd);
  vec3 n = normalize(vN);
  Mat m = terrainMaterial(p, n, t, baseAt(p.xz), pixelDx, pixelDy);
  vec3 ns = applyTS(n, m.nrm, terrainBumpStrength(t));
  gbWrite(t, ns, GB_TERRAIN, m, 1.0);
}
