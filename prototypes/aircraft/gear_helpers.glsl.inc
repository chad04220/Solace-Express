// Shared fleet gear geometry. The original wheel envelope and contact centres stay intact.
// Fine tread, recess rings and bolt heads are shaded once at a hit, not in every ray step.
vec2 gearWheelDetails(vec3 q, vec2 res, float r, float h, bool braked){
  if (sdBox(q, vec3(h + 0.08, r*0.69 + 0.04, r*0.69 + 0.04)) >= res.x) return res;
  vec3 face = vec3(abs(q.x) - h - 0.004, q.yz);
  float rim = sdRoundCylX(face, r*0.55, 0.018, 0.007);
  float cap = sdRoundCylX(face - vec3(0.020, 0.0, 0.0), r*0.23, 0.020, 0.008);
  float metal = min(rim, cap);
  if (braked) metal = min(metal, sdRoundCylX(q + vec3(h + 0.022, 0.0, 0.0), r*0.65, 0.012, 0.005));
  return opU(res, vec2(metal, 8.0));
}
vec2 gearLegDetails(vec3 p, vec2 res, vec3 upper, vec3 lower, float shaft, bool oleo){
  vec3 centre = (upper + lower)*0.5;
  if (sdBox(p - centre, abs(upper - lower)*0.5 + vec3(0.32, 0.15, 0.32)) >= res.x) return res;
  float metal = sdCapsule(p, mix(upper, lower, 0.86), mix(upper, lower, 0.97), shaft*1.45);
  if (oleo) {
    metal = min(metal, sdCapsule(p, mix(upper, lower, 0.12), mix(upper, lower, 0.53), shaft*1.5));
    float scale = clamp(shaft/0.06, 0.65, 1.5);
    vec3 elbow = mix(upper, lower, 0.72) + vec3(0.0, 0.0, 0.12*scale);
    metal = min(metal, sdCapsule(p, mix(upper, lower, 0.56), elbow, 0.013*scale));
    metal = min(metal, sdCapsule(p, elbow, mix(upper, lower, 0.87), 0.013*scale));
    metal = min(metal, sdCapsule(p, upper + vec3(-0.18*scale, 0.0, -0.18*scale), mix(upper, lower, 0.37), 0.020*scale));
  }
  return opU(res, vec2(metal, 8.0));
}
