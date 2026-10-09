//! kMapMain

uniform vec4 uMapView;   // centre x, centre z, half extent (m), metres per texel
uniform vec2 uMapRes;
void main(){
  vec2 uv = gl_FragCoord.xy/uMapRes;
  vec2 c0 = uMapView.xy + (uv*2.0 - 1.0)*uMapView.z;
  float foot = uMapView.w;
  float tq = clamp(foot*900.0, 900.0, 20000.0);   // material detail matched to the texel footprint
  vec3 L = normalize(vec3(-0.55, 0.75, -0.4));
  vec3 acc = vec3(0.0);
  for (int si = 0; si < 4; si++) {
    vec2 wp = c0 + (vec2(si & 1, si >> 1) - 0.5)*foot*0.7;
    vec4 base = baseAt(wp);
    float h = terrainH(wp, 9);
    // land cover comes from ~40 m mask cells: warp the material lookup so forest and field edges come out organic,
    // but not on and around the airfields, where it would bend and break up the runways
    float apK = 0.0;
    for (int ai = 0; ai < 16; ai++) {
      if (ai >= uApCount) break;
      float hl = uApDim[ai].x*0.5;
      apK = max(apK, smoothstep(hl + 900.0, hl + 350.0, length(wp - uAp[ai].xy)));
    }
    vec2 mw = wp + ((vec2(cn3(vec3(wp*0.012, 1.3)), cn3(vec3(wp*0.012, 7.9))) - 0.5)*70.0 + (vec2(cn3(vec3(wp*0.05, 3.1)), cn3(vec3(wp*0.05, 5.7))) - 0.5)*18.0)*(1.0 - apK);
    vec3 col;
    if (h < 0.0) {   // sea: turquoise shallows over sand, deepening to blue, a white surf line on the shore
      float dpt = -h;
      col = mix(vec3(0.25, 0.62, 0.62), vec3(0.03, 0.17, 0.33), smoothstep(0.0, 35.0, dpt));
      col = mix(col, vec3(0.015, 0.07, 0.17), smoothstep(35.0, 220.0, dpt));
      col += vec3(0.02)*(cn3(vec3(wp*0.02, 0.0)) - 0.5);
      col = mix(col, vec3(0.85, 0.9, 0.9), smoothstep(1.2, 0.0, dpt)*0.6);
    } else {
      vec3 n = terrainNormal(wp, tq);
      Mat m = terrainMaterial(vec3(mw.x, h, mw.y), n, tq, baseAt(mw), vec2(foot, 0.0), vec2(0.0, foot));
      float sun = max(dot(n, L), 0.0);
      col = m.alb*(0.42 + 0.9*sun) + m.emit*0.0;
    }
    acc += col;
  }
  vec3 col = acc*0.25*1.25;
  col = col/(1.0 + col*0.35);
  oColor = vec4(pow(clamp(col, 0.0, 1.0), vec3(1.0/2.2)), 1.0);
}
