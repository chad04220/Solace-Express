//! kEntVS

layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec4 aAux;   // part, ao, u, v
layout(location=3) in vec4 iA; layout(location=4) in vec4 iB;   // position + yaw | scale + seed
uniform vec4 uWheel0; uniform vec2 uWheel1; // dynamic packet; all zero for parked scenery
uniform mat4 uVP; uniform vec2 uJit; uniform float uLogC; uniform float uTime; uniform int uKind; uniform int uShadowPass;
uniform vec3 uCamV; uniform float uFar; uniform float uThin; uniform float uThinRef;   // view pass: per-instance distance thinning (entKeep)
uniform vec3 uWind;   // surface wind velocity (windsocks)
uniform mat4 uPanoView; uniform vec2 uPano;   // a panoramic camera feed: projected onto its cylinder (camRay)
out vec3 vW; out vec3 vL; out vec3 vLN; out vec4 vAux;
flat out vec4 vInst;   // seed, yaw, scale y, instance height
flat out vec3 vScale;
void main(){
  if (uShadowPass == 0 && uThin > 0.5) {   // thin out towards the far limit (the ground texture takes over distant forest)
    float d = length(iA.xyz - uCamV);
    if (fract(iB.w*7.13) >= min(1.0, uThinRef*uThinRef/max(d*d, 1.0)) || d >= uFar) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
  }
  vec3 posed=aPos, posedN=aNrm;
  int wheel=int(aAux.x+.5)-27;
#if ENT_BUILDINGS
  if(wheel>=0 && wheel<6) {
    float a=wheel<4?uWheel0[wheel]:uWheel1[wheel-4], c=cos(a),s=sin(a);
    vec2 q=aPos.yz-aAux.zw;
    posed.yz=aAux.zw+vec2(c*q.x-s*q.y,s*q.x+c*q.y);
    posedN.yz=vec2(c*aNrm.y-s*aNrm.z,s*aNrm.y+c*aNrm.z);
  }
#endif
  // trees and bushes: no two alike. Each instance turns its crown round the trunk by its own amount, more towards the
  // top (the whorls, tiers and clumps come round at other bearings), pushes it out of round its own way and leans a
  // little - one mesh per species read as one tree copied across the hills (both passes: the shadow is the same tree)
#if ENT_TREES
  if (uKind <= 6) {
    float sd = iB.w, t = clamp(posed.y/(uKind == 6 ? 1.8 : uKind == 5 ? 10.0 : 12.0), 0.0, 1.6);
    float tw = (fract(sd*3.71) - 0.5)*2.4*t, ct = cos(tw), st = sin(tw);
    mat2 R = mat2(ct, st, -st, ct);
    posed.xz = R*posed.xz; posedN.xz = R*posedN.xz;
    float ph = atan(posed.z, posed.x);
    posed.xz *= 1.0 + min(t, 1.0)*(0.14*sin(2.0*ph + sd*41.0) + 0.07*sin(3.0*ph + sd*23.0));
    if (uKind != 6) posed.xz += vec2(cos(sd*57.0), sin(sd*57.0))*0.45*fract(sd*8.3)*t*t;   // (up to ~0.5 m at 12 m)
  }
#endif
  vec3 lp = posed*iB.xyz;
  // foliage sways a little in the wind, more towards the top and the frond tips
#if ENT_TREES
  if (uKind <= 6 && (aAux.x < 3.5 || aAux.x > 17.5) && uShadowPass == 0) {
    float h = max(aPos.y, 0.0)/14.0;
    float ph = uTime*(1.1 + 0.4*fract(iB.w*7.0)) + iA.x*0.05 + iA.z*0.04;
    lp.xz += vec2(sin(ph), cos(ph*0.83))*0.06*h*h*iB.y + (aAux.x > 1.5 && aAux.x < 2.5 ? vec2(0.0, sin(ph*2.3 + aPos.x))*0.08*aAux.w : vec2(0.0));
  }
#endif
  float c = cos(iA.w), s = sin(iA.w);
  vec3 wp = vec3(c*lp.x + s*lp.z, lp.y, -s*lp.x + c*lp.z) + iA.xyz;
  vec3 ln = normalize(posedN/iB.xyz);
#if ENT_BUILDINGS
  if (uKind == 40 && abs(aAux.x - 23.0) < 0.5) {
    // windsock: the sock (modelled along +x from the pole top) streams downwind, filling out by ~15 kt and drooping
    // when calm, with a little flutter. Built straight in world space, then expressed back in the instance frame.
    vec2 w = uWind.xz; float sp = length(w), k = clamp(sp/7.7, 0.0, 1.0);
    vec2 wd = sp > 0.1 ? w/sp : vec2(1.0, 0.0);
    float ph = uTime*(2.0 + 2.5*k) + iA.x*0.13;
    wd = normalize(wd + vec2(-wd.y, wd.x)*sin(ph)*0.1*(0.25 + k));
    vec3 d = normalize(vec3(wd.x*max(k, 0.08), -(1.0 - k)*1.3 - 0.06 + 0.04*sin(ph*1.7)*k, wd.y*max(k, 0.08)));
    vec3 s1 = normalize(cross(d, vec3(0.0, 1.0, 0.0)) + vec3(1e-4, 0.0, 0.0)), s2 = cross(s1, d);
    vec3 top = iA.xyz + vec3(0.0, 6.0*iB.y, 0.0);
    wp = top + d*aPos.x + s2*(aPos.y - 6.0) + s1*aPos.z;
    vec3 wn = normalize(d*aNrm.x + s2*aNrm.y + s1*aNrm.z);
    ln = vec3(c*wn.x - s*wn.z, wn.y, s*wn.x + c*wn.z);
    lp = aPos;
  }
#endif
  vW = wp; vL = wheel>=0 && wheel<6 ? aPos*iB.xyz : lp; vLN = ln; vAux = aAux;
  vInst = vec4(iB.w, iA.w, iB.y, iA.y); vScale = iB.xyz;
  gl_Position = uVP*vec4(wp, 1.0);
  bool behind = false;
  if (uShadowPass == 0 && uPano.x > 0.0) {   // (the whole triangle is dropped where it reaches round behind the camera)
    vec3 c = (uPanoView*vec4(wp, 1.0)).xyz; float a = atan(c.x, -c.z), d = length(c);
    gl_Position = vec4(a/uPano.x*d, c.y/max(length(c.xz), 1e-3)/uPano.y*d, 0.0, d); behind = abs(a) > 1.9;
  }
  if (uShadowPass == 0) {
    gl_Position.xy -= 2.0*uJit*gl_Position.w;   // the frame's sub-pixel jitter
    gl_Position.z = (log2(max(1e-6, 1.0 + gl_Position.w))*uLogC - 1.0)*gl_Position.w;   // logarithmic depth: 0.3 m .. 40 km
    if (behind) gl_Position.z = 2.0*gl_Position.w;
  }
}
