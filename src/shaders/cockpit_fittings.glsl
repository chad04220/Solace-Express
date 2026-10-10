//! kCockpitFittings
//! Modular conventional flight decks, using the reviewed per-model layout record.
// Four bounded modules: pilot scan, instructor/co-pilot scan, engine/fuel, and live flight-status utility.
// The faces reuse the current instrument atlas and glass engine page. Their geometry and material coordinates
// come from these same mounts; no extra camera, light, display pass, or material texture is needed.
vec4 fleetModule(int i){ CockpitLayout L=cockpitLayout(); return i==0?L.pilot:i==1?L.copilot:i==2?L.systems:L.status; }
vec2 fleetModuleHalf(int i,vec4 m){ return i<2?cockpitFlightHalf(m.w):i==2?cockpitSystemsHalf(m.w):vec2(m.w,m.w*.70); }
vec3 fleetModuleFrame(vec3 p,int i,vec4 mount){ vec3 q=p-cockpitMount(mount); if(i==3)q.yz=rot2(q.yz,.35); if(MODEL_IS(14)&&i<2)q.xz=rot2(q.xz,i==0?.16:-.16); return q; }
float fleetHousing(vec3 q,vec2 h,float radius,float chamfer){
  float body=sdRoundBox(q,vec3(h+vec2(.022),.050),radius);
  // Deliberate angular chamfers distinguish utility/research structures from the broad touring radii.
  float corner=(abs(q.x)+abs(q.y)-(h.x+h.y+.044-chamfer))*.70710678;
  return max(body,corner);
}
#if HAS_ATLAS || HAS_MERIDIAN
vec3 fleetRoundedHousingNormal(vec3 q, vec2 h, float radius, float chamfer) {
  vec3 b=vec3(h+vec2(.022),.050);
  vec3 d=abs(q)-b+radius;
  vec3 outer=max(d,vec3(0));
  float body=length(outer)+min(max(d.x,max(d.y,d.z)),0.0)-radius;
  float corner=(abs(q.x)+abs(q.y)-(h.x+h.y+.044-chamfer))*.70710678;
  if(corner>body) return vec3(sign(q.xy)*.70710678,0);
  if(dot(outer,outer)>1e-12) return normalize(outer)*sign(q);
  // Interior branch is needed because baked/sunk triangles can lie inside the field.
  if(d.x>d.y && d.x>d.z) return vec3(sign(q.x),0,0);
  if(d.y>d.z) return vec3(0,sign(q.y),0);
  return vec3(0,0,sign(q.z));
}
bool fleetPrimaryBezelNormal(vec3 p,int mid,out vec3 n) {
  if((!MODEL_IS(14) && !MODEL_IS(5)) || mid!=145) return false;
  CockpitLayout L=cockpitLayout();
  for(int i=0;i<2;i++) {
    vec4 mount=fleetModule(i); vec2 h=fleetModuleHalf(i,mount);
    vec3 q=fleetModuleFrame(p,i,mount);
    // Only the primary front faces and rounded front bezel, including their authored corner chamfer.
    // The z window excludes rear bridges, and the distance window excludes other material-145 furniture.
    if(q.z<=.030 || q.z>=.061 || abs(q.x)>=h.x+.024 || abs(q.y)>=h.y+.024) continue;
    if(abs(fleetHousing(q,h,min(L.structure.y,.025),L.structure.z))>.008) continue;
    n=fleetRoundedHousingNormal(q,h,min(L.structure.y,.025),L.structure.z);
    if(MODEL_IS(14)) n.xz=rot2(n.xz,i==0?-.16:.16); // inverse fleetModuleFrame transform
    return true;
  }
  return false;
}
#endif
// A backing beam may join a housing behind its glass, but must never bury its live face.
float fleetKeepFacesClear(vec3 p,float support){
  for(int i=0;i<4;i++){
    vec4 mount=fleetModule(i);vec2 h=fleetModuleHalf(i,mount)+vec2(.022);
    vec3 q=fleetModuleFrame(p,i,mount);
    float faceSpace=max(max(abs(q.x)-h.x,abs(q.y)-h.y),.014-q.z);
    support=max(support,-faceSpace);
  }
  return support;
}
// Bushmaster local prototype: supported analog modules and shell-fitted low brow.
float bushRaisedPrimaryRisers(vec3 p,float fuselage){
  float d=1e5;
  for(int i=0;i<2;i++){
    vec3 mount=cockpitMount(fleetModule(i));
    vec3 foot=vec3(mount.x,.072,-1.998);
    vec3 rear=mount-vec3(0,0,.040);
    d=min(d,sdCapsule(p,foot,rear,.012));
  }
  return max(fleetKeepFacesClear(p,d),fuselage+.060);
}

