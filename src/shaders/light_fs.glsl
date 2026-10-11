//! kLightFS
//! The lighting pass: one full-screen program reads the G-buffer and shades every pixel -
//! the sky where nothing was drawn; otherwise the sun through the terrain-shadow bake, the scenery shadow cascades and
//! the cloud shadow (an airframe: its own shadow and the terrain's from the objects pass), ambient, moonlight, the sky
//! reflection, the point lights, emission and the aerial perspective. A prelit pixel (the sea, a display, the cockpit)
//! only gets the aerial perspective. Writes colour + TAA class, view distance, and the
//! cloud mask for the quarter-resolution cloud pass.
uniform sampler2D uGB3;   // (uGB0..2: light_common)
uniform sampler2D uShProxy;   // the airframes' shadows on this pixel (kShadowProxyFS): the sun's, the three brightest shadow-casting lights'
vec4 gShProxy = vec4(1.0);
vec3 gEnvironmentReceiverNormal;
bool gEnvironmentReceiverEntity;
// the aircraft's shadow in a point light's beam, from the proxy (gbShadowSlot: the three brightest shadow-casting lights)
float lightShadow(int i, vec3 p, vec3 n, vec3 l, float d){
  int slot = gbShadowSlot(i);
  return slot < 0 ? 1.0 : gShProxy[1 + slot];
}
// Diffuse environment light integrates a hemisphere, rather than using a saturated
// zenith ray times 2.2 for every surface. Two broad quadrature directions approximate
// its low-frequency irradiance; walls see more horizon, horizontal roofs more zenith.
// Kept entirely in this deferred environment path: aircraft/cockpit helpers are untouched.
vec3 environmentAmbientLight(vec3 n){
  vec3 zenith = skyColor(normalize(vec3(0.3, 1.0, 0.2)));
  vec3 horizon = skyColor(normalize(vec3(0.3, 0.12, 0.2)));
  float up = clamp(n.y, 0.0, 1.0);
  vec3 sky = mix(horizon, zenith, 0.25 + 0.4*up)*1.5;
  vec3 ground = vec3(0.12, 0.11, 0.08)*(uSunCol.g + 0.02);
  return mix(ground, sky, n.y*0.5 + 0.5) + vec3(0.03, 0.04, 0.07)*uNight*0.4;
}
vec2 environmentShadowOffsets(float texel, float ndl){
  float grazing = 1.0 - clamp(ndl, 0.0, 1.0);
  // World metres: bounded to centimetres, not the old metre-scale near offset.
  return vec2(clamp(texel*(0.08 + 0.24*grazing), 0.02, 0.20),
              clamp(texel*(0.025 + 0.10*grazing), 0.015, 0.10));
}
// Receiver-plane depth change for one shadow texel. The matrix rows include the
// orthographic scales, so their squared lengths undo that scale without an inverse.
vec2 environmentShadowPlaneGradient(vec3 n, vec3 rowX, vec3 rowY, vec3 rowZ, vec2 pixel){
  float z = dot(n,rowZ)/max(dot(rowZ,rowZ),1e-12);
  float inverseZ = -sign(z)/max(abs(z),1e-4);
  vec2 slope = vec2(dot(n,rowX)/max(dot(rowX,rowX),1e-12),
                    dot(n,rowY)/max(dot(rowY,rowY),1e-12))*inverseZ*pixel;
  return vec2(clamp(slope.x,-4.0/6000.0,4.0/6000.0),
              clamp(slope.y,-4.0/6000.0,4.0/6000.0));
}
float environmentShadowTapDepth(float depth, vec2 gradient, vec2 fraction, vec2 corner){
  return depth + dot(gradient,corner-fraction);
}
float environmentWallShadowFilter(vec3 normal, vec3 sun, vec2 gradient, float contact){
  float span = (abs(gradient.x) + abs(gradient.y))*6000.0;
  return contact > 0.5 && abs(normal.y) < 0.65 && dot(normal,sun) > 0.03 && span > 0.08 ? 1.0 : 0.0;
}
float environmentShadowCascade(int c, vec3 p, vec3 n, float contact){
  float texel = c == 0 ? uShTexel.x : uShTexel.y;
  vec2 offsets = environmentShadowOffsets(texel, dot(n, uSunDir));
  float normalOffset = mix(texel*1.5, offsets.x, contact);
  mat4 lightMatrix = c == 0 ? uShM0 : uShM1;
  vec2 pixel = 1.0/vec2(c == 0 ? textureSize(uShMap0, 0) : textureSize(uShMap1, 0));
  vec3 rowX = vec3(lightMatrix[0].x,lightMatrix[1].x,lightMatrix[2].x);
  vec3 rowY = vec3(lightMatrix[0].y,lightMatrix[1].y,lightMatrix[2].y);
  vec3 rowZ = vec3(lightMatrix[0].z,lightMatrix[1].z,lightMatrix[2].z);
  vec2 gradient = environmentShadowPlaneGradient(gEnvironmentReceiverNormal,rowX,rowY,rowZ,pixel);
  // entity_render.cpp projects each cascade across 2*D = 6000 m of light-space depth.
  float depthBias = mix(c == 0 ? 0.0004 : 0.0006, offsets.y/6000.0, contact);
  gradient *= contact;
  vec4 h = lightMatrix*vec4(p + n*normalOffset, 1.0);
  vec3 q = h.xyz*0.5 + 0.5;
  if (max(abs(q.x - 0.5), abs(q.y - 0.5)) > 0.48 || q.z > 1.0) return 1.0;
  bool wallPlane = gEnvironmentReceiverEntity
    && environmentWallShadowFilter(gEnvironmentReceiverNormal,uSunDir,gradient,contact) > 0.5;
  // Sloped close entity walls can be wrongly classified as either fully lit or
  // fully shadowed by the initial hardware comparison. Do not gate their correction
  // on that result: four exact reads, skipping the otherwise redundant first read.
  float center = 1.0;
  if (!wallPlane) center = c == 0 ? shTap(uShMap0, q, depthBias) : shTap(uShMap1, q, depthBias);
  // Hardware 2x2 PCF compares one depth against four differently positioned samples.
  // On a sloped wall that creates repeated teeth. Only at a mixed close edge, redo
  // those four comparisons at exact texel centres with each receiver-plane depth.
  // One lookup in other interiors, four on close sloped walls, five on other
  // mixed edges. No wider blur, new map or loop.
  if (wallPlane || (contact > 0.5 && center > 0.01 && center < 0.99)) {
    vec2 cell = q.xy/pixel - 0.5, f = fract(cell);
    vec2 base = (floor(cell) + 0.5)*pixel;
    vec3 a = vec3(base,environmentShadowTapDepth(q.z,gradient,f,vec2(0.0)));
    vec3 b = vec3(base + pixel*vec2(1.0,0.0),environmentShadowTapDepth(q.z,gradient,f,vec2(1.0,0.0)));
    vec3 d = vec3(base + pixel*vec2(0.0,1.0),environmentShadowTapDepth(q.z,gradient,f,vec2(0.0,1.0)));
    vec3 e = vec3(base + pixel,environmentShadowTapDepth(q.z,gradient,f,vec2(1.0)));
    vec4 samples = c == 0 ? vec4(shTap(uShMap0,a,depthBias),shTap(uShMap0,b,depthBias),shTap(uShMap0,d,depthBias),shTap(uShMap0,e,depthBias))
                          : vec4(shTap(uShMap1,a,depthBias),shTap(uShMap1,b,depthBias),shTap(uShMap1,d,depthBias),shTap(uShMap1,e,depthBias));
    center = mix(mix(samples.x,samples.y,f.x),mix(samples.z,samples.w,f.x),f.y);
  }
  return center;
}
float environmentShadow(vec3 p, vec3 n, float distance){
  if (distance >= 180.0) return entShadow(p, n);
  if (uShOn == 0 || (uDbg & 4) != 0) return 1.0;
  float contact = 1.0 - smoothstep(90.0, 180.0, distance);
  float w0 = 1.0 - smoothstep(uShFadeR.x, uShFadeR.y, length(p.xz - uShFade.xy));
  float w1 = uShOn > 1 ? 1.0 - smoothstep(uShFadeR.z, uShFadeR.w, length(p.xz - uShFade.zw)) : 0.0;
  float s = 1.0;
  if (w0 < 1.0 && w1 > 0.0) s = mix(1.0, environmentShadowCascade(1, p, n, contact), w1);
  if (w0 > 0.0) s = mix(s, environmentShadowCascade(0, p, n, contact), w0);
  return s;
}
vec3 environmentReflection(vec3 direction);
vec3 shadeEnvironmentSurface(vec3 p, vec3 n, vec3 rd, Mat m, float shadow){
  vec3 v = -rd;
  vec3 col = pbr(n, v, uSunDir, m.alb, m.rough, m.metal, uSunCol*shadow*3.2);
  col += m.alb*environmentAmbientLight(n)*(0.55 + 0.45*n.y)*(1.0 - m.metal);
  // moonlight
  vec3 md = normalize(vec3(-0.4, 0.55, 0.6));
  col += pbr(n, v, md, m.alb, m.rough, m.metal, vec3(0.05,0.07,0.12)*uNight);
  // specular environment reflection
  vec3 r = reflect(rd, n);
  vec3 F = fresnelSchlick(max(dot(n, v), 0.0), mix(vec3(0.04), m.alb, m.metal));
  col += environmentReflection(r)*F*(1.0-m.rough)*(1.0-m.rough)*0.8;
  // Same point-light/shadow response as the shared surface path; only diffuse sky irradiance differs
  for (int i = 0; i < 12; i++) {
    if (i >= uPLN || (uDbg & 16) != 0) break;
    COST(3);
    vec3 lv = uPLP[i].xyz - p; float d2 = dot(lv, lv), d = sqrt(d2); vec3 l = lv/max(d, 1e-4);
    float ndl = dot(n, l);
    if (ndl <= 0.0) continue;
    vec3 E = uPLC[i].rgb/(d2 + uPLP[i].w*uPLP[i].w);
    if (uPLC[i].w > -1.5) E *= smoothstep(uPLC[i].w, mix(uPLC[i].w, 1.0, 0.3), dot(-l, uPLD[i].xyz));
    float lum = max(E.r, max(E.g, E.b))*ndl;
    if (lum < 0.0015) continue;
    if (uPLD[i].w > 0.0 && lum > 0.004) E *= lightShadow(i, p, n, l, d);
    col += pbr(n, v, l, m.alb, m.rough, m.metal, E);
  }
  col += m.emit;
  col += m.alb*vec3(0.7,0.75,1.0)*uLightning*0.4;
  return col;
}

