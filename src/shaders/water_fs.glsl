//! kWaterFS
//! The sea surface, shaded here (waves, Fresnel sky and cloud reflection, depth colour,
//! foam, sun glint) and written prelit: the lighting pass adds only the aerial perspective. On the round world it is
//! shaded in its own level (kPlanet): the view and the sun as that stretch of sea sees them.
in vec3 vW; in vec3 vC;
float lightShadow(int i, vec3 p, vec3 n, vec3 l, float d){ return 1.0; }   // (shadeSurface's hook; the sea never calls it)
void main(){
  // (t: the distance to where it is drawn, the lighting pass's; rd: the view as the sea there sees it, in its own level)
  vec3 p = vW; vec3 rd = vC - uCamPos; float t = length(rd); rd = planetTurn(rd/max(t, 1e-4), vW, uCamPos, -1.0);
  gSunDir = planetTurn(sunDirUniform(), vW, uCamPos, -1.0); gSunCol = sunLightAt(gSunDir.y, 0.0, uPlanetR)*uSunDim;   // (and its sun: LOCAL_SUN)
  vec2 pixelDx = dFdx(vW.xz), pixelDy = dFdy(vW.xz);
  vec3 col = waterShade(p, rd, t, pixelDx, pixelDy);
  if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
  gbWritePrelit(t, vec3(0.0, 1.0, 0.0), GB_WATER, col);
}
