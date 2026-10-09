//! kPlaneSDF
//! The aircraft distance fields: light aircraft and airliners from the packed model (mapPlaneBody), the XR-30 (mapJet) and
//! its sealed cockpit (mapJetCockpit), and the light fixtures (mapPlane).
// NV_SAFE_GEAR: the airframe's field without the retractable gear (its bays, fairings, doors and legs, the jets' too),
// built only for a driver whose compiler fails on the whole (renderer.cpp linkProgramCached)
#ifdef NV_SAFE_GEAR
const bool GEAR_FIELD = false;
#else
const bool GEAR_FIELD = true;
#endif
// Landing-gear bay: with the gear out (open > 0) a dark well is cut into the skin above the opening (c: centre of the
// opening, h: half width / half length, depth upwards) and two doors, hinged along the bay's long edges, swing down.
// Closed, the skin is untouched.
// A well cut into the airframe (box: its distance; w: height above its opening): it stays inside the structure - a 3 cm
// skin is left wherever it would come closer than that to another surface (the far side of a thin wing) - except across
// the opening itself, its lowest 4 cm. (Clamped everywhere, as it was, the opening kept its skin too: the wells were
// sealed voids behind open doors.)
// Swift matched initial supported body; metres, unchanged exterior.
// Requires existing sdRoundBox,sdBox,sdCapsule,smin,rot2 helpers.
// Explicit call gate gModelId==7. f is the unchanged world fuselage SDF.
// rd-box halfsize INCLUDES round radius. Headrest/posts remain disabled.
vec3 swiftSeatPanCenter(float side){return vec3(side*.250,-.170,-1.350);}
vec3 swiftSeatBackCenter(float side){return vec3(side*.250,.08866339043319735,-1.147791994999997);}
float swiftSeatPadded(vec3 p,float side){
  vec3 q=p-swiftSeatPanCenter(side);
  float d=sdRoundBox(q,vec3(.180,.055,.240),.050);
  d=smin(d,sdCapsule(q,vec3(-.125,.030,-.220),vec3(.125,.030,-.220),.045),.012);
  // Complete pan; no notch. Control clearance belongs to combined study.
  vec3 b=p-swiftSeatBackCenter(side);b.yz=rot2(b.yz,-.180);
  float bw=mix(.180,.145,smoothstep(-.140,.280,b.y));
  float back=sdRoundBox(b,vec3(bw,.360,.050),.045);
  // Original padded bolster retained;3.27mm torso contact is explicit soft contact.
  float bolster=sdRoundBox(vec3(abs(b.x)-bw+.025,b.y+.050,b.z+.030),vec3(.030,.260,.070),.030);
  back=smin(back,bolster,.025);
  return min(d,back);
}
float swiftSeatRailSupports(vec3 p,float side,float f){
  vec3 q=p-vec3(side*.250,0,-1.400);
  const float railY=-0.34843320515193255;
  const float postBase=-0.33443320515193253;
  const float panBase=-.225;
  float hh=(panBase-postBase)*.5;
  float d=sdRoundBox(vec3(abs(q.x)-.126,q.y-railY,q.z),vec3(.012,.014,.340),.004);
  d=min(d,sdRoundBox(vec3(abs(q.x)-.126,q.y-(postBase+hh),q.z-.050),vec3(.013,hh,.016),.003));
  // Essential: raw outer rail penetrates inner skin11.94mm. Mounting contact at cut is intentional.
  return max(d,f+.060);
}
float swiftHeelPlate(vec3 p,float x){
  return sdRoundBox(p-vec3(x,-0.2776539601856226,-1.7912581077357488),vec3(.055,.010,.080),.005);
}
float swiftHeelPost(vec3 p,float x,float f){
  float floorY=abs(x)>.250?-0.3735537019732611:-0.47636591520838556;
  float d=sdCapsule(p,vec3(x,floorY-.004,-1.7912581077357488),vec3(x,-0.2876539601856226,-1.7912581077357488),.012);
  return max(d,f+.060); // Raw lower caps fail14.47mm; this conforming cut is mandatory.
}
float swiftDrapedShoulderWeb(vec3 p,float side){
  vec3 q=p-vec3(side*.250,.05866339043319735,-1.2077919949999971);
  q.x=abs(q.x)-.110;q.yz=rot2(q.yz,-.180);
  return sdBox(q,vec3(.022,.330,.004));
}
// Optional conservative forward seat-draped lap belt/buckle resolution.
// Do NOT retain original rigid buckle relative Z-.055: it intersects pelvis.
// This is not a fitted worn restraint; classify/display it as draped seat furniture.
float swiftDrapedLapWeb(vec3 p,float side){return sdBox(p-vec3(side*.250,-.100,-1.511),vec3(.170,.022,.004));}
float swiftDrapedBuckle(vec3 p,float side){return sdRoundBox(p-vec3(side*.250,-.100,-1.516),vec3(.030,.020,.006),.004);}

// Swift bearing/post and bored push-pull support, inside the unchanged shell.
float swiftYokeSupport(vec3 p,float side,float f){
 vec3 c=vec3(side*.250,.270,-1.580);vec3 q=p-c;
 float bore=length(q.xy)-.015;
 float post=sdRoundBox(p-vec3(side*.268,-.1475,-1.580),vec3(.005,.4025,.020),.002);
 post=max(max(post,-bore),f+.060);
 float bearing=max(max(length(q.xy)-.024,-bore),abs(q.z)-.014);
 return min(post,bearing);
}
float swiftPowerFurniture(vec3 p){
 float cap=sdRoundBox(p-vec3(0,.070,-1.855),vec3(.075,.020,.025),.006);
 cap=max(cap,-(length(p.xy-vec2(-.040,.070))-.008));
 cap=max(cap,-(length(p.xy-vec2(.040,.070))-.007));
 return min(cap,sdRoundBox(p-vec3(0,-.040,-1.855),vec3(.045,.095,.025),.006));
}
// Bushmaster local prototype: matched seat/heel supports and articulated control mounts.
vec3 bushSeatPanCenter(float side){return vec3(side*.250,-.110,-1.200);}
vec3 bushSeatBackCenter(float side){return vec3(side*.250,.1486633904331974,-.9977919949999972);}
float bushSeatPadded(vec3 p,float side){
  vec3 q=p-bushSeatPanCenter(side);
  float d=sdRoundBox(q,vec3(.180,.055,.240),.050);
  d=smin(d,sdCapsule(q,vec3(-.125,.030,-.220),vec3(.125,.030,-.220),.045),.012);
  vec3 b=p-bushSeatBackCenter(side);b.yz=rot2(b.yz,-.180);
  float bw=mix(.180,.145,smoothstep(-.140,.280,b.y));
  float back=sdRoundBox(b,vec3(bw,.360,.050),.045);
  float bolster=sdRoundBox(vec3(abs(b.x)-bw+.025,b.y+.050,b.z+.030),vec3(.030,.260,.070),.030);
  back=smin(back,bolster,.025);
  return min(d,back);
}
float bushSeatRailSupports(vec3 p,float side,float f){
  vec3 q=p-vec3(side*.250,0,-1.250);
  const float railY=-.37143825255049546;
  const float postBase=-.35743825255049544;
  const float panBase=-.165;
  float hh=(panBase-postBase)*.5;
  float d=sdRoundBox(vec3(abs(q.x)-.126,q.y-railY,q.z),vec3(.012,.014,.340),.004);
  d=min(d,sdRoundBox(vec3(abs(q.x)-.126,q.y-(postBase+hh),q.z-.050),vec3(.013,hh,.016),.003));
  return max(d,f+.060);
}
float bushHeelPlate(vec3 p,float x){
  return sdRoundBox(p-vec3(x,-.34420534818562264,-1.641258367735749),vec3(.055,.010,.080),.005);
}
float bushHeelPost(vec3 p,float x,float f){
  float floorY=abs(x)>.250?-.4283319412965394:-.4951561671431459;
  float d=sdCapsule(p,vec3(x,floorY-.004,-1.641258367735749),vec3(x,-.35420534818562266,-1.641258367735749),.012);
  return max(d,f+.060); // Raw lower caps fail14.30mm; this conforming cut is mandatory.
}
float bushFinalBlade(vec3 p,float side,float f){
  const float bottom=-.49188279796463386,top=.200;
  vec3 center=vec3(side*.250,.5*(bottom+top),-1.500);
  return max(sdRoundBox(p-center,vec3(.005,.5*(top-bottom),.020),.002),f+.060);
}
float bushDrapedShoulderWeb(vec3 p,float side){
  vec3 q=p-vec3(side*.250,.11866339043319743,-1.057791994999997);
  q.x=abs(q.x)-.110;q.yz=rot2(q.yz,-.180);
  return sdBox(q,vec3(.022,.330,.004));
}
float bushDrapedLapWeb(vec3 p,float side){return sdBox(p-vec3(side*.250,-.040,-1.361),vec3(.170,.022,.004));}
float bushDrapedBuckle(vec3 p,float side){return sdRoundBox(p-vec3(side*.250,-.040,-1.366),vec3(.030,.020,.006),.004);}

float bushmasterStickSocket(vec3 p,float side){
  vec3 q=p-bushmasterStickPivot(side);
  return max(max(length(q)-.026,.020-length(q)),q.y+.003);
}
float bushmasterStickSupport(vec3 p,float side,float f){
  return min(bushFinalBlade(p,side,f),bushmasterStickSocket(p,side));
}

float bushmasterPowerFurniture(vec3 p){
  float saddle=sdRoundBox(p-vec3(0,.015,-1.400),vec3(.030,.020,.027),.006);
  float support=sdRoundBox(p-vec3(0,-.125,-1.400),vec3(.025,.145,.0325),.004);
  float base=sdRoundBox(p-vec3(0,-.280,-1.465),vec3(.025,.020,.100),.004);
  vec3 axis=p-vec3(0,.020,-1.400);
  float recess=max(sdCylX(axis,.028,.060),.010-abs(p.x));
  saddle=max(saddle,-recess);support=max(support,-recess);
  float spindle=sdCylX(axis,.010,.035);
  return min(min(saddle,support),min(base,spindle));
}

vec2 wellCut(vec2 res, float box, float w){
  // (none from the cockpit: there the fuselage is a 6 cm shell, the opening's band went through it and the nose well
  // was a hole in the cabin floor beside the rudder pedals, the ground below showing through it)
  if (gPS.w > 0.5) return res;
  float well = max(box, min(res.x + 0.03, w - 0.04));
  if (-well > res.x) return vec2(-well, 6.0);
  res.x = max(res.x, -well); return res;
}
// c.y is the skin height at the hinges; the cut reaches `below` further down to open a curved belly between them.
vec2 gearWell(vec3 q, vec2 res, vec3 c, vec2 h, float depth, float below){
  vec3 r = q - c;
  return wellCut(res, sdBox(r - vec3(0.0, 0.5*(depth - below), 0.0), vec3(h.x, 0.5*(depth + below), h.y)), r.y);
}
// (a well pitched about +x, as partRyz: along a sloping floor)
vec2 gearWellP(vec3 q, vec2 res, vec3 c, vec2 h, float depth, float below, float pitch){
  vec3 r = transpose(partRyz(pitch))*(q - c);
  return wellCut(res, sdBox(r - vec3(0.0, 0.5*(depth - below), 0.0), vec3(h.x, 0.5*(depth + below), h.y)), r.y);
}
vec2 gearBay(vec3 q, vec2 res, vec3 c, vec2 h, float depth, float below, float open){
  if (open <= 0.001) return res;
  vec3 r = q - c;
  res = gearWell(q, res, c, h, depth, below);
  float a = open*1.45, t = 0.012;
  for (int k = 0; k < 2; k++) {
    float s = k == 0 ? 1.0 : -1.0;
    vec2 v = rot2(vec2(-s*(r.x - s*h.x), r.y), a);   // door frame: u inward from the hinge, y up (door rotated down by a)
    float door = sdBox(vec3(v.x - h.x*0.5, v.y + t, r.z), vec3(h.x*0.5, t, h.y - 0.01));
    res = opU(res, vec2(door, 5.0));
  }
  return res;
}
float sdFuselage(vec3 p){
  vec3 sec = fusSection(p.z);
  vec2 q = vec2(p.x, p.y - sec.z);
  float rnd = gM[15].z;
  float m = min(sec.x, sec.y);
  float dEll = (length(q/sec.xy) - 1.0)*m;
  float r = m*mix(0.3, 1.0, rnd);
  vec2 d = abs(q) - sec.xy + r;
  float dRR = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
  float d2 = mix(dRR, dEll, rnd);
  float dz = max(gM[1].x - p.z, p.z - gM[8].x);
  // Signed caps keep the nose/tail shell closed and make the cap rim a small rolled edge.
  float roll = min(m*0.2, 0.025);
  vec2 cap = vec2(d2, dz) + roll;
  return min(max(cap.x, cap.y), 0.0) + length(max(cap, 0.0)) - roll;
}