// A cheap distant environment for fitted glass/clearcoat. Downward reflection sees
// ground irradiance instead of mirroring the blue sky through the horizon.
vec3 environmentReflection(vec3 direction){
  vec3 sky = skyColor(normalize(vec3(direction.x, max(direction.y, 0.025), direction.z)));
  vec3 ground = vec3(0.18, 0.175, 0.155)*(uSunCol*max(uSunDir.y, 0.0)*0.9 + sky*0.65 + vec3(0.008));
  return mix(ground, sky, smoothstep(-0.12, 0.16, direction.y));
}
vec3 shadeEnvironmentGlazing(vec3 p, vec3 n, vec3 rd, Mat m, float shadow){
  // The unresolved front/back dielectric interfaces of a thin pane reflect about
  // 7.5% head-on together, versus 4% at one air/glass interface.
  vec3 v = -rd, F = fresnelSchlick(max(dot(n, v), 0.0), vec3(0.075));
  // m.alb is the recessed room/cabin, not a diffuse coating on the glass surface.
  vec3 col = m.alb*environmentAmbientLight(vec3(0.0, 1.0, 0.0))*0.65*(1.0 - F);
  col += environmentReflection(reflect(rd, n))*F*(1.0 - m.rough)*(1.0 - m.rough);
  col += pbr(n, v, uSunDir, vec3(0.0), m.rough, 0.0, uSunCol*shadow*3.2);
  col += pbr(n, v, normalize(vec3(-0.4, 0.55, 0.6)), vec3(0.0), m.rough, 0.0, vec3(0.05, 0.07, 0.12)*uNight);
  // Same bounded light list as opaque surfaces; no additional loop per glass pixel.
  for (int i = 0; i < 12; i++) {
    if (i >= uPLN || (uDbg & 16) != 0) break;
    COST(3);
    vec3 lv = uPLP[i].xyz - p; float d2 = dot(lv, lv), d = sqrt(d2); vec3 l = lv/max(d, 1e-4);
    float ndl = dot(n, l);
    if (ndl <= 0.0) continue;
    vec3 E = uPLC[i].rgb/(d2 + uPLP[i].w*uPLP[i].w);
    if (uPLC[i].w > -1.5) E *= smoothstep(uPLC[i].w, mix(uPLC[i].w, 1.0, 0.3), dot(-l, uPLD[i].xyz));
    float lum = max(E.r, max(E.g, E.b))*ndl;
    if (lum < 0.0015) continue;
    if (uPLD[i].w > 0.0 && lum > 0.004) E *= lightShadow(i, p, n, l, d);
    col += pbr(n, v, l, vec3(0.0), m.rough, 0.0, E);
  }
  return col + m.emit*(1.0 - F);
}

