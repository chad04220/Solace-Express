//! kTerrainFS
//! The ground's material into the G-buffer (terrainMaterial, as the ray tracer shaded it), lit later by the lighting pass.
in vec3 vW; in vec3 vN;
void main(){
  vec3 p = vW; vec3 rd = p - uCamPos; float t = length(rd);
  vec3 n = normalize(vN);
  Mat m = terrainMaterial(p, n, t, baseAt(p.xz));
  vec3 ns = applyTS(n, m.nrm, t < 2000.0 ? 0.6 : 0.25);
  gbWrite(t, ns, GB_TERRAIN, m, 1.0);
}
