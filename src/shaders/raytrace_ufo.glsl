//! kRaytraceUfo
//! ------------------------------------------------------------------------------------------------
//! UFO encounter: a saucer with a pressure cabin whose hatch rides open on a carrier over two visitors who dance,
//! laugh and wave (Codex's visitor crew and craft; own piece: MSVC 64 KB limit). Geometry and pose depend only on
//! the encounter state (uUfoAnim) and the visitor's side, never on the camera; the cabin floor and both visitors
//! exist at every hatch phase, hidden by the shell and the hatch themselves.

uniform int uUfoOn; uniform vec3 uUfoPos; uniform mat3 uUfoRot;
uniform vec4 uUfoAnim;   // hatch open 0..1, animation clock (s), laugh 0..1, wave 0..1
// Visitor anatomy v2. Units: local metres. +X front, +Y up, +Z left.
// Geometry and pose depend ONLY on encounter state and visitor side, never camera.
struct AlienPose {vec3 pelvis;vec3 chest;vec3 head;vec3 hip[2];vec3 knee[2];vec3 ankle[2];vec3 shoulder[2];vec3 elbow[2];vec3 wrist[2];float nod;float yaw;float laugh;float wave;};
vec3 alienKnee(vec3 root,vec3 end,float upper,float lower,vec3 pole){
 vec3 d=end-root;float l=clamp(length(d),.001,upper+lower-.0001);vec3 axis=normalize(d);
 float along=(upper*upper-lower*lower+l*l)/(2.*l);
 vec3 bend=normalize(pole-axis*dot(pole,axis));
 return root+axis*along+bend*sqrt(max(upper*upper-along*along,0.));
}
AlienPose alienPose(float T,float side,float laugh,float wave){
 AlienPose r;float dance=(1.-laugh)*(1.-wave),ph=T*5.2+side*1.3;
 // Weight shifts over two fixed support soles. Pelvis compression, never root levitation.
 r.pelvis=vec3(-.012,.638-.025*(.5+.5*cos(ph))*dance-.010*laugh*(.5+.5*sin(T*13.)),.032*sin(ph*.5)*dance);
 r.chest=r.pelvis+vec3(.010+.022*laugh,.359,.013*sin(ph*.5)*dance);
 r.head=r.chest+vec3(.002, .360,0.);
 r.nod=-.10*laugh+.025*sin(T*13.)*laugh+.035*sin(ph)*dance;r.yaw=.10*sin(ph*.5)*dance;
 r.laugh=laugh;r.wave=wave;
 for(int j=0;j<2;j++){
  float s=j==0?-1.:1.;r.hip[j]=r.pelvis+vec3(0.,0.,s*.095);r.ankle[j]=vec3(.012,.085,s*.128);
  r.knee[j]=alienKnee(r.hip[j],r.ankle[j],.295,.285,vec3(1,0,0));
  r.shoulder[j]=r.chest+vec3(0.,.068,s*.19);
  vec3 rest=r.shoulder[j]+vec3(.055,-.42,s*.075);
  vec3 danceTarget=r.shoulder[j]+vec3(.12,-.13+.18*sin(ph+s*1.3),s*(.25+.035*cos(ph)));
  vec3 belly=r.pelvis+vec3(.137,.195,s*.145);
  vec3 salute=r.shoulder[j]+vec3(.07,.30,s*.19+.055*sin(T*8.));
  vec3 hand=mix(mix(danceTarget,belly,laugh),s==side?salute:rest,wave);
  vec3 dir=hand-r.shoulder[j];hand=r.shoulder[j]+normalize(dir)*min(length(dir),.498);
  r.wrist[j]=hand;r.elbow[j]=alienKnee(r.shoulder[j],hand,.265,.245,vec3(-.4,-.2,s));
 }
 return r;
}
vec3 alienHeadPoint(vec3 a,AlienPose r){vec3 q=a-r.head;q.xy=rot2(q.xy,r.nod);q.xz=rot2(q.xz,r.yaw);return q;}
// Anatomical frame: +front is the PALM normal, +up points along fingers.
// Thumb is always on the mirrored radial (body-side) edge. During laugh the
// fingers turn inward, palm faces the belly, and that same radial edge turns up.
mat3 alienHandFrame(vec3 wrist,vec3 elbow,float signSide,float laugh){
 vec3 up=normalize(mix(normalize(wrist-elbow),vec3(.22,0.,-.9754999*signSide),laugh));
 vec3 thumbAim=normalize(mix(vec3(0.,0.,-signSide),vec3(0.,1.,0.),laugh));
 vec3 thumb=normalize(thumbAim-up*dot(thumbAim,up));
 vec3 across=-signSide*thumb,front=normalize(cross(up,across));
 return mat3(front,up,across);
}
// Four distinct fingers, two phalanges each, and opposed two-phalange thumb.
float alienHand(vec3 p,vec3 wrist,vec3 elbow,float signSide,float open,float laugh){
 mat3 frame=alienHandFrame(wrist,elbow,signSide,laugh);vec3 front=frame[0],up=frame[1],across=frame[2];
 vec3 v=p-wrist;vec3 q=vec3(dot(v,front),dot(v,up),dot(v,across));
 float d=sdEllipsoid(q-vec3(0,.037,0),vec3(.026,.049,.045));
 for(int f=0;f<4;f++){
  float z=(float(f)-1.5)*.023;float len=.056-.009*abs(float(f)-1.3);
  vec3 a=vec3(0,.068,z),b=a+vec3(.006*(1.-open),len*.53,z*.13*open),c=b+vec3(.018*(1.-open),len*.47,z*.13*open);
  d=smin(d,min(sdCapsule(q,a,b,.010),sdCapsule(q,b,c,.009)),.009);
 }
 vec3 ta=vec3(mix(.007,-.002,laugh),.019,-signSide*.035),tb=vec3(mix(.024,-.006,laugh),.043,-signSide*.060),tc=vec3(mix(.030,-.012,laugh),.069,-signSide*.073);
 d=smin(d,min(sdCapsule(q,ta,tb,.014),sdCapsule(q,tb,tc,.011)),.015);return d;
}
vec2 mapAlien(vec3 a,float side){
 float outer=sdBox(a-vec3(.04,.83,0),vec3(.46,.84,.72));if(outer>.08)return vec2(outer,79.);
 AlienPose r=alienPose(uUfoAnim.y,side,uUfoAnim.z,uUfoAnim.w);vec2 res=vec2(1e5,79.);
 // Structured pressure garment: shaped pelvis, abdomen, rib cage, natural shoulder girdle.
 float suit=smin(sdEllipsoid(a-r.pelvis-vec3(0,.038,0),vec3(.121,.13,.158)),sdEllipsoid(a-r.chest+vec3(0,.065,0),vec3(.12,.213,.181)),.055);
 for(int j=0;j<2;j++){
  float s=j==0?-1.:1.;
  float leg=smin(sdRoundCone(a,r.hip[j],r.knee[j],.088,.061),sdRoundCone(a,r.knee[j],r.ankle[j],.061,.044),.025);
  suit=smin(suit,leg,.035);
  float arm=smin(sdRoundCone(a,r.shoulder[j],r.elbow[j],.068,.045),sdRoundCone(a,r.elbow[j],r.wrist[j],.045,.029),.018);
  suit=smin(suit,arm,.027);
  vec3 boot=a-vec3(.052,.059,s*.128);
  res=opU(res,vec2(sdEllipsoid(boot,vec3(.127,.059,.061)),80.));
  if(length(a-r.wrist[j])-.17<min(res.x,suit))res=opU(res,vec2(alienHand(a,r.wrist[j],r.elbow[j],s,mix(.65,1.,max(r.wave,r.laugh)),r.laugh*(1.-r.wave)),72.));
  // Sleeve cuff seals and restrained shoulder guards, no floating jewellery.
  vec3 ax=normalize(r.wrist[j]-r.elbow[j]),cq=a-(r.wrist[j]-.015*ax);
  res=opU(res,vec2(max(length(cq-ax*dot(cq,ax))-.036,abs(dot(cq,ax))-.014),81.));
 }
 res=opU(res,vec2(suit,79.));
 vec3 collar=a-(r.chest+vec3(0,.122,0));
 res=opU(res,vec2(length(vec2(length(collar.xz)-.058,collar.y))-.012,80.));
 vec3 chestPanel=a-r.chest+vec3(-.008,.055,0);
 float panel=max(sdEllipsoid(chestPanel,vec3(.119,.165,.148)),-chestPanel.x+.072);
 res=opU(res,vec2(panel,84.));
 float zipper=sdCapsule(a,r.chest+vec3(.117,-.14,0),r.chest+vec3(.112,.067,0),.007);
 res=opU(res,vec2(zipper,80.));
 vec3 badge=a-r.chest-vec3(.115,.029,-.076);
 res=opU(res,vec2(sdRoundBox(badge,vec3(.012,.026,.026),.007),81.));
 float neck=sdCapsule(a,r.chest+vec3(0,.115,0),r.head+vec3(-.035,-.115,0),.053);
 res=opU(res,vec2(neck,72.));
 if(length(a-r.head)-.30>res.x)return res;
 vec3 q=alienHeadPoint(a,r);
 float head=smin(sdEllipsoid(q-vec3(-.025,.045,0),vec3(.167,.209,.178)),sdEllipsoid(q-vec3(.029,-.095,0),vec3(.130,.115,.126)),.047);
 // Brow, cheek pads and nose bridge provide a readable three dimensional face.
 head=smin(head,sdEllipsoid(q-vec3(.130,.014,0),vec3(.049,.080,.038)),.022);
 head=smin(head,sdEllipsoid(vec3(q.x-.091,q.y+.065,abs(q.z)-.086),vec3(.032,.036,.039)),.024);
 // Subtle orbital ridges and a narrow nasal tip, not spherical cheek mounds.
 head=smin(head,sdEllipsoid(vec3(q.x-.101,q.y-.068,abs(q.z)-.086),vec3(.043,.015,.061)),.013);
 head=smin(head,sdEllipsoid(q-vec3(.156,-.037,0),vec3(.027,.028,.026)),.012);
 float skin=smin(head,neck,.025);
 vec3 eq=vec3(q.x-.134,q.y-.029,abs(q.z)-.087);eq.yz=rot2(eq.yz,.24);
 float socket=max(sdEllipsoid(eq,vec3(.057,.045,.060)),abs(eq.y)+.25*abs(eq.z)-.044);skin=max(skin,-socket);
 float mouthOpen=.004+.029*r.laugh;vec3 mq=q-vec3(.145,-.122,0);
 // Lips make a small upturned arc rather than a flat painted mouth.
 mq.y-=(.035+.13*r.wave)*mq.z*mq.z/.06;
 float mouth=sdEllipsoid(mq,vec3(.028,mouthOpen,.043));skin=max(skin,-mouth);
 float nostril=sdEllipsoid(vec3(q.x-.175,q.y+.048,abs(q.z)-.018),vec3(.009,.006,.006));
 skin=max(skin,-nostril);
 res=opU(res,vec2(skin,72.));
 res=opU(res,vec2(sdEllipsoid(vec3(q.x-.168,q.y+.048,abs(q.z)-.018),vec3(.007,.005,.005)),73.));
 float eye=max(sdEllipsoid(eq+vec3(.008,0,0),vec3(.050,.041,.055)),abs(eq.y)+.25*abs(eq.z)-.040);
 // Iris/pupil are pigmentation on the curved corneal surface, never protruding geometry.
 float ir=length(vec2((q.y-.029)/.024,(abs(q.z)-.088)/.027));
 res=opU(res,vec2(eye,ir>.43&&ir<1.?82.:73.));
 res=opU(res,vec2(sdEllipsoid(mq+vec3(.009,0,0),vec3(.023,mouthOpen*.82,.040)),74.));
 return res;
}
// Call for alien material IDs from shadeUfo; body-space coordinate is sufficient for subtle surface grain.
void alienMaterial(int id,vec3 p,inout Mat m){
 m.metal=0.;m.emit=vec3(0.);m.nrm=vec3(0,0,1);
 if(id==72){float grain=sin(p.x*175.)*sin(p.y*183.)*sin(p.z*169.);m.alb=vec3(.29,.37,.285)*(1.+.022*grain);m.rough=.76;}
 if(id==73){m.alb=vec3(.012,.022,.019);m.rough=.18;}
 if(id==74){m.alb=vec3(.095,.026,.024);m.rough=.69;}
 if(id==79){float weave=sin(p.y*270.)*sin(p.z*250.);m.alb=vec3(.055,.085,.091)*(1.+.035*weave);m.rough=.69;}
 if(id==80){m.alb=vec3(.025,.032,.033);m.rough=.77;}
 if(id==81){m.alb=vec3(.40,.44,.38);m.metal=.55;m.rough=.34;}
 if(id==84){m.alb=vec3(.105,.143,.145);m.rough=.62;}
 if(id==82){m.alb=vec3(.022,.047,.032);m.rough=.18;}
}