float bushShellFittedPilotBrow(vec3 p,float fuselage){
  vec4 mount=fleetModule(0);vec2 h=fleetModuleHalf(0,mount);
  vec3 q=fleetModuleFrame(p,0,mount)-vec3(0,h.y+.020,.008);
  return max(sdRoundBox(q,vec3(h.x+.027,.008,.083),.006),fuselage+.066);
}

float swiftShellFittedPilotBrow(vec3 p,float fuselage){
  vec4 m=fleetModule(0); vec2 h=fleetModuleHalf(0,m);
  vec3 q=fleetModuleFrame(p,0,m)-vec3(0,h.y+.027,.008);
  return max(sdRoundBox(q,vec3(h.x+.027,.014,.083),.012),fuselage+.066);
}
float swiftDisplayRearSupports(vec3 p,float fuselage){
  float pz=gM[21].w;
  vec3 statusRearLocal=vec3(0,0,-.040);
  statusRearLocal.yz=rot2(statusRearLocal.yz,-.35);
  vec3 statusRear=cockpitMount(fleetModule(3))+statusRearLocal;
  float status=sdCapsule(p,vec3(.020,.216,pz-.107),statusRear,.014);
  vec3 copilot=cockpitMount(fleetModule(1));
  float riser=sdCapsule(p,vec3(copilot.x,-.010,pz-.025),copilot-vec3(0,0,.040),.014);
  return max(fleetKeepFacesClear(p,min(status,riser)),fuselage+.065);
}