// Tapered airfoil panel. s = spanwise (>= 0 from root), c = chordwise from root LE (aft +), t = thickness axis.
// cutC: chordwise position (relative to local LE, as chord fraction) behind which the panel is trimmed when s in [cut0, cut1].
float sdPanel(float s, float c, float t, float span, float rc, float tc, float sweep, float th, float cutF, float cut0, float cut1){
  float k = clamp(s/span, 0.0, 1.0);
  float ch = mix(rc, tc, k); float le = sweep*k;
  float r1 = th*ch*0.5, r2 = max(0.004*ch, 0.005);
  float d2 = sdUnevenCapsule2(vec2(t, c - le - r1), r1, r2, max(ch - r1 - r2, 0.01));
  if (s > cut0 && s < cut1) d2 = max(d2, c - le - ch*cutF);
  float ds = s - span;
  d2 = -smin(-d2, -ds, min(th*ch*0.2, 0.025));  // round only the fixed panel's outer tip
  float d = length(max(vec2(d2, ds), 0.0)) + min(max(d2, ds), 0.0);
  return max(d, -s - 0.02);
}
// Hinged control surface behind the hinge line. defl = geometry rotation (radians) in the (t, c) plane.
// (sdSurface, the hinged control surface: plane_parts.glsl)

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
vec2 gearLegDetails(vec3 p, vec2 res, vec3 upper, vec3 lower, float shaft, bool oleo, bool brace){
  vec3 centre = (upper + lower)*0.5;
  if (sdBox(p - centre, abs(upper - lower)*0.5 + vec3(0.32, 0.15, 0.32)) >= res.x) return res;
  float metal = sdCapsule(p, mix(upper, lower, 0.86), mix(upper, lower, 0.97), shaft*1.45);
  if (oleo) {
    metal = min(metal, sdCapsule(p, mix(upper, lower, 0.12), mix(upper, lower, 0.53), shaft*1.5));
    float scale = clamp(shaft/0.06, 0.65, 1.5);
    vec3 elbow = mix(upper, lower, 0.72) + vec3(0.0, 0.0, 0.12*scale);
    metal = min(metal, sdCapsule(p, mix(upper, lower, 0.56), elbow, 0.013*scale));
    metal = min(metal, sdCapsule(p, elbow, mix(upper, lower, 0.87), 0.013*scale));
    if (brace) metal = min(metal, sdCapsule(p, upper + vec3(-0.18*scale, 0.0, -0.18*scale), mix(upper, lower, 0.37), 0.020*scale));
  }
  return opU(res, vec2(metal, 8.0));
}
// The gear's rigid parts at rest (plane_parts.glsl gearPartPose places them): the right main leg extended (body
// space, its leg from mount); the nose and tail wheels extended and straight, about their steering pivots; a door
// closed, in its hinge's frame. The airframe field places them itself (each piece once, the near door only), so a
// shader that inlines the field carries each shape once.
vec2 gearMainShape(vec3 l, vec3 mount){
  vec4 G0 = gM[18], G1 = gM[19];
  int gtype = int(gM[0].y + 0.5);
  float track = G0.x, wr = G0.y, mz = G0.z;
  vec3 wc = vec3(track, wr - G1.x, mz); float legs, tyres, shaft, halfWidth;
  float door = 1e9;
  if (gtype == 3) {   // a twin-wheel bogie on a heavy leg, raked from its pivot (gearNacFold), and a door on the leg: the
                      // nacelle's skin over the slot the leg swings through, where it lies stowed
    legs = sdCapsule(l, mount, vec3(track, wc.y + 0.05, mz), 0.09);
    legs = min(legs, sdCapsule(l, vec3(track - 0.25, wc.y, mz), vec3(track + 0.25, wc.y, mz), 0.05));
    tyres = min(sdRoundCylX(l - wc - vec3(0.22, 0.0, 0.0), wr, 0.11, 0.06), sdRoundCylX(l - wc + vec3(0.22, 0.0, 0.0), wr, 0.11, 0.06));
    shaft = 0.06; halfWidth = 0.11;   // (its links and brace at the light legs' size: they stay inside the nacelle's tapering tail)
    NacFold f = gearNacFold();
    mat3 Rs = partRyz(f.ang);
    vec3 b = f.P + Rs*(l - f.P);
    door = max(abs(gearNacSkin(b.x - track, b.y, b.z) + 0.012) - 0.012, max(abs(b.x - track) - 0.125, max(f.z1 - b.z, b.z - f.zs)));
    // and its two brackets from the leg (in the stowed frame: from the leg's line down to the door in the nacelle's
    // floor - the door rides 20 cm off the leg, and on nothing it hung beside it like a loose rod)
    vec3 ft = f.P + Rs*(vec3(track, wc.y + 0.05, mz) - f.P), ab = ft - f.P;
    float za = mix(f.z1, f.zs, 0.25), zb = mix(f.z1, f.zs, 0.75);
    vec2 na = nacSection(za), nb = nacSection(zb);
    vec3 sa = vec3(track, na.x - na.y + 0.012, za), sb = vec3(track, nb.x - nb.y + 0.012, zb);
    vec3 la = f.P + ab*clamp(dot(sa - f.P, ab)/dot(ab, ab), 0.0, 1.0);
    vec3 lb = f.P + ab*clamp(dot(sb - f.P, ab)/dot(ab, ab), 0.0, 1.0);
    legs = min(legs, min(sdCapsule(b, la, sa, 0.025), sdCapsule(b, lb, sb, 0.025)));
  } else {            // from its hinge
    legs = sdCapsule(l, mount, wc + vec3(0.0, 0.05, 0.0), 0.06);
    tyres = sdRoundCylX(l - wc, wr, 0.1, 0.05);
    shaft = 0.06; halfWidth = 0.1;
  }
  vec2 res = opU(opU(vec2(legs, 8.0), vec2(tyres, 6.0)), vec2(door, 5.0));
  vec3 wq = l - wc; if (gtype == 3) wq.x = abs(wq.x) - 0.22;
  res = gearWheelDetails(wq, res, wr, halfWidth, true);
  // (a folding leg's side brace on its outboard side: inboard, the fold swung it up through the top of the wing; a leg
  // swung forward has none - the wheel's turn would carry it out through the fairing's floor)
  return gearLegDetails(gtype == 4 ? vec3(2.0*track - l.x, l.yz) : l, res, mount, wc + vec3(0.0, 0.05, 0.0), shaft, true, !(gtype == 4 && gearSwingMain()));
}
vec2 gearNoseShape(vec3 l){
  vec4 G0 = gM[18], G1 = gM[19];
  int gtype = int(gM[0].y + 0.5);
  float wr = G0.y, gh = G1.x;
  vec3 secN = fusSection(G0.w);
  float nwr = gtype == 3 ? wr*0.75 : wr*0.85, rLeg = gtype >= 3 ? 0.07 : 0.035, fx = 0.06 + (gtype == 3 ? 0.15 : 0.0);
  vec3 nc = vec3(0.0, nwr - gh, 0.0), top = gtype >= 3 ? vec3(0.0, gearNoseFold().x, 0.0) : vec3(0.0, secN.z - secN.y*0.7, -0.05);   // (a retracting leg from its pivot)
  float nl = sdCapsule(l, top, nc + vec3(0.0, nwr*0.9, 0.0), rLeg);
  nl = min(nl, sdCapsule(vec3(abs(l.x), l.yz), vec3(fx, nc.y + nwr*0.9, 0.0), vec3(fx, nc.y, 0.0), 0.02));
  float nt = gtype == 3 ? min(sdRoundCylX(l - nc - vec3(0.15, 0.0, 0.0), nwr, 0.07, 0.04), sdRoundCylX(l - nc + vec3(0.15, 0.0, 0.0), nwr, 0.07, 0.04))
                        : sdRoundCylX(l - nc, nwr, 0.055, 0.035);
  vec2 res = opU(vec2(nl, 8.0), vec2(nt, 6.0));
  if (gtype == 0) {   // the spat
    float sp = sdEllipsoid(l - nc - vec3(0.0, 0.05, 0.06), vec3(0.1, nwr*1.12, nwr*2.2));
    sp = max(max(sp, -(l.y - (nc.y - nwr*0.5))), -sdCylX(l - nc, nwr*0.54, 0.12));
    res = opU(res, vec2(sp, 1.0));
  }
  res = gearWheelDetails(gtype == 3 ? vec3(abs(l.x) - 0.15, l.yz) - nc : l - nc, res, nwr, gtype == 3 ? 0.07 : 0.055, false);
  return gearLegDetails(l, res, top, nc + vec3(0.0, nwr*0.9, 0.0), rLeg, true, true);
}
vec2 gearTailShape(vec3 l){
  vec4 G1 = gM[19];
  vec3 tsec = fusSection(G1.y - 0.3), tc = vec3(0.0, -G1.x + 0.11*gM[0].x + 0.1, 0.0), top = vec3(0.0, tsec.z - tsec.y*0.6, -0.3);
  vec2 res = opU(vec2(sdCapsule(l, top, tc + vec3(0.0, 0.03, -0.05), 0.02), 8.0), vec2(sdRoundCylX(l - tc, 0.1, 0.035, 0.02), 6.0));
  res = gearWheelDetails(l - tc, res, 0.1, 0.035, false);
  return gearLegDetails(l, res, top, tc + vec3(0.0, 0.03, -0.05), 0.02, false, false);
}
// a vertical bay's door from its hinge (x 0) to the bay's centre line; a fold well's from its hinge (z 0), spanwise about x 0
float gearDoorV(vec3 l, vec2 h){ return sdBox(vec3(l.x - h.x*0.5, l.y + 0.012, l.z), vec3(h.x*0.5, 0.012, h.y - 0.01)); }
float gearDoorF(vec3 l, GearWell g){ float hzD = g.hz - 0.01; return sdBox(vec3(l.x, l.y + 0.012, l.z - hzD*0.5), vec3(0.5*(g.x1 - g.x0) - 0.01, 0.012, hzD*0.5)); }
#ifdef PART_BAKE
vec2 gearPartField(int k, vec3 l){
  int gtype = int(gM[0].y + 0.5);
  if (k == PT_GEAR_MAIN) return gearMainShape(l, gtype == 3 ? gearNacFold().P : gearSwingMain() ? gearSwingOf().H : gearHinge());
  if (k == PT_GEAR_NOSE) return gearNoseShape(l);
  if (k == PT_GEAR_TAIL) return gearTailShape(l);
  // (each door in the paint of the skin it closes into: the fuselage's livery, the wing's under a main's fairing, a nacelle's)
  if (k == PT_GEAR_MDOOR && gtype == 4 && !gearSwingMain()) return vec2(gearDoorF(l, gearFoldWell()), 2.0);
  return vec2(gearDoorV(l, gearVBay(k == PT_GEAR_NDOOR).h), k == PT_GEAR_NDOOR ? 1.0 : gtype == 3 ? 5.0 : 2.0);
}
#endif

