//! kEffectsFS
//! The raster renderer's effects pass, over the lit and clouded frame: the XR-40's cloak (its cloaked part shows the
//! frame behind it along a slightly bent ray, with the skin's shimmer), the propeller discs, the vapour cone, the
//! research jets' exhaust plumes (over the clouds), the weapons and the cockpit hologram -
//! composed in order, with their TAA classes. Reads the frame and writes a copy of it.
in vec2 vUV; out vec4 oColor;
uniform sampler2D uRawTex; uniform sampler2D uSceneDepth;
void main(){
  gZero = min(uQuality, 0);
  loadMain();
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec4 raw = texelFetch(uRawTex, px, 0);
  vec3 col = raw.rgb; float taaFlag = raw.a;
  float t = texelFetch(uSceneDepth, px, 0).r;
  int cls = int(texelFetch(uGB0, px, 0).w + 0.5);
  bool pod = cls == GB_POD || cls == GB_DISPLAY;
  vec2 ndc = (vUV + uJit)*2.0 - 1.0;
  vec3 rd = camRay(ndc), ro = uCamPos;
  float jitter = fract(52.9829189*fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) + uSeed);   // interleaved gradient noise, rotated per frame
  bool cockpitView = uPlaneOn == 1 && gPS.w > 0.5 && uWreck == 0;
  int type = RESEARCH_ON ? int(gM[0].z + 0.5) : 0;
  // XR-40 cloak: a pixel on the cloaked craft (left out of the G-buffer) sees the frame behind it along a bent ray
  if (uWr[4].w > 0.001 && uPlaneOn == 1 && uWreck == 0 && !cockpitView && type == 6 && uPano.x <= 0.0) {
    float hullT = 0.0;
    if (uHullOn == 1) { float hv = texelFetch(uEnv, px, 0).g; hullT = hv > 1e29 ? hv : (hv > 0.0 ? max(uHullNear, hv*0.999 - 0.1) : 0.0); }
    vec2 hc = tracePlaneHull(ro, rd, t, hullT);
    if (hc.x > 0.0) {
      vec3 hp = ro + rd*hc.x, ckLp = transpose(uPlaneRot)*(hp - uPlanePos);
      if (ckLp.z < uWr[6].y) {
        vec3 ckN = uPlaneRot*planeNormal(ckLp);
        vec3 rdB = normalize(rd - (ckN - rd*dot(ckN, rd))*0.035);
        vec3 q = hp + rdB*max(t - hc.x, 0.5), v = transpose(uCamRot)*(q - ro);   // (the world behind, about where this ray met it)
        vec2 uv = vec2(v.x/max(-v.z, 1e-3)/(uTanHalf*uAspect), v.y/max(-v.z, 1e-3)/uTanHalf)*0.5 + 0.5 - uJit;
        ivec2 bp = clamp(ivec2(uv*uRes), ivec2(0), ivec2(uRes) - 1);
        col = cloakSkin(texelFetch(uRawTex, bp, 0).rgb, ckN, rd, ckLp, ckLp.z - uWr[6].y + 0.8);
        col = applyFog(col, ro, rd, hc.x);
        taaFlag = 0.5;
      }
    }
  }
  // propeller discs (motion-blurred), composited over the scene
  if (uPlaneOn == 1 && uWreck == 0) {
    mat3 inv = transpose(uPlaneRot);
    vec3 lo = inv*(ro - uPlanePos), ld = inv*rd;
    for (int i = 0; i < 2; i++) {
      if (i >= uPropCount) break;
      vec4 pr = uProp[i];
      if (abs(ld.z) < 1e-4) continue;
      float tp = (pr.z - lo.z)/ld.z;
      if (tp < 0.0 || tp > t) continue;
      vec3 hp = lo + ld*tp - pr.xyz;
      float r = length(hp.xy);
      if (r > pr.w) continue;
      float blades = uPr.z; float blur = uPr.y;
      float ang = atan(hp.y, hp.x) - uPr.x;
      float bl = smoothstep(0.86, 0.95, cos(blades*ang*0.5*2.0))*(1.0 - smoothstep(pr.w*0.9, pr.w, r));
      float a = mix(bl, 0.10 + 0.08*smoothstep(0.6, 1.0, cos(blades*ang)) + 0.25*smoothstep(pr.w*0.95, pr.w, r), blur);
      vec3 pc = vec3(0.04)*(uSunCol*max(uSunDir.y, 0.0) + 0.2) + vec3(0.6, 0.6, 0.1)*smoothstep(pr.w*0.9, pr.w, r)*0.3;
      col = mix(col, pc, clamp(a, 0.0, 1.0)*0.85);
    }
  }
  if (uPlaneOn == 1 && uWreck == 0 && uVapor.x > 0.01) { vec3 c0 = col; col = vaporCone(col, ro, rd, t, jitter); if (dot(abs(col - c0), vec3(1.0)) > 0.02) taaFlag = min(taaFlag, 0.2); }
  vec3 plE = vec3(0.0); float plT = 1.0;
  if (uPlaneOn == 1 && uWreck == 0 && gPS.w < 0.5 && isMantis()) { plE = mantisPlume(ro, rd, t, jitter); plT = gPlumeT; }
  if (uPlaneOn == 1 && uWreck == 0 && gPS.w < 0.5 && type == 5) { plE = jetPlumes(ro, rd, t, jitter); plT = gPlumeT; }
  if (uPlaneOn == 1 && uWreck == 0 && gPS.w < 0.5 && type == 6) { plE = wraithPlumes(ro, rd, t, jitter); plT = gPlumeT; }
  if (plE.r + plE.g + plE.b > 0.03) taaFlag = min(taaFlag, 0.2);
  if (!pod && uFxBeams + uFxBombs + uFxBlasts > 0) col = weaponsFx(col, ro, rd, t);
  col = col*plT + plE;   // (the flames over the clouds, which the cloud pass has already laid under them)
  if (cockpitView && type == 6 && !pod) col += wrHolo(ro, rd, t);   // the hologram floats inside the cabin, in front of everything
  if (any(isnan(col)) || any(isinf(col)) || !(col.r + col.g + col.b < 1e7)) col = vec3(0.0);
  oColor = vec4(clamp(col, vec3(0.0), vec3(3e4)), taaFlag);
}
