//! kWaterFS
//! The sea surface, shaded here (waves, Fresnel sky and cloud reflection, depth colour,
//! foam, sun glint) and written prelit: the lighting pass adds only the aerial perspective.
in vec3 vW;
float lightShadow(int i, vec3 p, vec3 n, vec3 l, float d){ return 1.0; }   // (shadeSurface's hook; the sea never calls it)
void main(){
  vec3 p = vW; vec3 rd = p - uCamPos; float t = length(rd); rd /= max(t, 1e-4);
  vec3 col = waterShade(p, rd, t);
  if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
  gbWritePrelit(t, vec3(0.0, 1.0, 0.0), GB_WATER, col);
}
