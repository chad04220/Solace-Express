//! kMantisSDF
//! The XR-10 Mantis airframe distance field (Codex's proposal, docs/design/additional-aircraft): a forward-swept
//! twin-jet systems demonstrator with all-moving canards, a sealed camera cockpit, an internal store bay with two
//! recessed missile cradles, and telescopic gear. Engine code 7 (mapPlaneBody dispatches here).
// Every moving part is placed by its rig helper from its rest geometry: a hinge (a rotation about a pivot and axis),
// a slide (a translation) or a stretch (the telescopic legs, scaled along their axis). The channels the game drives
// come in through gWr (uWr, or a bake's state): gWr[0] = bay doors, port cradle, starboard cradle, internal store
// (0..1 each); gWr[1] = port dart aboard, starboard dart aboard, store aboard (1 while carried).
// The rest of the shape is constant rigid placement: nothing here is time-dependent.
bool mtPart(int id){
  if (id == 34 && gWr[1].x < 0.5) return false;
  if (id == 35 && gWr[1].y < 0.5) return false;
  if (id == 36 && gWr[1].z < 0.5) return false;
  return true;
}
vec3 mtHinge(vec3 p, int id){
  vec3 o = vec3(0.0), a = vec3(1.0, 0.0, 0.0); float t = 0.0;
  if (id == 10) { o = vec3(-0.85, -0.15, -3.6); t = -(0.22*gCtl.x); }
  else if (id == 11) { o = vec3(0.85, -0.15, -3.6); t = -(0.22*gCtl.x); }
  else if (id == 12) { o = vec3(-4.753838379808, -0.62, 1.023220897856); a = vec3(-0.9363291776, 0.0, -0.3511234416); t = -(-0.3*gCtl.y); }
  else if (id == 13) { o = vec3(4.753838379808, -0.62, 1.023220897856); a = vec3(0.9363291776, 0.0, -0.3511234416); t = -(-0.3*gCtl.y); }
  else if (id == 14) { o = vec3(-2.609644563104, -0.62, 1.82729357912); a = vec3(-0.9363291776, 0.0, -0.3511234416); t = -(-0.48*gPS.y); }
  else if (id == 15) { o = vec3(2.609644563104, -0.62, 1.82729357912); a = vec3(0.9363291776, 0.0, -0.3511234416); t = -(0.48*gPS.y); }
  else if (id == 16) { o = vec3(0.0, 1.3875898, 5.63044957); a = vec3(0.0, 0.939693, -0.34202); t = -(0.3*gCtl.z); }
  else if (id == 30) { o = vec3(-0.59, -0.94, 1.6); a = vec3(0.0, 0.0, 1.0); t = -(-1.38*gWr[0].x); }
  else if (id == 31) { o = vec3(0.59, -0.94, 1.6); a = vec3(0.0, 0.0, 1.0); t = -(1.38*gWr[0].x); }
  else if (id == 41) { o = vec3(0.32, -0.23, -3.15); t = -(0.2*gCtl.x); }
  else if (id == 42) { o = vec3(-0.37, -0.2, -3.12); t = -(-0.6*gCtl.w + 0.3); }
  vec3 q = p - o;
  return o + q*cos(t) + cross(a, q)*sin(t) + a*dot(a, q)*(1.0 - cos(t));
}
vec3 mtSlide(vec3 p, int id){
  if (id >= 20 && id <= 22) return p - vec3(0.0, -1.0, 0.0)*(1.295*gPS.x);
  if (id == 23) return p - vec3(-1.0, 0.0, 0.0)*(0.64*gPS.x);
  if (id == 24) return p - vec3(1.0, 0.0, 0.0)*(0.64*gPS.x);
  if (id == 25) return p - vec3(0.0, 0.0, 1.0)*(1.06*gPS.x);
  if (id == 32 || id == 34) return p - vec3(0.0, -1.0, 0.0)*(0.48*gWr[0].y);
  if (id == 33 || id == 35) return p - vec3(0.0, -1.0, 0.0)*(0.48*gWr[0].z);
  if (id == 36) return p - vec3(0.0, -1.0, 0.0)*(0.9*gWr[0].w);
  if (id == 43) return p - vec3(0.0, 0.0, 1.0)*(-0.06*gCtl.z);
  if (id == 44) return p - vec3(0.0, 0.0, 1.0)*(0.06*gCtl.z);
  return p;
}
// the telescopic upper legs: 0.25 m at rest, 1.295 m longer with the gear down, stretched along -y from their origin
vec3 mtStretch(vec3 p, int id){
  vec3 o = id == 26 ? vec3(-1.6, -0.35, 0.6) : id == 27 ? vec3(1.6, -0.35, 0.6) : vec3(0.0, -0.35, -5.76);
  vec3 a = vec3(0.0, -1.0, 0.0); float sc = (1.295*gPS.x + 0.25)/0.25;
  vec3 q = p - o;
  return p + a*dot(q, a)*(1.0/sc - 1.0);
}
vec3 mantisWingFrame(vec3 p, float side) {
    vec3 q=p-vec3(side*3.45,-0.62,0.85);
    return vec3(0.936329*side*q.x-0.351123*q.z,q.y,0.351123*side*q.x+0.936329*q.z);
}
// Matched full-depth control partitions. These extend past the parent trailing edge;
// the parent SDF alone defines the exterior. 4 mm per-edge reveal is intentional.
// A coaxial rounded leading pocket retains clearance as each surface rotates.
float mantisWingControlMask(vec3 q, float x, float h, float inset) {
    float slab=sdBox(q-vec3(x,0,5.62),vec3(h,2.,5.));
    float hinge=sdCylX(q-vec3(x,0,.62),.20,h);
    return min(slab,hinge)+inset;
}
float mantisRudderMask(vec3 q,float inset) {
    float slab=sdBox(q-vec3(0,0,5.49),vec3(2.,.95,5.));
    float hinge=sdCylX((q-vec3(0,0,.49)).yxz,.10,.95);
    return min(slab,hinge)+inset;
}
// Fixed anisotropic flattening: min singular value 0.12 multiplies distance.
// Thus this is a conservative lower bound (Lipschitz <=1), unlike an unscaled ellipsoid estimator.
float mantisWingSolid(vec3 p,float side) {
    vec3 q=vec3(side*p.x,(p.y+.62)/.12,p.z);
    return .12*sdRoundCone(q,vec3(.80,0,1.65),vec3(5.88,0,-.22),1.45,.65);
}
float mantisFinSolid(vec3 p) {
    vec3 q=vec3(p.x/.09,p.y,p.z);
    return .09*sdRoundCone(q,vec3(0,.46,4.96),vec3(0,2.30,5.63),.93,.38);
}
float mantisHullSolid(vec3 p) {
    float d;
        d=sdRoundCone(p,vec3(0,.05,-7.93),vec3(0,.05,-3.9),.07,.98);
        d=min(d,sdRoundCone(p,vec3(0,.05,-3.9),vec3(0,.04,2.65),.98,1.05));
        d=min(d,sdRoundCone(p,vec3(0,.04,2.65),vec3(0,.28,7.93),1.05,.07));
    return d;
}
float mantisCabinCavity(vec3 p) { return sdRoundBox(p-vec3(0,.16,-3.35),vec3(.65,.66,1.05),.31); }
vec2 mapMantis(vec3 p) {
    vec2 r=vec2(1e5,1.0); vec3 q; float d;
    if(mtPart(0)) {
        d=mantisHullSolid(p);
        d=min(d,sdRoundBox(p-vec3(0,-.65,1.6),vec3(.73,.32,1.32),.06));
        // Permanent cockpit cavity, opaque roof remains: closed pressure-shell solid.
        d=max(d,-mantisCabinCavity(p));
        // Always-cut weapon bay and all gear bays (never dependent on door state).
        d=max(d,-sdRoundBox(p-vec3(0,-.88,1.6),vec3(.60,.45,1.16),.04));
        d=min(d,sdRoundBox(p-vec3(0,-.55,-5.76),vec3(.42,.31,.77),.16));
        d=max(d,-sdBox(p-vec3(0,-.74,-5.76),vec3(.31,.34,.52)));
        r=opU(r,vec2(d,1));
        for(int k=-1;k<=1;k+=2)
            r=opU(r,vec2(sdRoundBox(p-vec3(float(k)*.30,-.811,-5.23),vec3(.018,.018,1.06),.006),8));
        for(int s=-1;s<=1;s+=2) {
            float side=float(s);
            // Integral main-gear fairings and permanent wells.
            d=sdRoundBox(p-vec3(side*1.6,-.55,.6),vec3(.52,.31,.77),.16);
            d=max(d,-sdBox(p-vec3(side*1.6,-.74,.6),vec3(.31,.34,.52)));
            r=opU(r,vec2(d,1));
            // Fixed shutter runners retain the sliding panels through full travel.
            for(int k=-1;k<=1;k+=2)
                r=opU(r,vec2(sdRoundBox(p-vec3(side*1.92,-.811,.6+float(k)*.51),vec3(.65,.018,.018),.006),8));
            // Chines flow from nose to wing, actual round cones, no sheet geometry.
            r=opU(r,vec2(sdRoundCone(p,vec3(side*.33,-.12,-6.4),vec3(side*.88,-.20,-.3),.045,.14),110));
        }
    }
    if(mtPart(1)) {
        for(int s=-1;s<=1;s+=2) {
            q=mantisWingFrame(p,float(s));
            d=mantisWingSolid(p,float(s));
            // Flap and aileron clearances fixed into trailing edge.
            d=max(d,-mantisWingControlMask(q,-1.13,.79,0.));
            d=max(d,-mantisWingControlMask(q,1.16,1.02,0.));
            d=max(d,-sdBox(p-vec3(float(s)*1.6,-.74,.6),vec3(.31,.34,.52)));
            r=opU(r,vec2(d,(abs(q.z+.70)<.055 && abs(q.x+.4)<1.8)?111.:2.));
            // Amber datum is material only: no detached decorative geometry.
            r=opU(r,vec2(sdCapsule(p,vec3(float(s)*6.52,-.62,-.15),vec3(float(s)*6.52,-.62,.03),.024),18));
        }
        // Dorsal rear fin with separate rudder recess; rounded, swept lean.
        q=p-vec3(0,1.22,5.17); q.yz=vec2(.939693*q.y-.342020*q.z,.342020*q.y+.939693*q.z);
        d=mantisFinSolid(p);
        d=max(d,-mantisRudderMask(q,0.));
        r=opU(r,vec2(d,3));
    }
    if(mtPart(2)) {
        for(int s=-1;s<=1;s+=2) {
            q=p-vec3(float(s)*1.55,.05,3.18);
            d=sdRoundCone(q,vec3(0,0,-1.45),vec3(0,0,1.62),.48,.37);
            d=max(d,-sdCapsule(q,vec3(0,0,-1.56),vec3(0,0,-.95),.35));
            d=max(d,-sdCapsule(q,vec3(0,0,1.40),vec3(0,0,1.94),.27));
            r=opU(r,vec2(d,5));
            r=opU(r,vec2(sdTorus((q-vec3(0,0,-1.43)).xzy,vec2(.397,.035)),8));
            r=opU(r,vec2(sdCylX((q-vec3(0,0,-.95)).zyx,.34,.018),21));
            r=opU(r,vec2(sdCylX((q-vec3(0,0,1.40)).zyx,.265,.02),17));
            r=opU(r,vec2(sdRoundBox(p-vec3(float(s)*1.1,.03,3),vec3(.45,.12,.65),.09),110));
            // Missile recess housings, permanently open downward.
            q=p-vec3(float(s)*1.08,-.60,-1.85);
            d=sdRoundBox(q,vec3(.28,.25,1.10),.09);
            d=max(d,-sdRoundBox(q-vec3(0,-.15,0),vec3(.19,.20,1.03),.05));
            r=opU(r,vec2(d,110));
        }
        r=opU(r,vec2(sdCapsule(p,vec3(0,1.04,-1.6),vec3(0,1.35,-1.45),.024),8));
        r=opU(r,vec2(sdRoundBox(p-vec3(0,.25,-7.52),vec3(.15,.06,.15),.04),64));
    }
    for(int id=10;id<=11;id++) if(mtPart(id)) {
        q=mtHinge(p,id); float side=id==10?-1.:1.;
        q-=vec3(side*1.72,-.15,-3.60); q.x*=side;
        r=opU(r,vec2(.16*sdRoundCone(vec3(q.x,q.y/.16,q.z),vec3(-.50,0,0),vec3(.75,0,.05),.45,.30),q.x>.65?111.:110.));

    }
    for(int id=12;id<=15;id++) if(mtPart(id)) {
        float side=(id==12||id==14)?-1.:1.; q=mantisWingFrame(mtHinge(p,id),side);
        float x=id<=13?1.16:-1.13; float h=id<=13?1.02:.79;
        d=max(mantisWingControlMask(q,x,h,.004),mantisWingSolid(mtHinge(p,id),side));
        r=opU(r,vec2(d,110));
    }
    if(mtPart(16)) {
        q=mtHinge(p,16)-vec3(0,1.22,5.17);q.yz=vec2(.939693*q.y-.342020*q.z,.342020*q.y+.939693*q.z);
        d=max(mantisRudderMask(q,.004),mantisFinSolid(mtHinge(p,16)));
        r=opU(r,vec2(d,110));
    }
    for(int id=20;id<=22;id++) if(mtPart(id)) {
        float x=id==20?-1.6:(id==21?1.6:0.);float z=id==22?-5.76:.6;
        q=mtSlide(p,id)-vec3(x,-.28,z);
        r=opU(r,vec2(sdCapsule(q,vec3(0,.10,0),vec3(0,-.20,0),.065),8));
        r=opU(r,vec2(sdCapsule(q,vec3(0,.18,0),vec3(0,-.25,.10),.037),60));
        r=opU(r,vec2(sdCylX(q-vec3(0,-.28,0),.26,.13),6));
        r=opU(r,vec2(sdCylX(q-vec3(0,-.28,0),.13,.137),8));
        r=opU(r,vec2(sdCapsule(q,vec3(-.15,-.28,0),vec3(-.15,.04,0),.035),8));
        r=opU(r,vec2(sdCapsule(q,vec3(.15,-.28,0),vec3(.15,.04,0),.035),8));
    }
    for(int id=26;id<=28;id++) if(mtPart(id)) {
        float x=id==26?-1.6:(id==27?1.6:0.);float z=id==28?-5.76:.6;
        q=mtStretch(p,id)-vec3(x,-.475,z);
        r=opU(r,vec2(sdBox(q,vec3(.055,.125,.055)),8));
    }
    for(int id=23;id<=25;id++) if(mtPart(id)) {
        float x=id==23?-1.6:(id==24?1.6:0.);float z=id==25?-5.76:.6;
        q=mtSlide(p,id)-vec3(x,-.84,z);
        r=opU(r,vec2(sdRoundBox(q,vec3(.318,.022,.528),.008),1));
    }
    for(int id=30;id<=31;id++) if(mtPart(id)) {
        float side=id==30?-1.:1.;q=mtHinge(p,id)-vec3(side*.30,-.94,1.6);
        r=opU(r,vec2(sdRoundBox(q,vec3(.304,.024,1.164),.006),110));
        r=opU(r,vec2(sdRoundBox(q-vec3(0,-.021,0),vec3(.028,.008,.85),.005),111));
    }
    for(int id=32;id<=33;id++) if(mtPart(id)) {
        float side=id==32?-1.:1.;q=mtSlide(p,id)-vec3(side*1.08,-.59,-1.85);
        r=opU(r,vec2(sdRoundBox(q,vec3(.10,.06,.88),.025),8));
        // Paired inboard guide rods stay captured in the housing roof at full deployment.
        // They share this registered carrier slide; their retracted upper ends hide in the hull.
        for(int k=-1;k<=1;k+=2) {
            float z=float(k)*.55;
            r=opU(r,vec2(sdCapsule(q,vec3(0,.035,z),vec3(-side*.16,.035,z),.026),8));
            r=opU(r,vec2(sdCapsule(q,vec3(-side*.16,.035,z),vec3(-side*.16,.53,z),.024),8));
        }
    }
    for(int id=34;id<=35;id++) if(mtPart(id)) {
        float side=id==34?-1.:1.;q=mtSlide(p,id)-vec3(side*1.08,-.72,-1.85);
        r=opU(r,vec2(sdRoundCone(q,vec3(0,0,-.89),vec3(0,0,.66),.025,.12),110));
        r=opU(r,vec2(sdRoundBox(q-vec3(0,0,.50),vec3(.25,.025,.18),.018),111));
        r=opU(r,vec2(sdRoundBox(q-vec3(0,0,.50),vec3(.025,.20,.18),.018),110));
    }
    if(mtPart(36)) {
        q=mtSlide(p,36)-vec3(0,-.70,1.6);
        r=opU(r,vec2(sdCapsule(q,vec3(0,0,-.63),vec3(0,0,.60),.16),110));
        r=opU(r,vec2(sdRoundBox(q-vec3(0,0,.56),vec3(.30,.035,.25),.025),111));
    }
    bool cab = gPS.w > 0.5 && mantisCabinCavity(p) < 0.4;   // the cabin: sealed, so only from the seat, and only near it
    if(cab && mtPart(40)) {
        // MANTIS angular graphite/amber research station. All details static.
        // floor cassette
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,-0.385000,-3.350000),vec3(0.380000,0.025000,0.700000),0.008000),113.0));
        // seat floor rail
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.205000,-0.320000,-2.990000),vec3(0.022000,0.045000,0.280000),0.008000),119.0));
        // seat floor rail
        r=opU(r,vec2(sdRoundBox(p-vec3(0.205000,-0.320000,-2.990000),vec3(0.022000,0.045000,0.280000),0.008000),119.0));
        // seat pan carbon shell
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,-0.205000,-2.980000),vec3(0.275000,0.045000,0.300000),0.008000),113.0));
        // seat pan cushion
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,-0.140000,-2.980000),vec3(0.235000,0.055000,0.265000),0.025000),114.0));
        // seat back shell
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.175000,-2.615000),vec3(0.280000,0.320000,0.055000),0.008000),113.0));
        // seat back pad
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.175000,-2.680000),vec3(0.218000,0.300000,0.040000),0.018000),114.0));
        // lumbar side bolster
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.250000,0.040000,-2.755000),vec3(0.035000,0.175000,0.090000),0.016000),114.0));
        // pan side bolster
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.245000,-0.100000,-2.990000),vec3(0.022000,0.055000,0.230000),0.015000),114.0));
        // shoulder harness
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.100000,0.205000,-2.726000),vec3(0.026000,0.265000,0.010000),0.004000),116.0));
        // harness lower diagonal
        r=opU(r,vec2(sdCapsule(p,vec3(-0.100000,-0.055000,-2.726000),vec3(-0.055000,-0.110000,-2.830000),0.014000),116.0));
        // lap webbing
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.115000,-0.075000,-2.860000),vec3(0.090000,0.010000,0.032000),0.004000),116.0));
        // lumbar side bolster
        r=opU(r,vec2(sdRoundBox(p-vec3(0.250000,0.040000,-2.755000),vec3(0.035000,0.175000,0.090000),0.016000),114.0));
        // pan side bolster
        r=opU(r,vec2(sdRoundBox(p-vec3(0.245000,-0.100000,-2.990000),vec3(0.022000,0.055000,0.230000),0.015000),114.0));
        // shoulder harness
        r=opU(r,vec2(sdRoundBox(p-vec3(0.100000,0.205000,-2.726000),vec3(0.026000,0.265000,0.010000),0.004000),116.0));
        // harness lower diagonal
        r=opU(r,vec2(sdCapsule(p,vec3(0.100000,-0.055000,-2.726000),vec3(0.055000,-0.110000,-2.830000),0.014000),116.0));
        // lap webbing
        r=opU(r,vec2(sdRoundBox(p-vec3(0.115000,-0.075000,-2.860000),vec3(0.090000,0.010000,0.032000),0.004000),116.0));
        // harness release buckle
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,-0.070000,-2.840000),vec3(0.036000,0.018000,0.040000),0.008000),119.0));
        // buckle release face
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,-0.050000,-2.840000),vec3(0.025000,0.009000,0.027000),0.004000),111.0));
        // headrest shell
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.562000,-2.690000),vec3(0.178000,0.100000,0.065000),0.008000),113.0));
        // headrest cushion
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.562000,-2.762000),vec3(0.150000,0.083000,0.022000),0.014000),114.0));
        // headrest datum
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.075000,0.580000,-2.788000),vec3(0.013000,0.045000,0.009000),0.004000),115.0));
        // headrest datum
        r=opU(r,vec2(sdRoundBox(p-vec3(0.075000,0.580000,-2.788000),vec3(0.013000,0.045000,0.009000),0.004000),115.0));
        // pitch fixed pivot socket
        r=opU(r,vec2(sdRoundBox(p-vec3(0.320000,-0.315000,-3.150000),vec3(0.050000,0.060000,0.055000),0.008000),113.0));
        // throttle fixed pivot socket
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.370000,-0.302500,-3.120000),vec3(0.050000,0.072500,0.055000),0.008000),113.0));
        // pedal fixed slider rail
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.170000,-0.335000,-3.900000),vec3(0.030000,0.022000,0.240000),0.008000),119.0));
        // pedal fixed slider rail
        r=opU(r,vec2(sdRoundBox(p-vec3(0.170000,-0.335000,-3.900000),vec3(0.030000,0.022000,0.240000),0.008000),119.0));
        // side avionics console
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.488000,-0.160000,-3.490000),vec3(0.067000,0.070000,0.370000),0.008000),113.0));
        // side service touch slab
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.489000,-0.080000,-3.660000),vec3(0.049000,0.012000,0.130000),0.008000),117.0));
        // static test key
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.489000,-0.075000,-3.400000),vec3(0.029000,0.015000,0.019000),0.005000),119.0));
        // static test key
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.489000,-0.075000,-3.340000),vec3(0.029000,0.015000,0.019000),0.005000),119.0));
        // static test key
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.489000,-0.075000,-3.280000),vec3(0.029000,0.015000,0.019000),0.005000),119.0));
        // console amber task strip
        r=opU(r,vec2(sdCapsule(p,vec3(-0.420000,-0.095000,-3.830000),vec3(-0.420000,-0.095000,-3.490000),0.010000),115.0));
        // rear vent housing
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.384000,0.120000,-2.660000),vec3(0.065000,0.160000,0.040000),0.008000),113.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.384000,-0.005000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.384000,0.045000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.384000,0.095000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.384000,0.145000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.384000,0.195000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.384000,0.245000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // side structural longeron
        r=opU(r,vec2(sdCapsule(p,vec3(-0.552000,0.095000,-3.970000),vec3(-0.552000,0.095000,-2.940000),0.018000),119.0));
        // side avionics console
        r=opU(r,vec2(sdRoundBox(p-vec3(0.488000,-0.160000,-3.490000),vec3(0.067000,0.070000,0.370000),0.008000),113.0));
        // side service touch slab
        r=opU(r,vec2(sdRoundBox(p-vec3(0.489000,-0.080000,-3.660000),vec3(0.049000,0.012000,0.130000),0.008000),117.0));
        // static test key
        r=opU(r,vec2(sdRoundBox(p-vec3(0.489000,-0.075000,-3.400000),vec3(0.029000,0.015000,0.019000),0.005000),119.0));
        // static test key
        r=opU(r,vec2(sdRoundBox(p-vec3(0.489000,-0.075000,-3.340000),vec3(0.029000,0.015000,0.019000),0.005000),119.0));
        // static test key
        r=opU(r,vec2(sdRoundBox(p-vec3(0.489000,-0.075000,-3.280000),vec3(0.029000,0.015000,0.019000),0.005000),119.0));
        // console amber task strip
        r=opU(r,vec2(sdCapsule(p,vec3(0.420000,-0.095000,-3.830000),vec3(0.420000,-0.095000,-3.490000),0.010000),115.0));
        // rear vent housing
        r=opU(r,vec2(sdRoundBox(p-vec3(0.384000,0.120000,-2.660000),vec3(0.065000,0.160000,0.040000),0.008000),113.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(0.384000,-0.005000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(0.384000,0.045000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(0.384000,0.095000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(0.384000,0.145000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(0.384000,0.195000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(0.384000,0.245000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // side structural longeron
        r=opU(r,vec2(sdCapsule(p,vec3(0.552000,0.095000,-3.970000),vec3(0.552000,0.095000,-2.940000),0.018000),119.0));
        // arch leg
        r=opU(r,vec2(sdCapsule(p,vec3(-0.555000,0.060000,-3.020000),vec3(-0.555000,0.420000,-3.020000),0.020000),113.0));
        // arch chamfer
        r=opU(r,vec2(sdCapsule(p,vec3(-0.555000,0.420000,-3.020000),vec3(-0.365000,0.675000,-3.020000),0.020000),113.0));
        // arch leg
        r=opU(r,vec2(sdCapsule(p,vec3(0.555000,0.060000,-3.020000),vec3(0.555000,0.420000,-3.020000),0.020000),113.0));
        // arch chamfer
        r=opU(r,vec2(sdCapsule(p,vec3(0.555000,0.420000,-3.020000),vec3(0.365000,0.675000,-3.020000),0.020000),113.0));
        // arch crown
        r=opU(r,vec2(sdCapsule(p,vec3(-0.365000,0.675000,-3.020000),vec3(0.365000,0.675000,-3.020000),0.020000),113.0));
        // ceiling task housing
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.215000,0.731000,-3.580000),vec3(0.070000,0.020000,0.200000),0.008000),113.0));
        // ceiling task diffuser
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.215000,0.706000,-3.580000),vec3(0.045000,0.010000,0.170000),0.005000),115.0));
        // ceiling task housing
        r=opU(r,vec2(sdRoundBox(p-vec3(0.215000,0.731000,-3.580000),vec3(0.070000,0.020000,0.200000),0.008000),113.0));
        // ceiling task diffuser
        r=opU(r,vec2(sdRoundBox(p-vec3(0.215000,0.706000,-3.580000),vec3(0.045000,0.010000,0.170000),0.005000),115.0));
        // instrument blade
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.050000,-4.055000),vec3(0.435000,0.175000,0.045000),0.008000),113.0));
        // instrument surround
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.266000,0.070000,-4.000000),vec3(0.140000,0.115000,0.025000),0.008000),119.0));
        // instrument face
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.266000,0.070000,-3.970000),vec3(0.118000,0.093000,0.012000),0.008000),117.0));
        // static switch stem
        r=opU(r,vec2(sdCapsule(p,vec3(-0.180000,-0.100000,-3.997000),vec3(-0.180000,-0.100000,-3.967000),0.011000),119.0));
        // static switch stem
        r=opU(r,vec2(sdCapsule(p,vec3(-0.250000,-0.100000,-3.997000),vec3(-0.250000,-0.100000,-3.967000),0.011000),119.0));
        // static switch stem
        r=opU(r,vec2(sdCapsule(p,vec3(-0.320000,-0.100000,-3.997000),vec3(-0.320000,-0.100000,-3.967000),0.011000),119.0));
        // switch guard
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.270000,-0.145000,-3.980000),vec3(0.120000,0.010000,0.025000),0.004000),111.0));
        // instrument surround
        r=opU(r,vec2(sdRoundBox(p-vec3(0.266000,0.070000,-4.000000),vec3(0.140000,0.115000,0.025000),0.008000),119.0));
        // instrument face
        r=opU(r,vec2(sdRoundBox(p-vec3(0.266000,0.070000,-3.970000),vec3(0.118000,0.093000,0.012000),0.008000),117.0));
        // static switch stem
        r=opU(r,vec2(sdCapsule(p,vec3(0.180000,-0.100000,-3.997000),vec3(0.180000,-0.100000,-3.967000),0.011000),119.0));
        // static switch stem
        r=opU(r,vec2(sdCapsule(p,vec3(0.250000,-0.100000,-3.997000),vec3(0.250000,-0.100000,-3.967000),0.011000),119.0));
        // static switch stem
        r=opU(r,vec2(sdCapsule(p,vec3(0.320000,-0.100000,-3.997000),vec3(0.320000,-0.100000,-3.967000),0.011000),119.0));
        // switch guard
        r=opU(r,vec2(sdRoundBox(p-vec3(0.270000,-0.145000,-3.980000),vec3(0.120000,0.010000,0.025000),0.004000),111.0));
        // center systems panel
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.050000,-3.990000),vec3(0.082000,0.112000,0.027000),0.008000),113.0));
        // center annunciator
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,-0.025000,-3.953000),vec3(0.051000,0.012000,0.011000),0.004000),115.0));
        // center annunciator
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.025000,-3.953000),vec3(0.051000,0.012000,0.011000),0.004000),115.0));
        // center annunciator
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.075000,-3.953000),vec3(0.051000,0.012000,0.011000),0.004000),115.0));
        // center annunciator
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.125000,-3.953000),vec3(0.051000,0.012000,0.011000),0.004000),115.0));
        // front camera slab
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.410000,-4.135000),vec3(0.410000,0.177000,0.012000),0.006000),112.0));
        // front pane side frame
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.428000,0.410000,-4.128000),vec3(0.012000,0.190000,0.023000),0.004000),119.0));
        // front pane side frame
        r=opU(r,vec2(sdRoundBox(p-vec3(0.428000,0.410000,-4.128000),vec3(0.012000,0.190000,0.023000),0.004000),119.0));
        // front pane horizontal frame
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.215000,-4.128000),vec3(0.436000,0.010000,0.023000),0.004000),119.0));
        // front pane horizontal frame
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.605000,-4.128000),vec3(0.436000,0.010000,0.023000),0.004000),119.0));
        // side camera slab
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.559000,0.365000,-3.510000),vec3(0.012000,0.172000,0.370000),0.006000),112.0));
        // side pane end frame
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.551000,0.365000,-3.896000),vec3(0.023000,0.185000,0.010000),0.004000),119.0));
        // side pane end frame
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.551000,0.365000,-3.124000),vec3(0.023000,0.185000,0.010000),0.004000),119.0));
        // side pane horizontal frame
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.551000,0.176000,-3.510000),vec3(0.023000,0.011000,0.390000),0.004000),119.0));
        // side pane horizontal frame
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.551000,0.554000,-3.510000),vec3(0.023000,0.011000,0.390000),0.004000),119.0));
        // side camera slab
        r=opU(r,vec2(sdRoundBox(p-vec3(0.559000,0.365000,-3.510000),vec3(0.012000,0.172000,0.370000),0.006000),112.0));
        // side pane end frame
        r=opU(r,vec2(sdRoundBox(p-vec3(0.551000,0.365000,-3.896000),vec3(0.023000,0.185000,0.010000),0.004000),119.0));
        // side pane end frame
        r=opU(r,vec2(sdRoundBox(p-vec3(0.551000,0.365000,-3.124000),vec3(0.023000,0.185000,0.010000),0.004000),119.0));
        // side pane horizontal frame
        r=opU(r,vec2(sdRoundBox(p-vec3(0.551000,0.176000,-3.510000),vec3(0.023000,0.011000,0.390000),0.004000),119.0));
        // side pane horizontal frame
        r=opU(r,vec2(sdRoundBox(p-vec3(0.551000,0.554000,-3.510000),vec3(0.023000,0.011000,0.390000),0.004000),119.0));
        // ceiling fixture carrier
        r=opU(r,vec2(sdCapsule(p,vec3(-0.215000,0.715000,-3.580000),vec3(-0.215000,0.675000,-3.020000),0.014000),113.0));
        // console floor outrigger
        r=opU(r,vec2(sdCapsule(p,vec3(-0.470000,-0.200000,-3.750000),vec3(-0.340000,-0.365000,-3.750000),0.018000),113.0));
        // sill console support
        r=opU(r,vec2(sdCapsule(p,vec3(-0.550000,-0.160000,-3.750000),vec3(-0.552000,0.095000,-3.750000),0.016000),113.0));
        // console floor outrigger
        r=opU(r,vec2(sdCapsule(p,vec3(-0.470000,-0.200000,-3.350000),vec3(-0.340000,-0.365000,-3.350000),0.018000),113.0));
        // sill console support
        r=opU(r,vec2(sdCapsule(p,vec3(-0.550000,-0.160000,-3.350000),vec3(-0.552000,0.095000,-3.350000),0.016000),113.0));
        // pane sill bracket
        r=opU(r,vec2(sdCapsule(p,vec3(-0.551000,0.170000,-3.800000),vec3(-0.552000,0.095000,-3.800000),0.015000),119.0));
        // pane sill bracket
        r=opU(r,vec2(sdCapsule(p,vec3(-0.551000,0.170000,-3.200000),vec3(-0.552000,0.095000,-3.200000),0.015000),119.0));
        // instrument floor support
        r=opU(r,vec2(sdCapsule(p,vec3(-0.300000,-0.100000,-4.020000),vec3(-0.300000,-0.365000,-3.980000),0.018000),113.0));
        // vent seat bracket
        r=opU(r,vec2(sdCapsule(p,vec3(-0.250000,0.120000,-2.650000),vec3(-0.380000,0.120000,-2.650000),0.016000),113.0));
        // ceiling fixture carrier
        r=opU(r,vec2(sdCapsule(p,vec3(0.215000,0.715000,-3.580000),vec3(0.215000,0.675000,-3.020000),0.014000),113.0));
        // console floor outrigger
        r=opU(r,vec2(sdCapsule(p,vec3(0.470000,-0.200000,-3.750000),vec3(0.340000,-0.365000,-3.750000),0.018000),113.0));
        // sill console support
        r=opU(r,vec2(sdCapsule(p,vec3(0.550000,-0.160000,-3.750000),vec3(0.552000,0.095000,-3.750000),0.016000),113.0));
        // console floor outrigger
        r=opU(r,vec2(sdCapsule(p,vec3(0.470000,-0.200000,-3.350000),vec3(0.340000,-0.365000,-3.350000),0.018000),113.0));
        // sill console support
        r=opU(r,vec2(sdCapsule(p,vec3(0.550000,-0.160000,-3.350000),vec3(0.552000,0.095000,-3.350000),0.016000),113.0));
        // pane sill bracket
        r=opU(r,vec2(sdCapsule(p,vec3(0.551000,0.170000,-3.800000),vec3(0.552000,0.095000,-3.800000),0.015000),119.0));
        // pane sill bracket
        r=opU(r,vec2(sdCapsule(p,vec3(0.551000,0.170000,-3.200000),vec3(0.552000,0.095000,-3.200000),0.015000),119.0));
        // instrument floor support
        r=opU(r,vec2(sdCapsule(p,vec3(0.300000,-0.100000,-4.020000),vec3(0.300000,-0.365000,-3.980000),0.018000),113.0));
        // vent seat bracket
        r=opU(r,vec2(sdCapsule(p,vec3(0.250000,0.120000,-2.650000),vec3(0.380000,0.120000,-2.650000),0.016000),113.0));
    }
    if(cab && mtPart(41)) {q=mtHinge(p,41);r=opU(r,vec2(sdCapsule(q,vec3(.32,-.23,-3.15),vec3(.32,.07,-3.15),.033),13));}
    if(cab && mtPart(42)) {q=mtHinge(p,42);r=opU(r,vec2(sdCapsule(q,vec3(-.37,-.20,-3.12),vec3(-.37,.04,-3.12),.037),13));}
    for(int id=43;id<=44;id++) if(cab && mtPart(id)) {q=mtSlide(p,id);float side=id==43?-1.:1.;r=opU(r,vec2(sdRoundBox(q-vec3(side*.17,-.28,-3.9),vec3(.10,.04,.16),.025),61));}
    return r;
}
