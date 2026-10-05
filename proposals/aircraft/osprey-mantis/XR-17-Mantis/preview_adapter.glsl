#version 330 core
out vec4 color; uniform vec3 eye,target;uniform vec2 resolution; uniform float focal; uniform vec3 cameraUp; uniform float ortho, renderMode;
// Preview-only primitive compatibility adapter, copied from Solace-Express a298771.
float sdBox(vec3 p, vec3 b){ vec3 q = abs(p)-b; return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0); }
float sdRoundBox(vec3 p, vec3 b, float r){ vec3 q = abs(p)-b+r; return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0) - r; }
float sdCapsule(vec3 p, vec3 a, vec3 b, float r){ vec3 pa=p-a, ba=b-a; float h=clamp(dot(pa,ba)/dot(ba,ba),0.0,1.0); return length(pa-ba*h)-r; }
float sdTorus(vec3 p, vec2 t){ return length(vec2(length(p.xz) - t.x, p.y)) - t.y; }
float sdRoundCone(vec3 p, vec3 a, vec3 b, float r1, float r2){
  vec3 ba = b - a; float l2 = dot(ba,ba); float rr = r1 - r2; float a2 = l2 - rr*rr; float il2 = 1.0/l2;
  vec3 pa = p - a; float y = dot(pa,ba); float z = y - l2; vec3 xv = pa*l2 - ba*y; float x2 = dot(xv,xv);
  float y2 = y*y*l2; float z2 = z*z*l2; float k = sign(rr)*rr*rr*x2;
  if( sign(z)*a2*z2 > k ) return sqrt(x2 + z2)*il2 - r2;
  if( sign(y)*a2*y2 < k ) return sqrt(x2 + y2)*il2 - r1;
  return (sqrt(x2*a2*il2)+y*rr)*il2 - r1;
}
float sdCylX(vec3 p, float r, float h){ vec2 d = abs(vec2(length(p.yz), p.x)) - vec2(r,h); return min(max(d.x,d.y),0.0) + length(max(d,0.0)); }
mat2 rot(float a){ float c=cos(a), s=sin(a); return mat2(c,-s,s,c); }


