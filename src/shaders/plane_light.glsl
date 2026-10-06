//! kPlaneLight
//! The light on an aircraft surface after planeMaterial: the sun with the terrain's, the airframe's own, the scenery's and
//! the clouds' shadows on the exterior; the cabin's fixtures and the sun through the windows inside; the sealed research
//! cockpit from its fixtures alone. Shared by the ray tracer and the raster renderer's objects pass.
// The lit colour of an airframe pixel from planeMaterial(): the terrain, airframe, scenery and cloud shadows of the sun,
// then PBR with the ambient and the cabin fixtures (interior), the fixtures alone (sealed pod) or the surface shading
vec3 planeLight(vec3 p, vec3 rd, float t, int mid, Mat m, vec3 n, vec3 lp, vec3 ln, bool interior, bool podMat, bool trafHit){
  float sunVis = smoothstep(-0.05, 0.05, uSunDir.y);
  vec4 E = gM[22];
  vec3 col;
  // sun shadow: the terrain's is one value for the whole intact airframe (from the CPU); inside the cabin the
  // self-shadow ray only needs to get out through the cabin and the wing above it, not cross the whole aircraft
  float sh = 0.0;
  if (sunVis > 0.0 && !podMat) {
    float tsh = (trafHit || uWreck > 0) ? terrainShadow(p, uSunDir, t) : uPlaneTSh;
    if (tsh > 0.0 && !trafHit) {
      // the airframe's own shadow (and the sun through the cabin windows): from its sun shadow map on the raster path;
      // the march of the field only under the moving parts' mask, and on the ray tracer
      // (the map settles the pixels it is sure of - lit or shadowed, nearly all of them; its edge texels, where it
      // would show its blocks, and the moving parts' mask take the march; a surface the sun grazes or faces away
      // from takes nothing from it in any case)
      float ndl = dot(n, uSunDir);
      float ms = (uAfShOn & 1) != 0 && uWreck == 0 ? (ndl < (interior ? 0.3 : 0.08) ? 0.0 : shMapLookupB(0, p, n, 1.0 + 2.0*(1.0 - ndl))) : -1.0;   // (inside, a surface the sun barely faces is lit by the fixtures and the ambient alone: its thin frames alias in the map)
      // (inside, an occluder within ~0.3 m of the receiver is a thin frame or the receiver's own far side: the map's
      // texels alias along it, so the march decides there - a small share of the cabin's pixels)
      bool thin = interior && ms >= 0.0 && ms < 0.995 && gShMapOcc*2.0*planeBound() < 0.3;
      if (ms >= 0.995) {}
      else if (ms >= 0.0 && ms <= 0.005 && !thin) tsh = 0.0;
      else { gShMax = interior ? 3.5 : 1e9; tsh *= planeShadow(p + n*0.02, uSunDir); gShMax = 1e9; }
    }
    // (and the scenery's: an airframe parked by a hangar or under trees sits in the same shadow as the ground
    // round it - the aircraft aren't in the scenery cascades, so this never shadows the airframe itself)
    sh = tsh > 0.0 ? tsh*cloudShadow(p)*entShadow(p, n) : 0.0;
  }
  if (podMat) {  // sealed research cockpit: lit only by its modelled fixtures, low and moody
    mat3 inv = transpose(gPR);
    int engP = int(gM[0].z + 0.5);
    col = (engP == 6 ? wraithPodLight(lp, inv*n, inv*(-rd), m, E.xyz) : podLight(lp, inv*n, inv*(-rd), m, E.xyz))*interiorAO(lp, ln) + m.emit;
  } else if (interior) {
    vec3 v = -rd; mat3 inv = transpose(gPR);
    float ao = interiorAO(lp, ln);
    vec3 F = fresnelSchlick(max(dot(n, v), 0.0), mix(vec3(0.04), m.alb, m.metal));
    col = pbr(n, v, uSunDir, m.alb, m.rough, m.metal, uSunCol*sh*3.2)
        + (m.alb*(1.0 - m.metal)*(ambientLight(n)*0.4 + ambientLight(vec3(0.0, 1.0, 0.0))*0.2)
           + skyColor(normalize(reflect(rd, n) + vec3(0.0, 0.3, 0.0)))*F*(1.0 - m.rough)*(1.0 - m.rough)*0.35)*ao
        + cabinLight(lp, inv*n, inv*v, m, E.xyz, gM[21].w, E.w)*ao + m.emit;
  } else {
    col = shadeSurface(p, n, rd, m, sh);
    vec3 h = normalize(-rd + uSunDir);
    col += uSunCol*pow(max(dot(n, h), 0.0), 400.0)*sh*3.0*float(mid <= 5);
  }
  return col;
}