// ---------------- XR-30 Specter research jet (engine code 5): blended lifting body with chines, cranked delta with
// elevons, all-moving canards, canted twin fins, 2D pitch-vectoring nozzles, opaque sensor canopy.
// The cockpit is a sealed pod: the pilot sees outside only through the panoramic and side display screens.
vec2 mapJetCockpit(vec3 p){
  vec4 E4 = gM[22]; vec3 q = p - E4.xyz;
  float cPitch = gCtl.x, cRoll = gCtl.y, cThr = gCtl.w;
  vec2 res = vec2(-sdEllipsoid(q - vec3(0.0, -0.05, 0.25), vec3(0.8, 0.72, 1.45)), 40.0);
  // Deep forward footwell gives the seated pilot a real floor below the control shelf.
  res.x=max(res.x,-sdRoundBox(q-vec3(0,-.78,-.60),vec3(.29,.18,.40),.045));
  res=partAt(res,PT_WR_PEDAL,vec2(-1,0),p);res=partAt(res,PT_WR_PEDAL,vec2(1,0),p);
  float r = length(q.xz), ang = atan(q.x, -q.z);
  // panoramic front display in a chamfered bezel, annunciator strip above it
  // (the displays deep behind their glass, out to the bezel: as thin slabs - 12 mm, 10 mm, 8 mm - the bake's lattice
  // missed them in long strips along their curve, and the pod's shell showed through as shards across the view)
  float scr = max(max(abs(r - 0.667) - 0.033, abs(ang) - 1.25), abs(q.y - 0.02) - 0.30);
  res = opU(res, vec2(scr, 41.0));
  float bez = max(max(abs(r - 0.675) - 0.02, abs(ang) - 1.3), abs(q.y - 0.02) - 0.345);
  bez = max(bez, -max(max(r - 0.66, abs(ang) - 1.25), abs(q.y - 0.02) - 0.30));   // window cut-out
  res = opU(res, vec2(bez, 44.0));
  res = opU(res, vec2(max(max(abs(r - 0.664) - 0.02, abs(ang) - 0.62), abs(q.y - 0.348) - 0.016), 49.0));
  // Stepped arc of five canted modules, with a deliberately wider central primary flight display.
  // The original window/camera cylinder stays untouched. The low saddle leaves distinct gaps between modules.
  {
    float saddle = max(max(r - 0.71, 0.47 - r), max(abs(ang) - 1.18, max(q.y + 0.505, -0.56 - q.y)));
    // Relieve the lower inner rim for full-rudder shin travel; retain the upper 35 mm of support.
    saddle = max(saddle, 0.47 + clamp(-0.54 - q.y, 0.0, 0.02) - r);
    res = opU(res, vec2(saddle, 44.0));
    for (int i = 0; i < 5; i++) {
      ResearchPanel panel = specterPanel(i);
      vec3 l = researchPanelFrame(q, panel);
      float body = sdRoundBox(l + vec3(0.0, 0.0, 0.030), vec3(panel.h + vec2(0.018), 0.030), 0.012);
      body = max(body, (abs(l.x) + abs(l.y) - panel.h.x - panel.h.y - 0.036 + panel.corner)*0.70710678);
      res = opU(res, vec2(body, l.z > -0.010 && researchPanelShape(l.xy, panel) < 0.0 ? 45.0 : 44.0));
    }
  }
  // side display bays beside the pilot (camera feeds) with framed bezels and vents
  vec3 sq = vec3(abs(q.x) - 0.635, q.y - 0.04, q.z - 0.24);  // bring the bezel corners inside the curved pod
  res = opU(res, vec2(sdBox(sq - vec3(0.02, 0.0, 0.0), vec3(0.025, 0.2, 0.3)), q.x < 0.0 ? 42.0 : 43.0));
  float sbz = sdRoundBox(sq - vec3(0.02, 0.0, 0.0), vec3(0.014, 0.23, 0.33), 0.012);
  sbz = max(sbz, -sdBox(sq - vec3(-0.01, 0.0, 0.0), vec3(0.02, 0.2, 0.3)));
  res = opU(res, vec2(sbz, 44.0));
  // side consoles: shelf with a small display and a grid of backlit keys; stick and throttle mounted on them
  {
    vec3 cq = vec3(abs(q.x) - 0.52, q.y + 0.44, q.z - 0.08);
    float shelf = sdRoundBox(cq, vec3(0.13, 0.05, 0.36), 0.015);
    // (its display and its keys flush in its top: the keys - 24 mm caps, 8 mm apart, 11 mm proud - were steps far
    // finer than the bake's lattice, and came out a heap of melted blocks; plane_material.glsl draws them)
    float sid = cq.y > 0.04 && abs(cq.x - 0.02) < 0.075 && abs(cq.z + 0.2) < 0.06 ? (q.x < 0.0 ? 52.0 : 53.0) :
                cq.y > 0.04 && abs(cq.x) < 0.112 && abs(cq.z - 0.136) < 0.096 ? 54.0 : 44.0;
    res = opU(res, vec2(shelf, sid));
  }
  // overhead switch panel angled towards the pilot
  {
    vec3 oq = q - vec3(0.0, 0.46, -0.25); oq.yz = rot2(oq.yz, 0.55);
    float ov = sdRoundBox(oq, vec3(0.26, 0.018, 0.13), 0.01);
    res = opU(res, vec2(ov, 44.0));
    vec3 tq = oq + vec3(0.0, 0.02, 0.0);
    vec2 c2 = clamp(floor(tq.xz/vec2(0.05, 0.06) + 0.5), vec2(-4.0, -1.0), vec2(4.0, 1.0));
    tq.xz -= c2*vec2(0.05, 0.06);
    res = opU(res, vec2(sdCapsule(tq, vec3(0.0), vec3(0.0, -0.022, 0.008), 0.0045), 47.0));
    res = opU(res, vec2(sdBox(oq + vec3(0.0, 0.019, 0.0), vec3(0.25, 0.001, 0.125)), 55.0));
  }
  // sculpted seat: shell, bolsters, headrest with light strip, harness
  {
    float seat = sdRoundBox(q - vec3(0.0, -0.6, 0.12), vec3(0.24, 0.06, 0.26), 0.05);
    vec3 bq = q - vec3(0.0, -0.17, 0.42); bq.yz = rot2(bq.yz, 0.22);
    seat = min(seat, sdRoundBox(bq, vec3(0.23, 0.42, 0.05), 0.05));
    seat = min(seat, sdRoundBox(vec3(abs(q.x) - 0.25, q.y + 0.3, q.z - 0.3), vec3(0.04, 0.24, 0.12), 0.03));
    seat = min(seat, sdRoundBox(vec3(abs(q.x) - 0.22, q.y + 0.52, q.z - 0.1), vec3(0.035, 0.07, 0.22), 0.03));
    seat = min(seat, sdRoundBox(q - vec3(0.0, 0.2, 0.47), vec3(0.13, 0.1, 0.05), 0.04));
    res = opU(res, vec2(seat, 46.0));
    // Four short supports terminate in the actual curved pod floor, not a shared
    // conventional fit. Two crossbars overlap the seat underside by 2 mm.
    float seatSupports=1e5;
    for(int station=0;station<2;station++) {
      float railZ=station==0?-.04:.28;
      seatSupports=min(seatSupports,sdCapsule(q,vec3(-.18,-.673,railZ),vec3(.18,-.673,railZ),.015));
      float floorY=-.05-.72*sqrt(max(1.0-pow(.16/.8,2.0)-pow((railZ-.25)/1.45,2.0),0.0));
      vec3 leg=vec3(abs(q.x)-.16,q.y,q.z);
      seatSupports=min(seatSupports,sdCapsule(leg,vec3(0,floorY-.008,railZ),vec3(0,-.673,railZ),.014));
    }
    res=opU(res,vec2(seatSupports,44.0));
    vec3 hq = vec3(abs(q.x) - 0.1, q.y + 0.05, q.z - 0.36); hq.yz = rot2(hq.yz, 0.22);
    res = opU(res, vec2(sdBox(hq, vec3(0.022, 0.32, 0.006)), 56.0));
  }
  // side stick (right) follows pitch and roll; throttle grip (left) slides with the throttle
  {
    vec3 sb = q - vec3(0.42, -0.375, -0.02);
    res = opU(res, vec2(sdRoundBox(sb, vec3(0.045, 0.02, 0.07), 0.015), 47.0));   // the stick's base
    res = partAt(res, PT_JET_STICK, vec2(0.0), p);   // the stick and the throttle grip: rigid parts (plane_parts.glsl)
    res = partAt(res, PT_JET_THR, vec2(0.0), p);
  }
  // LED strips: ceiling spine, under the display bezel, along both consoles, footwell
  float led = sdCapsule(q, vec3(0.0, 0.50, -0.55), vec3(0.0, 0.60, 0.65), 0.01);
  led = min(led, max(max(abs(r - 0.69) - 0.005, abs(ang) - 1.15), abs(q.y + 0.565) - 0.004));
  led = min(led, sdCapsule(vec3(abs(q.x), q.y, q.z), vec3(0.39, -0.395, -0.25), vec3(0.39, -0.395, 0.42), 0.0025));
  res = opU(res, vec2(led, 48.0));
  // Two warm strips embed 4 mm in the closed footwell sidewalls, clear of shins and pedals.
  res = opU(res, vec2(sdCapsule(vec3(abs(q.x), q.y, q.z), vec3(0.286, -0.83, -0.77), vec3(0.286, -0.83, -0.48), 0.008), 57.0));
  // recessed ceiling light bars either side of the spine: machined housings with warm diffuser lenses (id 58)
  {
    vec3 lq = vec3(abs(q.x) - 0.2, q.y - 0.58, q.z - 0.15);
    res = opU(res, vec2(sdRoundBox(lq, vec3(0.034, 0.035, 0.21), 0.01), 44.0));
    res = opU(res, vec2(sdRoundBox(lq + vec3(0.0, 0.035, 0.0), vec3(0.022, 0.003, 0.19), 0.002), 58.0));
  }
  return res;
}
// the XR-30's nozzle in its own frame (x across, z aft from its front edge, the pivot; plane_parts.glsl jtPartPose
// turns it by the vectoring angle): the 2D duct's walls, soot-black inside, the afterburner's flame-holder rings and
// spray bars, a ribbed liner and the convergent-divergent flaps that form the throat. The last turbine stage behind it
// is the airframe's (jtTurbine)
vec2 jtNozzleShape(vec3 l){
  vec3 nq = l - vec3(0.0, 0.0, 0.5);
  float box = sdRoundBox(nq, vec3(0.44, 0.31, 0.5), 0.06);
  float cav = sdBox(nq - vec3(0.0, 0.0, 0.25), vec3(0.36, 0.23, 0.6));
  float nzlIn = -cav;
  vec2 res = vec2(max(box, nzlIn), nzlIn > box - 0.001 ? 37.0 : 33.0);
  if (cav < 0.05) {
    vec3 hq = nq - vec3(0.0, 0.0, -0.08);
    float gut = min(sdTorus(hq.xzy, vec2(0.17, 0.012)), sdTorus(hq.xzy, vec2(0.085, 0.01)));          // V-gutter rings
    vec2 bq = hq.xy; float ba = atan(bq.y, bq.x); ba = mod(ba + 0.3927, 0.7854) - 0.3927;
    vec2 br2 = length(bq)*vec2(cos(ba), sin(ba));
    float bars = max(sdBox(vec3(br2.x - 0.13, br2.y, hq.z + 0.04), vec3(0.11, 0.006, 0.006)), max(abs(nq.x) - 0.35, abs(nq.y) - 0.22));
    res = opU(res, vec2(min(gut, bars), 37.0));
    float ribs = max(abs(fract(nq.z/0.11) - 0.5)*0.11 - 0.01, -(nzlIn + 0.012));
    ribs = max(ribs, max(nq.z - 0.02, -0.3 - nq.z));
    res = opU(res, vec2(ribs, 37.0));
    // the C-D flaps: the duct narrows to the throat at z 0.28, then opens slightly to the exit
    float hz = 0.23 - 0.075*exp(-pow((nq.z - 0.28)/0.16, 2.0));
    float flap = max(max((hz - abs(nq.y))*0.9, abs(nq.x) - 0.36), max(0.06 - nq.z, nq.z - 0.56));
    res = opU(res, vec2(flap, 37.0));
  }
  return res;
}
// the duct the nozzle continues, cut into the airframe at its rest angle (l: the right nozzle's frame at rest), with
// the last turbine stage behind a hot tail cone deep in it
vec2 jtTurbine(vec2 res, vec3 l){
  vec3 nq = l - vec3(0.0, 0.0, 0.5);
  float cav = sdBox(nq - vec3(0.0, 0.0, 0.25), vec3(0.36, 0.23, 0.6));
  res.x = max(res.x, -cav);   // (the duct is hollow right down to the turbine: no airframe inside it)
  if (cav < 0.05) {
    vec3 iq = nq - vec3(0.0, 0.0, -0.35);
    float ang = atan(iq.y, iq.x), rr = length(iq.xy);
    float disc = max(sdBox(iq, vec3(0.36, 0.23, 0.02)), -iq.z - 0.02);
    float blades = max(max(abs(fract(ang*23.0/6.2832 + rr*1.5) - 0.5)*rr*0.27 - 0.006, abs(iq.z - 0.03) - 0.012), rr - 0.225);
    float hub = sdRoundCone(iq, vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, 0.24), 0.1, 0.02);
    res = opU(res, vec2(min(min(disc, blades), hub), 36.0));
  }
  return res;
}
// its gear (body space, right side): a strut from its mount A to its foot B, a main wheel (wq: from its centre), the
// nose's twin wheels (nq: from their axle's centre)
vec2 jtStrut(vec3 l, vec3 A, vec3 B, float r){
  return gearLegDetails(l, vec2(sdCapsule(l, A, B, r), 8.0), A, B, r, true, true);
}
vec2 jtMainWheel(vec3 wq){ return gearWheelDetails(wq, vec2(sdRoundCylX(wq, 0.38, JT_TYRE_H, 0.05), 6.0), 0.38, JT_TYRE_H, true); }
vec2 jtNoseWheels(vec3 nq){ vec3 q = vec3(abs(nq.x) - 0.1, nq.yz); return gearWheelDetails(q, vec2(sdRoundCylX(q, 0.33, 0.07, 0.04), 6.0), 0.33, 0.07, false); }
// the main's leg at rest (right side, body space): its strut from the hinge H down the line to the wheel's centre wc,
// a fork either side of the tyre and the axle through it (the wheel turns about that line as it swings: gearSwingR).
// No side brace: the turn would carry it out of the wing
vec2 jtMainLeg(vec3 l, vec3 H, vec3 wc){
  vec3 u = normalize(H - wc), f = wc + u*0.44, sd = vec3(JT_TYRE_H + 0.035, 0.0, 0.0);
  vec2 res = gearLegDetails(l, vec2(sdCapsule(l, H, f, 0.07), 8.0), H, f, 0.07, true, false);
  vec3 lq = vec3(wc.x + abs(l.x - wc.x), l.yz);
  res = opU(res, vec2(min(sdCapsule(lq, f + sd, wc + sd, 0.025), sdCapsule(l, wc - sd, wc + sd, 0.035)), 8.0));
  return opU(res, vec2(sdCapsule(l, f - sd, f + sd, 0.03), 8.0));
}
// The research jets' retractable tricycle gear (the XR-30's and the XR-40's; plane_parts.glsl jtGearOf): the main's
// fairing under the wing (blended in: flush where the wing is thick enough) and its well, the nose's well along the
// belly; the doors over them, the legs and the wheels rigid parts (plane_parts.glsl jtPartPose). (mainDepth, mainBelow:
// the old bays' depths, unused)
vec2 jtGear(vec3 p, vec3 ap, vec2 res, float mainDepth, float mainBelow){
  JtGear g = jtGearOf();
  bool wr = int(gM[0].z + 0.5) == 6;
  float fl = g.mh.y + 0.03, top = g.H.y + 0.06;
  vec3 fe = ap - vec3(g.mc.x, 0.5*(g.mc.y + top), g.mc.z);
  float fr = min(0.08, 0.5*(top - g.mc.y));   // (rounded along its sides, its floor flat under the doors)
  float fair = sdRoundBox(fe, vec3(g.mh.x + 0.01 + fr, 0.5*(top - g.mc.y), fl + 0.4), fr);
  fair = max(fair, (abs(fe.z) - fl - 2.0*(ap.y - g.mc.y))*0.4472);   // (its ends taper up into the wing)
  float fd = res.x;
  res.x = smin(res.x, fair, 0.1);
  if (fair < fd) res.y = wr ? 80.0 : 31.0;
  res = gearWell(ap, res, g.mc, g.mh, g.md, 0.03);
  res = gearWellP(p, res, g.nc, g.nh, g.nd, g.nb, g.np);
  if (gPartMode != -1) return res;
  float a = gearDoorAngle(), sm = ap.x > g.mc.x ? 1.0 : -1.0, sn = p.x > 0.0 ? 1.0 : -1.0;   // (the near door of each bay, by the side of its centre line)
  res = opU(res, vec2(gearDoorV(transpose(partMirror(-sm)*partRxy(-a))*(ap - g.mc - vec3(sm*g.mh.x, 0.0, 0.0)), g.mh), wr ? 80.0 : 31.0));   // (in the skin's own paint)
  mat3 Rp = partRyz(g.np);
  res = opU(res, vec2(gearDoorV(transpose(Rp*partMirror(-sn)*partRxy(-a))*(p - g.nc - Rp*vec3(sn*g.nh.x, 0.0, 0.0)), g.nh), wr ? 80.0 : 30.0));
  vec4 G0 = gM[18]; float gh = gM[19].x;
  vec3 wc = vec3(G0.x, 0.38 - gh, G0.z), nc = vec3(0.0, 0.33 - gh, G0.w);
  Pose M = jtMainPose(mat3(1.0));   // (one struct to a declaration, no local arrays, no struct members written in a loop: NVIDIA's
  Pose N = jtNosePose();            // compiler failed on those - C9999 "Unhandled expr op assign" - and the game would not start)
  vec3 lm = transpose(M.R)*(ap - M.T), ln = transpose(N.R)*(p - N.T);
  res = opU(res, jtMainLeg(lm, g.H, wc));
  res = opU(res, jtMainWheel(lm - wc));
  res = opU(res, jtStrut(ln, g.P, nc + vec3(0.0, 0.1, 0.0), 0.06));
  res = opU(res, jtNoseWheels(ln - nc));
  return res;
}
// the XR-30's rigid parts at rest (plane_parts.glsl places them): the right elevon and rudder (body space), the right
// canard in its pivot's frame, the nozzle in its own frame, the gear extended (body space), a door in its hinge's frame
vec2 jtPartField(int k, vec3 l){
  vec4 G0 = gM[18]; float gh = gM[19].x;
  if (k == PT_JT_NOZZLE) return jtNozzleShape(l);
  if (k == PT_JT_LEGM) return jtMainLeg(l, jtGearOf().H, vec3(G0.x, 0.38 - gh, G0.z));
  if (k == PT_JT_WHEELM) return jtMainWheel(l - vec3(G0.x, 0.38 - gh, G0.z));
  if (k == PT_JT_LEGN) return jtStrut(l, jtGearOf().P, vec3(0.0, 0.43 - gh, G0.w), 0.06);
  if (k == PT_JT_WHEELN) return jtNoseWheels(l - vec3(0.0, 0.33 - gh, G0.w));
  bool wrd = int(gM[0].z + 0.5) == 6;   // (the doors in the skin's paint: the XR-40's, the XR-30's wing and body)
  if (k == PT_JT_DOORM) return vec2(gearDoorV(l, jtGearOf().mh), wrd ? 80.0 : 31.0);
  if (k == PT_JT_DOORN) return vec2(gearDoorV(l, jtGearOf().nh), wrd ? 80.0 : 30.0);
  if (k == PT_JT_ELEVON) return vec2(sdSurface(l.x, l.z + 1.6, l.y - (-0.18 - l.x*0.035), 5.6, 7.2, 1.2, 5.6, 0.04, 0.84, 1.2, 5.3, 0.0, 0.0), 31.0);
  if (k == PT_JT_CANARD) return vec2(sdPanel(l.x, l.z + 0.6, l.y, 1.5, 1.5, 0.45, 1.0, 0.05, 1.0, 0.0, 0.0), 31.0);
  if (k == PT_JT_RUDDER) {
    vec3 q = l - vec3(1.0, 0.3, 4.6); q.xy = rot2(q.xy, 0.42);
    return vec2(sdSurface(q.y, q.z, q.x, 2.3, 2.6, 1.0, 1.9, 0.05, 0.7, 0.15, 2.2, 0.0, 0.0), 31.0);
  }
  return vec2(1e9, 0.0);
}
vec2 mapJet(vec3 p){
  float gear = gPS.x, inside = gPS.w;
  if (inside > 0.5) return mapJetCockpit(p);
  float cPitch = gCtl.x, cRoll = gCtl.y, cYaw = gCtl.z;
  vec3 ap = vec3(abs(p.x), p.y, p.z);
  float sgn = p.x > 0.0 ? 1.0 : -1.0;
  float body = sdFuselage(p);
  body = smin(body, sdEllipsoid(p - vec3(0.0, -0.12, -3.4), vec3(1.75, 0.1, 5.6)), 0.3);    // chines
  body = smin(body, sdEllipsoid(p - vec3(0.0, 0.36, 1.6), vec3(0.55, 0.3, 6.0)), 0.3);       // dorsal spine
  body = smin(body, sdRoundBox(ap - vec3(0.82, -0.12, 4.6), vec3(0.5, 0.42, 3.4), 0.3), 0.35); // engine bays
  body = max(body, -sdRoundBox(ap - vec3(0.95, -0.34, -1.4), vec3(0.3, 0.19, 0.62), 0.08));   // intakes
  vec2 res = vec2(body, 30.0);
  // cranked delta wing with elevons
  {
    float s = ap.x, t = p.y - (-0.18 - s*0.035), c = p.z + 1.6;
    float wing = sdPanel(s, c, t, 5.6, 7.2, 1.2, 5.6, 0.04, 0.84, 1.2, 5.3);
    float elevon = gPartMode == -2 ? 1e9 : sdSurface(s, c, t, 5.6, 7.2, 1.2, 5.6, 0.04, 0.84, 1.2, 5.3, -cPitch*0.3 - cRoll*sgn*0.3, 0.0);   // (elevons, canards, rudders: rigid parts)
    wing = min(wing, elevon);
    float d = smin(res.x, wing, 0.25);
    res = vec2(d, wing < res.x ? 31.0 : res.y);
  }
  // all-moving canards
  if (gPartMode != -2) {
    vec3 q = ap - vec3(0.6, -0.02, -6.4); q.yz = rot2(q.yz, cPitch*0.3);
    float can = sdPanel(q.x, q.z + 0.6, q.y, 1.5, 1.5, 0.45, 1.0, 0.05, 1.0, 0.0, 0.0);
    res = opU(res, vec2(can, 31.0));
  }
  // canted twin fins with rudders
  {
    vec3 q = ap - vec3(1.0, 0.3, 4.6); q.xy = rot2(q.xy, 0.42);
    float fin = sdPanel(q.y, q.z, q.x, 2.3, 2.6, 1.0, 1.9, 0.05, 0.7, 0.15, 2.2);
    float rud = gPartMode == -2 ? 1e9 : sdSurface(q.y, q.z, q.x, 2.3, 2.6, 1.0, 1.9, 0.05, 0.7, 0.15, 2.2, -cYaw*0.4*sgn, 0.0);
    float f2 = min(fin, rud);
    res = vec2(smin(res.x, f2, 0.12), f2 < res.x ? 31.0 : res.y);
  }
  // 2D thrust-vectoring nozzles, rigid parts (plane_parts.glsl jtPartPose): they vector in pitch about their front
  // edge with the stick (and down to the hover setting). The duct and the turbine at its end are the airframe's
  {
    vec3 l = ap - vec3(0.82, -0.12, 7.75);
    res = jtTurbine(res, l);
    if (gPartMode == -1) res = opU(res, jtNozzleShape(vec3(l.x, rot2(l.yz, -gFlame.z))));
  }
  // sensor canopy (opaque gold film) and LED strips along the chines and wing leading edges
  // Lower the crown by 11 cm and blend the shoulders, retaining the opaque sensor film.
  float canopy = sdEllipsoid(p - vec3(0.0, 0.44, -4.6), vec3(0.62, 0.37, 2.0));
  res = vec2(smin(res.x, canopy, 0.16), canopy < res.x ? 32.0 : res.y);
  float led = sdCapsule(ap, vec3(1.55, -0.12, -2.6), vec3(0.35, -0.08, -7.6), 0.022);
  led = min(led, sdCapsule(ap, vec3(1.3, -0.24, -0.25), vec3(5.45, -0.39, 3.9), 0.02));
  led = min(led, sdCapsule(ap, vec3(0.62, 0.62, -3.0), vec3(0.3, 0.72, 2.0), 0.015));
  res = opU(res, vec2(led, 34.0));
  return res;
}
vec2 mapWraith(vec3 p);
vec2 mapWraithCockpit(vec3 p);
vec2 mapPlaneBody(vec3 p);
// light fixtures: a faired housing set into the airframe with a domed lens facing out along the light's axis
vec2 mapPlane(vec3 p){
  COST(1);
  vec2 res = mapPlaneBody(p);
  if (gPS.w > 0.5 || gPartMode >= 0) return res;   // (a rigid part's bake: its own frame, no lamp housings)
  int engine = int(gM[0].z + 0.5);
  bool jet = RESEARCH_ON && engine == 5, wr = RESEARCH_ON && engine == 6;
  vec3 tip = wr ? vec3(6.25, -0.24, 2.0) : jet ? vec3(5.67, -0.38, 4.4) : vec3(gM[9].x + 0.07, gM[10].x + gM[9].x*gM[10].z, gM[10].y + gM[9].w + gM[9].z*0.25);
  vec3 fin = wr ? vec3(0.0, 0.53, 1.6) : jet ? vec3(0.0, 0.67, 1.6) : isMantis() ? mantisFinTop() : vec3(0.0, gM[15].x + gM[14].x + 0.04, gM[15].y + gM[14].w + gM[14].z*0.4);
  vec3 tail = wr ? vec3(0.0, -0.1, 7.86) : jet ? vec3(0.0, 0.45, 7.6) : isMantis() ? mantisTailLight() : vec3(0.0, gM[8].w, gM[8].x + 0.03);
  float lx = gM[9].x*0.3;
  vec3 land = wr ? vec3(-0.2, -0.38, -6.6) : jet ? vec3(-1.8, -0.26, 0.15) : vec3(-lx, gM[10].x + lx*gM[10].z, gM[10].y + gM[9].w*0.3 - 0.02);
  // IDs 95..100 always mean port nav, starboard nav, tail, beacon, port/starboard landing.
  // Neither menu/flight order, current owner, flashing phase nor lamp state can change this shape.
  for (int i = 0; i < 6; i++) {
    vec3 c = i == 0 ? vec3(-tip.x, tip.yz) : i == 1 ? tip : i == 2 ? tail : i == 3 ? fin : i == 4 ? land : vec3(-land.x, land.yz);
    vec3 d = i == 0 ? vec3(-1,0,0) : i == 1 ? vec3(1,0,0) : i == 2 ? vec3(0,0,1) : i == 3 ? vec3(0,1,0) : vec3(0,0,-1);
    vec3 q = p - c;
    if (dot(q, q) > 0.09) continue;
    res = opU(res, vec2(sdRoundCone(q, -d*0.16, -d*0.025, 0.06, 0.05), 94.0));
    res = opU(res, vec2(max(length(q + d*0.012) - 0.05, -dot(q, d) - 0.012), 95.0 + float(i)));
  }
  return res;
}
// The Osprey C6's cabin trim (Codex, proposals/aircraft/osprey-mantis): static fittings in body coordinates (+z aft),
// drawn for that type only (gModelId == kOsprey in aircraft.h).
// IDs 120 ivory composite, 121 copper anodized trim, 122 tobacco upholstery,
// 123 dark cocoa textile, 124 warm-white lens. Static geometry ONLY.
vec2 mapOspreyCabinTrim(vec3 p) {
  vec2 r=vec2(1e5,120.0);
  // Copper brow sits below the existing glareshield and above all gauge faces.
  vec3 copperBrow=vec3(0,gM[22].y-.187,gM[22].z-.616);
  r=opU(r,vec2(sdRoundBox(p-copperBrow,vec3(.48,.009,.008),.004),121.0));   // follows the authored crew station under its coaming
  // Sculpted ivory sill and copper inset: well below the side-window opening.
  vec3 q=vec3(abs(p.x),p.y,p.z);
  r=opU(r,vec2(sdRoundBox(q-vec3(.598,.125,-1.93),vec3(.023,.080,.47),.016),120.0));
  r=opU(r,vec2(sdRoundBox(q-vec3(.570,.154,-1.93),vec3(.009,.008,.40),.004),121.0));
  // Cocoa inset map pocket on each door. Recessed opening is geometry, not paint.
  float pocket=sdRoundBox(q-vec3(.580,-.090,-1.90),vec3(.025,.100,.245),.014);
  pocket=max(pocket,-sdRoundBox(q-vec3(.555,-.040,-1.90),vec3(.022,.052,.205),.012));
  r=opU(r,vec2(pocket,123.0));
  // Join the lowered front crew floor to the fixed rear cabin deck without moving its seats.
  float crewFloor=gM[22].y-1.06;
  if(crewFloor < -.321) {
    float t=clamp((p.z+1.58)/.24,0.0,1.0),slope=(-.316-crewFloor)/.24;
    float ry=mix(crewFloor,-.316,t);
    float ramp=max(abs(p.x)-.53,abs(p.z+1.46)-.12);
    ramp=max(ramp,(abs(p.y-(ry-.014))-.014)/sqrt(1.0+slope*slope));
    r=opU(r,vec2(ramp,120.0)); // existing final90mm shell inset also clips this join
  }
  // Aft floor extension, centre aisle runner and paired copper edge strips.
  r=opU(r,vec2(sdRoundBox(p-vec3(0,-.330,.075),vec3(.53,.014,1.435),.008),120.0));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,-.308,.15),vec3(.095,.008,1.31),.004),123.0));
  r=opU(r,vec2(sdRoundBox(q-vec3(.109,-.306,.15),vec3(.004,.008,1.31),.004),121.0));
  // Four passenger seats, two abreast in two rows, inspired by the twin's pilot seats.
  for(int row=0;row<2;row++) {
    float z=-.38+float(row)*.92;
    vec3 s=vec3(abs(p.x)-.325,p.y,p.z-z);
    r=opU(r,vec2(sdRoundBox(s-vec3(0,-.105,0),vec3(.173,.065,.245),.035),122.0));
    vec3 b=s-vec3(0,.20,.235); b.yz=rot2(b.yz,-.12);
    r=opU(r,vec2(sdRoundBox(b,vec3(.163,.255,.050),.035),120.0));
    r=opU(r,vec2(sdRoundBox(b-vec3(0,0,-.052),vec3(.133,.220,.018),.018),122.0));
    r=opU(r,vec2(sdRoundBox(s-vec3(0,.480,.270),vec3(.100,.055,.045),.028),122.0));
    // broad sewn channels (minimum 8 mm thickness); lower frame terminates on floor.
    for(int rib=0;rib<3;rib++) {
      float x=float(rib-1)*.072;
      r=opU(r,vec2(sdRoundBox(b-vec3(x,0,-.073),vec3(.004,.180,.004),.004),123.0));
    }
    vec3 leg=vec3(abs(s.x)-.118,s.y+.247,s.z);
    r=opU(r,vec2(sdRoundBox(leg,vec3(.013,.064,.175),.008),121.0));
    r=opU(r,vec2(sdRoundBox(s-vec3(0,-.028,-.025),vec3(.142,.010,.015),.006),123.0));
    r=opU(r,vec2(sdRoundBox(s-vec3(0,-.015,-.025),vec3(.024,.008,.022),.006),121.0));
  }
  // Low cabin side liners and luggage-wall inlay stay under the windows.
  r=opU(r,vec2(sdRoundBox(q-vec3(.585,.040,.20),vec3(.022,.190,1.19),.018),120.0));
  r=opU(r,vec2(sdRoundBox(q-vec3(.557,.202,.20),vec3(.008,.008,1.14),.004),121.0));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,.23,1.548),vec3(.405,.340,.014),.025),120.0));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,.23,1.525),vec3(.215,.140,.008),.016),123.0));
  // Physical containment: every custom point is at least 90 mm inside the outer SDF.
  r.x=max(r.x,sdFuselage(p)+.090);
  return r;
}
vec3 ospreyCabinAlbedo(int id) {
  if(id==120)return vec3(.82,.76,.62);
  if(id==121)return vec3(.54,.235,.095);
  if(id==122)return vec3(.30,.115,.052);
  if(id==123)return vec3(.062,.040,.028);
  return vec3(.95,.82,.58);
}
// XR-20: a compact centerline research workstation. Instrument glass has its own live material mapping;
// the side-stick, throttle and pedals use the exact rigid-part interfaces used by the production mesh renderer.
vec2 mapMantisCockpit(vec3 p, float f){
  vec3 E=gM[22].xyz; float z=gM[21].w; vec3 q=p-E;
  vec2 r=vec2(1e5,11.0);
  // Faceted instrument bridge: flight strip above, engine and navigation displays below.
  r=opU(r,vec2(sdRoundBox(p-vec3(0,E.y-.355,z),vec3(.46,.23,.055),.025),14.0));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,E.y-.325,z+.059),vec3(.395,.19,.009),.012),130.0));
  vec3 d=vec3(abs(p.x)-.235,p.y-(E.y-.695),p.z-z-.16);
  r=opU(r,vec2(sdRoundBox(d,vec3(.22,.145,.055),.025),14.0));
  r=opU(r,vec2(sdRoundBox(d-vec3(0,0,.058),vec3(.185,.117,.01),.01),p.x<0.0?131.0:132.0));
  // Short hood never blocks the forward pane, warm datum trim and under-panel illumination.
  r=opU(r,vec2(sdRoundBox(p-vec3(0,E.y-.15,z-.02),vec3(.47,.018,.13),.012),14.0));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,E.y-.168,z+.103),vec3(.415,.006,.008),.004),133.0));
  // Floor/firewall and two connected side consoles. They end below the canopy rail.
  // Closed local pedal footwell. The same low pedal station is retained. The complete
  // plate/arm sweep fits between x +/- .18, top E.y-.87, forward wall z-.140,
  // and lowered floor E.y-1.085. This pocket does not alter the seat floor.
  float baseFloor=sdRoundBox(q-vec3(0,-1.055,-.15),vec3(.58,.025,1.02),.012);
  float floorOpening=sdBox(p-vec3(0,E.y-1.02,z+.020),vec3(.18,.20,.16));
  baseFloor=max(baseFloor,-floorOpening);
  float footFloor=sdBox(p-vec3(0,E.y-1.110,z+.020),vec3(.205,.025,.185));
  // Aft riser meets the unchanged floor; sides meet the unchanged firewall.
  float aftRiser=sdBox(p-vec3(0,E.y-1.0575,z+.180),vec3(.205,.0275,.025));
  float floorSides=sdBox(vec3(abs(p.x)-.1925,p.y-(E.y-1.0575),p.z-(z+.020)),vec3(.0125,.0275,.185));
  r=opU(r,vec2(max(min(baseFloor,min(footFloor,min(aftRiser,floorSides))),f+.055),11.0));
  float firewall=sdBox(p-vec3(0,E.y-.81,z-.04),vec3(.55,.24,.045));
  float opening=sdBox(p-vec3(0,E.y-.985,z-.055),vec3(.18,.115,.25));
  firewall=max(firewall,-opening);
  float frontWall=sdBox(p-vec3(0,E.y-.985,z-.185),vec3(.205,.140,.045));
  float sideReturns=sdBox(vec3(abs(p.x)-.1925,p.y-(E.y-.985),p.z-(z-.0675)),vec3(.0125,.140,.0725));
  float topReturn=sdBox(p-vec3(0,E.y-.8575,z-.0675),vec3(.205,.0125,.0725));
  r=opU(r,vec2(max(min(firewall,min(frontWall,min(sideReturns,topReturn))),f+.055),63.0));
  vec3 c=vec3(abs(q.x)-.43,q.y+.75,q.z+.03);
  r=opU(r,vec2(sdRoundBox(c,vec3(.115,.21,.48),.04),63.0));
  r=opU(r,vec2(sdRoundBox(c-vec3(0,.21,0),vec3(.10,.012,.43),.01),66.0));
  r=opU(r,vec2(sdRoundBox(c-vec3(-.104,.10,0),vec3(.008,.008,.37),.004),133.0));
  r=partAt(r,PT_JET_STICK,vec2(0),p); r=partAt(r,PT_JET_THR,vec2(0),p);
  r=partAt(r,PT_PEDAL,vec2(0,-1),p); r=partAt(r,PT_PEDAL,vec2(0,1),p);
  // Single bolstered seat, harness and headrest: a clear central footwell, with no duplicate copilot parts.
  r=opU(r,vec2(sdRoundBox(q-vec3(0,-.78,.12),vec3(.235,.065,.28),.045),12.0));
  // The dedicated seat bypasses the fleet rails: add connected rails/posts here.
  float seatRails=sdRoundBox(vec3(abs(q.x)-.16,q.y+1.019,q.z-.12),vec3(.016,.014,.30),.004);
  float seatPosts=sdRoundBox(vec3(abs(q.x)-.16,q.y+.921,q.z-.12),vec3(.019,.086,.024),.004);
  r=opU(r,vec2(min(seatRails,seatPosts),60.0));
  vec3 b=q-vec3(0,-.37,.40); b.yz=rot2(b.yz,-.16);
  r=opU(r,vec2(sdRoundBox(b,vec3(.22,.35,.065),.04),12.0));
  r=opU(r,vec2(sdRoundBox(vec3(abs(b.x)-.20,b.y,b.z+.045),vec3(.035,.27,.055),.025),12.0));
  r=opU(r,vec2(sdRoundBox(q-vec3(0,.025,.47),vec3(.115,.075,.06),.03),12.0));
  r=opU(r,vec2(sdRoundBox(vec3(abs(b.x)-.095,b.y,b.z+.068),vec3(.024,.30,.007),.004),69.0));
  r=opU(r,vec2(sdRoundBox(q-vec3(0,-.70,.02),vec3(.21,.018,.022),.009),69.0));
  r=opU(r,vec2(sdRoundBox(q-vec3(0,-.694,.00),vec3(.035,.022,.015),.006),60.0));
  // Aft equipment wall seals the occupied volume before the wing/engine structure begins.
  r=opU(r,vec2(max(abs(p.z-(E.z+.80))-.035,f+.05),63.0));
  // All fittings are physically contained in the skin, leaving at least 5 cm of the shell.
  r.x=max(r.x,f+.05); return r;
}
const int kOspreyModel = 8;   // (aircraft.h kOsprey)
// Compact aft twin-engine bank. Real lever slots and fixed pivot axles retain floor support.
vec2 twinPowerBankField(vec3 p,vec3 pc,float pw,float ph,float pd){
  float body=sdRoundBox(p-pc+vec3(0,.003,0),vec3(pw,ph+.003,pd),.030);
  float slots=1e5,axles=1e5;
  for(int j=0;j<3;j++){
    bool flap=j==2;
    float x=flap?pw*.6:(j==0?-.035:.035),z=flap?pd*.80:-pd*.35;
    vec3 q=p-pc-vec3(x,ph-.012,z);
    slots=min(slots,sdRoundBox(q,vec3(.013,.040,flap?.026:.055),.004));
    vec3 aq=p-pc-vec3(x,ph-.020,z);
    axles=min(axles,sdCylX(aq,.007,flap?.018:.022));
  }
  return opU(vec2(max(body,-slots),63.0),vec2(axles,60.0));
}
vec2 mapPlaneBody(vec3 p){
#ifdef PART_BAKE
  if (gPartMode >= 0) return partField(gPartMode, p);   // (the mesh bake: one rigid part alone, in its own frame)
#endif
  {   // the research jets: their own airframes, then their gear - the same for both, called once here so the program
      // carries one copy of it (written into each airframe it was two, and the march's every pixel paid for the size)
    int engJ = int(gM[0].z + 0.5);
    if (RESEARCH_ON && (!FLEET_ON || engJ == 5 || engJ == 6)) {   // (a research jet's own build: nothing else follows)
      vec2 r = JET_ON && (!WRAITH_ON || engJ == 5) ? mapJet(p) : mapWraith(p);
      return gPS.w > 0.5 || !GEAR_FIELD ? r : jtGear(p, vec3(abs(p.x), p.y, p.z), r, engJ == 6 ? 0.7 : 0.8, engJ == 6 ? 0.03 : 0.06);
    }
  }
  float L = gM[0].x; int gtype = int(gM[0].y + 0.5); int eng = int(gM[0].z + 0.5); float R = gM[0].w;
  float gear = gPS.x, flaps = gPS.y, steer = gPS.z, inside = gPS.w;
  float cPitch = gCtl.x, cRoll = gCtl.y, cYaw = gCtl.z, cThr = gCtl.w;
  // ---------------- fuselage (hollow with window openings in cockpit view)
  float f = sdFuselage(p);
  vec2 res = vec2(f, 1.0);
  float winHole = 1e9;   // (from the cockpit: the distance to the window openings, negative in them - the visors keep to the roof)
  if (inside > 0.5) {
    // hollow cabin with real window openings
    vec4 E = gM[22]; vec4 WS = gM[23]; vec3 sec = fusSection(p.z);
    float shell = abs(f + 0.03) - 0.03;
    float holeWs = sdBox(p - vec3(0.0, WS.z + 1.0, 0.5*(WS.x + WS.y)), vec3(sec.x*1.25, 1.0, 0.5*(WS.y - WS.x)));
    float post = gM[21].z > 1.5 ? min(abs(p.x) - 0.03, abs(abs(p.x) - abs(E.x) - 0.42) - 0.035) : abs(p.x) - 0.025;
    holeWs = max(holeWs, -post);
    float sideTop = sec.z + sec.y*0.78;
    float holeSide = sdBox(p - vec3(0.0, 0.5*(WS.z - 0.12 + sideTop), 0.5*(WS.y + WS.w)), vec3(5.0, 0.5*(sideTop - WS.z + 0.12), 0.5*(WS.w - WS.y)));
    holeSide = max(holeSide, 0.3 - abs(p.x));
    holeSide = max(holeSide, -(abs(p.z - WS.y - 0.04) - 0.025));
    // the window openings' cut faces in the frames' trim, not the shell's paint: where the flat cut meets the curved
    // roof at a shallow angle the face is a long wedge, and in the light shell colour it read as a hole to the sky
    // (and rounded, a 3 cm lip: a hard cut meeting the curved roof at a shallow angle left a knife edge far thinner than
    // the mesh's lattice, which came off it serrated against the sky)
    // (inside, the trim runs 4 cm from every opening, and over the whole pillar between the windscreen and a side
    // window - within 10 cm of both: by where the rounding reached alone - 2.6 cm - a 5 cm post between two openings
    // was trim only just, and where the pillar widened into the roof the headliner showed through it in long slivers)
    float shell0 = shell;
    winHole = isMantis() ? mantisWindow(p) : min(holeWs, holeSide);
    shell = -smin(-shell, winHole, 0.03);
    res = vec2(shell, shell > shell0 + 1e-4 || (f < -0.03 && (winHole < 0.04 || max(holeWs, holeSide) < 0.1)) ? 63.0 : 11.0);
    // rear bulkhead: a trimmed baggage wall closes the cabin behind the last seats / windows (instead of looking
    // straight down the hollow tail cone)
    float zB = gM[20].x > 0.5 ? gM[20].z + 0.15 : WS.w + (gM[21].z > 0.5 ? 0.9 : 0.75);
    res = opU(res, vec2(max(f + 0.03, abs(p.z - zB) - 0.02), 63.0));
  }
  // Each part below is skipped when its bounding box is farther than the nearest surface found so far (plus its blend
  // radius): its own distance can only be larger, so the result is unchanged - but a sample in the cabin no longer
  // evaluates the wingtips, the tail and the wheels (the cockpit view takes ~30 such samples per pixel).
  // ---------------- main wing with flaps and ailerons
  vec4 W0b = gM[9], W1b = gM[10], W2b = gM[11];
  float wy0 = min(W1b.x, W1b.x + W0b.x*W1b.z), wy1 = max(W1b.x, W1b.x + W0b.x*W1b.z);
  float wyLo = (W2b.x > 0.5 ? min(wy0, -0.35*R) : wy0) - 0.45 - W2b.z, wyHi = wy1 + 0.45 + W2b.z;
  float wz0 = W1b.y + min(0.0, W0b.w) - 0.45, wz1 = W1b.y + max(W0b.y, W0b.w + W0b.z) + 0.45;
  if (sdBox(p - vec3(0.0, 0.5*(wyLo + wyHi), 0.5*(wz0 + wz1)), vec3(W0b.x + 0.35, 0.5*(wyHi - wyLo), 0.5*(wz1 - wz0))) < res.x + 0.06*R + 0.1) {
    vec4 W0 = gM[9], W1 = gM[10], W2 = gM[11];
    float span = W0.x, rc = W0.y, tc = W0.z, sw = W0.w, th = W1.w;
    float s = abs(p.x);
    float t = p.y - (W1.x + s*W1.z);
    float c = p.z - W1.y;
    float sgn = p.x > 0.0 ? 1.0 : -1.0;
    float fus0 = 0.55*R, flap0 = flapRoot(), flapEnd = span*W2.w, ailEnd = span*0.94;
    float wing = sdPanel(s, c, t, span, rc, tc, sw, th, 0.74, flap0, ailEnd);
    // (the flaps and ailerons are rigid parts with meshes of their own: the airframe bake leaves them out, plane_parts.glsl)
    float flp = flaps + (sgn < 0.0 ? gFlapDL : 0.0);   // (the left one's own, if it stopped)
    float flap = gPartMode == -2 ? 1e9 : sdSurface(s, c, t, span, rc, tc, sw, th, 0.74, flap0, flapEnd, flp*0.62, flp*0.1);
    // right aileron TE goes UP for right roll; left goes down
    float ail = gPartMode == -2 ? 1e9 : sdSurface(s, c, t, span, rc, tc, sw, th, 0.74, flapEnd + 0.03, ailEnd, -cRoll*sgn*0.33, 0.0);
    float wd = min(wing, min(flap, ail));
    if (W2.z > 0.01) {  // winglet
      float wl = sdPanel(t - 0.02, c - sw - tc*0.15, s - span + 0.05, W2.z, tc*0.85, tc*0.4, 0.55, 0.09, 1.0, 0.0, 0.0);
      wd = smin(wd, wl, 0.08);
    }
    if (gM[15].w > 0.5) {  // leading-edge slats (STOL)
      float sl = sdPanel(s, c + 0.09, t + 0.03, span*0.95, rc*0.16, tc*0.16, sw, 0.5, 1.0, 0.0, 0.0);
      wd = min(wd, max(sl, fus0 + 0.4 - s));
    }
    float fd = res.x;
    if (inside > 0.5) { wd = max(wd, -f); res.x = min(res.x, wd); }
    else res.x = smin(res.x, wd, 0.08*R);
    if (wd < fd) res.y = 2.0;
    // lift struts
    if (W2.x > 0.5) {
      vec3 sec = fusSection(p.z);
      float k = clamp(W2.y/span, 0.0, 1.0); float ch = mix(rc, tc, k); float le = sw*k;
      vec3 top1 = vec3(W2.y, W1.x + W2.y*W1.z - th*ch*0.4, W1.y + le + ch*0.25);
      vec3 base = vec3(0.6*R, -0.35*R, W1.y + rc*0.35);
      vec3 ap = vec3(abs(p.x), p.y, p.z);
      float st = sdCapsule(ap, base, top1, 0.035);
      if (gM[15].w > 0.5) st = min(st, sdCapsule(ap, base, top1 + vec3(0.0, 0.0, ch*0.45), 0.03));
      if (inside > 0.5) st = max(st, -f);  // the exterior attachment must not protrude into the hollow cabin
      res = opU(res, vec2(st, 8.0));
    }
  }
  // ---------------- tail
  vec4 V0b = gM[14], V1b = gM[15], H0b = gM[12], H1b = gM[13];
  float tz0 = min(V1b.y, H1b.y) - 0.35, tz1 = max(V1b.y + max(V0b.y, V0b.w + V0b.z), H1b.y + max(H0b.y, H0b.w + H0b.z)) + 0.35;
  float ty0 = min(V1b.x, H1b.x - H0b.x*abs(H1b.z)) - 0.45, ty1 = max(V1b.x + V0b.x, H1b.x + H0b.x*abs(H1b.z)) + 0.45;
  if (sdBox(p - vec3(0.0, 0.5*(ty0 + ty1), 0.5*(tz0 + tz1)), vec3(max(H0b.x, isMantis() ? MT_FIN_X + sin(MT_CANT)*V0b.x : 0.3) + 0.35, 0.5*(ty1 - ty0), 0.5*(tz1 - tz0))) < res.x + 0.12*R) {
    vec4 V0 = gM[14], V1 = gM[15];
    vec3 vq=vec3(p.x,p.y-V1.x,p.z-V1.y);
    if(isMantis()) vq.xy=rot2(vec2(abs(p.x)-MT_FIN_X,p.y-V1.x),MT_CANT);
    float s = vq.y, c = vq.z, t = vq.x;
    float h = V0.x;
    float hasT = gM[13].w;
    float rud0 = hasT > 0.5 ? 0.05 : 0.08*h;
    float fin = sdPanel(s, c, t, h, V0.y, V0.z, V0.w, 0.11, 0.66, rud0, h*0.97);
    // right rudder (yaw +) swings the trailing edge to the right (+x)
    float rud = gPartMode == -2 ? 1e9 : sdSurface(s, c, t, h, V0.y, V0.z, V0.w, 0.11, 0.66, rud0, h*0.97, -cYaw*0.5, 0.0);   // (rudder and elevators: rigid parts too)
    if(isMantis()) { rud=1e9; if(gPartMode==-1) { vec2 pr=partAt(vec2(1e9,3),PT_RUDDER,vec2(p.x<0.0?-1.0:1.0,0),p); rud=pr.x; } }
    float tail = min(fin, rud);
    vec4 H0 = gM[12], H1 = gM[13];
    float hs = abs(p.x), ht = p.y - (H1.x + hs*H1.z), hc = p.z - H1.y;
    float stab = sdPanel(hs, hc, ht, H0.x, H0.y, H0.z, H0.w, 0.1, 0.68, 0.12, H0.x*0.98);
    // Pull raises an aft elevator; the Mantis canard lowers its trailing edge to lift the nose.
    float elev = gPartMode == -2 ? 1e9 : sdSurface(hs, hc, ht, H0.x, H0.y, H0.z, H0.w, 0.1, 0.68, 0.12, H0.x*0.98, isMantis() ? elevDefl(cPitch) : -elevDefl(cPitch), 0.0);
    tail = min(tail, min(stab, elev));
    if (hasT > 0.5) tail = smin(tail, sdEllipsoid(p - vec3(0.0, H1.x, H1.y + H0.y*0.45), vec3(0.18, 0.2, H0.y*0.55)), 0.08);
    // inside, the tail surfaces stop at the cabin wall as the wing does (a canard - the Mantis's horizontal tail sits
    // ahead of the pilot - otherwise crossed both footwells)
    if (inside > 0.5) { tail = max(tail, -f); res = vec2(min(res.x, tail), tail < res.x ? 3.0 : res.y); }
    else { float d = smin(res.x, tail, 0.12*R); res = vec2(d, tail < res.x ? 3.0 : res.y); }
  }
  // ---------------- engines
  {
    vec4 N0 = gM[16], N1 = gM[17];
    vec4 S0 = gM[1];
    float eb;   // distance to the engines' bounding box
    if (eng <= 1) { float sr0 = max(N1.y, 0.1); eb = sdBox(p - vec3(0.0, S0.w, S0.x + 0.5*(1.75 - sr0*2.3)), vec3(0.95, 1.1, 0.5*(1.75 + sr0*2.3) + 0.25)); }
    else if (eng <= 3) { vec3 nq = vec3(abs(p.x) - N0.x, p.y - N0.y, p.z); float yr = abs(gM[10].x + N0.x*gM[10].z - N0.y);
                         eb = sdBox(nq - vec3(0.0, 0.0, N0.w + 0.5*(N1.x - N1.y*2.3)), vec3(N0.z + 0.6, N0.z + yr + 0.6, 0.5*(N1.x + N1.y*2.3) + 0.4)); }
    else if (isMantis()) eb = sdBox(p-vec3(0.0,0.15,3.20),vec3(1.30,1.40,5.10));
    else { vec3 nq = vec3(abs(p.x), p.y - N0.y, p.z - N0.w); eb = sdBox(nq - vec3(0.5*(N0.x + N0.z), 0.0, 0.5*N1.x), vec3(0.5*(N0.x + N0.z) + 0.3, N0.z + 0.4, 0.5*N1.x + 0.75)); }
    if (eb > res.x + 0.12) {}
    else if (eng <= 1) {
      float sr = N1.y;
      float spin = sdRoundCone(p, vec3(0.0, S0.w, S0.x - sr*2.3), vec3(0.0, S0.w, S0.x + 0.05), 0.015, sr);
      float spinJoin = smin(res.x, spin, 0.015);
      res = vec2(spinJoin, spin < res.x ? 16.0 : res.y);
      if (eng == 0) {
        vec3 sec = fusSection(S0.x + 0.9);
        float ex = sdCapsule(vec3(abs(p.x), p.y, p.z), vec3(0.12, sec.z - sec.y*0.85, S0.x + 0.9), vec3(0.16, sec.z - sec.y - 0.06, S0.x + 1.15), 0.035);
        res = opU(res, vec2(ex, 17.0));
      } else {
        vec3 sec = fusSection(S0.x + 1.2);
        vec3 ap = vec3(abs(p.x), p.y, p.z);
        float ex = sdCapsule(ap, vec3(sec.x*0.85, sec.z + 0.05, S0.x + 1.15), vec3(sec.x + 0.22, sec.z + 0.12, S0.x + 1.5), 0.085);
        ex = max(ex, -sdCapsule(ap, vec3(sec.x*0.85, sec.z + 0.05, S0.x + 1.15), vec3(sec.x + 0.4, sec.z + 0.14, S0.x + 1.6), 0.06));
        res = opU(res, vec2(ex, 17.0));
        float lip = sdCapsule(p, vec3(-0.12, S0.w - 0.32, S0.x + 0.45), vec3(0.12, S0.w - 0.32, S0.x + 0.45), 0.07);
        res.x = smin(res.x, lip, 0.08);
      }
    } else if (eng <= 3) {
      vec3 np = vec3(abs(p.x) - N0.x, p.y - N0.y, p.z);
      float nr = N0.z, z0 = N0.w, len = N1.x;
      float wingY = gM[10].x + N0.x*gM[10].z;
      float nac = sdRoundCone(np, vec3(0.0, 0.0, z0), vec3(0.0, 0.02, z0 + len*0.3), nr*0.72, nr);
      nac = smin(nac, sdRoundCone(np, vec3(0.0, 0.02, z0 + len*0.3), vec3(0.0, wingY - N0.y - nr*0.25, z0 + len), nr, nr*0.35), 0.1);
      if (eng == 3) {
        nac = smin(nac, sdEllipsoid(np - vec3(0.0, -nr*0.75, z0 + 0.45), vec3(nr*0.35, nr*0.22, 0.5)), 0.08);
        float ex = sdCapsule(np, vec3(nr*0.8, 0.1, z0 + len*0.35), vec3(nr*1.05, 0.15, z0 + len*0.5), 0.09);
        res = opU(res, vec2(ex, 17.0));
      }
      float nacJoin = smin(res.x, nac, nr*0.08);
      res = vec2(nacJoin, nac < res.x ? 5.0 : res.y);
      float sr = N1.y;
      res = opU(res, vec2(sdRoundCone(np, vec3(0.0, 0.0, z0 - sr*2.3), vec3(0.0, 0.0, z0 + 0.05), 0.015, sr), 16.0));
    } else if (isMantis()) {
      // One centerline powerplant: a raised dorsal mouth aft of the sealed cockpit routes into one core.
      // Cutting the same inlet/nozzle voids through the skin prevents hidden fuselage caps in either opening.
      float nr=N0.z, len=N1.x; vec3 core=vec3(0.0,N0.y,N0.w), end=mantisNozzle();
      vec3 elbow=vec3(0.0,.32,1.38);
      vec3 ductP=p*vec3(1,1.6,1), ductM=MT_INLET*vec3(1,1.6,1), ductE=elbow*vec3(1,1.6,1);
      float shell=sdRoundCone(ductP,ductM,ductE,nr*.91,nr)/1.6;
      shell=smin(shell,sdRoundCone(p,elbow,core+vec3(0,0,1.00),nr,nr),.10);
      shell=min(shell,sdRoundCone(p,core,end,nr,nr*.88));
      shell=max(shell,MT_INLET.z-p.z); shell=max(shell,p.z-end.z);
      float inlet=sdCapsule(ductP,ductM-vec3(0,0,.30),ductE,nr*.77)/1.6;
      float exitBore=sdCapsule(p,end-vec3(0,0,.78),end+vec3(0,0,1.1),nr*.72);
      float openings=min(inlet,exitBore);
      shell=max(shell,-openings); res.x=max(res.x,-openings);
      res=opU(res,vec2(shell,5.0));
      // Thin structural tail shelves retain the existing canted-fin roots without recreating side nacelles.
      vec3 shelf=vec3(abs(p.x)-.86,p.y-.16,p.z-5.30);
      res=opU(res,vec2(sdRoundBox(shelf,vec3(.29,.09,1.12),.07),5.0));
      // One intake fan, recessed along the descending duct; one aft turbine behind an unobstructed exhaust lip.
      vec3 fanP=mix(ductM,ductE,.68);
      vec3 ductAxis=normalize(ductE-ductM);
      float fan=max(length((ductP-fanP)-ductAxis*dot(ductP-fanP,ductAxis))-nr*.76,abs(dot(ductP-fanP,ductAxis))-.035)/1.6;
      res=opU(res,vec2(fan,21.0));
      vec3 tq=p-(end-vec3(0,0,.64));
      float turbine=max(length(tq.xy)-nr*.70,abs(tq.z)-.035);
      res=opU(res,vec2(turbine,134.0));
      // The metal nozzle is one circular annulus, with no center spike or mirrored overlapping engine instance.
      float lip=max(abs(length((p-end).xy)-nr*.79)-nr*.065,abs(p.z-end.z+.055)-.055);
      res=opU(res,vec2(lip,17.0));
    } else {
      vec3 np = vec3(abs(p.x) - N0.x, p.y - N0.y, p.z - N0.w);
      float nr = N0.z, len = N1.x;
      float nac = sdRoundCone(np, vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, len), nr, nr*0.78);
      float inlet = sdCapsule(np, vec3(0.0, 0.0, -0.4), vec3(0.0, 0.0, 0.18), nr*0.82);
      nac = max(nac, -inlet);
      float exhaust = sdCapsule(np, vec3(0.0, 0.0, len - 0.15), vec3(0.0, 0.0, len + 1.0), nr*0.6);   // (the tailpipe: the cone below sat sealed inside the cap)
      nac = max(nac, -exhaust);
      res = opU(res, vec2(nac, 5.0));
      float fan = sdCapsule(np, vec3(0.0, 0.0, 0.2), vec3(0.0, 0.0, 0.3), nr*0.83);
      res = opU(res, vec2(fan, 21.0));
      float cone = sdRoundCone(np, vec3(0.0, 0.0, len - 0.25), vec3(0.0, 0.0, len + 0.3), nr*0.5, 0.04);
      res = opU(res, vec2(cone, 17.0));
      vec3 sec = fusSection(N0.w + len*0.5);
      float px0 = sec.x*0.7, px1 = max(px0 + 0.05, N0.x - nr*0.8);
      float pylon = sdRoundBox(vec3(abs(p.x) - 0.5*(px0 + px1), p.y - N0.y, p.z - N0.w - len*0.5), vec3(0.5*(px1 - px0) + 0.05, 0.06, len*0.28), 0.04);
      res.x = smin(res.x, pylon, 0.1);
    }
  }
  // ---------------- cargo pod
  if (gM[17].w > 0.5) {
    vec3 sec = fusSection(-0.5);
    float pod = sdRoundBox(p - vec3(0.0, sec.z - sec.y - 0.18, -0.4), vec3(0.42, 0.2, 2.6), 0.17);
    pod = smin(pod, sdEllipsoid(p - vec3(0.0, sec.z - sec.y - 0.2, -3.0), vec3(0.42, 0.22, 0.8)), 0.2);
    res.x = smin(res.x, pod, 0.12);
  }
  // ---------------- landing gear (fixed types hang below the lower fuselage: a plane bounds them)
  // The retracting legs, the nose and tail wheels and the bays' doors are rigid parts (plane_parts.glsl
  // gearPartPose, gearPartField above): placed here by their poses, left out of the airframe's mesh bake. The wells are
  // the airframe's, always open: the doors close over them.
  float gearTop = 1e5;
  if (gtype <= 2) { vec3 sg0 = fusSection(gM[18].z), sg1 = fusSection(gM[19].z < 0.5 ? gM[18].w : gM[19].y);
                    gearTop = max(sg0.z - sg0.y*0.5, sg1.z - sg1.y*0.5) + 0.12; }
  bool retract = gtype >= 3;
  if ((retract ? GEAR_FIELD : gear > 0.02) && p.y - gearTop < res.x) {
    vec4 G0 = gM[18], G1 = gM[19];
    float track = G0.x, wr = G0.y, mz = G0.z, gh = G1.x;
    vec3 ap = vec3(abs(p.x), p.y, p.z);
    float side = p.x < 0.0 ? -1.0 : 1.0;
    if (retract) {
      float a = gearDoorAngle(), up = gearUp();
      if (gtype == 3) {   // the nacelle's wheel bay and the slot the leg swings through; the near door (by the side of the
                          // bay's centre line); the leg, folded
        VBay b = gearVBay(false); NacFold f = gearNacFold();
        res = gearWellP(ap, res, b.c, b.h, b.depth, b.below, b.pitch);
        vec2 ns = nacSection(ap.z);
        float sf = ns.x - sqrt(max(ns.y*ns.y - 0.0169, 0.0));   // (the floor's height at the slot's edges)
        res = wellCut(res, sdBox(ap - vec3(track, 0.5*(sf + f.P.y + 0.05), 0.5*(f.z1 - 0.05 + f.zs)), vec3(0.13, 0.5*(f.P.y + 0.35 - sf), 0.5*(f.zs - f.z1 + 0.05))), ap.y - sf);
        if (gPartMode == -1) {
          float s = ap.x > b.c.x ? 1.0 : -1.0;
          mat3 Rp = partRyz(b.pitch);
          res = opU(res, vec2(gearDoorV(transpose(Rp*partMirror(-s)*partRxy(-a))*(ap - b.c - Rp*vec3(s*b.h.x, 0.0, 0.0)), b.h), 5.0));
          mat3 Rf = partRyz(up*f.ang);
          res = opU(res, gearMainShape(transpose(Rf)*(ap - f.P) + f.P, f.P));
        }
      } else if (gearSwingMain()) {   // swung forward into the wing: its fairing (blended in; inside a wing thick enough
                                      // it never shows), its well, the near door, the leg swung
        GearSwing g = gearSwingOf(); VBay b = gearVBay(false);
        float zc = 0.5*(g.z0 + g.z1), fl = 0.5*(g.z1 - g.z0) + 0.03, top = g.H.y + 0.06;
        vec3 fe = ap - vec3(track, 0.5*(g.fy + top), zc);
        float fr = min(0.08, 0.5*(top - g.fy));   // (rounded along its sides, its floor flat under the doors)
        float fair = sdRoundBox(fe, vec3(g.hw + 0.01 + fr, 0.5*(top - g.fy), fl + 0.4), fr);
        fair = max(fair, (abs(fe.z) - fl - 2.0*(ap.y - g.fy))*0.4472);   // (its ends taper up into the wing)
        fair = max(fair, ap.z - gearFairAft());                           // (and stop short of the flaps' hinge line)
        float fd = res.x;
        res.x = smin(res.x, fair, 0.1);
        if (fair < fd) res.y = 2.0;   // (the wing's paint)
        res = gearWell(ap, res, b.c, b.h, b.depth, b.below);
        if (gPartMode == -1) {
          float s = ap.x > b.c.x ? 1.0 : -1.0;
          res = opU(res, vec2(gearDoorV(transpose(partMirror(-s)*partRxy(-a))*(ap - b.c - vec3(s*b.h.x, 0.0, 0.0)), b.h), 2.0));
          mat3 Rf = gearSwingR(vec3(track, wr - gh, mz) - g.H, -1.0, up);
          res = opU(res, gearMainShape(transpose(Rf)*(ap - g.H) + g.H, g.H));
        }
      } else {   // the fold's fairing under the wing root (blended in; inside a wing thick enough it never shows), its well, the near door, the leg folded
        GearFold f = gearFold(); GearWell g = gearFoldWellOf(f);
        vec3 bq = inverse(g.F.R)*(ap - g.F.T), bc = vec3(0.5*(g.x0 + g.x1), 0.0, 0.0);
        float ft = max(gM[10].x - g.F.T.y, 0.02), fr = min(0.07, 0.5*ft);   // (up to the wing's mid-plane: it only ever bulges below)
        vec3 fe = bq - bc; float fh = 0.5*(g.x1 - g.x0) + 0.03, fl = g.hz + 0.03, ext = 1.5*ft;
        float fair = sdRoundBox(fe - vec3(0.0, ft*0.5, 0.0), vec3(fh, ft*0.5, fl + ext), fr);
        fair = max(fair, (abs(fe.z) - fl - 1.5*bq.y)*0.5547);   // (its ends taper up into the wing; its outboard side stays clear of the flap's root)
        fair = max(fair, fe.z - (gearFairAft() - mz));   // (the taper stops short of the flaps' hinge line)
        float fd = res.x;
        res.x = smin(res.x, fair, 0.08);
        if (fair < fd) res.y = 2.0;   // (the wing's paint)
        res = wellCut(res, sdBox(bq - bc - vec3(0.0, 0.5*(g.depth - 0.04) - 0.02, 0.0), vec3(0.5*(g.x1 - g.x0), 0.5*(g.depth + 0.04), g.hz)), bq.y);
        if (gPartMode == -1) {
          float s = bq.z > 0.0 ? 1.0 : -1.0, ca = cos(a), sa = sin(a);
          mat3 DR = mat3(1.0, 0.0, 0.0,  0.0, ca, -s*sa,  0.0, -sa, -s*ca);
          res = opU(res, vec2(gearDoorF(transpose(DR)*(bq - bc - vec3(0.0, 0.0, s*g.hz)), g), 2.0));
          mat3 Rf = partRxy(up*(f.dl - 1.5707963));
          res = opU(res, gearMainShape(transpose(Rf)*(ap - f.H) + f.H, f.H));
        }
      }
      if (G1.z < 0.5) {   // the nose wheel's well along the belly and its near door
        VBay b = gearVBay(true);
        res = gearWellP(p, res, b.c, b.h, b.depth, b.below, b.pitch);
        float s = p.x > 0.0 ? 1.0 : -1.0;
        mat3 Rp = partRyz(b.pitch);
        if (gPartMode == -1) res = opU(res, vec2(gearDoorV(transpose(Rp*partMirror(-s)*partRxy(-a))*(p - b.c - Rp*vec3(s*b.h.x, 0.0, 0.0)), b.h), 1.0));
      }
    } else {   // fixed mains: in place
      vec3 wc = vec3(track, wr - gh, mz);
      vec3 secM = fusSection(mz);
      float legs, tyres, spats = 1e5;
      if (gtype == 0) {
        legs = sdCapsule(ap, vec3(secM.x*0.75, secM.z - secM.y*0.8, mz), wc + vec3(-0.06, 0.04, 0.0), 0.03);
        tyres = sdRoundCylX(ap - wc, wr, 0.065, 0.04);
        float sp = sdEllipsoid(ap - wc - vec3(0.0, 0.04, 0.06), vec3(0.1, wr*1.05, wr*1.9));
        spats = max(max(sp, -(ap.y - (wc.y - wr*0.5))), -sdCylX(ap - wc, wr*0.54, 0.12));
      } else if (gtype == 1) {
        legs = sdCapsule(ap, vec3(secM.x*0.7, secM.z - secM.y*0.85, mz), wc + vec3(-0.08, 0.06, 0.0), 0.045);
        tyres = sdRoundCylX(ap - wc, wr, 0.09, 0.05);
        tyres = min(tyres, sdRoundCylX(ap - wc - vec3(0.06, 0.0, 0.0), wr*0.45, 0.04, 0.02));
      } else {
        legs = min(sdCapsule(ap, vec3(secM.x*0.8, secM.z - secM.y*0.8, mz - 0.35), wc, 0.03), sdCapsule(ap, vec3(secM.x*0.8, secM.z - secM.y*0.8, mz + 0.3), wc, 0.03));
        tyres = sdRoundCylX(ap - wc, wr, 0.14, 0.09);
      }
      res = opU(res, vec2(legs, 8.0));
      res = opU(res, vec2(tyres, 6.0));
      res = gearWheelDetails(ap - wc, res, wr, gtype == 0 ? 0.065 : gtype == 1 ? 0.09 : 0.14, true);
      vec3 mount = vec3(secM.x*(gtype == 2 ? 0.8 : gtype == 0 ? 0.75 : 0.7), secM.z - secM.y*(gtype == 1 ? 0.85 : 0.8), mz);
      vec3 ankle = gtype == 2 ? wc : wc + vec3(gtype == 0 ? -0.06 : -0.08, gtype == 1 ? 0.06 : 0.04, 0.0);
      res = gearLegDetails(ap, res, mount, ankle, gtype == 1 ? 0.045 : 0.03, false, false);
      res = opU(res, vec2(spats, 1.0));
    }
    // the nose wheel (steered by the pedals, raised with the gear) or the tail wheel
    if (gPartMode == -1) {
      mat3 Rs = partRxz(steer);
      float ns = gearNoseShow();
      if (G1.z < 0.5 && ns > 0.01) { Pose X = gearNosePose(ns); res = opU(res, gearNoseShape(transpose(X.R)*(p - X.T)/ns)*vec2(ns, 1.0)); }
      else res = opU(res, gearTailShape(transpose(Rs)*(p - vec3(0.0, 0.0, G1.y))));
    }
  }
  // ---------------- small details: nav lights, beacon, antennas, pitot
  {
    vec4 W0 = gM[9], W1 = gM[10];
    vec3 tip = vec3(W0.x + 0.02, W1.x + W0.x*W1.z, W1.y + W0.w + W0.z*0.25);
    res = opU(res, vec2(length(vec3(abs(p.x), p.y, p.z) - tip) - 0.045, 18.0));
    vec4 V0 = gM[14], V1 = gM[15];
    res = opU(res, vec2(length(p - (isMantis() ? mantisFinTop() : vec3(0.0, V1.x + V0.x + 0.04, V1.y + V0.w + V0.z*0.4))) - 0.05, 19.0));
    vec3 sec = fusSection(0.2);
    float ant = sdRoundBox(p - vec3(0.0, sec.z + sec.y + 0.11, 0.2), vec3(0.006, 0.12, 0.05), 0.004);
    res = opU(res, vec2(ant, 8.0));   // (8: exterior metal - 13 is a cockpit material, lit as if inside the cabin)
    float span = W0.x, rcd = W0.y, tcd = W0.z, swp = W0.w, thk = W1.w;
    {   // pitot tube under the left wing: a faired mast and the probe pointing into the airflow
      float ks = 0.62, ch = mix(rcd, tcd, ks), le = W1.y + swp*ks, sx = -span*ks;
      float yu = W1.x + span*ks*W1.z - thk*ch*0.42;   // just inside the wing's underside at the quarter chord
      vec3 mb = vec3(sx, yu - 0.1, le + ch*0.3);
      vec3 q = p - mb;
      if (dot(q, q) < 0.16) {
        float pit = sdRoundBox(vec3(q.x, q.y - 0.055, q.z - 0.01), vec3(0.006, 0.06, 0.022), 0.005);
        pit = min(pit, sdCapsule(q, vec3(0.0, 0.0, 0.02), vec3(0.0, 0.0, -0.2), 0.008));
        res = opU(res, vec2(pit, 8.0));
      }
    }
    {   // static dischargers: two wicks off each wing's trailing edge near the tip
      float ks = 0.915, ch = mix(rcd, tcd, ks), te = W1.y + swp*ks + ch;
      vec3 q = vec3(abs(p.x) - span*ks, p.y - (W1.x + span*ks*W1.z), p.z - te);
      if (dot(q, q) < 0.09) {
        float dz = (tcd - rcd)/span*0.035*span + swp/span*0.035*span;   // the trailing edge's slope over the wicks' spacing
        float wk = min(sdCapsule(q, vec3(-0.035*span, 0.0, -dz - 0.01), vec3(-0.035*span, 0.0, -dz + 0.11), 0.0035),
                       sdCapsule(q, vec3(0.035*span, 0.0, dz - 0.01), vec3(0.035*span, 0.0, dz + 0.11), 0.0035));
        res = opU(res, vec2(wk, 6.0));
      }
    }
    {   // VHF blade antenna under the belly, raked aft
      float za = gM[23].w + 0.5; vec3 sb = fusSection(za);
      vec3 q = p - vec3(0.0, sb.z - sb.y - 0.06, za);
      if (dot(q, q) < 0.04) {
        q.zy = rot2(q.zy, -0.35);
        res = opU(res, vec2(sdRoundBox(q, vec3(0.004, 0.07, 0.035), 0.003), 8.0));
      }
    }
  }
  // ---------------- cockpit interior (only rendered from inside)
  // Ids: 10 panel (instruments drawn on it), 11 shell/floor, 12 seats, 13 controls, 14 glareshield & overhead,
  // 60 brushed metal, 61 rubber, 63 trim panels, 64 light lenses, 65 radio stack, 66 satin black (bezels, knobs),
  // 67 centre engine display (glass cockpits), 68 red knobs / buttons, 69 harness webbing
  if (inside > 0.5 && isMantis()) return opU(res,mapMantisCockpit(p,f));
  if (inside > 0.5) {
    vec4 E = gM[22]; float pz = gM[21].w, phw = E.w; int ck = int(gM[21].z + 0.5);
    float pf = pz + 0.045;
    if (fleetCabin()) res = opU(res,mapFleetPanel(p,f));
    else {
    float panel = sdRoundBox(p - vec3(0.0, E.y - 0.38, pz), vec3(phw, 0.22, 0.045), 0.015);   // (its top under the glareshield's hood)
    panel = max(panel, f + 0.06);  // contour the panel corners to the inside of the cowling
    res = opU(res, vec2(panel, 10.0));
    // raised bezels framing each pilot's instrument cluster
    {
      float cx = ck == 2 ? 0.02 : (ck == 1 ? 0.09 : 0.05), hx = ck == 2 ? 0.2 : (ck == 1 ? 0.255 : 0.215), hy = ck == 2 ? 0.098 : 0.112;
      bool co = p.x*E.x < 0.0;
      if (co) hx = coCluster(ck).y;
      float sx = !co ? E.x + cx : -E.x + coCluster(ck).x + coShift(E, ck);   // copilot cluster: other seat, kept on the panel
      vec2 fq = vec2(p.x - sx, p.y - (E.y - 0.32));
      vec2 dq = abs(fq) - vec2(hx, hy) + 0.02; float fr = length(max(dq, 0.0)) + min(max(dq.x, dq.y), 0.0) - 0.02;
      float frame = max(abs(fr) - 0.007, abs(p.z - pf - 0.006) - 0.006);
      res = opU(res, vec2(frame, 66.0));
    }
    // glareshield with a warm LED strip under its lip that floods the panel: a short hood over the instruments, then
    // the dash top sloping down to the windscreen's base. (It ran level all the way to the windscreen 8 cm under the
    // eye, and its far edge cut the view 3-4 deg below the horizon - the windscreen itself reaches 7-16 deg - so the
    // world showed only in a strip above it. Now 8-10 deg over the nose, or the windscreen's own edge)
    vec4 WSg = gM[23];
    float gz0 = min(WSg.x - 0.05, pz - 0.2);
    const float gy = 0.157;                                   // the hood's centre under the eye (top 13.5 cm, bottom 17.9 cm)
    float zr = pz + 0.06, zh = pz - 0.08;                     // the hood's lip over the panel and its front
    float glare = sdRoundBox(p - vec3(0.0, E.y - gy, 0.5*(zh + zr)), vec3(phw*0.97, 0.022, 0.5*(zr - zh)), 0.018);
    {
      float yf = min(E.y - gy, WSg.z + 0.01 - 0.022);         // (the slope's centre line ends just under the windscreen's base)
      vec2 ax = vec2((E.y - gy) - yf, zh - gz0);              // (y, z) from its front end to its back
      vec3 gq = p - vec3(0.0, 0.5*(E.y - gy + yf), 0.5*(zh + gz0));
      gq.yz = rot2(gq.yz, atan(ax.x, ax.y));
      glare = smin(glare, sdRoundBox(gq, vec3(phw*0.97, 0.022, 0.5*length(ax) + 0.01), 0.018), 0.02);
    }
    glare = max(glare, f + 0.055);
    res = opU(res, vec2(glare, 14.0));
    // (the strip under the lip, trimmed to the cabin wall as the glareshield is and ending short of its ends)
    res = opU(res, vec2(max(sdCapsule(p, vec3(-phw*0.85, E.y - gy - 0.024, pz + 0.07), vec3(phw*0.85, E.y - gy - 0.024, pz + 0.07), 0.0035), f + 0.075), 64.0));
    // centre: radio / transponder stack below the clusters; glass cockpits add an engine display between the PFDs
    res = opU(res, vec2(sdRoundBox(p - vec3(0.0, E.y - 0.505, pf + 0.012), vec3(0.115, 0.06, 0.016), 0.004), 65.0));
    if (ck == 2) res = opU(res, vec2(sdRoundBox(p - vec3(0.0, E.y - 0.31, pf + 0.008), vec3(0.085, 0.085, 0.01), 0.004), 67.0));
    // eyeball air vents at the panel corners
    {
      vec3 vq = vec3(abs(p.x) - gCab1.w, p.y - (E.y - 0.19), p.z - pf);
      res = opU(res, vec2(max(sdRoundCylX(vq.zyx, 0.03, 0.012, 0.004), -sdRoundCylX(vq.zyx - vec3(0.012, 0.0, 0.0), 0.02, 0.01, 0.002)), 60.0));
      res = opU(res, vec2(length(vq - vec3(0.0, 0.0, 0.004)) - 0.019, 66.0));
    }
    }
    float floor_ = sdBox(p - vec3(0.0, (gModelId==2?.560:(gModelId==7?.520:E.y)) - 1.08, E.z), vec3(phw, 0.02, 1.6));
    floor_ = max(floor_, f + 0.04);
    res = opU(res, vec2(floor_, 11.0));
    // seats: pan with a front roll, bolstered back, headrest, rails, lap belt and shoulder harness
    // (each group below is skipped when its bounding box is farther than the nearest surface so far, as outside)
    vec3 sp = vec3(abs(p.x) - abs(E.x), p.y, p.z);
    float sbTop = max(gCab0.y + 0.16, E.y + 0.06);
    if(gModelId==2){
      float side=p.x<0.0?-1.0:1.0;
      res=opU(res,vec2(bushSeatPadded(p,side),12.0));
      res=opU(res,vec2(bushSeatRailSupports(p,side,f),60.0));
      res=opU(res,vec2(min(bushDrapedShoulderWeb(p,side),bushDrapedLapWeb(p,side)),69.0));
      res=opU(res,vec2(bushDrapedBuckle(p,side),60.0));
    } else if(gModelId==7){
      float side=p.x<0.0?-1.0:1.0;
      res=opU(res,vec2(swiftSeatPadded(p,side),12.0));
      res=opU(res,vec2(swiftSeatRailSupports(p,side,f),60.0));
      res=opU(res,vec2(min(swiftDrapedShoulderWeb(p,side),swiftDrapedLapWeb(p,side)),69.0));
      res=opU(res,vec2(swiftDrapedBuckle(p,side),60.0));
    } else if (sdBox(sp - vec3(0.0, 0.5*(E.y - 1.08 + sbTop), E.z + 0.13), vec3(max(gCab0.x, 0.2) + 0.08, 0.5*(sbTop - E.y + 1.08), 0.49)) < res.x) {
      float sw = gCab0.x;
      float seatDrop=gCabSeat.y;
      float seat = sdRoundBox(sp - vec3(0.0, E.y - seatDrop, E.z + 0.05), vec3(sw, 0.055, 0.24), 0.05);
      seat = smin(seat, sdCapsule(sp, vec3(-sw + 0.055, E.y-seatDrop+.03, E.z-.17), vec3(sw-.055,E.y-seatDrop+.03,E.z-.17), 0.045), 0.012);
      vec3 bp = sp - vec3(0.0, E.y - 0.4, E.z + 0.37); bp.yz = rot2(bp.yz, -0.18);   // reclined 10 deg: the top leans aft (+z)
      float bw = mix(min(sw, 0.2), min(sw, 0.145), smoothstep(-0.14, 0.28, bp.y));
      float back = sdRoundBox(bp, vec3(bw, 0.36, 0.05), 0.045);
      back = smin(back, sdRoundBox(vec3(abs(bp.x) - bw + 0.025, bp.y + 0.05, bp.z + 0.03), vec3(0.03, 0.26, 0.07), 0.03), 0.025);   // tapered upper bolsters clear the curved roof
      seat = min(seat, back);
      if (gCab0.y > -50.0) {   // headrest on two posts above the seat back
        vec3 hr = sp - vec3(0.0, gCab0.y, E.z + 0.455); hr.yz = rot2(hr.yz, -0.18);   // tilted with the back
        seat = min(seat, sdRoundBox(hr, vec3(0.11, 0.075, 0.045), 0.035));
        seat = min(seat, sdCapsule(vec3(abs(sp.x) - 0.06, sp.y, sp.z), vec3(0.0, E.y - 0.07, E.z + 0.43), vec3(0.0, gCab0.y - 0.05, E.z + 0.446), 0.008));
      }
      res = opU(res, vec2(seat, 12.0));
      float railX = min(0.15, sw*0.7);
      float railY=gCabSeat.x+.014,panBase=E.y-seatDrop-.055,postBase=railY+.014;
      float rails=sdRoundBox(vec3(abs(sp.x)-railX,sp.y-railY,sp.z-E.z),vec3(.012,.014,.34),.004);
      float postHalf=max((panBase-postBase)*.5,.015);
      rails=min(rails,sdRoundBox(vec3(abs(sp.x)-railX,sp.y-(postBase+postHalf),sp.z-E.z-.05),vec3(.013,postHalf,.016),.003));
      res = opU(res, vec2(rails, 60.0));
      vec3 hq = vec3(abs(sp.x) - 0.11, sp.y - (E.y - 0.43), sp.z - (E.z + 0.31)); hq.yz = rot2(hq.yz, -0.18);
      float belt = sdBox(hq, vec3(0.022, 0.33, 0.004));
      belt = min(belt, sdBox(vec3(sp.x, sp.y - (E.y-seatDrop+.07), sp.z - (E.z - 0.05)), vec3(sw - 0.01, 0.022, 0.004)));
      res = opU(res, vec2(belt, 69.0));
      res = opU(res, vec2(sdRoundBox(vec3(sp.x, sp.y - (E.y-seatDrop+.07), sp.z - (E.z - 0.055)), vec3(0.03, 0.02, 0.006), 0.004), 60.0));   // buckle
    }
    // Two compact linked center sticks in the trainers; other models retain their yokes.
    if(swiftCompactYoke()) {
      float side=p.x<0.0?-1.0:1.0;
      res=opU(res,vec2(swiftYokeSupport(p,side,f),60.0));
      res=partAt(res,PT_YOKE_SHAFT,vec2(side,0),p);
      res=partAt(res,PT_YOKE_WHEEL,vec2(side,0),p);
    } else if(bushmasterFloorStick()) {
      // No legacy yoke near-field bound: the entire floor blade must survive static bake.
      float side=p.x<0.0?-1.0:1.0;
      res=opU(res,vec2(bushmasterStickSupport(p,side,f),60.0));
      res=partAt(res,PT_YOKE_SHAFT,vec2(side,0),p);
      res=partAt(res,PT_YOKE_WHEEL,vec2(side,0),p);
    } else if(compactTrainerStick()) {
      float side=p.x<0.0?-1.0:1.0;vec3 pivot=trainerStickPivot(side);
      if(sdBox(p-pivot-vec3(0,-.15,0),vec3(.12,.50,.18))<res.x) {
        // Raked narrow support terminates in the unchanged inner floor; never outside skin.
        float floorY=gModelId==0?-.4018632253:-.4447097837;
        vec3 foot=vec3(pivot.x,floorY-.010,E.z-.300);
        vec3 elbow=vec3(pivot.x,pivot.y,E.z-.300);
        float column=min(sdCapsule(p,foot,elbow,.018),sdCapsule(p,elbow,pivot,.018));
        column=max(column,f+.025);
        res=opU(res,vec2(column,60.0));
        res=opU(res,vec2(length(p-pivot)-.018,66.0));
        res=partAt(res,PT_YOKE_SHAFT,vec2(side,0),p);
        res=partAt(res,PT_YOKE_WHEEL,vec2(side,0),p);
      }
    } else {
    // Shared floor-column architecture, with each reviewed bearing/rod dimension retained.
    if(floorSupportedYoke()) {
      bool utility=utilityFloorYoke();float ys=p.x<0.0?-1.0:1.0;
      vec3 top=cockpitYokeMount(ys)+vec3(0,0,utility?.100:.050);
      vec3 foot=utility?vec3(top.x,E.y-1.070,E.z-.260):vec3(top.x,gModelId==6?-.645622863:(gModelId==9?E.y-1.070:-.170000024),top.z);
      if(utility || sdBox(p-(foot+top)*.5,vec3(.040,(top.y-foot.y)*.5+.040,.040))<res.x) {
        float post=sdCapsule(p,foot,top-vec3(0,utility?.046:.048,0),utility?.022:.018);
        post=max(post,f+.025);
        vec3 b=p-top;float radial=length(b.xy);
        float bearing=max(max(radial-(utility?.034:.035),(utility?.020:.021)-radial),abs(b.z)-(utility?.030:.023));
        if(utility)bearing=max(bearing,f+.025);
        res=opU(res,vec2(post,60.0));res=opU(res,vec2(bearing,utility?60.0:66.0));
      }
    }
    // control yokes: pull moves toward the pilot, roll right turns the yoke clockwise - both yokes alike (each in the
    // pilot's own frame, not a mirror image: the copilot's turns the same way, as the linked controls do)
    {
      float ys = p.x < 0.0 ? -1.0 : 1.0;
      vec3 ym = fleetCabin()?cockpitYokeMount(ys):vec3(ys*abs(E.x),E.y-.43,pz);
      vec3 yp = vec3(-(p.x-ym.x),p.y-ym.y,p.z-ym.z);
      if (sdBox(yp - vec3(0,0,floorSupportedYoke()?.110:.170), vec3(.19,.19,floorSupportedYoke()?.230:.200)) < res.x) {
      if(!floorSupportedYoke()) res = opU(res, vec2(sdCylX(yp.zyx - vec3(0.05, 0.0, 0.0), 0.03, 0.012), 66.0));            // shaft collar
      res = partAt(res, PT_YOKE_SHAFT, vec2(ys, 0.0), p);   // the shaft and the wheel: rigid parts (plane_parts.glsl)
      res = partAt(res, PT_YOKE_WHEEL, vec2(ys, 0.0), p);
      }
    }
    }
    // rudder pedals with toe brakes on metal arms: right rudder pushes the right pedal forward
    {
      // Floor-mounted pedal faces, well forward of the yoke. The shared pose uses the fitted inner belly.
      if (sdBox(vec3(abs(p.x)-gCab2.z,p.y-gCab2.x,p.z-gCab2.y),vec3(.17,.12,.17))<res.x) {
        float ps=p.x<0.0?-1.0:1.0;
        res=partAt(res,PT_PEDAL,vec2(ps,p.x-ps*gCab2.z<0.0?-1.0:1.0),p);
      }
    }
    // Closed floor-backed heel rests support the 32-degree shoe contact paths in the glass twins.
    if(gModelId==6 || gModelId==9) {
      float heelTop=gCab2.x-.10618064348,heelZ=gCab2.y+.21857179004;
      float innerHalf=gModelId==6?.08155441626:.04153902776;
      float outerHalf=gModelId==6?.05978298455:.03892687641;
      vec3 hq=vec3(abs(p.x),p.y,p.z);
      float heel=sdRoundBox(hq-vec3(gCab2.z-gCab2.w,heelTop-innerHalf,heelZ),vec3(.061,innerHalf,.083),.006);
      heel=min(heel,sdRoundBox(hq-vec3(gCab2.z+gCab2.w,heelTop-outerHalf,heelZ),vec3(.061,outerHalf,.083),.006));
      heel=max(heel,f+.025);
      res=opU(res,vec2(heel,60.0));
    }
    // Closed, floor-supported heel pads under the trainers' sliding rudder contact paths.
    if(gModelId==2){
      float side=p.x<0.0?-1.0:1.0;float pair=p.x-side*gCab2.z<0.0?-1.0:1.0;
      float x=side*gCab2.z+pair*gCab2.w;
      res=opU(res,vec2(bushHeelPlate(p,x),61.0));res=opU(res,vec2(bushHeelPost(p,x,f),60.0));
    } else if(gModelId==7){
      float side=p.x<0.0?-1.0:1.0;float pair=p.x-side*gCab2.z<0.0?-1.0:1.0;
      float x=side*gCab2.z+pair*gCab2.w;
      res=opU(res,vec2(swiftHeelPlate(p,x),61.0));res=opU(res,vec2(swiftHeelPost(p,x,f),60.0));
    } else if(compactTrainerStick()) {
      { // nearest mirrored heel pad; four disconnected copies, no duplicated field evaluations
        float ss=p.x<0.0?-1.0:1.0;float qq=p.x-ss*gCab2.z<0.0?-1.0:1.0;
        vec3 heel=vec3(ss*gCab2.z+qq*gCab2.w,gCab2.x-.00794630,gCab2.y+.25374173);
        float plate=sdRoundBox(p-heel+vec3(0,.006,0),vec3(.055,.006,.085),.004);
        float leg=sdCapsule(p,vec3(heel.x,E.y-1.06,heel.z),heel-vec3(0,.012,0),.012);
        leg=max(leg,f+.025);
        res=opU(res,vec2(plate,61.0));res=opU(res,vec2(leg,60.0));
      }
    }
    // footwell wall: closes the space between the floor and the panel's lower edge; the pedals hang from it
    {
      float fy0 = (gModelId==2?.560:(gModelId==7?.520:E.y)) - 1.08, fy1 = (gModelId==2?.560:(gModelId==7?.520:E.y)) - 0.58;
      float fw = sdBox(p - vec3(0.0, 0.5*(fy0 + fy1), pz - 0.18), vec3(phw, 0.5*(fy1 - fy0), 0.04));
      res = opU(res, vec2(max(fw, f + 0.04), 11.0));
    }
    // Model2 power quadrant is outside the old pedestal's proximity/bake guard.
    if(swiftCompactYoke()){
      res=opU(res,vec2(swiftPowerFurniture(p),145.0));
      res=partAt(res,PT_THR_KNOB,vec2(0),p);
      float mix_=min(sdCapsule(p,vec3(.040,.070,-1.890),vec3(.040,.070,-1.715),.005),length(p-vec3(.040,.070,-1.710))-.018);
      res=opU(res,vec2(mix_,68.0));
    }
    if(bushmasterPowerLever()){
      res=opU(res,vec2(bushmasterPowerFurniture(p),145.0));
      res=partAt(res,PT_THR_KNOB,vec2(0),p);
      res=opU(res,vec2(bushmasterPowerLeverLocal(p-bushmasterPowerPivot(1.0),1.0,.018),68.0));
    }
    // centre pedestal: trim wheel, fuel selector; throttle (push-pull knobs or levers), mixture, flap lever
    {
      float pw,ph,pd; vec3 pc; partPedestal(pc,pw,ph,pd);
      if (sdBox(p-pc, vec3(pw+.12,ph+.24,pd+.12)) < res.x) {
      res = opU(res, compactTwinPowerBank()?twinPowerBankField(p,pc,pw,ph,pd):vec2(sdRoundBox(p-pc,vec3(pw,ph,pd),.03),63.0));
      vec3 tw = p - vec3(pw + 0.004, pc.y + ph*0.2, pc.z + pd*0.35);
      res = opU(res, vec2(sdCylX(tw, 0.075, 0.012), 66.0));
      res = opU(res, vec2(sdRoundCylX((p - vec3(0.0, pc.y + ph + 0.012, pc.z + pd*0.4)).yxz, 0.035, 0.012, 0.004), 66.0));   // fuel selector
      if (ck == 0) {
        if(!compactTrainerStick() && !bushmasterPowerLever() && !swiftCompactYoke()) {
        res = partAt(res, PT_THR_KNOB, vec2(0.0), p);   // (a rigid part: plane_parts.glsl)
        CockpitLayout ML=cockpitLayout();bool authoredRow=fleetCabin();
        float mixX=authoredRow?ML.controls.z+((gModelId==0||gModelId==1)?.065:.075):.06;
        float mixY=E.y-(authoredRow?ML.controls.w:.55);
        float mixZ0=pz+(authoredRow?-.012:.04);
        float mix_=min(sdCapsule(p,vec3(mixX,mixY,mixZ0),vec3(mixX,mixY,pz+.08),.005),length(p-vec3(mixX,mixY,pz+.085))-.018);
        res = opU(res, vec2(mix_, 68.0));
        }
      } else {
        res = partAt(res, PT_THR_LEVER, vec2(gModelId==4?1.0:(p.x < 0.0 ? -1.0 : 1.0), 0.0), p);   // the throttle levers and the flap lever: rigid parts (plane_parts.glsl)
        res = partAt(res, PT_FLAP_LEVER, vec2(0.0), p);
      }
      }
    }
    if (ck == 2) res = opU(res, vec2(sdRoundBox(p - vec3(0.0, gCab1.z, E.z - 0.2), vec3(0.22, 0.03, 0.3), 0.02), 14.0));
    // Legacy decorative switch row belongs to the old slab; authored decks supply their own controls.
    if(!fleetCabin()) {
      vec3 swp = p - vec3(0.0, E.y - 0.565, pz + 0.05);
      if (sdBox(swp - vec3(0.0, 0.0, 0.01), vec3(phw, 0.03, 0.03)) < res.x) {
      float sw = 0.032; float cell = clamp(floor(swp.x/sw + 0.5), -12.0, 12.0);
      swp.x -= cell*sw;
      float sws = sdRoundBox(swp - vec3(0.0, 0.0, 0.01), vec3(0.006, 0.012, 0.012), 0.003);
      sws = max(sws, abs(p.x) - phw*0.85);
      sws = max(sws, -(abs(p.x) - 0.13));                              // leave the radio stack clear
      res = opU(res, vec2(sws, 13.0));
      }
    }
    // Linked duplicated sidewall power controls leave the center leg corridor open.
    // These solids are outside the center pedestal's proximity guard.
    if(compactTrainerStick()) {
      float side=p.x<0.0?-1.0:1.0;vec3 q=vec3(abs(p.x),p.y,p.z);float wx=gCab0.w;
      vec3 cap=vec3(.425,E.y-.250,E.z-.270),mixcap=vec3(.350,E.y-.190,E.z-.265);
      float lower=sdRoundBox(q-cap,vec3(.020,.020,.025),.010);
      float upper=sdRoundBox(q-mixcap,vec3(.025),.010);
      vec3 foot=vec3(.425,E.y-.535,E.z-.270);
      float support=min(sdCapsule(q,foot,vec3(.425,E.y-.265,E.z-.270),.012),sdCapsule(q,foot,vec3(wx,E.y-.535,E.z-.270),.012));
      support=min(support,sdCapsule(q,mixcap,cap,.012));
      res=opU(res,vec2(min(lower,upper),145.0));res=opU(res,vec2(support,60.0));
      res=partAt(res,PT_THR_KNOB,vec2(side,0),p);
      vec3 mixGrip=trainerMixtureGrip(side);
      float mixture=min(sdCapsule(p,vec3(side*.350,E.y-.190,E.z-.265),mixGrip-vec3(0,0,.005),.005),length(p-mixGrip)-.018);
      res=opU(res,vec2(mixture,68.0)); // retains the authored fixed mixture handle semantics
    }
    // side trim panels with armrests, door handles and map pockets
    {
      float wx = gCab0.w;
      if (sdBox(vec3(abs(p.x) - wx, p.y - (swiftPreservedFurnitureY() - 0.73), p.z - (E.z - 0.2)), vec3(0.09, 0.36, 0.68)) < res.x) {
      vec3 ap = vec3(abs(p.x) - wx, p.y - (swiftPreservedFurnitureY() - (compactTrainerStick()?.535:.500)), p.z - (E.z - 0.15));
      res = opU(res, vec2(sdRoundBox(ap, vec3(0.05, 0.035, 0.38), 0.02), 12.0));
      float trim = sdRoundBox(vec3(abs(p.x) - wx + 0.02, p.y - (swiftPreservedFurnitureY() - 0.8), p.z - (E.z - 0.25)), vec3(0.025, 0.26, 0.6), 0.02);
      trim = max(trim, f + 0.045);
      res = opU(res, vec2(trim, 63.0));
      float handleZ=E.z+(compactTrainerStick()?.200:-.420);
      res=opU(res,vec2(sdCapsule(vec3(abs(p.x)-wx+.012,p.y-(swiftPreservedFurnitureY()-.420),p.z-handleZ),vec3(0),vec3(0,0,.110),.009),60.0));
      if(compactTrainerStick()) {
        vec3 hq=vec3(abs(p.x),p.y,p.z);
        float posts=min(sdCapsule(hq,vec3(wx-.012,swiftPreservedFurnitureY()-.560,E.z+.200),vec3(wx-.012,swiftPreservedFurnitureY()-.420,E.z+.200),.006),sdCapsule(hq,vec3(wx-.012,swiftPreservedFurnitureY()-.560,E.z+.310),vec3(wx-.012,swiftPreservedFurnitureY()-.420,E.z+.310),.006));
        res=opU(res,vec2(max(posts,f+.045),60.0));
      }
      }
    }
    // sun visors folded up against the headliner: a pad 2 cm thick that follows the roof's own curve (an even layer
    // under it), 26 cm across ahead of each seat. (A flat plate turned to the roof's slope met the curved roof only along
    // a line: the copilot's came apart where the cabin trimmed it and hung a loose black fragment at the windscreen top)
    vec3 vp = vec3(abs(p.x) - abs(E.x), p.y, p.z - (E.z - 0.30));
    if (!fleetCabin() && abs(vp.x) < 0.16 && abs(vp.z) < 0.08 && p.y > E.y + 0.04) {
      float pad = max(abs(f + 0.071) - 0.012, 0.02 - winHole);                  // 2.4 cm, 3 mm under the headliner, 2 cm clear of the windows
      float outline = sdRoundBox(vec3(vp.x, 0.0, vp.z), vec3(0.13, 1.0, 0.05), 0.03);
      res = opU(res, vec2(-smin(-pad, -outline, 0.012), 63.0));   // (rounded where the outline meets the pad: a clean edge off the lattice; in the cabin's trim)
    }
    // overhead console: dome light and two map lights (modelled lenses - the cabin's night lighting)
    {
      vec3 oc = p - vec3(0.0, ck == 2 ? gCab1.z - 0.075 : gCab0.z, E.z - 0.05);   // (under the glass cockpit's overhead plate, which hid its lenses)
      if (sdBox(oc, vec3(0.12, 0.06, 0.22)) < res.x) {
      res = opU(res, vec2(sdRoundBox(oc, vec3(0.09, 0.025, 0.18), 0.015), 14.0));
      res = opU(res, vec2(sdRoundCylX((oc + vec3(0.0, 0.024, 0.02)).yxz, 0.04, 0.004, 0.002), 64.0));
      res = opU(res, vec2(sdRoundCylX((vec3(abs(oc.x) - 0.06, oc.y + 0.024, oc.z - 0.12)).yxz, 0.013, 0.004, 0.002), 64.0));
      }
    }
    // the magnetic compass on the glareshield: a rounded black housing, its card behind a window on the face towards the
    // pilot (78: the card, plane_material.glsl). It was a plain black box
    if (!fleetCabin()) {
    vec3 cq = p - vec3(0.0, E.y - 0.101, pz - 0.05);   // (on the hood: its top 13.5 cm under the eye)
    float compass = sdRoundBox(cq, vec3(0.045, 0.034, 0.03), 0.012);   // (the half sizes include the rounding: its face is flat 33 x 22 mm either side)
    res = opU(res, vec2(compass, cq.z > 0.024 && abs(cq.x) < 0.03 && abs(cq.y - 0.002) < 0.017 ? 78.0 : 66.0));
    }
  }
  if (inside > 0.5 && gModelId == kOspreyModel) res = opU(res, mapOspreyCabinTrim(p));   // the Osprey's cabin trim
  return res;
}