vec2 opU(vec2 a,vec2 b){return a.x<b.x?a:b;}
uniform vec4 gPS,gCtl; uniform float uCustom[8]; uniform int onlyPart;
bool partOn(int id){if(onlyPart>=0)return id==onlyPart;if(onlyPart==-2&&id>=40)return false; if(id==34&&uCustom[4]<.5)return false;if(id==35&&uCustom[5]<.5)return false;if(id==36&&uCustom[6]<.5)return false;return true;}
vec3 rigHinge(vec3 p,int id){vec3 o=vec3(0),a=vec3(1,0,0);float t=0.;
if(id==10){o=vec3(-0.85,-0.15,-3.6);a=normalize(vec3(1.0,0.0,0.0));t=-(0.22*gCtl.x+0);}
if(id==11){o=vec3(0.85,-0.15,-3.6);a=normalize(vec3(1.0,0.0,0.0));t=-(0.22*gCtl.x+0);}
if(id==12){o=vec3(-4.753838379808,-0.62,1.023220897856);a=normalize(vec3(-0.9363291776,0.0,-0.3511234416));t=-(-0.3*gCtl.y+0);}
if(id==13){o=vec3(4.753838379808,-0.62,1.023220897856);a=normalize(vec3(0.9363291776,0.0,-0.3511234416));t=-(-0.3*gCtl.y+0);}
if(id==14){o=vec3(-2.609644563104,-0.62,1.82729357912);a=normalize(vec3(-0.9363291776,0.0,-0.3511234416));t=-(-0.48*gPS.y+0);}
if(id==15){o=vec3(2.609644563104,-0.62,1.82729357912);a=normalize(vec3(0.9363291776,0.0,-0.3511234416));t=-(0.48*gPS.y+0);}
if(id==16){o=vec3(0.0,1.3875898,5.63044957);a=normalize(vec3(0.0,0.939693,-0.34202));t=-(0.3*gCtl.z+0);}
if(id==30){o=vec3(-0.59,-0.94,1.6);a=normalize(vec3(0.0,0.0,1.0));t=-(-1.38*uCustom[0]+0);}
if(id==31){o=vec3(0.59,-0.94,1.6);a=normalize(vec3(0.0,0.0,1.0));t=-(1.38*uCustom[0]+0);}
if(id==41){o=vec3(0.32,-0.23,-3.15);a=normalize(vec3(1.0,0.0,0.0));t=-(0.2*gCtl.x+0);}
if(id==42){o=vec3(-0.37,-0.2,-3.12);a=normalize(vec3(1.0,0.0,0.0));t=-(-0.6*gCtl.w+0.3);}
vec3 q=p-o;return o+q*cos(t)+cross(a,q)*sin(t)+a*dot(a,q)*(1.-cos(t));}
vec3 rigSlide(vec3 p,int id){
if(id==20)return p-normalize(vec3(0.0,-1.0,0.0))*(1.295*gPS.x+0);
if(id==21)return p-normalize(vec3(0.0,-1.0,0.0))*(1.295*gPS.x+0);
if(id==22)return p-normalize(vec3(0.0,-1.0,0.0))*(1.295*gPS.x+0);
if(id==23)return p-normalize(vec3(-1.0,0.0,0.0))*(0.64*gPS.x+0);
if(id==24)return p-normalize(vec3(1.0,0.0,0.0))*(0.64*gPS.x+0);
if(id==25)return p-normalize(vec3(0.0,0.0,1.0))*(1.06*gPS.x+0);
if(id==32)return p-normalize(vec3(0.0,-1.0,0.0))*(0.48*uCustom[1]+0);
if(id==33)return p-normalize(vec3(0.0,-1.0,0.0))*(0.48*uCustom[2]+0);
if(id==34)return p-normalize(vec3(0.0,-1.0,0.0))*(0.48*uCustom[1]+0);
if(id==35)return p-normalize(vec3(0.0,-1.0,0.0))*(0.48*uCustom[2]+0);
if(id==36)return p-normalize(vec3(0.0,-1.0,0.0))*(0.9*uCustom[3]+0);
if(id==43)return p-normalize(vec3(0.0,0.0,1.0))*(-0.06*gCtl.z+0);
if(id==44)return p-normalize(vec3(0.0,0.0,1.0))*(0.06*gCtl.z+0);
return p;}
vec3 rigStretch(vec3 p,int id){
if(id==26){vec3 o=vec3(-1.6,-0.35,0.6);vec3 a=normalize(vec3(0.0,-1.0,0.0));float sc=(1.295*gPS.x+0.25)/0.25;vec3 q=p-o;return p+a*dot(q,a)*(1./sc-1.);}
if(id==27){vec3 o=vec3(1.6,-0.35,0.6);vec3 a=normalize(vec3(0.0,-1.0,0.0));float sc=(1.295*gPS.x+0.25)/0.25;vec3 q=p-o;return p+a*dot(q,a)*(1./sc-1.);}
if(id==28){vec3 o=vec3(0.0,-0.35,-5.76);vec3 a=normalize(vec3(0.0,-1.0,0.0));float sc=(1.295*gPS.x+0.25)/0.25;vec3 q=p-o;return p+a*dot(q,a)*(1./sc-1.);}
return p;}
// XR-17 Mantis Tier B. Requires primitive library and provisional partOn/rig* contract.
// All transforms outside rig helpers are CONSTANT rigid placements, never animation.
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
vec2 mapCustom_Mantis(vec3 p) {
    vec2 r=vec2(1e5,1.0); vec3 q; float d;
    if(partOn(0)) {
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
    if(partOn(1)) {
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
    if(partOn(2)) {
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
    for(int id=10;id<=11;id++) if(partOn(id)) {
        q=rigHinge(p,id); float side=id==10?-1.:1.;
        q-=vec3(side*1.72,-.15,-3.60); q.x*=side;
        r=opU(r,vec2(.16*sdRoundCone(vec3(q.x,q.y/.16,q.z),vec3(-.50,0,0),vec3(.75,0,.05),.45,.30),q.x>.65?111.:110.));

    }
    for(int id=12;id<=15;id++) if(partOn(id)) {
        float side=(id==12||id==14)?-1.:1.; q=mantisWingFrame(rigHinge(p,id),side);
        float x=id<=13?1.16:-1.13; float h=id<=13?1.02:.79;
        d=max(mantisWingControlMask(q,x,h,.004),mantisWingSolid(rigHinge(p,id),side));
        r=opU(r,vec2(d,110));
    }
    if(partOn(16)) {
        q=rigHinge(p,16)-vec3(0,1.22,5.17);q.yz=vec2(.939693*q.y-.342020*q.z,.342020*q.y+.939693*q.z);
        d=max(mantisRudderMask(q,.004),mantisFinSolid(rigHinge(p,16)));
        r=opU(r,vec2(d,110));
    }
    for(int id=20;id<=22;id++) if(partOn(id)) {
        float x=id==20?-1.6:(id==21?1.6:0.);float z=id==22?-5.76:.6;
        q=rigSlide(p,id)-vec3(x,-.28,z);
        r=opU(r,vec2(sdCapsule(q,vec3(0,.10,0),vec3(0,-.20,0),.065),8));
        r=opU(r,vec2(sdCapsule(q,vec3(0,.18,0),vec3(0,-.25,.10),.037),60));
        r=opU(r,vec2(sdCylX(q-vec3(0,-.28,0),.26,.13),6));
        r=opU(r,vec2(sdCylX(q-vec3(0,-.28,0),.13,.137),8));
        r=opU(r,vec2(sdCapsule(q,vec3(-.15,-.28,0),vec3(-.15,.04,0),.035),8));
        r=opU(r,vec2(sdCapsule(q,vec3(.15,-.28,0),vec3(.15,.04,0),.035),8));
    }
    for(int id=26;id<=28;id++) if(partOn(id)) {
        float x=id==26?-1.6:(id==27?1.6:0.);float z=id==28?-5.76:.6;
        q=rigStretch(p,id)-vec3(x,-.475,z);
        r=opU(r,vec2(sdBox(q,vec3(.055,.125,.055)),8));
    }
    for(int id=23;id<=25;id++) if(partOn(id)) {
        float x=id==23?-1.6:(id==24?1.6:0.);float z=id==25?-5.76:.6;
        q=rigSlide(p,id)-vec3(x,-.84,z);
        r=opU(r,vec2(sdRoundBox(q,vec3(.318,.022,.528),.008),1));
    }
    for(int id=30;id<=31;id++) if(partOn(id)) {
        float side=id==30?-1.:1.;q=rigHinge(p,id)-vec3(side*.30,-.94,1.6);
        r=opU(r,vec2(sdRoundBox(q,vec3(.304,.024,1.164),.006),110));
        r=opU(r,vec2(sdRoundBox(q-vec3(0,-.021,0),vec3(.028,.008,.85),.005),111));
    }
    for(int id=32;id<=33;id++) if(partOn(id)) {
        float side=id==32?-1.:1.;q=rigSlide(p,id)-vec3(side*1.08,-.59,-1.85);
        r=opU(r,vec2(sdRoundBox(q,vec3(.10,.06,.88),.025),8));
        // Paired inboard guide rods stay captured in the housing roof at full deployment.
        // They share this registered carrier slide; their retracted upper ends hide in the hull.
        for(int k=-1;k<=1;k+=2) {
            float z=float(k)*.55;
            r=opU(r,vec2(sdCapsule(q,vec3(0,.035,z),vec3(-side*.16,.035,z),.026),8));
            r=opU(r,vec2(sdCapsule(q,vec3(-side*.16,.035,z),vec3(-side*.16,.53,z),.024),8));
        }
    }
    for(int id=34;id<=35;id++) if(partOn(id)) {
        float side=id==34?-1.:1.;q=rigSlide(p,id)-vec3(side*1.08,-.72,-1.85);
        r=opU(r,vec2(sdRoundCone(q,vec3(0,0,-.89),vec3(0,0,.66),.025,.12),110));
        r=opU(r,vec2(sdRoundBox(q-vec3(0,0,.50),vec3(.25,.025,.18),.018),111));
        r=opU(r,vec2(sdRoundBox(q-vec3(0,0,.50),vec3(.025,.20,.18),.018),110));
    }
    if(partOn(36)) {
        q=rigSlide(p,36)-vec3(0,-.70,1.6);
        r=opU(r,vec2(sdCapsule(q,vec3(0,0,-.63),vec3(0,0,.60),.16),110));
        r=opU(r,vec2(sdRoundBox(q-vec3(0,0,.56),vec3(.30,.035,.25),.025),111));
    }
    if(partOn(40)) {
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
    if(partOn(41)) {q=rigHinge(p,41);r=opU(r,vec2(sdCapsule(q,vec3(.32,-.23,-3.15),vec3(.32,.07,-3.15),.033),13));}
    if(partOn(42)) {q=rigHinge(p,42);r=opU(r,vec2(sdCapsule(q,vec3(-.37,-.20,-3.12),vec3(-.37,.04,-3.12),.037),13));}
    for(int id=43;id<=44;id++) if(partOn(id)) {q=rigSlide(p,id);float side=id==43?-1.:1.;r=opU(r,vec2(sdRoundBox(q-vec3(side*.17,-.28,-3.9),vec3(.10,.04,.16),.025),61));}
    return r;
}
// STATIC ILLUSTRATIVE MATERIALS ONLY. No live camera, telemetry or functional switches.
// Body-space p. Invoke ahead of host fallback for ids112..119.
float mantisLine(vec2 p,vec2 a,vec2 b,float w){vec2 q=p-a;vec2 d=b-a;return 1.-smoothstep(w,w+.002,length(q-d*clamp(dot(q,d)/dot(d,d),0.,1.)));}
float mantisGlyph(vec2 p,int c){
 // 3x5 static block lettering: C A M S T I R E F O 0 1 2
 if(any(lessThan(p,vec2(0)))||any(greaterThanEqual(p,vec2(3,5))))return 0.;
 int k=int(floor(p.x))+3*(4-int(floor(p.y)));int b=0;
 if(c==0)b=29263; // C
 if(c==1)b=23530; // A
 if(c==2)b=23549; // M
 if(c==3)b=31183; // S
 if(c==4)b=9367;  // T
 if(c==5)b=29847; // I
 if(c==6)b=23275; // R
 if(c==7)b=29391; // E
 if(c==8)b=4815;  // F
 if(c==9)b=31599; // O / zero
 if(c==10)b=29850;// one
 if(c==11)b=29671;// two
 return float((b>>k)&1);
}
float mantisWord(vec2 uv,int word){
 vec2 t=uv/.009;int col=int(floor(t.x/4.));vec2 q=vec2(mod(t.x,4.),t.y);int c=-1;
 if(word==0){if(col==0)c=0;if(col==1)c=1;if(col==2)c=2;}
 if(word==1){if(col==0)c=3;if(col==1)c=4;if(col==2)c=1;if(col==3)c=4;if(col==4)c=5;if(col==5)c=0;}
 if(word==2){if(col==0)c=6;if(col==1)c=7;if(col==2)c=8;}
 return c<0?0.:mantisGlyph(q,c);
}
vec3 mantisCabinMaterial(float m,vec3 p){
 vec3 amber=vec3(1.,.48,.08), graph=vec3(.035,.045,.054);
 if(m==113.)return graph*(.93+.07*step(.5,fract(p.z*65.)));
 if(m==114.)return vec3(.11,.125,.135)*(.92+.08*step(.5,fract(p.y*90.)));
 if(m==115.)return amber;
 if(m==116.)return vec3(.41,.29,.13)*(.85+.15*step(.5,fract(p.y*120.)));
 if(m==118.)return vec3(.009,.014,.017);
 if(m==119.)return vec3(.25,.29,.31);
 if(m==112.){
  bool front=p.z < -4.08;vec2 uv=front?vec2(p.x,p.y-.410):vec2(p.x<0.?-p.z-3.51:p.z+3.51,p.y-.365);
  vec2 sz=front?vec2(.410,.177):vec2(.370,.172);
  float grid=max(1.-smoothstep(.001,.002,abs(mod(uv.x+.035,.070)-.035)),1.-smoothstep(.001,.002,abs(mod(uv.y+.035,.070)-.035)));
  vec3 c=mix(vec3(.024,.073,.082),vec3(.050,.14,.15),grid*.6);
  float crosshair=max(mantisLine(uv,vec2(-.025,0),vec2(.025,0),.0015),mantisLine(uv,vec2(0,-.025),vec2(0,.025),.0015));
  float text=max(mantisWord(uv-vec2(-sz.x+.025,sz.y-.060),0),mantisWord(uv-vec2(-.104,-sz.y+.025),1));
  float datum=1.-smoothstep(.002,.004,abs(abs(uv.x)-sz.x+.02));datum*=step(abs(uv.y),.055);
  return mix(c,amber,max(text,max(crosshair,datum)));
 }
 if(m==117.){
  bool side=abs(p.x)>.42;vec2 uv=side?vec2(p.z+3.66,(abs(p.x)-.489)*1.7):vec2(p.x-sign(p.x)*.266,p.y-.07);
  if(side && p.x<0.)uv=-uv;
  float text=mantisWord(uv-vec2(-.095,.025),2);
  float bars=0.;for(int i=0;i<4;i++){float y=-.015-float(i)*.017;bars=max(bars,step(abs(uv.y-y),.003)*step(-.095,uv.x)*step(uv.x,.055-float(i)*.028));}
  return mix(vec3(.015,.035,.042),amber,max(text,bars));
 }
 return graph;
}

vec3 shade(float m,vec3 p){
 if(m>=112. && m<=119.)return mantisCabinMaterial(m,p);
 if(m==111.)return vec3(.95,.43,.055);if(m==110.||m==8.||m==60.)return vec3(.34,.39,.43);
 if(m==112.){float line=step(.96,fract(p.x*20.))*step(.96,fract(p.y*20.));return mix(vec3(.035,.17,.20),vec3(.2,.7,.8),line);}
 if(m==67.)return vec3(.07,.25,.28);
 if(m==6.||m==61.)return vec3(.025);if(m==112.||m==67.)return vec3(.06,.33,.42);
 if(m==17.)return vec3(.09,.14,.16);if(m==21.)return vec3(.14);if(m==18.)return p.x<0.?vec3(.8,.04,.02):vec3(.03,.65,.2);
 if(m==12.)return vec3(.13,.15,.16);return vec3(.065,.085,.105);}
void main(){
 vec2 uv=(gl_FragCoord.xy-.5*resolution)/resolution.y;
 vec3 w=normalize(target-eye),u=normalize(cross(w,cameraUp)),v=cross(u,w),rd=normalize(w*focal+u*uv.x+v*uv.y);
 vec3 rayEye=eye; if(ortho>0.){rayEye+=ortho*(u*uv.x+v*uv.y);rd=w;}
 float t=0.;vec2 hit=vec2(1e4);vec3 p;bool yes=false;
 for(int i=0;i<420;i++){p=rayEye+rd*t;hit=mapCustom_Mantis(p);if(hit.x<.0015){yes=true;break;}t+=max(.001,hit.x*.83);if(t>65.)break;}
 vec3 col=mix(vec3(.07,.085,.11),vec3(.21,.24,.28),gl_FragCoord.y/resolution.y);
 if(yes){vec2 e=vec2(.003,0);vec3 n=normalize(vec3(mapCustom_Mantis(p+e.xyy).x-mapCustom_Mantis(p-e.xyy).x,mapCustom_Mantis(p+e.yxy).x-mapCustom_Mantis(p-e.yxy).x,mapCustom_Mantis(p+e.yyx).x-mapCustom_Mantis(p-e.yyx).x));vec3 l=normalize(vec3(-.6,1,-.4));float ao=1.;for(int i=1;i<5;i++){float h=float(i)*.11;ao-=max(0.,h-mapCustom_Mantis(p+n*h).x)*.35;}col=shade(hit.y,p)*(.26+.74*max(0.,dot(n,l)))*ao;if(hit.y==115.||hit.y==112.||hit.y==117.)col=mix(col,shade(hit.y,p),.62);col+=vec3(.10)*pow(max(0.,dot(reflect(-l,n),-rd)),40.);if(renderMode==2.){col=vec3(.6)*(.65+.35*abs(dot(n,rd)));}}
 if(renderMode==1.) col=yes?vec3(.65)*(.7+.3*1.):vec3(.035);
 color=vec4(pow(col,vec3(1./2.2)),1.);}
