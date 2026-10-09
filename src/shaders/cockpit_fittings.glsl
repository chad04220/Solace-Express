//! kCockpitFittings
//! Modular conventional flight decks, using the reviewed per-model layout record.
// Four bounded modules: pilot scan, instructor/co-pilot scan, engine/fuel, and live flight-status utility.
// The faces reuse the current instrument atlas and glass engine page. Their geometry and material coordinates
// come from these same mounts; no extra camera, light, display pass, or material texture is needed.
vec4 fleetModule(int i){ CockpitLayout L=cockpitLayout(); return i==0?L.pilot:i==1?L.copilot:i==2?L.systems:L.status; }
vec2 fleetModuleHalf(int i,vec4 m){ return i<2?cockpitFlightHalf(m.w):i==2?cockpitSystemsHalf(m.w):vec2(m.w,m.w*.70); }
vec3 fleetModuleFrame(vec3 p,int i,vec4 mount){ vec3 q=p-cockpitMount(mount); if(i==3)q.yz=rot2(q.yz,.35); return q; }
float fleetHousing(vec3 q,vec2 h,float radius,float chamfer){
  float body=sdRoundBox(q,vec3(h+vec2(.022),.050),radius);
  // Deliberate angular chamfers distinguish utility/research structures from the broad touring radii.
  float corner=(abs(q.x)+abs(q.y)-(h.x+h.y+.044-chamfer))*.70710678;
  return max(body,corner);
}
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
  if(gModelId==1 || gModelId==3 || gModelId==8)return false; // one connected crew brow
  if(gModelId==2 || gModelId==4 || gModelId==7)return tile==0;
  return true;
}
vec2 fleetRoleStructure(vec3 p,float fuselage){
  vec4 E=gM[22];float pz=gM[21].w;vec2 r=vec2(1e5,145.0);
  if(gModelId==0 || gModelId==1){
  // Trainer power controls now have fitted side-console caps; no obsolete footwell apron.
  if(gModelId==1){
    // The Wren's continuous, softly rounded touring fascia and broad common brow.
    float back=sdRoundBox(p-vec3(-.016,E.y-.369,pz-.060),vec3(.536,.171,.028),.025);
    float stem=sdRoundBox(p-vec3(.015,E.y-.493,pz-.036),vec3(.139,.165,.025),.018);
    r=opU(r,vec2(min(back,stem),63.0));
    float brow=sdRoundBox(p-vec3(-.016,E.y-.187,pz-.021),vec3(.524,.014,.065),.014);
    r=opU(r,vec2(brow,14.0));
  }
}else if(gModelId==2){
    // Sparse exposed utility rail, leaving two open low leg wells.
    float rail=sdRoundBox(p-vec3(-.055,.072,pz-.068),vec3(.426,.012,.027),.006);
    r=opU(r,vec2(rail,63.0));
    // Preserve the status support in world space; obsolete low power apron is removed.
    float statusMount=sdCapsule(p,vec3(0,.030,pz+.010),vec3(0,.010,pz+.080),.013);
    r=opU(r,vec2(min(statusMount,bushRaisedPrimaryRisers(p,fuselage)),145.0));
  }else if(gModelId==3){
    // One squared instrument workbench, instead of three unrelated freestanding pods.
    float back=sdRoundBox(p-vec3(0,E.y-.348,pz-.091),vec3(.564,.176,.038),.008);
    r=opU(r,vec2(back,145.0));
    float brow=sdRoundBox(p-vec3(0,E.y-.170,pz-.020),vec3(.559,.014,.076),.008);
    r=opU(r,vec2(brow,14.0));
  }else if(gModelId==4){
    // Deep pilot/power workbench with a centered connected low utility shelf backing.
    float left=sdRoundBox(p-vec3(-.245,E.y-.400,pz-.086),vec3(.360,.170,.030),.012);
    float utility=sdRoundBox(p-vec3(0,E.y-.638,pz+.012),vec3(.120,.095,.040),.012);
    r=opU(r,vec2(min(left,utility),63.0));
  }else if(gModelId==6){
    // Executive center stack joins both shared displays without entering the copilot leg well.
    float stack=sdRoundBox(p-vec3(0,E.y-.5675,pz-.025),vec3(.055,.4475,.095),.012);
    stack=min(stack,sdRoundBox(p-vec3(0,E.y-.120,pz+.085),vec3(.075,.032,.025),.008));
    r=opU(r,vec2(stack,145.0));
  }else if(gModelId==9){
    // Shared forward stack: connected to the bridge and footwell wall, leaving both crew leg wells open.
    float stack=sdRoundBox(p-vec3(0,E.y-.750,pz-.025),vec3(.110,.265,.135),.012);
    r=opU(r,vec2(stack,145.0));
  }else if(gModelId==7){
    // A slim, sloping central island connects raised gear/flap status to low right power/fuel.
    CockpitLayout L=cockpitLayout();vec3 a=cockpitMount(L.status),b=cockpitMount(L.systems);
    vec3 q=p-vec3((a.xy+b.xy)*.5,pz-.107);vec2 d=a.xy-b.xy;
    q.xy=rot2(q.xy,atan(d.x,d.y));
    float spine=sdRoundBox(q,vec3(.085,.5*length(d)+.065,.024),.020);
    r=opU(r,vec2(min(spine,swiftDisplayRearSupports(p,fuselage)),145.0));
  }else if(gModelId==8){
    // Ivory connected shell with dark one-piece coaming; original copper cabin trim stays present.
    float ivory=sdRoundBox(p-vec3(0,E.y-.388,pz-.068),vec3(.557,.190,.028),.025);
    float stem=sdRoundBox(p-vec3(.016,E.y-.554,pz-.032),vec3(.130,.087,.029),.025);
    r=opU(r,vec2(min(ivory,stem),120.0));
    float brow=sdRoundBox(p-vec3(0,E.y-.184,pz-.024),vec3(.548,.012,.064),.012);
    r=opU(r,vec2(brow,14.0));
  }
  r.x=max(fleetKeepFacesClear(p,r.x),fuselage+.065); // fit supports without covering live faces
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
      if(fleetIndividualBrow(i)) outv=opU(outv,vec2(gModelId==2?bushShellFittedPilotBrow(p,fuselage):gModelId==7?swiftShellFittedPilotBrow(p,fuselage):sdRoundBox(lip,vec3(h.x+.027,.014,.083),.012),14.0));
      // Subdued satin edge in the aircraft's identity color, deliberately unlit.
      outv=opU(outv,vec2(sdRoundBox(q-vec3(0,h.y+.012,.060),vec3(h.x*.92,.005,.008),.004),146.0));
    }
  }
  outv=opU(outv,fleetRoleStructure(p,fuselage));
  // Low structural bridge, leaving leg wells open rather than filling the view with one large blank slab.
  float by=gModelId==2?.030:swiftPreservedFurnitureY()-.53;
  float bridge=sdRoundBox(p-vec3(0,by,pz-.025),vec3(E.w*.90,L.structure.w,.045),min(.018,L.structure.y));
  outv=opU(outv,vec2(max(fleetKeepFacesClear(p,bridge),fuselage+.06),63.0));
  // Short anti-glare deck: its leading edge descends to the windshield sill and remains beneath the useful view.
  float rear=pz-.058, front=max(gM[23].x-.025,rear-.20);
  float yr=swiftPreservedFurnitureY()-L.structure.x, yf=min(yr,gM[23].z-.025);
  vec3 hood=p-vec3(0,.5*(yr+yf),.5*(rear+front));
  vec2 axis=vec2(yr-yf,rear-front); hood.yz=rot2(hood.yz,atan(axis.x,axis.y));
  float deck=sdRoundBox(hood,vec3(E.w*.95,.015,.5*length(axis)),.013);
  outv=opU(outv,vec2(max(deck,fuselage+.065),14.0));
  // A low firewall closes the hollow nose below the windshield, without a long horizontal glare shelf.
  float fyTop=min(swiftPreservedFurnitureY()-L.structure.x,gM[23].z-.012),fyBottom=(gModelId==2?.560:swiftPreservedFurnitureY())-1.07;
  float firewall=sdBox(p-vec3(0,(fyTop+fyBottom)*.5,gM[23].x+.035),vec3(E.w*.95,(fyTop-fyBottom)*.5,.022));
  outv=opU(outv,vec2(max(firewall,fuselage+.065),145.0));

  // Small robust matte vents, integrated into the outer ends rather than chrome rings dominating the scan.
  for(int side=-1;side<=1;side+=2){
    vec3 q=p-vec3(float(side)*min(E.w-.045,gCab1.w),(gModelId==2?.560:swiftPreservedFurnitureY())-.205,pz+.007);
    outv=opU(outv,vec2(sdRoundBox(q,vec3(.023,.019,.028),.014),66.0));
  }
  return outv;
}
int fleetPanelId(vec3 p,int id){
  if(!fleetCabin() || id<140 || id>145)return id;
  for(int i=0;i<4;i++){
    vec4 mount=fleetModule(i); vec2 h=fleetModuleHalf(i,mount); vec3 q=fleetModuleFrame(p,i,mount);
    if(q.z>.034 && q.z<.062 && abs(q.x)<h.x && abs(q.y)<h.y)return 140+i;
  }
  return 145;
}
