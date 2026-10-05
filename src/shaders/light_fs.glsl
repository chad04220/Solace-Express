//! kLightFS
//! The lighting pass: one full-screen program reads the G-buffer and shades every pixel the way the ray tracer did -
//! the sky where nothing was drawn; otherwise the sun through the terrain-shadow bake, the scenery shadow cascades and
//! the cloud shadow (an airframe: its own shadow and the terrain's from the objects pass), ambient, moonlight, the sky
//! reflection, the point lights, emission and the aerial perspective. A prelit pixel (the sea, a display, the cockpit)
//! only gets the aerial perspective. Writes what the ray tracer wrote: colour + TAA class, view distance, and the
//! cloud mask for the quarter-resolution cloud pass.
uniform sampler2D uGB3;   // (uGB0..2: light_common)
uniform sampler2D uShProxy;   // the airframes' shadows on this pixel (kShadowProxyFS): the sun's, the three brightest shadow-casting lights'
vec4 gShProxy = vec4(1.0);
// the aircraft's shadow in a point light's beam, from the proxy (gbShadowSlot: the three brightest shadow-casting lights)
float lightShadow(int i, vec3 p, vec3 n, vec3 l, float d){
  int slot = gbShadowSlot(i);
  return slot < 0 ? 1.0 : gShProxy[1 + slot];
}
void main(){
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec4 g0 = texelFetch(uGB0, px, 0);
  int cls = int(g0.w + 0.5);
  vec2 ndc = (vUV + uJit)*2.0 - 1.0;
  vec3 rd = camRay(ndc), ro = uCamPos;
  if (cls == GB_SKY) { oColor = vec4(skyColor(rd), 1.0); oDepth = 1e6; oCloudMask = 1.0; return; }
  float t = g0.x;
  vec3 p = ro + rd*t, n = octDec(g0.yz);
  vec4 g1 = texelFetch(uGB1, px, 0), g2 = texelFetch(uGB2, px, 0), g3 = texelFetch(uGB3, px, 0);
  int flags = int(g3.w*255.0 + 0.5);
  gShProxy = texelFetch(uShProxy, px, 0);
  Mat m; m.alb = g1.rgb*g1.rgb; m.rough = g1.a; m.metal = g2.a; m.emit = g2.rgb; m.nrm = vec3(0.0, 0.0, 1.0);
  float sunVis = smoothstep(-0.05, 0.05, uSunDir.y);
  bool prelit = cls == GB_WATER || cls == GB_DISPLAY || cls == GB_POD || cls == GB_CABIN;
  vec3 col;
  if (prelit) col = m.emit;
  else {
    // the sun: the terrain's shadow (from the bake; an airframe's from the objects pass, with its own), the scenery's
    // from the cascades, the clouds'
    float sh = 0.0;
    if (sunVis > 0.0) {
      if (cls == GB_UFO) sh = cloudShadow(p);
      else if (cls == GB_DEBRIS) sh = g3.z;
      else if (cls == GB_PLANE || cls == GB_TRAFFIC || cls == GB_WRECK) { sh = g3.z*g3.y; if (sh > 0.0) sh *= cloudShadow(p)*entShadow(p, n); }
      else { sh = terrainShadow(p + n*0.5 + (cls == GB_ENTITY || cls == GB_FOLIAGE ? vec3(0.0, 0.5, 0.0) : vec3(0.0)), uSunDir, t); if (sh > 0.0) sh *= entShadow(p, n)*cloudShadow(p)*gShProxy.r; }
    }
    col = shadeSurface(p, n, rd, m, sh);
    if (cls == GB_FOLIAGE) {   // light through the leaves when the sun is behind them, and a soft wrap
      float back = pow(max(dot(rd, uSunDir), 0.0), 3.0)*0.9 + 0.12*max(dot(-n, uSunDir), 0.0);
      col += m.alb*vec3(0.85, 1.0, 0.55)*uSunCol*sh*back*1.6;
    }
    if ((flags & GBF_GLINT) != 0) { vec3 h = normalize(-rd + uSunDir); col += uSunCol*pow(max(dot(n, h), 0.0), 400.0)*sh*3.0; }   // painted skin
  }
  bool sealed = cls == GB_POD || cls == GB_DISPLAY;   // the cockpit: nothing outside the aircraft reaches it
  if (!sealed) col = applyFog(col, ro, rd, t);
  if (any(isnan(col)) || any(isinf(col)) || !(col.r + col.g + col.b < 1e7)) col = vec3(0.0);
  // the TAA's history class: 1 world, 0.5 rigid with the aircraft (0.55 a display), 0.2 moving, 0 none
  float taa = (flags & GBF_MOVING) != 0 ? 0.2 : (flags & GBF_RIGID) != 0 ? ((flags & GBF_DISPLAY) != 0 ? 0.55 : 0.5) : cls == GB_DISPLAY ? 0.0 : 1.0;
  oColor = vec4(clamp(col, vec3(0.0), vec3(3e4)), taa);
  oDepth = t;
  oCloudMask = sealed ? 0.0 : 1.0;
}
