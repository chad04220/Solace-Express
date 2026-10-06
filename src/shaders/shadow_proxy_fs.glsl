//! kShadowProxyFS
//! The raster renderer's shadow proxy: what the airframes' distance fields shadow on the surfaces already in the
//! G-buffer - the player's aircraft and the traffic in the sun on the ground and the scenery (R), and the airframe in
//! the beams of the three brightest shadow-casting point lights, the landing lights (G, B, A) - marched exactly as the ray
//! tracer did, so the lighting pass can stay free of the fields. With the player's airframe baked to a mesh its shadow
//! comes from shadow maps instead (rasterShadowMaps: the sun in layer 0, the three lights in 1-3); the field is marched
//! only where the moving parts' hull lies in the map, and for the traffic.
in vec2 vUV; out vec4 oColor;
// the traffic's sun shadows from their maps (uAfShMap layers 4 + k, where uTrafShOn has bit k): one projection and four
// taps per aircraft, instead of a march through its field
uniform mat4 uTrafShVP[12];
float trafficShadowMaps(vec3 p, vec3 n){
  float s = 1.0;
  vec2 ts = 1.5/vec2(textureSize(uAfShMap, 0).xy);
  for (int k = 0; k < 12; k++) {
    if (k >= uTrafficN) break;
    if ((uTrafShOn & (1 << k)) == 0) continue;
    vec4 q = uTrafShVP[k]*vec4(p + n*0.1, 1.0);
    vec3 u = q.xyz/q.w*0.5 + 0.5;
    if (u.x <= 0.0 || u.x >= 1.0 || u.y <= 0.0 || u.y >= 1.0) continue;   // (outside its map: nothing of it between)
    // (a receiver beyond the map's far plane - the ground under an aircraft in the air - is behind everything the map
    // holds: its depth is clamped just short of the cleared 1.0, so any occluder shadows it and an empty texel doesn't)
    float z = min(u.z, 0.999), lit = 0.0;
    for (int j = 0; j < 4; j++) lit += texture(uAfShMap, vec3(u.xy + (vec2(float(j & 1), float(j >> 1)) - 0.5)*ts, float(4 + k))).r >= z - 0.002 ? 1.0 : 0.0;
    s *= lit*0.25;
  }
  return s;
}
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
    sunS = m >= 0.0 ? mix(m, 1.0, uWr[4].w*0.88) : planeShadow(p + n*0.2, uSunDir);   // (a cloaked XR-40 barely darkens the ground: planeShadow's own fade)
    if (uTrafficN > 0) sunS *= trafficShadow(p)*trafficShadowMaps(p, n);
  }
  vec3 ls = vec3(1.0);
  for (int i = 0; i < 12; i++) {
    if (i >= uPLN) break;
    int slot = gbShadowSlot(i);
    if (slot < 0) continue;
    vec3 L = uPLP[i].xyz - p; float d2 = dot(L, L), d = sqrt(d2); vec3 l = L/max(d, 1e-4);
    // only where the lighting pass reads the shadow (shadeSurface: the same terms and the same thresholds): the
    // surface facing the light, inside a spotlight's cone, and lit above its minimum - the rest of the pixels in the
    // beam's reach were marched or looked up for nothing
    float ndl = dot(n, l);
    if (ndl <= 0.0) continue;
    vec3 E = uPLC[i].rgb/(d2 + uPLP[i].w*uPLP[i].w);
    if (uPLC[i].w > -1.5) E *= smoothstep(uPLC[i].w, mix(uPLC[i].w, 1.0, 0.3), dot(-l, uPLD[i].xyz));
    if (max(E.r, max(E.g, E.b))*ndl <= 0.004) continue;
    {
      float m = (uAfShOn & (2 << slot)) != 0 ? shMapLookup(1 + slot, p, n) : -1.0;
      ls[slot] = m >= 0.0 ? m : planeLightShadow(i, p, n, l, d);
    }
  }
  oColor = vec4(sunS, ls);
}
