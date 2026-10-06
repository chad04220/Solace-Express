//! kAfShMap
//! The player's airframe shadow maps (raster_renderer.cpp rasterShadowMaps): its baked static mesh seen from the sun
//! (layer 0) and from the three brightest shadow-casting lights (1-3), with the moving parts' hull as a mask. Read by
//! the shadow proxy (the airframe's shadow on the ground), and by the airframe's own lighting for its self-shadow and
//! the sun through the cabin windows (plane_light.glsl), in place of a march of the field per pixel. uAfShOn: which
//! layers hold a map this frame.
uniform sampler2DArray uAfShMap;   // depth from the light
uniform sampler2DArray uAfShMov;   // the moving parts' hull from the light (1: march the field here)
uniform mat4 uAfShVP[4]; uniform int uAfShOn;
// 1 lit, 0 shadowed (soft between), or -1: the field decides (a moving part may be here)
float gShMapOcc = 1e9;   // after a lookup: how far (in the map's depth units, 0..1) the nearest occluder lies in front of the receiver
float shMapLookupB(int layer, vec3 p, vec3 n, float biasK){
  gShMapOcc = 1e9;
  vec4 q = uAfShVP[layer]*vec4(p + n*0.06, 1.0);
  if (q.w <= 0.0) return 1.0;
  vec3 u = q.xyz/q.w*0.5 + 0.5;
  if (u.x < 0.0 || u.x > 1.0 || u.y < 0.0 || u.y > 1.0) return 1.0;   // outside the map: nothing of the airframe between
  if (texture(uAfShMov, vec3(u.xy, float(layer))).r > 0.5) return -1.0;
  float z = min(u.z, 1.0), bias = (layer == 0 ? 0.0012 : 0.0006)*biasK;
  vec2 ts = 1.0/vec2(textureSize(uAfShMap, 0).xy);
  float s = 0.0;
  for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
    float d = texture(uAfShMap, vec3(u.xy + vec2(float(dx), float(dy))*ts, float(layer))).r;
    if (d >= z - bias) s += 1.0; else gShMapOcc = min(gShMapOcc, z - d);
  }
  return s/9.0;
}
float shMapLookup(int layer, vec3 p, vec3 n){ return shMapLookupB(layer, p, n, 1.0); }
