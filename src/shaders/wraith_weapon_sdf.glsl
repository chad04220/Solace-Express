//! kWraithWeaponSDF
// Separately baked XR-40 loadout cartridges. Existing emitter/hatch/arm poses are
// unchanged; these local fields fit the same socket envelope and bay clearance.
// No animation or selected-loadout state changes a cached part's geometry.
float wrWeaponCylinder(vec3 p,float r,float halfZ){return sdCylX(p.zyx,r,halfZ);}
float wrWeaponRing(vec3 p,float outer,float inner,float halfZ){
 return max(wrWeaponCylinder(p,outer,halfZ),-wrWeaponCylinder(p,inner,halfZ+.012));
}
vec2 wrKineticCartridge(vec3 p){
 // Machined receiver, rear feed cover and a structural breech plate.
 vec2 h=vec2(sdRoundBox(p-vec3(0,0,.04),vec3(.119,.083,.267),.014),90.);
 h=opU(h,vec2(sdRoundBox(p-vec3(0,.091,.095),vec3(.086,.017,.137),.008),92.));
 h=opU(h,vec2(wrWeaponCylinder(p-vec3(0,-.02,-.26),.109,.055),92.));
 // Six individually bored barrels, rooted in that breech. Closed aft ends remain.
 for(int i=0;i<6;i++){
  float a=float(i)*1.0471975512;
  vec3 q=p-vec3(cos(a)*.065,sin(a)*.065-.02,-.667);
  float shell=wrWeaponCylinder(q,.018,.393);
  float bore=wrWeaponCylinder(q-vec3(0,0,-.022),.009,.393);
  h=opU(h,vec2(max(shell,-bore),-bore>shell?93.:92.));
 }
 // Three positive attachment bands tie the cluster together, not floating rings.
 for(int j=0;j<3;j++){
  float z=-.36-float(j)*.29;
  h=opU(h,vec2(wrWeaponRing(p-vec3(0,-.02,z),.096,.075,.020),90.));
 }
 // Fluted muzzle crown and a closed centre spindle register the cassette.
 h=opU(h,vec2(wrWeaponRing(p-vec3(0,-.02,-1.045),.094,.082,.026),92.));
 h=opU(h,vec2(sdCapsule(p,vec3(0,-.02,-.30),vec3(0,-.02,-1.07),.026),90.));
 for(int i=0;i<5;i++){
  float z=-.17+float(i)*.084;
  h=opU(h,vec2(sdRoundBox(vec3(abs(p.x)-.134,p.y,p.z-z),vec3(.005,.042,.012),.003),93.));
 }
 // Captive fasteners sit in the rear receiver and feed cover.
 for(int i=0;i<2;i++)for(int j=0;j<2;j++)
  h=opU(h,vec2(sdCapsule(p,vec3((i==0?-1.:1.)*.068,.108,j==0?.015:.19),vec3((i==0?-1.:1.)*.068,.116,j==0?.015:.19),.010),92.));
 return h;
}
vec2 wrChargedCartridge(vec3 p){
 vec2 h=vec2(sdRoundBox(p-vec3(0,0,.03),vec3(.115,.085,.277),.016),90.);
 // Two shielded capacitor packs, ceramic break and conductive acceleration rails.
 for(int i=-1;i<=1;i+=2){
  vec3 q=p-vec3(float(i)*.072,-.02,-.34);
  h=opU(h,vec2(sdRoundBox(q-vec3(0,0,.30),vec3(.041,.060,.206),.009),92.));
  h=opU(h,vec2(sdRoundBox(q-vec3(0,0,-.313),vec3(.024,.027,.369),.006),82.));
  for(int j=0;j<4;j++){
   float z=-.13-float(j)*.145;
   h=opU(h,vec2(sdRoundBox(q-vec3(0,0,z),vec3(.031,.038,.018),.006),93.));
  }
 }
 // A forward grounded collar joins both rails and guards; the aperture is recessed within it.
 h=opU(h,vec2(wrWeaponRing(p-vec3(0,-.02,-1.034),.086,.047,.030),90.));
 h=opU(h,vec2(wrWeaponCylinder(p-vec3(0,-.02,-1.048),.049,.025),93.));
 h=opU(h,vec2(wrWeaponCylinder(p-vec3(0,-.02,-1.067),.038,.008),91.));
 for(int i=-1;i<=1;i+=2)
  h=opU(h,vec2(sdRoundBox(p-vec3(0,float(i)*.079-.02,-.82),vec3(.109,.013,.254),.006),90.));
 // Copper bus strap positively overlaps the receiver, with insulated fixing pads.
 h=opU(h,vec2(sdRoundBox(p-vec3(0,.099,.025),vec3(.076,.010,.183),.005),82.));
 for(int j=0;j<3;j++)h=opU(h,vec2(sdRoundBox(p-vec3(0,.109,-.11+float(j)*.135),vec3(.084,.008,.015),.004),93.));
 return h;
}
vec2 wrPlasmaPayload(vec3 p){
 // The familiar core retained inside its own restrained load-bearing cage.
 vec2 h=vec2(length(p)-.267,89.);
 for(int j=-1;j<=1;j+=2){
  float z=float(j)*.152;
  h=opU(h,vec2(wrWeaponRing(p-vec3(0,0,z),.238,.221,.020),92.));
 }
 for(int i=0;i<6;i++){
  float a=float(i)*1.0471975512;vec2 d=vec2(cos(a),sin(a));
  // Spherical brace chords remain outside the core, with ends embedded in rings.
  float halfArc=asin(.168/.281);
  for(int segment=0;segment<6;segment++){
   float a0=mix(-halfArc,halfArc,float(segment)/6.);
   float a1=mix(-halfArc,halfArc,float(segment+1)/6.);
   vec3 p0=vec3(d*(.281*cos(a0)),.281*sin(a0));
   vec3 p1=vec3(d*(.281*cos(a1)),.281*sin(a1));
   h=opU(h,vec2(sdCapsule(p,p0,p1,.010),90.));
  }
 }
 h=opU(h,vec2(wrWeaponCylinder(p-vec3(0,0,.26),.061,.018),92.));
 return h;
}
vec2 wrPenetratorPayload(vec3 p){
 // Solid cylindrical penetrator jacket with a proper ogive nose and rear fuse plug.
 float shaft=wrWeaponCylinder(p-vec3(0,0,.08),.158,.57);
 float nose=sdRoundCone(p,vec3(0,0,-.49),vec3(0,0,-.91),.158,.010);
 vec2 h=vec2(min(shaft,nose),92.);
 h=opU(h,vec2(wrWeaponCylinder(p-vec3(0,0,.665),.132,.042),90.));
 h=opU(h,vec2(wrWeaponCylinder(p-vec3(0,0,.71),.062,.018),93.));
 // Four thick-rooted cruciform fins, swept at both ends and joined to the jacket.
 for(int i=0;i<4;i++){
  float a=float(i)*1.5707963268;vec3 q=p;q.xy=rot2(q.xy,a);
  float fin=sdRoundBox(q-vec3(.178,0,.485),vec3(.058,.011,.245),.004);
  fin=max(fin,(q.x-.236-(q.z-.48)*.22)*.9766);
  fin=max(fin,(q.x-.236+(q.z-.48)*.18)*.9842);
  h=opU(h,vec2(fin,90.));
 }
 for(int j=0;j<2;j++)h=opU(h,vec2(wrWeaponRing(p-vec3(0,0,-.23+float(j)*.49),.166,.150,.018),82.));
 return h;
}
vec2 wrEmpPayload(vec3 p){
 vec2 h=vec2(wrWeaponCylinder(p,.191,.405),90.);
 // Protected segmented capacitor canister, longitudinal ribs and bolted end plates.
 for(int i=0;i<8;i++){
  float a=float(i)*.7853981634;vec3 q=p;q.xy=rot2(q.xy,a);
  h=opU(h,vec2(sdRoundBox(q-vec3(.193,0,0),vec3(.016,.036,.322),.008),92.));
 }
 for(int s=-1;s<=1;s+=2){
  h=opU(h,vec2(wrWeaponCylinder(p-vec3(0,0,float(s)*.408),.206,.025),92.));
  h=opU(h,vec2(wrWeaponCylinder(p-vec3(0,0,float(s)*.449),.122,.019),93.));
  for(int i=0;i<6;i++){
   float a=float(i)*1.0471975512;
   h=opU(h,vec2(wrWeaponCylinder(p-vec3(cos(a)*.149,sin(a)*.149,float(s)*.438),.012,.008),90.));
  }
 }
 for(int j=-1;j<=1;j++)h=opU(h,vec2(wrWeaponRing(p-vec3(0,0,float(j)*.06),.224,.188,.014),82.));
 // Low-powered status strip, not an exposed energy ball.
 h=opU(h,vec2(wrWeaponRing(p,.230,.188,.008),89.));
 return h;
}
vec2 wrWeaponPartField(int k,vec3 p){
 if(k==PT_WR_KINETIC)return wrKineticCartridge(p);
 if(k==PT_WR_CHARGED)return wrChargedCartridge(p);
 if(k==PT_WR_PENETRATOR)return wrPenetratorPayload(p);
 if(k==PT_WR_EMP)return wrEmpPayload(p);
 return wrPlasmaPayload(p);
}
