//! kAfShMap
//! The player's airframe shadow maps (raster_renderer.cpp rasterShadowMaps): its baked static mesh seen from the sun
//! (layer 0) and from the three brightest shadow-casting lights (1-3), with the moving parts' hull as a mask. Read by
//! the shadow proxy (the airframe's shadow on the ground), and by the airframe's own lighting for its self-shadow and
//! the sun through the cabin windows (plane_light.glsl), in place of a march of the field per pixel. uAfShOn: which
//! layers hold a map this frame.
uniform sampler2DArray uAfShMap;   // depth from the light
uniform sampler2DArray uAfShMov;   // the moving parts' hull from the light (1: march the field here)
uniform mat4 uAfShVP[4]; uniform int uAfShOn;
// the cockpit view's cabin sun map (Renderer::rasterShadowMaps): 5 m about the eye, 2.4 mm a texel; -1 outside it
uniform sampler2DShadow uCabShMap; uniform mat4 uCabShVP, uCabShVPc; uniform int uCabShOn; uniform float uCabShBias;   // (compared and filtered by the hardware: each tap a bilinear blend of four texels' tests, so a shadow's edge is a gradient, not 2.4 mm steps)
float cabShLookup(vec3 p, vec3 n, float biasK){
  // (from the hit relative to the camera when the pass has it exactly - uCabShVPc: from there - for at the map's edges the
  // world point's float spacing alone is 1.6 of the map's texels: plane_common.glsl gRelSet)
  vec3 u = (gRelSet ? uCabShVPc*vec4(gRel + n*0.008, 1.0) : uCabShVP*vec4(p + n*0.008, 1.0)).xyz*0.5 + 0.5;
  if (u.x < 0.002 || u.x > 0.998 || u.y < 0.002 || u.y > 0.998 || u.z >= 1.0) return -1.0;
  vec2 ts = 1.0/vec2(textureSize(uCabShMap, 0));
  float s = 0.0, bias = uCabShBias*biasK;
  for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) s += texture(uCabShMap, vec3(u.xy + vec2(float(dx), float(dy))*ts, u.z - bias));
  return s/9.0;
}
// 1 lit, 0 shadowed (soft between), or -1: the field decides (a moving part may be here)
float shMapLookupB(int layer, vec3 p, vec3 n, float biasK){
  if (layer == 0 && uCabShOn == 1) { float c = cabShLookup(p, n, biasK); if (c >= 0.0) return c; }
  float off = layer == 0 ? 0.06 + 0.12*(1.0 - abs(dot(n, uSunDir))) : 0.06;   // (the sun's offset grows as it grazes the surface)
  vec4 q = uAfShVP[layer]*vec4(p + n*off, 1.0);
  if (q.w <= 0.0) return 1.0;
  vec3 u = q.xyz/q.w*0.5 + 0.5;
  if (u.x < 0.0 || u.x > 1.0 || u.y < 0.0 || u.y > 1.0) return 1.0;   // outside the map: nothing of the airframe between
  if (texture(uAfShMov, vec3(u.xy, float(layer))).r > 0.5) return -1.0;
  float z = min(u.z, 1.0), bias = (layer == 0 ? 0.0012 : 0.0006)*biasK;
  vec2 ts = 1.0/vec2(textureSize(uAfShMap, 0).xy);
  float s = 0.0;
  for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
    float d = texture(uAfShMap, vec3(u.xy + vec2(float(dx), float(dy))*ts, float(layer))).r;
    if (d >= z - bias) s += 1.0;
  }
  return s/9.0;
}
float shMapLookup(int layer, vec3 p, vec3 n){ return shMapLookupB(layer, p, n, 1.0); }