// UFO proposal: all geometry is present at every encounter phase.
// Body coordinates in metres; +x is the hatch / player-facing side.
// Material IDs 70/71/75..78,85..89 belong to the craft. Alien IDs are separate.
float ufoRing(vec3 p, float radius, float tube) { return length(vec2(length(p.xz)-radius,p.y))-tube; }
vec3 ufoSector(vec3 p, float count) {
  float a=atan(p.z,p.x), s=6.28318530718/count;
  a=mod(a+s*.5,s)-s*.5;
  return vec3(length(p.xz)*cos(a),p.y,length(p.xz)*sin(a));
}
float ufoAperture(vec3 q) { return sdRoundBox(q-vec3(2.95,1.40,0),vec3(1.40,1.38,1.82),.20); }
// Radially ruled opening: its boundary is constant in angle through the skin.
// This prevents the expanding hatch edges cutting into a Cartesian box lip.
float ufoDoorCut(vec3 q) {
  float rr=length(q);
  return ufoAperture(q*(3.04/max(rr,.001)))*min(rr/3.04,1.0)*.92;
}
// Separate rigid part transform for future mesh extraction: radial lift, then yaw.
// A rigid 0.36m outward carrier translation precedes the bearing yaw.
vec2 ufoHatchRig(float h) {
  return vec2(.36*smoothstep(0.0,.20,h),1.95*smoothstep(.20,1.0,h));
}
float ufoHatch(vec3 q, float h) {
  vec2 rig=ufoHatchRig(h); q.xz=rot2(q.xz,-rig.y); q.x-=rig.x;
  float skin=max(abs(length(q)-3.04)-.06,-q.y);
  return max(skin,ufoDoorCut(q)+.018);
}
// Captured carriage on the fixed roof race. Sleeve yaws with the carrier;
// piston and door bracket translate outward, then yaw together. These are
// separately extractable rigid parts; only bearing/piston engagement contacts.
vec2 ufoHatchHardware(vec3 q, float h) {
  vec2 rig=ufoHatchRig(h); q.xz=rot2(q.xz,-rig.y);
  vec3 car=q-vec3(1.47,2.82,0);
  float saddle=sdRoundBox(car,vec3(.135,.10,.17),.028);
  float slot=sdRoundBox(car,vec3(.054,.052,.22),.025);
  vec2 res=vec2(max(max(saddle,-slot),3.12-length(q)),85);
  // Outer captured slide and polished piston, both horizontal and load bearing.
  res=opU(res,vec2(sdCapsule(q,vec3(1.53,2.82,0),vec3(1.81,2.82,0),.066),86));
  res=opU(res,vec2(sdCapsule(q,vec3(1.40+rig.x,2.82,0),vec3(1.83+rig.x,2.82,0),.041),85));
  vec3 a=q; a.x-=rig.x;
  // A fork is attached to the pressure door with two flush hinge lugs.
  vec3 fork=vec3(a.x,a.y,abs(a.z));
  res=opU(res,vec2(sdCapsule(fork,vec3(1.83,2.82,.13),vec3(1.80,2.49,.13),.047),85));
  res=opU(res,vec2(sdCapsule(a,vec3(1.83,2.82,-.13),vec3(1.83,2.82,.13),.047),85));
  res=opU(res,vec2(sdRoundBox(fork-vec3(1.80,2.49,.13),vec3(.077,.080,.054),.020),86));
  return res;
}
vec2 mapUfoStatic(vec3 p) {
  float r=length(p.xz); vec3 q=p-vec3(0,.95,0);
  float hull=sdEllipsoid(p,vec3(7.35,.94,7.35));
  hull=smin(hull,sdEllipsoid(p-vec3(0,.36,0),vec3(4.55,.84,4.55)),.18);
  hull=smin(hull,sdEllipsoid(p-vec3(0,-.58,0),vec3(3.40,.91,3.40)),.14);
  // Real pressure cabin cut out of the upper hull, with no phase-dependent reveal.
  float cavity=max(length(q)-2.98,.025-q.y);
  float threshold=max(ufoAperture(q),.025-q.y);
  hull=max(hull,-min(cavity,threshold));
  // Shallow radial panel seams are cut into the upper shell, not painted stripes.
  vec3 sec=ufoSector(p,20.0);
  float groove=max(max(abs(sec.z)-.016,3.45-r),max(r-7.05,.20-p.y));
  groove=max(groove,-(hull+.027)); hull=max(hull,-groove);
  vec2 res=vec2(hull,70);
  // Replaceable inspection covers are tangent-mounted over the pressure hull.
  // Each has a dark captured gasket, a metal lid and two recessed fastener heads.
  vec3 cover=sec-vec3(5.46,.648,0); cover.xy=rot2(cover.xy,.142);
  res=opU(res,vec2(sdRoundBox(cover,vec3(.51,.027,.285),.016),86));
  res=opU(res,vec2(sdRoundBox(cover-vec3(0,.012,0),vec3(.474,.025,.248),.016),85));
  vec3 fast=vec3(abs(cover.x)-.41,cover.y-.039,cover.z);
  res=opU(res,vec2(max(abs(fast.y)-.006,length(fast.xz)-.025),86));
  // Load-bearing rim extrusion, dark recessed waist, and thin captured light lens.
  res=opU(res,vec2(sdRoundBox(vec3(r-7.13,p.y,0),vec3(.31,.22,.2),.085),85));
  res=opU(res,vec2(sdRoundBox(vec3(r-7.425,p.y,0),vec3(.028,.105,.10),.022),86));
  // Twenty-four discrete navigation windows; solid baffles break up the ring.
  vec3 rim=ufoSector(p,24.0);
  float lens=max(ufoRing(p,7.445,.027),abs(rim.z)-.49);
  res=opU(res,vec2(lens,75));
  float dome=max(abs(length(q)-3.04)-.06,-q.y);
  dome=max(dome,-ufoDoorCut(q));
  res=opU(res,vec2(dome,71));
  // Fixed gasket around the aperture is inside the shell, never in the door sweep.
  float gasket=max(abs(length(q)-2.960)-.020,abs(ufoDoorCut(q))-.043);
  gasket=max(gasket,-q.y); res=opU(res,vec2(gasket,86));
  // Cabin base pressure flange and upper hatch-bearing race.
  res=opU(res,vec2(max(ufoRing(q-vec3(0,.025,0),3.07,.065),-ufoDoorCut(q)),85));
  res=opU(res,vec2(ufoRing(q-vec3(0,2.82,0),1.47,.045),85));
  vec3 raceMount=ufoSector(q,8.0);
  res=opU(res,vec2(sdCapsule(raceMount,vec3(1.38,2.76,0),vec3(1.47,2.82,0),.033),85));
  // Recessed central service mast and antenna, modest scale and emissive area.
  res=opU(res,vec2(sdRoundBox(q-vec3(0,3.04,0),vec3(.28,.115,.28),.07),85));
  res=opU(res,vec2(sdCapsule(q,vec3(0,3.10,0),vec3(0,3.65,0),.025),85));
  res=opU(res,vec2(length(q-vec3(0,3.68,0))-.055,75));
  // Real floor slab, centred within the carved pressure volume. Feet top = .04.
  float floorD=max(abs(q.y+.025)-.065,length(q.xz)-2.975);
  res=opU(res,vec2(floorD,77));
  float floorRing=max(ufoRing(q-vec3(0,.044,0),2.42,.018),-q.x-.45);
  res=opU(res,vec2(floorRing,88));
  // Fixed interior fixtures, all outside the two performers' working envelope.
  vec3 side=vec3(q.x,q.y,abs(q.z));
  float bench=sdRoundBox(side-vec3(-.85,.30,2.13),vec3(.62,.26,.28),.12);
  res=opU(res,vec2(bench,86));
  float back=sdRoundBox(side-vec3(-1.08,.67,2.10),vec3(.30,.34,.21),.12);
  res=opU(res,vec2(back,87));
  vec3 console=q-vec3(-2.08,.61,0); console.xy=rot2(console.xy,-.23);
  res=opU(res,vec2(sdRoundBox(console,vec3(.32,.57,1.12),.10),85));
  res=opU(res,vec2(sdRoundBox(console-vec3(.321,.19,0),vec3(.017,.24,.91),.025),89));
  // Ceiling lamps sit on the inner sphere and face the cabin; no reveal switch.
  res=opU(res,vec2(sdRoundBox(q-vec3(-.25,2.77,0),vec3(.40,.045,.44),.04),88));
  // Six physically shrouded underside drives, intake recesses and centre keel.
  vec3 eng=ufoSector(p,6.0)-vec3(4.75,-.51,0);
  float er=length(eng.xz);
  vec3 mount=vec3(eng.x,eng.y,abs(eng.z));
  res=opU(res,vec2(sdCapsule(mount,vec3(0,.12,.55),vec3(0,-.25,.38),.075),85));
  float pod=sdRoundBox(vec3(er-.39,eng.y+.18,0),vec3(.12,.29,.20),.06);
  res=opU(res,vec2(pod,85));
  res=opU(res,vec2(max(abs(eng.y+.42)-.045,er-.33),86));
  res=opU(res,vec2(length(vec2(er-.255,eng.y+.472))-.026,76));
  res=opU(res,vec2(ufoRing(p-vec3(0,-1.34,0),2.08,.105),85));
  res=opU(res,vec2(ufoRing(p-vec3(0,-1.43,0),1.96,.038),76));
  float keel=max(abs(p.y+1.47)-.11,r-1.65);
  res=opU(res,vec2(keel,86));
  // Retained bounding sphere: farthest rim < 7.48m; mast top 4.685m.
  return res;
}
vec2 mapUfoStructure(vec3 p) {
  vec3 q=p-vec3(0,.95,0);
  return opU(mapUfoStatic(p),opU(vec2(ufoHatch(q,uUfoAnim.x),78),ufoHatchHardware(q,uUfoAnim.x)));
}
vec2 mapUfo(vec3 p) {
  vec2 res=mapUfoStructure(p); vec3 q=p-vec3(0,.95,0);
  const float AS=1.45;
  vec2 a=mapAlien((q-vec3(0.0,.04,-.7))/AS,-1.0); a.x*=AS;
  vec2 b=mapAlien((q-vec3(.8,.04,.75))/AS,1.0); b.x*=AS;
  return opU(res,opU(a,b));
}
// Pure material function; the raster migration can use it without changing the rig.
void ufoCraftMaterial(inout Mat m, int id, vec3 p, vec3 n) {
  float r=length(p.xz), a=atan(p.z,p.x);
  m.nrm=vec3(0,0,1); m.emit=vec3(0); m.metal=.78; m.rough=.30;
  m.alb=vec3(.18,.23,.26);
  if(id==70) {
    // Fine machining, deliberately restrained so the structural silhouette dominates.
    float grain=.96+.04*sin(r*310.0);
    float panel=mod(floor((a+3.14159265)*20.0/6.2831853),4.0);
    m.alb=vec3(.18,.23,.26)*grain*(.88+.055*panel); m.rough=.36;
    if(p.y<-.10) {m.alb*=.62;m.rough=.42;}
    if(r>6.80) m.alb*=.60;
    if(r>4.5&&r<4.55 || r>6.56&&r<6.60) m.alb*=.60;
  } else if(id==71||id==78) {
    m.alb=mix(vec3(.10,.155,.19),vec3(.19,.13,.21),.22*pow(abs(n.z),3.0)); m.rough=.31;
    vec3 q=p-vec3(0,.95,0);
    if(q.y<.34) {m.alb=vec3(.042,.065,.075);m.rough=.44;}
    if(dot(n,normalize(q))<0.0) {m.alb=vec3(.18,.21,.23);m.metal=.25;m.rough=.57;}
    if(id==78) {m.alb*=1.13; float h=ufoHatchRig(uUfoAnim.x).y; vec3 d=q;d.xz=rot2(d.xz,-h);
      if(abs(d.z)<.55 && d.y>1.30&&d.y<1.39) {m.alb=vec3(.075,.12,.14);m.metal=.35;}}
  } else if(id==85) { m.alb=vec3(.26,.30,.32);m.rough=.29; }
  else if(id==86) {m.alb=vec3(.035,.047,.055);m.rough=.52;m.metal=.40;}
  else if(id==87) {m.alb=vec3(.16,.20,.21);m.rough=.72;m.metal=.0;}
  else if(id==75) {m.alb=vec3(.12,.25,.28);m.metal=.05;m.rough=.17;
    float amber=step(4.5,mod(floor((a+3.14159265)*24.0/6.2831853),6.0));
    m.emit=p.y>4.0?vec3(.75,.22,.055):mix(vec3(.10,.48,.59),vec3(.75,.33,.08),amber)*1.15;}
  else if(id==76) {m.alb=vec3(.10,.18,.22);m.metal=.05;m.rough=.22;m.emit=vec3(.18,.54,.74)*1.5;}
  else if(id==77) {m.alb=vec3(.085,.105,.12);m.metal=.28;m.rough=.58;
    vec2 seam=abs(fract(p.xz/.48)-.5); if(max(seam.x,seam.y)>.473) m.alb*=.5;}
  else if(id==88) {m.alb=vec3(.30,.42,.44);m.metal=0.0;m.emit=vec3(.45,.70,.74)*1.1;}
  else if(id==89) {m.alb=vec3(.015,.037,.045);m.metal=.0;m.rough=.21;
    float stripe=step(.82,fract(p.z*4.0))*step(.4,fract(p.y*10.0));m.emit=vec3(.10,.50,.55)*(.22+.65*stripe);}
}

