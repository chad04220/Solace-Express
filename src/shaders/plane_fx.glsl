//! kPlaneFx
//! One XR-30-style exhaust field for every reheat-capable nozzle, in its actual moving frame.
// ---------------------------------------------------------------- research jet exhaust plumes
// Volumetric emission marched through each plume (body space). Dry thrust: a blue core whose length and brightness
// follow the throttle, an orange-tipped flame from mid power and pale shock cells towards full military power. Reheat
// (phasing in from 85% actual spool, matching thrust): a translucent blue-violet shell at the
// nozzle, a train of white-yellow shock diamonds (Mach disks joined by the expansion / compression cones, spaced
// wider as the pressure ratio climbs with Mach) inside an orange flame that flares, flickers and reddens downstream.
// Effects-only uniforms: changing live exhaust parameters does not invalidate baked aircraft geometry.
uniform int uExhaustCount;
uniform vec4 uExhaustExit[4];  // body exit xyz, local horizontal radius
uniform vec4 uExhaustAxis[4];  // body direction xyz, local vertical radius
uniform vec4 uExhaustPower[4]; // spool, reheat, per-engine intensity, Mach
uniform int uExhaustEngine[4]; // physical engine index of each compacted live slot
vec4 exhaustEnginePower(int engine){
  if (!gOwn) return vec4(gFlame.xy, 1.0, gFlame.w); // AI reads its own packed traffic state
  for (int i = 0; i < 4; ++i) {
    if (i >= uExhaustCount) break;
    if (uExhaustEngine[i] == engine) return uExhaustPower[i];
  }
  return vec4(0.0); // an absent engine has no nozzle glow, fan drive, plume or exhaust light
}
float gPlumeT = 1.0;   // light from behind that gets through the flames (reheat gas and soot absorb a little)
vec3 plumeOne(vec3 lo, vec3 ld, float tmax, vec3 o, vec3 ax, vec2 radius, vec4 power, float jit){
  float sp = clamp(power.x, 0.0, 1.0), ab = clamp(power.y, 0.0, 1.0), intensity = clamp(power.z, 0.0, 1.6);
  if (sp < 0.02 || intensity <= 0.0 || tmax <= 0.0) return vec3(0.0);
  radius = clamp(radius, vec2(0.05), vec2(2.0));
  ax = normalize(ax);
  float scale = clamp(sqrt(radius.x*radius.y)/sqrt(0.36*0.23), 0.5, 1.65);
  float L = (mix(1.6, 4.5, sp) + 8.0*ab)*scale;
  // A finite cylinder bounds all nonzero samples. Axial and radial rejection happen before the 28-step loop,
  // including tilted/side-on jets, rays parallel to the jet, and rays already clipped by the opaque scene depth.
  float boundR = 2.0*max(max(radius.x, radius.y), 0.55*scale*(1.0 + 0.5*ab));
  vec3 co = lo - o;
  float cax = dot(co, ax), dax = dot(ld, ax), t0 = 0.0, t1 = tmax;
  if (abs(dax) > 1e-6) {
    float ta = -cax/dax, tb = (L - cax)/dax;
    t0 = max(t0, min(ta, tb)); t1 = min(t1, max(ta, tb));
  } else if (cax < 0.0 || cax > L) return vec3(0.0);
  vec3 cr = co - ax*cax, dr = ld - ax*dax;
  float qa = dot(dr, dr), qb = dot(cr, dr), qc = dot(cr, cr) - boundR*boundR;
  if (qa > 1e-8) {
    float h = qb*qb - qa*qc;
    if (h <= 0.0) return vec3(0.0);
    h = sqrt(h); t0 = max(t0, (-qb - h)/qa); t1 = min(t1, (-qb + h)/qa);
  } else if (qc > 0.0) return vec3(0.0);
  if (t1 <= t0) return vec3(0.0);
  // Project a stable transverse basis. At every XR-30 pitch angle bx remains body +x, so its flat nozzle
  // remains flat; round XR-10/20/40 exits need no additional roll parameter, even at 90-degree pod tilt.
  vec3 ref = abs(ax.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
  vec3 bx = normalize(ref - ax*dot(ref, ax)), by = cross(ax, bx);
  float spacing = (0.8 + 0.3*clamp(power.w, 0.0, 2.5))*scale;
  // A camera inside the jet sees the flame ahead of it, without a screen-filling glow behind its eye.
  float camIn = (1.0 - smoothstep(1.5*scale, 3.5*scale, length(cr)))*smoothstep(-scale, 0.5*scale, cax)
                *(1.0 - smoothstep(L, L + 6.0*scale, cax));
  float dt = (t1 - t0)/28.0;
  vec3 acc = vec3(0.0);        // on screen behind the XR-30
  for (int i = 0; i < 28; i++) {
    COST(3);
    vec3 q = lo + ld*(t0 + (float(i) + jit)*dt) - o;
    float x = dot(q, ax);
    if (x < 0.0 || x > L) continue;
    float u = max(x, 0.0)/L;
    float nx = dot(q, bx), ny = dot(q, by);
    float cell = fract(x/spacing), ncell = x/spacing;
    // 2D nozzle: a flat jet that rounds out and spreads downstream, pinched at every shock cell in reheat
    float pinch = 1.0 - 0.14*ab*(0.5 + 0.5*cos(cell*6.2832))*(1.0 - u);
    float wx = mix(radius.x, 0.55*scale, u)*(1.0 + 0.5*ab*u)*pinch;
    float wy = mix(radius.y, 0.55*scale, u)*(1.0 + 0.5*ab*u)*pinch;
    float e2 = (nx*nx)/(wx*wx) + (ny*ny)/(wy*wy);
    if (e2 > 4.0) continue;
    float r = sqrt(e2);
    float turb = vnoise(vec2(x*2.2 - uTime*55.0, nx*4.0 + ny*6.0))*0.65 + vnoise(vec2(x*5.5 - uTime*95.0, ny*9.0 - nx*7.0))*0.35;
    float lip = smoothstep(0.0, 0.08*scale, x);
    // shock diamonds: Mach disk mid-cell plus the converging / diverging cone edges, fading cell by cell
    float dc = abs(cell - 0.5)*2.0;
    float decay = exp(-ncell*0.38)*smoothstep(0.15, 0.6, ncell);
    float bead = exp(-pow((cell - 0.5)/0.16, 2.0) - r*r*5.0);                      // the bright Mach disk region
    float cones = exp(-pow((r - 0.55*(1.0 - dc) - 0.05)/0.06, 2.0))*(1.0 - smoothstep(0.6, 0.9, r))*0.35;
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
    acc += e*lip*pow(1.0 - u, 0.8)*mix(1.0, smoothstep(3.0*scale, 14.0*scale, tcam), camIn)*dt;
    gPlumeT *= exp(-ab*intensity*(flame*0.9 + shell*0.3)*lip*dt/scale);
  }
  acc *= (0.9 + 0.1*sin(uTime*63.0))*0.35*intensity/scale;
  return acc/(1.0 + max(acc.r, max(acc.g, acc.b))*0.45);   // gentle hue-preserving roll-off keeps the orange orange
}
void shadeWraith(inout Mat m, int mid, vec3 lp, vec3 ln, float t);
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
vec3 researchPlumes(vec3 ro, vec3 rd, float tmax, float jit){
  if (uExhaustCount <= 0) return vec3(0.0);
  mat3 inv = transpose(uPlaneRot);
  vec3 lo = inv*(ro - uPlanePos), ld = inv*rd, col = vec3(0.0);
  // One implementation, no more than four active nozzles and 28 samples per intersected nozzle.
  for (int i = 0; i < 4; ++i) {
    if (i >= uExhaustCount) break;
    col += plumeOne(lo, ld, tmax, uExhaustExit[i].xyz, uExhaustAxis[i].xyz,
                    vec2(uExhaustExit[i].w, uExhaustAxis[i].w), uExhaustPower[i], jit);
  }
  return col;
}