bool fleetIndividualBrow(int tile){
  if(MODEL_IS(1) || MODEL_IS(3) || MODEL_IS(8) || MODEL_IS(13) || MODEL_IS(14))return false; // one connected crew brow
  if(MODEL_IS(2) || MODEL_IS(4) || MODEL_IS(7))return tile==0;
  return true;
}
vec2 fleetRoleStructure(vec3 p,float fuselage){
  vec4 E=gM[22];float pz=gM[21].w;vec2 r=vec2(1e5,145.0);
#if HAS_LARKSPUR
  if(MODEL_IS(13)){
    // A staggered olive workbench in an ivory liner, with the instrument tower right of the captain.
    float left=sdRoundBox(p-vec3(-.285,E.y-.350,pz-.055),vec3(.222,.170,.027),.020);
    float right=sdRoundBox(p-vec3(.363,E.y-.392,pz-.109),vec3(.175,.142,.027),.020);
    float tower=sdRoundBox(p-vec3(.037,E.y-.484,pz-.010),vec3(.083,.239,.032),.012);
    r=opU(r,vec2(min(min(left,right),tower),63.0));
    float brow=sdRoundBox(p-vec3(-.276,E.y-.168,pz-.025),vec3(.228,.013,.079),.012);
    brow=min(brow,sdRoundBox(p-vec3(.361,E.y-.230,pz-.078),vec3(.184,.013,.072),.012));
    r=opU(r,vec2(brow,14.0));
  }
#endif
#if HAS_ATLAS
  if(MODEL_IS(14)){
    // Canted crew bridges read as one wraparound deck; the tower is between real, empty knee wells.
    for(int i=0;i<2;i++){
      vec4 m=fleetModule(i);vec2 h=fleetModuleHalf(i,m);vec3 q=fleetModuleFrame(p,i,m);
      r=opU(r,vec2(sdRoundBox(q+vec3(0,.034,.075),vec3(h.x+.045,h.y+.060,.030),.014),63.0));
      r=opU(r,vec2(sdRoundBox(q-vec3(0,h.y+.031,.008),vec3(h.x+.045,.020,.109),.014),14.0));
      // Low side returns and thumb-reachable tiller shelves stay outside the seating corridor.
      float side=i==0?-1.0:1.0;
      vec3 sq=p-vec3(side*1.170,E.y-.615,E.z-.330);
      r=opU(r,vec2(sdRoundBox(sq,vec3(.170,.048,.410),.032),63.0));
      float support=sdRoundBox(p-vec3(side*1.380,E.y-.835,E.z-.330),vec3(.380,.205,.430),.022);
      r=opU(r,vec2(support,63.0));
    }
    float tower=sdRoundBox(p-vec3(0,E.y-.527,pz-.009),vec3(.197,.287,.047),.013);
    r=opU(r,vec2(tower,145.0));
    float statusSupport=sdCapsule(p,vec3(-.180,E.y-.360,pz+.010),vec3(-.287,E.y-.360,pz+.060),.014);
    r=opU(r,vec2(statusSupport,60.0));
  }
#endif
#if HAS_KESTREL || HAS_WREN
  if(MODEL_IS(0) || MODEL_IS(1)){
  // Trainer power controls now have fitted side-console caps; no obsolete footwell apron.
  if(MODEL_IS(1)){
    // The Wren's continuous, softly rounded touring fascia and broad common brow.
    float back=sdRoundBox(p-vec3(-.016,E.y-.369,pz-.060),vec3(.536,.171,.028),.025);
    float stem=sdRoundBox(p-vec3(.015,E.y-.493,pz-.036),vec3(.139,.165,.025),.018);
    r=opU(r,vec2(min(back,stem),63.0));
    float brow=sdRoundBox(p-vec3(-.016,E.y-.187,pz-.021),vec3(.524,.014,.065),.014);
    r=opU(r,vec2(brow,14.0));
  }
}
#endif
#if HAS_BUSHMASTER
  if(MODEL_IS(2)){
    // Sparse exposed utility rail, leaving two open low leg wells.
    float rail=sdRoundBox(p-vec3(-.055,.072,pz-.068),vec3(.426,.012,.027),.006);
    r=opU(r,vec2(rail,63.0));
    // Preserve the status support in world space; obsolete low power apron is removed.
    float statusMount=sdCapsule(p,vec3(0,.030,pz+.010),vec3(0,.010,pz+.080),.013);
    r=opU(r,vec2(min(statusMount,bushRaisedPrimaryRisers(p,fuselage)),145.0));
  }
#endif
#if HAS_ISLANDER
  if(MODEL_IS(3)){
    // One squared instrument workbench, instead of three unrelated freestanding pods.
    float back=sdRoundBox(p-vec3(0,E.y-.348,pz-.091),vec3(.564,.176,.038),.008);
    r=opU(r,vec2(back,145.0));
    float brow=sdRoundBox(p-vec3(0,E.y-.170,pz-.020),vec3(.559,.014,.076),.008);
    r=opU(r,vec2(brow,14.0));
  }
#endif
#if HAS_PELICAN
  if(MODEL_IS(4)){
    // Deep pilot/power workbench with a centered connected low utility shelf backing.
    float left=sdRoundBox(p-vec3(-.245,E.y-.400,pz-.086),vec3(.360,.170,.030),.012);
    float utility=sdRoundBox(p-vec3(0,E.y-.638,pz+.012),vec3(.120,.095,.040),.012);
    r=opU(r,vec2(min(left,utility),63.0));
  }
#endif
#if HAS_STARLING
  if(MODEL_IS(6)){
    // Executive center stack joins both shared displays without entering the copilot leg well.
    float stack=sdRoundBox(p-vec3(0,E.y-.5675,pz-.025),vec3(.055,.4475,.095),.012);
    stack=min(stack,sdRoundBox(p-vec3(0,E.y-.120,pz+.085),vec3(.075,.032,.025),.008));
    r=opU(r,vec2(stack,145.0));
  }
#endif
#if HAS_NIGHTJAR
  if(MODEL_IS(9)){
    // Shared forward stack: connected to the bridge and footwell wall, leaving both crew leg wells open.
    float stack=sdRoundBox(p-vec3(0,E.y-.750,pz-.025),vec3(.110,.265,.135),.012);
    r=opU(r,vec2(stack,145.0));
  }
#endif
#if HAS_SWIFT
  if(MODEL_IS(7)){
    // A slim, sloping central island connects raised gear/flap status to low right power/fuel.
    CockpitLayout L=cockpitLayout();vec3 a=cockpitMount(L.status),b=cockpitMount(L.systems);
    vec3 q=p-vec3((a.xy+b.xy)*.5,pz-.107);vec2 d=a.xy-b.xy;
    q.xy=rot2(q.xy,atan(d.x,d.y));
    float spine=sdRoundBox(q,vec3(.085,.5*length(d)+.065,.024),.020);
    r=opU(r,vec2(min(spine,swiftDisplayRearSupports(p,fuselage)),145.0));
  }
#endif
#if HAS_OSPREY
  if(MODEL_IS(8)){
    // Ivory connected shell with dark one-piece coaming; original copper cabin trim stays present.
    float ivory=sdRoundBox(p-vec3(0,E.y-.388,pz-.068),vec3(.557,.190,.028),.025);
    float stem=sdRoundBox(p-vec3(.016,E.y-.554,pz-.032),vec3(.130,.087,.029),.025);
    r=opU(r,vec2(min(ivory,stem),120.0));
    float brow=sdRoundBox(p-vec3(0,E.y-.184,pz-.024),vec3(.548,.012,.064),.012);
    r=opU(r,vec2(brow,14.0));
  }
#endif
  r.x=max(fleetKeepFacesClear(p,r.x),fuselage+.065); // fit supports without covering live faces
  return r;
}

