//! kShadowProxyFS
//! The raster renderer's shadow proxy: what the airframes' distance fields shadow on the surfaces already in the
//! G-buffer - the player's aircraft and the traffic in the sun on the ground and the scenery (R), and the airframe in
//! the beams of the three brightest shadow-casting point lights, the landing lights (G, B, A) - marched exactly as the ray
//! tracer did, so the lighting pass can stay free of the fields. With the player's airframe baked to a mesh its shadow
//! comes from shadow maps instead (rasterShadowMaps: the sun in layer 0, the three lights in 1-3); the field is marched
//! only where the moving parts' hull lies in the map, and for the traffic.
in vec2 vUV; out vec4 oColor;
void main(){
  gZero = min(uQuality, 0);
  loadMain();
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec4 g0 = texelFetch(uGB0, px, 0);
  int cls = int(g0.w + 0.5);
  if (cls == GB_SKY || cls == GB_WATER || cls == GB_DISPLAY || cls == GB_POD || cls == GB_CABIN) { oColor = vec4(1.0); return; }
  vec2 ndc = (vUV + uJit)*2.0 - 1.0;
  vec3 rd = camRay(ndc), ro = uCamPos;
  float t = g0.x;
  vec3 p = ro + rd*t, n = octDec(g0.yz);
  float sunS = 1.0;
  bool ground = cls == GB_TERRAIN || cls == GB_ENTITY || cls == GB_FOLIAGE;
  if (ground && uSunDir.y > -0.05 && t < 3000.0) {
    float m = (uAfShOn & 1) != 0 ? shMapLookup(0, p, n) : -1.0;
    sunS = m >= 0.0 ? m : planeShadow(p + n*0.2, uSunDir);
    if (uTrafficN > 0) sunS *= trafficShadow(p);
  }
  vec3 ls = vec3(1.0);
  for (int i = 0; i < 12; i++) {
    if (i >= uPLN) break;
    int slot = gbShadowSlot(i);
    if (slot < 0) continue;
    vec3 L = uPLP[i].xyz - p; float d = length(L);
    if (d < uPLP[i].w*40.0 + 400.0) {   // (beyond the beam's reach nothing is lit to shadow)
      float m = (uAfShOn & (2 << slot)) != 0 ? shMapLookup(1 + slot, p, n) : -1.0;
      ls[slot] = m >= 0.0 ? m : lightShadow(i, p, n, L/d, d);
    }
  }
  oColor = vec4(sunS, ls);
}
