//! kPlaneFx
//! The XR-9's exhaust plumes (volumetric emission marched through each jet).
// ---------------------------------------------------------------- research jet exhaust plumes
// Volumetric emission marched through each plume (body space). Dry thrust: a blue core whose length and brightness
// follow the throttle, an orange-tipped flame from mid power and pale shock cells towards full military power. Reheat
// (phasing in from ~70% spool): a translucent blue-violet shell at the
// nozzle, a train of white-yellow shock diamonds (Mach disks joined by the expansion / compression cones, spaced
// wider as the pressure ratio climbs with Mach) inside an orange flame that flares, flickers and reddens downstream.
float gPlumeT = 1.0;   // light from behind that gets through the flames (reheat gas and soot absorb a little)
vec3 plumeOne(vec3 lo, vec3 ld, float tmax, vec3 o, vec3 ax, float jit){
  float sp = gFlame.x, ab = gFlame.y;
  float L = mix(1.6, 4.5, sp) + 8.0*ab;
  vec3 c = o + ax*(L*0.5); float br = L*0.5 + 1.0;
  vec3 oc = lo - c; float b = dot(oc, ld), h = b*b - dot(oc, oc) + br*br;
  if (h <= 0.0) return vec3(0.0);
  h = sqrt(h); float t0 = max(-b - h, 0.0), t1 = min(-b + h, tmax);
  if (t1 <= t0) return vec3(0.0);
  vec3 ay = normalize(cross(ax, vec3(1.0, 0.0, 0.0)));
  float spacing = 0.8 + 0.3*clamp(gFlame.w, 0.0, 2.5);
  // a camera inside the jet (chase view right behind in reheat) sees the flame ahead of it, not a glow all around
  vec3 co = lo - o; float cax = dot(co, ax), crad = length(co - ax*cax);
  float camIn = smoothstep(3.5, 1.5, crad)*smoothstep(-1.0, 0.5, cax)*smoothstep(L + 6.0, L, cax);
  float dt = (t1 - t0)/28.0;   // 28 jittered steps (the TAA smooths the rest): 40 made the plume pixels the costliest
  vec3 acc = vec3(0.0);        // on screen behind the XR-9
  for (int i = 0; i < 28; i++) {
    COST(3);
    vec3 q = lo + ld*(t0 + (float(i) + jit)*dt) - o;
    float x = dot(q, ax);
    if (x < -0.05 || x > L) continue;
    float u = max(x, 0.0)/L;
    float nx = q.x, ny = dot(q, ay);
    float cell = fract(x/spacing), ncell = x/spacing;
    // 2D nozzle: a flat jet that rounds out and spreads downstream, pinched at every shock cell in reheat
    float pinch = 1.0 - 0.14*ab*(0.5 + 0.5*cos(cell*6.2832))*(1.0 - u);
    float wx = mix(0.37, 0.55, u)*(1.0 + 0.5*ab*u)*pinch, wy = mix(0.25, 0.55, u)*(1.0 + 0.5*ab*u)*pinch;
    float e2 = (nx*nx)/(wx*wx) + (ny*ny)/(wy*wy);
    if (e2 > 4.0) continue;
    float r = sqrt(e2);
    float turb = vnoise(vec2(x*2.2 - uTime*55.0, nx*4.0 + ny*6.0))*0.65 + vnoise(vec2(x*5.5 - uTime*95.0, ny*9.0 - nx*7.0))*0.35;
    float lip = smoothstep(-0.05, 0.08, x);
    // shock diamonds: Mach disk mid-cell plus the converging / diverging cone edges, fading cell by cell
    float dc = abs(cell - 0.5)*2.0;
    float decay = exp(-ncell*0.38)*smoothstep(0.15, 0.6, ncell);
    float bead = exp(-pow((cell - 0.5)/0.16, 2.0) - r*r*5.0);                      // the bright Mach disk region
    float cones = exp(-pow((r - 0.55*(1.0 - dc) - 0.05)/0.06, 2.0))*smoothstep(0.9, 0.6, r)*0.35;
    float diam = (bead + cones)*decay;
    // reheat
    float shell = exp(-pow((r - 0.8)/0.22, 2.0))*(1.0 - smoothstep(0.0, 0.3, u));
    float flame = exp(-e2*1.3)*smoothstep(0.2, 0.75, turb + 0.45*(1.0 - u))*smoothstep(0.02, 0.18, u);
    vec3 fCol = mix(vec3(1.0, 0.5, 0.14), vec3(0.85, 0.16, 0.04), smoothstep(0.4, 1.0, u));
    vec3 e = ab*(vec3(0.75, 0.38, 0.95)*shell*0.9 + vec3(1.0, 0.8, 0.45)*diam*7.0 + fCol*flame*(4.0 - 2.4*u)
                 + vec3(1.0, 0.62, 0.3)*exp(-e2*5.0)*(1.0 - smoothstep(0.0, 0.5, u))*1.2);
    // dry: a blue core that grows with the throttle, an orange-tipped flame from mid power, pale shock cells near full
    e += (1.0 - ab)*(vec3(0.3, 0.5, 1.0)*exp(-e2*2.5)*(1.0 - u)*3.2*sp
                     + mix(vec3(1.0, 0.55, 0.25), fCol, u)*flame*(1.8 - 0.8*u)*smoothstep(0.3, 0.85, sp)
                     + vec3(0.65, 0.78, 1.0)*diam*3.5*smoothstep(0.5, 0.95, sp));
    float tcam = t0 + (float(i) + jit)*dt;
    acc += e*lip*pow(1.0 - u, 0.8)*mix(1.0, smoothstep(3.0, 14.0, tcam), camIn)*dt;
    gPlumeT *= exp(-ab*(flame*0.9 + shell*0.3)*lip*dt);
  }
  acc *= (0.9 + 0.1*sin(uTime*63.0))*0.35;
  return acc/(1.0 + max(acc.r, max(acc.g, acc.b))*0.45);   // gentle hue-preserving roll-off keeps the orange orange
}
void shadeWraith(inout Mat m, int mid, vec3 lp, vec3 ln, float t);
vec3 wraithPlumes(vec3 ro, vec3 rd, float tmax, float jit);
vec3 vaporCone(vec3 col, vec3 ro, vec3 rd, float tmax, float jit);
vec3 cloakSkin(vec3 world, vec3 n, vec3 rd, vec3 lp, float front);
vec3 weaponsFx(vec3 col, vec3 ro, vec3 rd, float t);
void shadeWraithCockpit(inout Mat m, int mid, vec3 lp, vec3 ln, vec3 E);
vec3 wraithPodLight(vec3 p, vec3 n, vec3 v, Mat m, vec3 E);
vec3 wrHolo(vec3 ro, vec3 rd, float tmax);
vec3 wraithScreen(vec3 col, vec3 rd, int id, vec3 sl);
vec3 feedScreen(int id, vec3 sl, out vec3 rdc, out bool bomb);
vec3 wrFeedOverlay(vec3 col, vec3 sl);
float wrClip(vec3 lp, int mid);
vec3 wrClipAtlas(vec2 uv);
vec3 jetPlumes(vec3 ro, vec3 rd, float tmax, float jit){
  if (gFlame.x < 0.02) return vec3(0.0);
  mat3 inv = transpose(uPlaneRot);
  vec3 lo = inv*(ro - uPlanePos), ld = inv*rd;
  float a = gFlame.z;
  vec3 ax = vec3(0.0, -sin(a), cos(a));
  vec3 col = vec3(0.0);
  for (int s = -1; s <= 1; s += 2) {
    vec3 o = vec3(0.82*float(s), -0.12, 7.75) + ax*1.0;   // nozzle exit (pivot + 1 m along the swivelled axis)
    col += plumeOne(lo, ld, tmax, o, ax, jit);
  }
  return col;
}