vec3 atlasNavFrame(vec3 p){vec3 q=p-vec3(0,gM[22].y-.590,gM[21].w+.335);q.yz=rot2(q.yz,1.15);return q;}
#if HAS_ATLAS
// The tablet's own rounded housing has a thinner edge than the primary displays.
// Evaluate its unchanged box normal in the inclined local frame, including the visible side/back bevel.
bool atlasNavHousingNormal(vec3 p,int mid,out vec3 n){
  if(!MODEL_IS(14) || mid!=145) return false;
  vec3 q=atlasNavFrame(p);
  if(abs(q.x)>.193 || abs(q.y)>.143 || abs(q.z)>.028) return false;
  vec3 d=abs(q)-vec3(.187,.137,.022)+.012,outer=max(d,vec3(0));
  float field=length(outer)+min(max(d.x,max(d.y,d.z)),0.0)-.012;
  if(abs(field)>.006) return false;
  if(dot(outer,outer)>1e-12) n=normalize(outer)*sign(q);
  else if(d.x>d.y && d.x>d.z) n=vec3(sign(q.x),0,0);
  else if(d.y>d.z) n=vec3(0,sign(q.y),0);
  else n=vec3(0,0,sign(q.z));
  n.yz=rot2(n.yz,-1.15);
  return true;
}
#endif

