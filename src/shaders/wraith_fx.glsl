//! kWraithFx
//! The XR-11's plasma plumes, the transonic vapour cone, the cloak skin and the weapons effects.
// four round plasma jets, one per pod (thrust fractions in uWr[2]): a white-cyan core in a violet sheath that
// swirls slowly, with bright standing shock rings; longer, hotter and tighter-ringed in boost
vec3 plumeRound(vec3 lo, vec3 ld, float tmax, vec3 o, vec3 ax, float sp, float ab, float jit){
  float L = mix(3.0, 6.0, sp) + 8.0*ab;
  vec3 c = o + ax*(L*0.5); float br = L*0.5 + 0.7;
  vec3 oc = lo - c; float b = dot(oc, ld), h = b*b - dot(oc, oc) + br*br;
  if (h <= 0.0) return vec3(0.0);
  h = sqrt(h); float t0 = max(-b - h, 0.0), t1 = min(-b + h, tmax);
  if (t1 <= t0) return vec3(0.0);
  vec3 bx = normalize(cross(ax, abs(ax.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0))), by = cross(ax, bx);
  float spacing = 0.75 - 0.2*ab;
  float dt = (t1 - t0)/28.0; vec3 acc = vec3(0.0);
  for (int i = 0; i < 28; i++) {
    COST(3);
    vec3 q = lo + ld*(t0 + (float(i) + jit)*dt) - o;
    float x = dot(q, ax); if (x < -0.05 || x > L) continue;
    float u = max(x, 0.0)/L;
    vec3 rq = q - ax*x; float rr = length(rq);
    float w = mix(0.33, 0.2, u)*(1.0 + 0.9*ab*u);
    float r = rr/w; if (r > 2.2) continue;
    float ang = atan(dot(rq, by), dot(rq, bx));
    float helix = 0.5 + 0.5*sin(ang*3.0 - x*5.0 + uTime*24.0);
    float flick = vnoise(vec2(x*3.0 - uTime*60.0, ang*2.0 + rr*6.0));
    float cell = fract(x/spacing);
    float ring = exp(-pow((cell - 0.5)/0.06, 2.0))*exp(-pow((r - 0.5)/0.2, 2.0))*exp(-x/spacing*0.35)*step(0.4, x/spacing);
    float core = exp(-r*r*7.0)*pow(1.0 - u, 0.6);
    float sheath = exp(-pow((r - 0.7)/0.3, 2.0))*(0.55 + 0.45*helix)*(0.6 + 0.4*flick);
    vec3 shCol = mix(vec3(0.42, 0.22, 1.0), vec3(0.95, 0.3, 0.85), smoothstep(0.3, 1.0, u));
    vec3 e = vec3(0.75, 0.92, 1.0)*core*(7.0 + 5.0*ab) + shCol*sheath*(2.6 + 1.6*ab) + vec3(0.8, 0.9, 1.0)*ring*(4.0 + 6.0*ab);
    acc += e*smoothstep(-0.05, 0.08, x)*pow(1.0 - u, 1.2)*smoothstep(1.0, 4.0, t0 + (float(i) + jit)*dt)*dt;
    gPlumeT *= exp(-(sheath*0.35 + core*0.5)*sp*dt);
  }
  return acc*(0.5 + 0.7*sp);
}
vec3 wraithPlumes(vec3 ro, vec3 rd, float tmax, float jit){
  if (gFlame.x < 0.02) return vec3(0.0);
  mat3 inv = transpose(uPlaneRot);
  vec3 lo = inv*(ro - uPlanePos), ld = inv*rd;
  vec3 col = vec3(0.0);
  for (int i = 0; i < 4; i++) {
    float a = uWr[0][i] + uWr[3][i], y = uWr[1][i], a0 = uWr[0][i];
    vec3 ax = normalize(vec3(-sin(y), -sin(a)*cos(y), cos(a)*cos(y)));
    vec3 o = WR_POD[i] + vec3(0.0, -sin(a0), cos(a0))*1.5;
    float th = clamp(uWr[2][i], 0.0, 1.6);
    col += plumeRound(lo, ld, tmax, o, ax, clamp(th*1.3, 0.0, 1.0), gFlame.y, jit);
  }
  return col/(1.0 + max(col.r, max(col.g, col.b))*0.15);
}
// Transonic vapour cone (Prandtl-Glauert condensation): near Mach 1 in humid air the pressure drop behind the shock
// condenses a shell of fog around the airframe. A sharp leading edge at the shock, a bell that flares and thins aft,
// streaky and flickering, lit by the sun and the sky.
vec3 vaporCone(vec3 col, vec3 ro, vec3 rd, float tmax, float jit){
  mat3 inv = transpose(uPlaneRot);
  vec3 lo = inv*(ro - uPlanePos), ld = inv*rd;
  float z0 = uVapor.y, R0 = uVapor.z, Lc = uVapor.w;
  vec3 c = vec3(0.0, 0.0, z0 + Lc*0.5); float br = length(vec2(R0*1.8, Lc*0.5 + 0.5));
  vec3 oc = lo - c; float b = dot(oc, ld), h = b*b - dot(oc, oc) + br*br;
  if (h <= 0.0) return col;
  h = sqrt(h); float t0 = max(-b - h, 0.0), t1 = min(-b + h, tmax);
  if (t1 <= t0) return col;
  vec3 sunB = inv*uSunDir;
  vec3 lit = uSunCol*(0.75 + 0.5*pow(max(dot(rd, uSunDir), 0.0), 6.0))*max(uSunDir.y + 0.1, 0.0)*1.3 + skyColor(vec3(0.0, 1.0, 0.0))*1.1 + vec3(0.02);
  float dt = (t1 - t0)/24.0, T = 1.0; vec3 L = vec3(0.0);
  for (int i = 0; i < 24; i++) {
    vec3 q = lo + ld*(t0 + (float(i) + jit)*dt);
    float z = q.z - z0;
    if (z < -0.4 || z > Lc) continue;
    float zn = max(z, 0.0)/Lc;
    float rc = R0*(1.0 + 0.55*zn);
    float r = length(q.xy);
    float ang = atan(q.y, q.x);
    float shell = exp(-pow((r - rc)/(0.16*rc), 2.0));
    float front = smoothstep(-0.35, 0.05, z);                        // sharp edge at the shock
    float aft = exp(-zn*2.6);
    float streak = 0.35 + 0.65*vnoise(vec2(ang*9.0, zn*3.0 - uTime*6.0))*(0.7 + 0.3*vnoise(vec2(ang*31.0, uTime*20.0)));
    float dens = uVapor.x*shell*front*aft*streak*streak*4.0;
    float a = 1.0 - exp(-dens*dt);
    float self = 0.75 + 0.25*clamp(dot(normalize(vec3(q.xy, 0.0) + 1e-4), sunB.xyz), -1.0, 1.0);   // sunny side brighter
    L += T*a*lit*self; T *= 1.0 - a;
    if (T < 0.02) break;
  }
  return col*T + L;
}
// cloaked skin: the world seen through the craft (already traced along the bent ray) with a faint glassy rim,
// a shimmer of the hexagonal emitter lattice and a bright wavefront where the cloak is still spreading
vec3 cloakSkin(vec3 world, vec3 n, vec3 rd, vec3 lp, float front){
  float fres = pow(1.0 - abs(dot(n, -rd)), 4.0);
  vec3 r = reflect(rd, n);
  vec2 hx = vec2(lp.x*3.0 + lp.z*1.5, lp.z*2.6 - lp.y*3.0);
  float lat = smoothstep(0.46, 0.5, max(abs(fract(hx.x) - 0.5), abs(fract(hx.y) - 0.5)));
  float shimmer = 0.5 + 0.5*sin(uTime*4.0 + lp.z*3.0 + lp.x*5.0);
  vec3 col = world*0.97 + skyColor(r)*fres*0.1;
  col += gColStripe*(lat*0.012*shimmer + fres*0.015);
  col += gColStripe*exp(-abs(front)*6.0)*1.5*step(front, 50.0);   // the wavefront of the cloak sweeping along the craft
  return col;
}
// ---------------------------------------------------------------- XR-11 weapons in the world
// Laser bolts: a white-hot core in a crimson sheath, glowing along the beam (closest approach of the view ray to
// each beam segment, cut by the scene depth). Dark-energy bombs: black spheres wrapped in crawling violet plasma
// with a halo. Detonations: an expanding shell of violet fire around a collapsing black core, a flat shock ring
// and arcing filaments; the core swallows the light behind it.
vec3 weaponsFx(vec3 col, vec3 ro, vec3 rd, float t){
  for (int i = 0; i < 16; i++) {
    if (i >= uFxBeams) break;
    vec3 a = uBeamA[i].xyz, b = uBeamB[i].xyz; float r = uBeamA[i].w, I = uBeamB[i].w;
    vec3 u = b - a; float L = length(u); u /= max(L, 1e-3);
    vec3 w0 = ro - a; float bb = dot(rd, u), dd = dot(rd, w0), ee = dot(u, w0), den = 1.0 - bb*bb;
    float sR = den > 1e-5 ? (bb*ee - dd)/den : 0.0, sB = den > 1e-5 ? (ee - bb*dd)/den : ee;
    sB = clamp(sB, 0.0, L); sR = max(dot(a + u*sB - ro, rd), 0.0);
    if (sR > t) continue;
    float d = length(ro + rd*sR - (a + u*sB));
    float core = exp(-d*d/(r*r*0.25)), halo = pow(r*r/(d*d + r*r), 1.6);
    float flick = 0.85 + 0.15*sin(uTime*90.0 + sB*0.3);
    col += (vec3(1.0, 0.9, 0.95)*core*6.0 + vec3(1.0, 0.08, 0.2)*halo*1.6)*I*flick;
  }
  for (int i = 0; i < 8; i++) {
    if (i >= uFxBombs) break;
    vec3 c = uBombs[i].xyz; float R = uBombs[i].w;
    vec3 oc = ro - c; float b = dot(oc, rd), h = b*b - dot(oc, oc) + R*R;
    float tc = -b; if (tc < 0.0) continue;
    float dmin = length(oc + rd*tc);
    if (h > 0.0 && -b - sqrt(h) < t) {
      vec3 n = normalize(oc + rd*(-b - sqrt(h)));
      float fres = pow(1.0 - abs(dot(n, rd)), 2.5);
      float vein = vnoise3(n*5.0 + vec3(uTime*1.7)) + 0.5*vnoise3(n*13.0 - vec3(uTime*2.9));
      col = vec3(0.003) + vec3(0.6, 0.18, 1.0)*(fres*3.0 + pow(smoothstep(0.8, 1.2, vein), 2.0)*5.0) + vec3(0.25, 0.75, 1.0)*pow(smoothstep(1.1, 1.35, vein), 3.0)*6.0;
    } else if (tc < t) col += vec3(0.5, 0.15, 1.0)*exp(-(dmin - R)/(R*0.7))*0.9;
  }
  for (int i = 0; i < 6; i++) {
    if (i >= uFxBlasts) break;
    vec3 c = uBlast[i].xyz; float R = uBlast[i].w, age = uBlastI[i].x, I = uBlastI[i].y;   // c: ground zero
    float g = 1.0 - (1.0 - age)*(1.0 - age)*(1.0 - age);   // fast early growth
    float rise = smoothstep(0.05, 1.0, age);               // the fireball lifts off into a rolling cap
    vec3 oc = ro - c; float b = dot(oc, rd);
    float tc = max(-b, 0.0), dmin = length(oc + rd*tc);
    // the first instant: a white-violet flash that swamps everything around it
    if (tc < t + R) col += vec3(1.0, 0.85, 1.0)*exp(-dmin*dmin/(R*R*0.5))*max(0.0, 1.0 - age*7.0)*10.0*I;
    // condensation dome: a thin white shell racing out ahead of the fireball in the first moments (far side first)
    float dome = max(0.0, 1.0 - age*4.0);
    if (dome > 0.0) {
      float Rs = R*(0.4 + 4.5*sqrt(age)), hs = b*b - dot(oc, oc) + Rs*Rs;
      if (hs > 0.0) {
        hs = sqrt(hs);
        for (int s = 1; s >= 0; s--) {
          float ts = s == 0 ? -b - hs : -b + hs;
          if (ts <= 0.0 || ts > t) continue;
          vec3 n = (ro + rd*ts - c)/Rs;
          float limb = pow(1.0 - abs(dot(n, rd)), 3.0), up = smoothstep(-0.05, 0.25, n.y);   // the lower half is underground
          col = col*(1.0 - 0.3*limb*dome*up) + vec3(0.9, 0.88, 1.0)*(0.06 + 1.5*limb)*dome*up*I;
        }
      }
    }
    // on the ground: the shock ring racing outward and the blasted ground glowing violet-white, then cooling
    if (abs(rd.y) > 1e-3) {
      float tr = (c.y + 1.0 - ro.y)/rd.y;
      if (tr > 0.0 && tr < t + 3.0) {
        float rr = length((ro + rd*tr - c).xz), ring = R*(0.6 + 5.0*sqrt(age));
        col += vec3(0.65, 0.3, 1.0)*exp(-pow((rr - ring)/(R*0.08), 2.0))*(1.0 - age)*(1.0 - age)*4.0*I;
        col += vec3(0.8, 0.45, 1.0)*exp(-rr*rr/(R*R*0.35))*exp(-age*3.5)*2.5*I;
      }
    }
    // the cloud: fireball -> torus cap on a stem, a volume with plasma emission that cools into dark smoke
    float Hc = R*(0.25*g + 2.8*rise);                                // cap height above ground zero
    float sb = R*(0.2 + 0.85*g)*(1.0 - 0.45*rise);                   // fireball radius
    float cRm = R*(0.1 + 0.8*rise), cRr = R*(0.38 + 0.12*rise);      // torus major / minor radius
    float stemOn = smoothstep(0.04, 0.25, age);
    vec3 bc = c + vec3(0.0, Hc*0.5, 0.0);
    float bR = max(Hc*0.5 + max(sb, cRr)*1.2, length(vec2(cRm + cRr*1.2, Hc*0.5 + cRr*1.2)));
    vec3 ob = ro - bc; float bb2 = dot(ob, rd), h = bb2*bb2 - dot(ob, ob) + bR*bR;
    if (h <= 0.0) continue;
    h = sqrt(h); float t0 = max(-bb2 - h, 0.0), t1 = min(-bb2 + h, t);
    if (t1 <= t0) continue;
    const int NS = 28;
    float dt = (t1 - t0)/float(NS), tr = 1.0; vec3 acc = vec3(0.0);
    float jit = fract(52.9829189*fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float life = smoothstep(1.0, 0.72, age);                         // the whole cloud thins out at the end
    for (int k = 0; k < NS; k++) {
      COST(3);
      vec3 q = ro + rd*(t0 + (float(k) + jit)*dt) - c;
      vec3 qc = q - vec3(0.0, Hc, 0.0);
      float ball = length(qc)/sb;
      float tor = length(vec2(length(qc.xz) - cRm, qc.y*1.3))/cRr;
      float sw = R*(0.12 + 0.14*clamp(q.y/max(Hc, 1.0), 0.0, 1.0))*stemOn;
      float stem = (q.y > -R*0.1 && q.y < Hc) ? length(q.xz)/max(sw, 1e-3) : 9.0;
      float x0 = min(min(ball, mix(9.0, tor, rise)), stem);
      if (x0 > 1.5) continue;
      // rolling turbulence: noise advected outward from the core and up the stem
      vec3 flow = qc/R*2.4 - normalize(qc + vec3(1e-3))*uTime*0.9 + vec3(0.0, -uTime*0.6, 0.0);
      float nz = vnoise3(flow) + 0.5*vnoise3(flow*2.3 + vec3(7.1)) + 0.25*vnoise3(flow*5.1 - vec3(3.3));
      float x = x0 + (nz - 0.875)*0.45;
      float dens = smoothstep(1.0, 0.5, x)*life;
      if (dens <= 0.0) continue;
      // temperature: everything is hot at first, later only the core of the cap and the lower stem still glow
      float temp = exp(-age*2.8)*(1.3 - 0.7*clamp(x, 0.0, 1.0)) + 0.4*exp(-age*1.4)*smoothstep(0.7, 0.1, x)*(0.6 + 0.6*nz);
      vec3 eCol = mix(vec3(0.3, 0.05, 0.85), vec3(0.75, 0.35, 1.0), smoothstep(0.15, 0.6, temp));
      eCol = mix(eCol, vec3(1.0, 0.92, 1.0), smoothstep(0.7, 1.25, temp));
      float fil = pow(clamp(1.0 - abs(vnoise3(q/R*4.0 + vec3(0.0, uTime*2.5, 0.0)) - 0.5)*9.0, 0.0, 1.0), 6.0)*exp(-age*2.0);
      vec3 e = (eCol*temp*temp*(0.5 + 0.7*nz)*14.0 + vec3(0.55, 0.9, 1.0)*fil*12.0)*dens*I;
      float sig = dens*(0.6 + 0.6*nz)*mix(0.5, 3.5, smoothstep(0.08, 0.55, age))*5.0;
      // smoke body: dim and cool, lit from above by the sky and from inside by what still burns
      vec3 smokeC = vec3(0.05, 0.045, 0.06)*(0.6 + 0.6*smoothstep(-R, R, qc.y)) + vec3(0.35, 0.12, 0.7)*temp*0.4;
      acc += tr*(e + smokeC*sig)*dt/R;
      tr *= exp(-sig*dt/R);
      if (tr < 0.01) break;
    }
    col = col*tr + acc;
  }
  return col;
}
