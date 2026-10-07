//! kPlaneLight
//! The light on an aircraft surface after planeMaterial: the sun with the terrain's, the airframe's own, the scenery's and
//! the clouds' shadows on the exterior; the cabin's fixtures and the sun through the windows inside; the sealed research
//! cockpit from its fixtures alone. Shared by the objects pass and the aircraft mesh pass (plane_gb.glsl).
// The airframe's own shadow of the sun at a point on it: from its sun shadow map on the raster path; the march of the
// field only where the map can't say - under the moving parts' mask, or with no map this frame (inside the cabin a
// surface the sun grazes or faces away from takes nothing from it in any case). Inside, the map's soft value is used
// as it is, frames and edges too: marching there cost the cockpit view ~19 ms on an RTX 3070 Laptop (v3.25.0). One
// call for the exterior and the cabin alike: each march written out is another copy of the airframe's distance
float afSunSelf(vec3 p, vec3 n, bool interior){
  float ndl = dot(n, uSunDir);
  float ms = (uAfShOn & 1) != 0 && uWreck == 0 ? (ndl < (interior ? 0.3 : 0.05) ? 0.0 : shMapLookupB(0, p, n, 1.0 + 2.0*(1.0 - ndl))) : -1.0;   // (inside, a surface the sun barely faces is lit by the fixtures and the ambient alone: its thin frames alias in the map)
  if (ms >= 0.0) return ms;
  gShMax = interior ? 3.5 : 1e9;   // (inside, the ray only needs to get out through the cabin and the wing above it)
  float s = planeShadow(p + n*0.02, uSunDir);
  gShMax = 1e9;
  return s;
}
// The lit colour of an airframe pixel from planeMaterial() seen from the cockpit or inside the cabin: the sun (sunSh:
// the terrain's and the airframe's own shadows, from planeToGB) with the scenery's and the clouds', then PBR with the
// ambient and the cabin fixtures (interior), the fixtures alone (sealed pod) or the surface shading
vec3 planeLight(vec3 p, vec3 rd, float t, int mid, Mat m, vec3 n, vec3 lp, vec3 ln, bool interior, bool podMat, float sunSh){
  vec4 E = gM[22];
  vec3 col;
  // (the scenery's: an airframe parked by a hangar or under trees sits in the same shadow as the ground round it -
  // the aircraft aren't in the scenery cascades, so this never shadows the airframe itself)
  float sh = sunSh > 0.0 && !podMat ? sunSh*cloudShadow(p)*entShadow(p, n) : 0.0;
  float ao = podMat || interior ? interiorAO(lp, ln) : 1.0;   // (one call: the occlusion taps the field outside the mesh pass)
  if (RESEARCH_ON && podMat) {  // sealed research cockpit: lit only by its modelled fixtures, low and moody
    mat3 inv = transpose(gPR);
    int engP = int(gM[0].z + 0.5);
    col = (engP == 6 ? wraithPodLight(lp, inv*n, inv*(-rd), m, E.xyz) : podLight(lp, inv*n, inv*(-rd), m, E.xyz))*ao + m.emit;
  } else if (interior) {
    vec3 v = -rd; mat3 inv = transpose(gPR);
    vec3 F = fresnelSchlick(max(dot(n, v), 0.0), mix(vec3(0.04), m.alb, m.metal));
    col = pbr(n, v, uSunDir, m.alb, m.rough, m.metal, uSunCol*sh*3.2)
        + (m.alb*(1.0 - m.metal)*(ambientLight(n)*0.4 + ambientLight(vec3(0.0, 1.0, 0.0))*0.2)
           + skyColor(normalize(reflect(rd, n) + vec3(0.0, 0.3, 0.0)))*F*(1.0 - m.rough)*(1.0 - m.rough)*0.35)*ao
        + cabinLight(lp, inv*n, inv*v, m, E.xyz, gM[21].w, E.w)*ao + m.emit;
    // the sun's bounce: what comes through the windows lights the floor, the seats and the panel, and that lights the
    // rest - the headliner and the posts most (they face the lit floor). Without it the cabin's upper half sat black
    // in full sun
    float bounceSun = smoothstep(-0.05, 0.1, uSunDir.y)*cloudShadow(p);
    vec3 nB = inv*n;   // (body space: the cabin's floor is below whatever the attitude)
    col += m.alb*(1.0 - m.metal)*uSunCol*(bounceSun*0.07*(0.55 - 0.45*nB.y))*ao;
  } else {
    col = shadeSurface(p, n, rd, m, sh);
    vec3 h = normalize(-rd + uSunDir);
    col += uSunCol*pow(max(dot(n, h), 0.0), 400.0)*sh*3.0*float(mid <= 5);
  }
  return col;
}