vec3 atlasOverheadFrame(vec3 p){return p-vec3(0,gM[22].y+.480,gM[22].z-.260);}
vec2 mapNewFleetFurnishings(vec3 p,float fuselage){
  vec4 E=gM[22];vec2 r=vec2(1e5,63.0);
#if HAS_LARKSPUR
  if(MODEL_IS(13)){
    // A real second row of two seats, below the rear window belt, with floor-backed rails.
    vec3 q=vec3(abs(p.x)-.290,p.y,p.z);float panY=E.y-.640;
    float pad=sdRoundBox(q-vec3(0,panY,E.z+1.050),vec3(.190,.055,.225),.040);
    vec3 b=q-vec3(0,E.y-.335,E.z+1.310);b.yz=rot2(b.yz,-.14);
    pad=min(pad,sdRoundBox(b,vec3(.175,.315,.050),.040));
    pad=min(pad,sdRoundBox(q-vec3(0,E.y+.020,E.z+1.362),vec3(.105,.070,.045),.029));
    r=opU(r,vec2(max(pad,fuselage+.055),12.0));
    float rails=sdRoundBox(vec3(abs(q.x)-.115,p.y-(E.y-.840),p.z-E.z-1.040),vec3(.013,.145,.245),.004);
    r=opU(r,vec2(max(rails,fuselage+.055),60.0));
    float belt=sdRoundBox(q-vec3(0,panY+.063,E.z+1.060),vec3(.179,.009,.018),.004);
    r=opU(r,vec2(belt,69.0));
    // Slim saddle columns: no legacy yoke shaft is left in the instrument sightline.
    float side=p.x<0.0?-1.0:1.0;vec3 pivot=larkspurStickPivot(side);
    float post=sdCapsule(p,vec3(pivot.x,E.y-1.060,pivot.z+.050),pivot-vec3(0,.025,0),.017);
    r=opU(r,vec2(max(post,fuselage+.030),60.0));
    r=opU(r,vec2(sdRoundBox(p-pivot+vec3(0,.018,0),vec3(.036,.030,.038),.019),61.0));
    // The push-pull power bank stands on the pedestal, aft of the status screen.
    // Neither handle can disappear into that screen at full forward throttle.
    float bank=sdRoundBox(p-vec3(.009,E.y-.725,E.z-.485),vec3(.087,.059,.026),.010);
    r=opU(r,vec2(bank,145.0));
    float mix_=min(sdCapsule(p,vec3(.047,E.y-.690,E.z-.510),vec3(.047,E.y-.690,E.z-.325),.005),length(p-vec3(.047,E.y-.690,E.z-.320))-.018);
    r=opU(r,vec2(mix_,68.0));
  }
#endif
#if HAS_ATLAS
  if(MODEL_IS(14)){
    vec3 nav=atlasNavFrame(p);
    float box=sdRoundBox(nav,vec3(.187,.137,.022),.012);
    r=opU(r,vec2(box,nav.z>.012&&abs(nav.x)<.165&&abs(nav.y)<.115?147.0:145.0));
    float mount=sdCapsule(p,vec3(0,E.y-.710,gM[21].w+.350),vec3(0,E.y-.620,gM[21].w+.323),.033);
    r=opU(r,vec2(mount,60.0));
    // Reachable, sloping-independent overhead systems bank with its own live status glass.
    vec3 oh=atlasOverheadFrame(p);
    float overhead=sdRoundBox(oh,vec3(.230,.022,.310),.016);
    r=opU(r,vec2(overhead,oh.y<-.012&&abs(oh.x)<.150&&abs(oh.z)<.108?148.0:145.0));
    for(int side=-1;side<=1;side+=2){
      vec3 q=oh-vec3(float(side)*.190,-.026,0);
      float rail=sdRoundBox(q,vec3(.013,.013,.228),.008);
      r=opU(r,vec2(rail,60.0));
      // Four guarded hardware caps per side, rather than emissive decorative "screens".
      q.z-=clamp(floor(q.z/.115+.5),-2.0,2.0)*.115;
      r=opU(r,vec2(sdRoundBox(q-vec3(0,-.015,0),vec3(.024,.013,.023),.006),66.0));
    }
    // Short hangers attach overhead equipment to the actual crown without passing through crew heads.
    float roof=cabinRoof(fusSection(E.z-.260),.210)-.025;
    vec3 hang=vec3(abs(p.x)-.210,p.y,p.z-E.z+.260);
    float hy=.5*(roof+E.y+.510),hh=max(.010,.5*(roof-E.y-.510));
    r=opU(r,vec2(max(sdRoundBox(hang-vec3(0,hy,0),vec3(.013,hh,.235),.006),fuselage+.045),63.0));
    // Solid cabin partition and a separate, clearly outlined central flightdeck door.
    float wall=sdRoundBox(p-vec3(0,E.y-.085,E.z+1.350),vec3(1.75,.985,.045),.018);
    wall=max(wall,-sdRoundBox(p-vec3(0,E.y-.055,E.z+1.350),vec3(.385,.880,.075),.020));
    r=opU(r,vec2(max(wall,fuselage+.060),63.0));
    float door=sdRoundBox(p-vec3(0,E.y-.055,E.z+1.373),vec3(.372,.865,.023),.018);
    r=opU(r,vec2(door,145.0));
    r=opU(r,vec2(sdCapsule(p,vec3(.288,E.y-.260,E.z+1.340),vec3(.288,E.y-.145,E.z+1.340),.009),60.0));
  }
#endif
  return r;
}