void main(){
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec4 g0 = texelFetch(uGB0, px, 0);
  int cls = int(g0.w + 0.5);
  vec2 ndc = (vUV + uJit)*2.0 - 1.0;
  vec3 rd = camRay(ndc), ro = uCamPos;
  // Capture before the sky/deferred branches. A geometric receiver plane avoids
  // treating scanned plaster relief as a different shadow slope in every pixel.
  vec3 receiverDx = dFdx(rd*g0.x), receiverDy = dFdy(rd*g0.x);
  if (cls == GB_SKY) { gSunDir = sunDirUniform(); gSunCol = sunColUniform(); oColor = vec4(skyColorAt(rd, ro.y, uPlanetR), 1.0); oDepth = 1e6; oCloudMask = 1.0; return; }
  float t = g0.x;
  // the round world (kPlanet): the point was drawn at ro + rd t; it is at p in the flat world - every lookup's, the
  // G-buffer's normal's - and it is shaded there, in its own level: the view and the sun turned as it sees them (so
  // the far side of the curve falls into its own dusk)
  vec3 rdC = rd, p = planetFlat(ro + rd*t, ro), n = octDec(g0.yz);
  rd = planetTurn(rd, p, ro, -1.0); gSunDir = planetTurn(sunDirUniform(), p, ro, -1.0);
  gSunCol = sunLightAt(gSunDir.y, p.y, uPlanetR)*uSunDim;
  gEnvironmentReceiverNormal = n;
  gEnvironmentReceiverEntity = cls == GB_ENTITY;
  if (cls == GB_TERRAIN || cls == GB_ENTITY) {
    vec3 plane = cross(receiverDx,receiverDy);
    float norm2 = dot(plane,plane);
    if (norm2 > 1e-12 && max(length(receiverDx),length(receiverDy)) < max(0.5,t*0.015)) {
      plane = planetTurn(plane*inversesqrt(norm2), p, ro, -1.0);
      if (dot(plane,n) < 0.0) plane = -plane;
      if (dot(plane,n) > 0.25) gEnvironmentReceiverNormal = plane;
    }
  }
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
      else { sh = terrainShadow(p + n*0.5 + (cls == GB_ENTITY || cls == GB_FOLIAGE ? vec3(0.0, 0.5, 0.0) : vec3(0.0)), uSunDir, t); if (sh > 0.0) sh *= environmentShadow(p, n, t)*cloudShadow(p)*gShProxy.r; }
    }
    bool environment = cls == GB_TERRAIN || cls == GB_ENTITY || cls == GB_FOLIAGE;
    bool glazing = cls == GB_ENTITY && (flags & GBF_ENV_GLASS) != 0;
    col = glazing ? shadeEnvironmentGlazing(p, n, rd, m, sh)
                  : environment ? shadeEnvironmentSurface(p, n, rd, m, sh) : shadeSurface(p, n, rd, m, sh);
    if (cls == GB_ENTITY && (flags & GBF_ENV_CLEARCOAT) != 0) {
      vec3 F = fresnelSchlick(max(dot(n, -rd), 0.0), vec3(0.04));
      float coat = 0.55;
      col *= 1.0 - coat*F;
      col += coat*(environmentReflection(reflect(rd, n))*F*0.8
        + pbr(n, -rd, uSunDir, vec3(0.0), 0.15, 0.0, uSunCol*sh*3.2));
    }
    if (cls == GB_FOLIAGE) {   // light through the leaves when the sun is behind them, and a soft wrap
      float back = pow(max(dot(rd, uSunDir), 0.0), 3.0)*0.9 + 0.12*max(dot(-n, uSunDir), 0.0);
      col += m.alb*vec3(0.85, 1.0, 0.55)*uSunCol*sh*back*1.6;
    }
    if ((flags & GBF_GLINT) != 0) { vec3 h = normalize(-rd + uSunDir); col += uSunCol*pow(max(dot(n, h), 0.0), 400.0)*sh*3.0; }   // painted skin
  }
  bool sealed = cls == GB_POD || cls == GB_DISPLAY;   // the cockpit: nothing outside the aircraft reaches it
  if (!sealed) col = applyFog(col, ro, rdC, t, rd);
  if (any(isnan(col)) || any(isinf(col)) || !(col.r + col.g + col.b < 1e7)) col = vec3(0.0);
  // the TAA's history class: 1 world, 0.5 rigid with the aircraft (0.55 a display), 0.2 moving, 0 none
  float taa = (flags & GBF_MOVING) != 0 ? 0.2 : (flags & GBF_RIGID) != 0 ? ((flags & GBF_DISPLAY) != 0 ? 0.55 : 0.5) : cls == GB_DISPLAY ? 0.0 : 1.0;
  oColor = vec4(clamp(col, vec3(0.0), vec3(3e4)), taa);
  oDepth = t;
  oCloudMask = sealed ? 0.0 : 1.0;
}