float traceUfo(vec3 ro, vec3 rd, float tmax){
  vec3 oc = ro - uUfoPos; float b = dot(oc, rd), h = b*b - dot(oc, oc) + 81.0;
  if (h < 0.0) return -1.0;
  h = sqrt(h); float t = max(-b - h, 0.0), t1 = min(-b + h, tmax);
  if (t > t1) return -1.0;
  mat3 inv = transpose(uUfoRot); vec3 lo = inv*(ro - uUfoPos), ld = inv*rd;
  for (int i = 0; i < 140; i++) {
    float d = mapUfo(lo + ld*t).x;
    if (d < 0.002*max(1.0, t*0.02)) return t;
    t += d*0.85;
    if (t > t1) break;
  }
  return -1.0;
}
// The UFO's surface at a hit: its material and normal, for the ray tracer (shadeUfo) and the objects pass alike.
// cabin: a pixel inside the dome (a visitor or the floor), which carries the dance floor's and the ceiling lamps'
// light as emission so it needs no sun; ao: the floor's close-range contact occlusion under the visitors' boots
// (three bounded taps; the objects pass writes it to the G-buffer, the ray tracer applies it once here).
void ufoMaterial(vec3 p, vec3 rd, float t, out Mat m, out vec3 n, out bool cabin, out float ao){
  mat3 inv = transpose(uUfoRot);
  vec3 lp = inv*(p - uUfoPos);
  const vec2 k = vec2(1, -1); float e = 0.004;
  vec3 ln = normalize(k.xyy*mapUfo(lp + k.xyy*e).x + k.yyx*mapUfo(lp + k.yyx*e).x + k.yxy*mapUfo(lp + k.yxy*e).x + k.xxx*mapUfo(lp + k.xxx*e).x);
  int mid = int(mapUfo(lp).y + 0.5);
  n = uUfoRot*ln;
  float T = uUfoAnim.y;
  m.alb = vec3(0.7); m.rough = 0.3; m.metal = 0.0; m.emit = vec3(0.0); m.nrm = vec3(0, 0, 1);
  vec3 disco = 0.5 + 0.5*cos(T*3.0 + vec3(0.0, 2.1, 4.2));
  bool visitor = (mid >= 72 && mid <= 74) || (mid >= 79 && mid <= 82) || mid == 84;
  if (visitor) alienMaterial(mid, lp, m);
  else ufoCraftMaterial(m, mid, lp, ln);
  n = applyTS(n, m.nrm, 0.2);
  cabin = visitor || mid == 77;
  if (cabin) {   // the cabin's own light: the dance floor's colour from below, the cool ceiling lamps from above
    vec3 L1 = normalize(uUfoRot*vec3(0.0, 1.0, 0.0));
    m.emit += m.alb*(disco*0.8*max(dot(n, -L1), 0.0) + vec3(0.6, 0.9, 1.0)*0.7*max(dot(n, L1), 0.0) + 0.15);
  }
  ao = 1.0;
  if (mid == 77) {   // bounded floor-contact shading: only close-range occlusion, the geometry is unchanged
    float occ = 0.0;
    occ += 0.5*max(0.0, 0.025 - mapUfo(lp + ln*0.025).x)/0.025;
    occ += 0.3*max(0.0, 0.070 - mapUfo(lp + ln*0.070).x)/0.070;
    occ += 0.2*max(0.0, 0.140 - mapUfo(lp + ln*0.140).x)/0.140;
    ao = clamp(1.0 - 0.65*occ, 0.35, 1.0);
  }
}
vec3 shadeUfo(vec3 p, vec3 rd, float t){
  Mat m; vec3 n; bool cabin; float ao;
  ufoMaterial(p, rd, t, m, n, cabin, ao);
  return shadeSurface(p, n, rd, m, cloudShadow(p))*ao;
}