vec2 mapFleetPanel(vec3 p,float fuselage){
  CockpitLayout L=cockpitLayout(); vec4 E=gM[22]; float pz=gM[21].w;
  vec2 outv=vec2(1e5,145.0);
  for(int i=0;i<4;i++){
    vec4 mount=fleetModule(i); vec2 h=fleetModuleHalf(i,mount); vec3 q=fleetModuleFrame(p,i,mount);
    float body=fleetHousing(q,h,min(L.structure.y,.025),L.structure.z);
    // A real thick mount, with a broad flat face. Classification at pixel scale avoids lattice-sized jagged bezels.
    bool face=q.z>.034 && abs(q.x)<h.x && abs(q.y)<h.y;
    outv=opU(outv,vec2(body,face?140.0+float(i):145.0));
    if(i<2){
      vec3 lip=q-vec3(0,h.y+.027,.008);
      float brow=sdRoundBox(lip,vec3(h.x+.027,.014,.083),.012);
      // The trainer pilot brow ended 6-16 mm short of the inner wall, producing
      // a sub-lattice slit. Add only its outboard return, buried in the shell;
      // the inboard edge, height, depth and live instrument opening stay put.
      if(MODEL_IS(0) && i==0) brow=max(sdRoundBox(lip+vec3(.025,0,0),vec3(h.x+.052,.014,.083),.012),fuselage+.040);
#if HAS_BUSHMASTER && HAS_SWIFT
      if(fleetIndividualBrow(i)) outv=opU(outv,vec2(MODEL_IS(2)?bushShellFittedPilotBrow(p,fuselage):MODEL_IS(7)?swiftShellFittedPilotBrow(p,fuselage):brow,14.0));
#elif HAS_BUSHMASTER
      if(fleetIndividualBrow(i)) outv=opU(outv,vec2(MODEL_IS(2)?bushShellFittedPilotBrow(p,fuselage):brow,14.0));
#elif HAS_SWIFT
      if(fleetIndividualBrow(i)) outv=opU(outv,vec2(MODEL_IS(7)?swiftShellFittedPilotBrow(p,fuselage):brow,14.0));
#else
      if(fleetIndividualBrow(i)) outv=opU(outv,vec2(brow,14.0));
#endif
      // Subdued satin edge in the aircraft's identity color, deliberately unlit.
      outv=opU(outv,vec2(sdRoundBox(q-vec3(0,h.y+.012,.060),vec3(h.x*.92,.005,.008),.004),146.0));
    }
  }
  outv=opU(outv,fleetRoleStructure(p,fuselage));
  // Low structural bridge, leaving leg wells open rather than filling the view with one large blank slab.
  float by=MODEL_IS(2)?.030:swiftPreservedFurnitureY()-.53;
  float bridge=sdRoundBox(p-vec3(0,by,pz-.025),vec3(E.w*.90,L.structure.w,.045),min(.018,L.structure.y));
  outv=opU(outv,vec2(max(fleetKeepFacesClear(p,bridge),fuselage+.06),63.0));
  // Short anti-glare deck: its leading edge descends to the windshield sill and remains beneath the useful view.
  float rear=pz-.058, front=max(gM[23].x-.025,rear-.20);
  float yr=swiftPreservedFurnitureY()-L.structure.x, yf=min(yr,gM[23].z-.025);
  vec3 hood=p-vec3(0,.5*(yr+yf),.5*(rear+front));
  vec2 axis=vec2(yr-yf,rear-front); hood.yz=rot2(hood.yz,atan(axis.x,axis.y));
  float deck=sdRoundBox(hood,vec3(E.w*.95,.015,.5*length(axis)),.013);
  // Atlas keeps its verified rounded junction. The three light cabins overlap the 60 mm shell by 20 mm
  // so the deck cannot leave a sub-lattice air slit; its authored top and window cut stay unchanged.
  float deckFit=max(deck,fuselage+.065);
  if(MODEL_IS(14)) deckFit=-smin(-deck,-(fuselage+.065),.020);
  if(MODEL_IS(0) || MODEL_IS(1) || MODEL_IS(2)) deckFit=max(deck,fuselage+.040);
  outv=opU(outv,vec2(deckFit,14.0));
  // A low firewall closes the hollow nose below the windshield, without a long horizontal glare shelf.
  float fyTop=min(swiftPreservedFurnitureY()-L.structure.x,gM[23].z-.012),fyBottom=(MODEL_IS(2)?.560:swiftPreservedFurnitureY())-1.07;
  float firewall=sdBox(p-vec3(0,(fyTop+fyBottom)*.5,gM[23].x+.035),vec3(E.w*.95,(fyTop-fyBottom)*.5,.022));
  // Pelican's exposed sharp firewall top produced stepped corner highlights; keep its outer envelope.
  if(MODEL_IS(4)) firewall=sdRoundBox(p-vec3(0,(fyTop+fyBottom)*.5,gM[23].x+.035),vec3(E.w*.95,(fyTop-fyBottom)*.5,.022),.010);
  // Verified contact gaps in the five listed cabins need a hidden 20 mm shell overlap.
  float firewallFit=max(firewall,fuselage+.065);
  if(MODEL_IS(0) || MODEL_IS(1) || MODEL_IS(2) || MODEL_IS(7) || MODEL_IS(9)) firewallFit=max(firewall,fuselage+.040);
  outv=opU(outv,vec2(firewallFit,145.0));

  // Small robust matte vents, integrated into the outer ends rather than chrome rings dominating the scan.
  for(int side=-1;side<=1;side+=2){
    vec3 vent=vec3(float(side)*min(E.w-.045,gCab1.w),(MODEL_IS(2)?.560:swiftPreservedFurnitureY())-.205,pz+.007);
    // The transport's low side consoles support their vents instead of leaving them in the glazing.
    if(MODEL_IS(14)) vent=vec3(float(side)*1.170,E.y-.548,E.z-.630);
    vec3 q=p-vent;
    outv=opU(outv,vec2(sdRoundBox(q,vec3(.023,.019,.028),.014),66.0));
  }
  return outv;
}
int fleetPanelId(vec3 p,int id){
  if(!fleetCabin() || id<140 || id>148 || id==146)return id;
#if HAS_ATLAS
  if(MODEL_IS(14)){
    vec3 n=atlasNavFrame(p),o=atlasOverheadFrame(p);
    if(n.z>.012&&n.z<.034&&abs(n.x)<.165&&abs(n.y)<.115)return 147;
    if(o.y<-.012&&o.y>-.034&&abs(o.x)<.150&&abs(o.z)<.108)return 148;
  }
#endif
  for(int i=0;i<4;i++){
    vec4 mount=fleetModule(i); vec2 h=fleetModuleHalf(i,mount); vec3 q=fleetModuleFrame(p,i,mount);
    if(q.z>.034 && q.z<.062 && abs(q.x)<h.x && abs(q.y)<h.y)return 140+i;
  }
  return 145;
}
