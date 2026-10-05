//! kKestrelFX27SDF
//! XR-15 Peregrine (Codex's FX-27): native Solace research adaptation on its own field (engine code 8). Body +x right,+y up,+z aft.
//! No imported meshes/textures. All geometry is closed volume; moving geometry uses kfRig and the shared kfGear helpers.
// Parts: 0 hull/wells,1 wings,2 engines,3 fins,10/11 stabilators,12/13 ailerons,
// 14/15 flaps,16/17 rudders,20..22 wheels,23..25 shutters,26..28 telescopic sleeves,
// 30 canopy frame,40 cabin,41 stick,42 throttle,43/44 pedals. Fixed glazing openings.
vec2 kfGatlingPart(vec3 p,int id);
float kfGatlingBay(vec3 p);float kfGatlingHousing(vec3 p);
vec3 kfGatlingRig(vec3 p,int id);vec3 kfGatlingUnrig(vec3 p,int id);
float kfEll(vec3 p,vec3 r){return (length(p/r)-1.0)*min(r.x,min(r.y,r.z));}
float kfPoly(vec2 p,vec2 a,vec2 b,vec2 c,vec2 d){
  vec2 v[4]=vec2[4](a,b,c,d);float ds=1e6;bool pos=false,neg=false;
  for(int i=0;i<4;i++){vec2 e=v[(i+1)%4]-v[i],q=p-v[i];float t=clamp(dot(q,e)/dot(e,e),0.,1.);ds=min(ds,length(q-e*t));float z=e.x*q.y-e.y*q.x;pos=pos||z>0.;neg=neg||z<0.;}
  return pos&&neg?ds:-ds;
}
float kfExtrude(float d,float y,float h){vec2 q=vec2(d,abs(y)-h);return min(max(q.x,q.y),0.)+length(max(q,0.));}
vec3 kfRotate(vec3 p,vec3 o,vec3 a,float t){vec3 q=p-o;return o+q*cos(t)+cross(a,q)*sin(t)+a*dot(a,q)*(1.-cos(t));}
float kfSide(int id){return (id==10||id==12||id==14||id==16||id==20||id==23||id==26||id==43)?-1.:1.;}
float kfGearTravel(){return 1.85*smoothstep(.18,1.,clamp(gPS.x,0.,1.));}
float kfShutterTravel(){return smoothstep(0.,.18,clamp(gPS.x,0.,1.));}
vec3 kfGearOrigin(int id){int n=id>=26?id-26:id>=23?id-23:id-20;return vec3(n==0?-1.85:n==1?1.85:0.,-.08,n==2?-5.80:.70);}
void kfTransform(int id,out vec3 o,out vec3 a,out float angle,out vec3 shift){
 o=vec3(0);a=vec3(1,0,0);angle=0.;shift=vec3(0);
 float s=kfSide(id);
 if(id==10||id==11){o=vec3(s*1.50,.15,6.05);angle=-.28*gCtl.x;}
 if(id>=12&&id<=15){o=vec3(s*3.5,-.35,2.70);a=normalize(vec3(s,0,-.057));angle=id<=13?-.24*gCtl.y:.42*gPS.y*s;}
 if(id==16||id==17){o=vec3(s*1.52,.65,6.75);a=normalize(vec3(s*.30,1.,0));angle=.28*gCtl.z;}
 if(id>=20&&id<=22)shift=vec3(0,-kfGearTravel(),0);
 if(id>=23&&id<=25)shift=id==25?vec3(0,0,1.30*kfShutterTravel()):vec3(s*.78*kfShutterTravel(),0,0);
 if(id==41){o=vec3(0,.37,-4.05);a=normalize(vec3(1,0,-.001));angle=.22*gCtl.x;shift=vec3(.055*gCtl.y,0,0);}
 if(id==42){o=vec3(-.46,.46,-3.78);angle=-.55*gCtl.w+.20;}
 if(id==43||id==44)shift=vec3(0,0,s*.065*gCtl.z);
}
vec3 kfRig(vec3 p,int id){if(id>=50)return kfGatlingRig(p,id);vec3 o,a,t;float v;kfTransform(id,o,a,v,t);p=kfRotate(p-t,o,a,-v);
 if(id>=26&&id<=28){vec3 c=kfGearOrigin(id)+vec3(0,.24,0);float sc=(.24+kfGearTravel())/.24;p.y=c.y+(p.y-c.y)/sc;}
 return p;
}
vec3 kfUnrig(vec3 p,int id){if(id>=50)return kfGatlingUnrig(p,id);if(id>=26&&id<=28){vec3 c=kfGearOrigin(id)+vec3(0,.24,0);float sc=(.24+kfGearTravel())/.24;p.y=c.y+(p.y-c.y)*sc;}
 vec3 o,a,t;float v;kfTransform(id,o,a,v,t);return kfRotate(p,o,a,v)+t;
}
float kfWingSolid(vec3 p,float s){float x=s*p.x;float d=kfPoly(vec2(x,p.z),vec2(.85,-2.50),vec2(7.,1.70),vec2(7.,2.82),vec2(.85,3.18));return kfExtrude(d,p.y+.35,max(.045,.20-.022*x))/1.001;}
float kfWingMask(vec3 p,float s,float lo,float hi,float inset){
 vec3 q=vec3(s*p.x,p.y+.35,p.z-(2.90-.057*s*p.x));
 // Sheared hinge frame has operator norm <1.03; keep the mask a conservative distance.
 return max(max(lo-q.x,q.x-hi),min(-q.z,length(q.yz)-.19))/1.03+inset;
}
vec3 kfFinFrame(vec3 p,float s){vec3 q=p-vec3(s*1.52,.65,4.8);q.xy=vec2(.957826*s*q.x-.287348*q.y,.287348*s*q.x+.957826*q.y);return q;}
float kfFinSolid(vec3 p,float s){vec3 q=kfFinFrame(p,s);return kfExtrude(kfPoly(q.zy,vec2(0,0),vec2(1.50,2.28),vec2(2.5,2.28),vec2(2.95,0)),q.x,.075);}
float kfFinMask(vec3 p,float s,float inset){vec3 q=kfFinFrame(p,s);return max(max(.20-q.y,q.y-2.12),min(2.0-q.z,length(vec2(q.x,q.z-2.0))-.12))+inset;}
float kfCabinCavity(vec3 p){return sdRoundBox(p-vec3(0,1.02,-3.86),vec3(.66,1.04,1.29),.20);}
float kfBay(vec3 p,int n){vec3 c=vec3(n==0?-1.85:n==1?1.85:0.,-.15,n==2?-5.80:.70);return sdBox(p-c,vec3(.36,.53,.54));}
// Cut from the complete hull union as well as the nacelles: neither tail spine nor fin fairings may fill an exhaust.
float kfExhaustBore(vec3 p){float d=1e5;for(int i=-1;i<=1;i+=2)d=min(d,sdCapsule(p,vec3(float(i)*.87,-.12,7.20),vec3(float(i)*.87,-.12,9.50),.475));return d;}
float kfHull(vec3 p){
 vec3 q=vec3(p.x,p.y/0.67,p.z);
 float d=.67*sdRoundCone(q,vec3(0,.1,-9.06),vec3(0,.1,-4.1),.035,1.12);
 d=smin(d,.67*sdRoundCone(q,vec3(0,.1,-4.1),vec3(0,0,3.6),1.12,1.46),.18);
 d=smin(d,.67*sdRoundCone(q,vec3(0,0,3.6),vec3(0,.1,9.06),1.46,.04),.15);
 for(int i=-1;i<=1;i+=2){float s=float(i);
  vec3 v=vec3(p.x,p.y/.22,p.z);d=min(d,.22*sdRoundCone(v,vec3(s*.45,-.5,-6.5),vec3(s*1.9,-.5,-.55),.15,.70));
  float fairing=kfEll(p-vec3(s*1.25,.43,6.25),vec3(.52,.43,2.0));
  // Permanent stabilator travel pocket: the fin mount bridges above the entire pitch envelope.
  fairing=max(fairing,-sdBox(p-vec3(s*2.60,-.10,6.40),vec3(1.12,.80,1.90)));
  d=min(d,fairing);
  d=min(d,sdRoundBox(p-vec3(s*1.85,-.08,.70),vec3(.55,.58,.79),.28));
 }
 d=min(d,sdRoundBox(p-vec3(0,-.30,-5.80),vec3(.48,.35,.75),.13));
 for(int i=-1;i<=1;i+=2){float a=float(i);float chine=kfPoly(vec2(a*p.x,p.z),vec2(.22,-7.30),vec2(1.72,-2.35),vec2(2.20,.15),vec2(.55,.50));d=smin(d,kfExtrude(chine,p.y+.12,.105),.11);}
 d=max(d,-kfCabinCavity(p));for(int n=0;n<3;n++)d=max(d,-kfBay(p,n));
 d=min(d,kfGatlingHousing(p));d=max(d,-kfGatlingBay(p));
 d=max(d,-kfExhaustBore(p));return d;
}
vec2 kfPart(vec3 p,int id){if(id>=50)return kfGatlingPart(p,id);
 vec3 q=kfRig(p,id);float d=1e5;vec2 r=vec2(d,1);float s=kfSide(id);
 if(id==0){r=vec2(kfHull(p),1);
  for(int j=-1;j<=1;j+=2)r=opU(r,vec2(sdCylX((p-vec3(float(j)*.34,-.90,-1.50)).zxy,.030,.94),125));
  // Fixed guide tracks capture both edges of each shutter throughout its full travel.
  for(int i=-1;i<=1;i+=2)for(int j=-1;j<=1;j+=2){float a=float(i),b=float(j);r=opU(r,vec2(sdRoundBox(p-vec3(a*2.24,-.65,.70+b*.59),vec3(.82,.026,.022),.009),8));}
  for(int i=-1;i<=1;i+=2)r=opU(r,vec2(sdRoundBox(p-vec3(float(i)*.414,-.65,-5.15),vec3(.022,.026,1.22),.009),8));
 }
 if(id==1){for(int i=-1;i<=1;i+=2){float a=float(i);d=kfWingSolid(p,a);d=max(d,-kfWingMask(p,a,2.0,4.20,0.));d=max(d,-kfWingMask(p,a,4.30,6.80,0.));for(int n=0;n<3;n++)d=max(d,-kfBay(p,n));r=opU(r,vec2(d,2));} }
 if(id==2){for(int i=-1;i<=1;i+=2){float a=float(i);vec3 v=p-vec3(a*.87,-.12,0);d=sdRoundCone(v,vec3(0,0,-.1),vec3(0,0,7.72),.60,.60);
  d=max(d,-sdCapsule(v,vec3(0,0,-.9),vec3(0,0,.3),.46));d=max(d,-sdCapsule(v,vec3(0,0,7.20),vec3(0,0,8.8),.46));
  r=opU(r,vec2(d,5));r=opU(r,vec2(sdCylX((v-vec3(0,0,.36)).zyx,.445,.035),21));r=opU(r,vec2(sdCylX((v-vec3(0,0,7.15)).zyx,.445,.03),17));
  // Recessed, intentional engine internals: a backing disk, hub and eight radial stator vanes.
  r=opU(r,vec2(sdCapsule(v,vec3(0,0,7.12),vec3(0,0,7.24),.070),17));
  for(int n=0;n<8;n++){float t=float(n)*.785398;vec2 u=vec2(cos(t),sin(t));r=opU(r,vec2(sdCapsule(v,vec3(u*.11,7.21),vec3(u*.40,7.205),.012),8));}
  r=opU(r,vec2(sdTorus((v-vec3(0,0,7.21)).xzy,vec2(.414,.012)),17));
  r=opU(r,vec2(sdCylX(p-vec3(a*1.50,.15,6.05),.042,.17),8));
  for(int n=0;n<3;n++)r=opU(r,vec2(sdTorus((v-vec3(0,0,7.48+float(n)*.20)).xzy,vec2(.605,.025)),8));
 } }
 if(id==3){for(int i=-1;i<=1;i+=2){float a=float(i);d=max(kfFinSolid(p,a),-kfFinMask(p,a,0.));r=opU(r,vec2(d,3));}}
 if(id==10||id==11){vec3 v=vec3(s*q.x,q.y,q.z);d=kfPoly(v.xz,vec2(1.50,5.05),vec2(3.62,6.85),vec2(3.62,7.72),vec2(1.50,7.45));d=kfExtrude(d,v.y-.15,.075);d=max(d,-sdCylX(q-vec3(s*1.50,.15,6.05),.060,.20));r=vec2(d,3);}
 if(id>=12&&id<=15){float lo=id<=13?4.30:2.,hi=id<=13?6.8:4.2;r=vec2(max(kfWingSolid(q,s),kfWingMask(q,s,lo,hi,.012)),2);}
 if(id==16||id==17)r=vec2(max(kfFinSolid(q,s),kfFinMask(q,s,.012)),3);
 if(id>=20&&id<=22)r=kfGearAssembly(p,id-20);
 if(id>=23&&id<=25){vec3 c=kfGearOrigin(id);r=vec2(sdRoundBox(q-vec3(c.x,-.65,c.z),vec3(.373,.025,.552),.012),1);}
 if(id>=26&&id<=28)r=kfGearOleo(p,id-26);
 if(id==30){
  // Bubble-canopy silhouette with real openings, following the fleet's unglazed-window convention.
  // The permanent frame never depends on view or time; visibility in/out is symmetric.
  for(int k=0;k<2;k++){float z=k==0?-4.94:-2.77;for(int n=0;n<12;n++){float a=float(n)*3.141593/12.,b=float(n+1)*3.141593/12.;vec3 A=vec3(.72*cos(a),.74+.98*sin(a),z),B=vec3(.72*cos(b),.74+.98*sin(b),z);r=opU(r,vec2(sdCapsule(p,A,B,.027),110));}}
  for(int i=-1;i<=1;i+=2){float a=float(i);r=opU(r,vec2(sdCapsule(p,vec3(a*.72,.74,-4.94),vec3(a*.72,.74,-2.77),.040),110));}
  for(int i=-1;i<=1;i+=2)for(int j=0;j<2;j++){float a=float(i),z=j==0?-4.94:-2.77;r=opU(r,vec2(sdCapsule(p,vec3(a*.72,.74,z),vec3(a*.55,.35,z),.040),110));}
  r=opU(r,vec2(sdCapsule(p,vec3(0,1.72,-4.94),vec3(0,1.72,-2.77),.022),110));
 }
 if(id==40){
  r=vec2(sdRoundBox(p-vec3(0,.10,-3.80),vec3(.55,.045,1.06),.025),113);
  // Solid attachments: no hovering seat, consoles, instrument panel or control pedestal.
  for(int i=-1;i<=1;i+=2){float a=float(i);
   r=opU(r,vec2(sdCapsule(p,vec3(a*.18,.155,-3.67),vec3(a*.18,.155,-3.10),.025),119));
   r=opU(r,vec2(sdRoundBox(p-vec3(a*.49,.23,-3.92),vec3(.07,.10,.51),.015),113));
   r=opU(r,vec2(sdCapsule(p,vec3(a*.30,.14,-4.70),vec3(a*.30,.65,-4.70),.034),119));
   r=opU(r,vec2(sdRoundBox(p-vec3(a*.20,.17,-4.58),vec3(.040,.035,.28),.008),119));
  }
  r=opU(r,vec2(sdRoundBox(p-vec3(0,.25,-4.05),vec3(.075,.12,.080),.018),113));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,.32,-3.40),vec3(.31,.16,.36),.065),114));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,.80,-3.06),vec3(.31,.48,.070),.045),114));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,1.35,-3.07),vec3(.19,.13,.075),.025),114));
  for(int i=-1;i<=1;i+=2){float a=float(i);
   r=opU(r,vec2(sdCapsule(p,vec3(a*.13,1.17,-3.15),vec3(a*.09,.48,-3.28),.025),116));
   r=opU(r,vec2(sdRoundBox(p-vec3(a*.49,.45,-3.92),vec3(.11,.14,.66),.035),113));
   for(int n=0;n<7;n++)r=opU(r,vec2(sdRoundBox(p-vec3(a*.49,.605,-4.42+float(n)*.15),vec3(.065,.014,.032),.008),n==0?115:119));
   r=opU(r,vec2(sdRoundBox(p-vec3(a*.30,.77,-4.66),vec3(.255,.205,.065),.025),113));
   r=opU(r,vec2(sdRoundBox(p-vec3(a*.30,.78,-4.587),vec3(.21,.16,.015),.009),117));
   for(int n=0;n<4;n++)r=opU(r,vec2(sdCylX((p-vec3(a*.30-.16+float(n)*.106,.568,-4.58)).zxy,.014,.012),119));
  }
  r=opU(r,vec2(sdRoundBox(p-vec3(0,.42,-3.69),vec3(.05,.022,.046),.008),110));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,.60,-4.62),vec3(.095,.10,.04),.016),117));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,1.01,-4.72),vec3(.58,.045,.18),.03),113));
  // Small unobstructed HUD combiner frame, clear centre; top safely below canopy crown.
  for(int i=-1;i<=1;i+=2)r=opU(r,vec2(sdCapsule(p,vec3(float(i)*.18,1.06,-4.73),vec3(float(i)*.18,1.36,-4.65),.012),110));
  r=opU(r,vec2(sdCapsule(p,vec3(-.18,1.36,-4.65),vec3(.18,1.36,-4.65),.012),110));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,1.205,-4.69),vec3(.055,.004,.005),.003),115));
 }
 if(id==41){r=vec2(sdCapsule(q,vec3(0,.37,-4.05),vec3(0,.72,-4.05),.035),13);r=opU(r,vec2(sdRoundBox(q-vec3(0,.76,-4.05),vec3(.052,.09,.055),.028),13));}
 if(id==42)r=vec2(sdCapsule(q,vec3(-.46,.46,-3.78),vec3(-.46,.77,-3.78),.038),13);
 if(id==43||id==44)r=vec2(sdRoundBox(q-vec3(s*.20,.235,-4.58),vec3(.13,.045,.18),.018),61);
 return r;
}
float kfPartDistance(vec3 p,int id){return kfPart(p,id).x;}
// A uniform-dependent lower bound keeps GLSL drivers from cloning the entire part field
// at every constant loop iteration. The engine-8 dispatcher guarantees first == 0.
vec2 mapKestrelFX27(vec3 p){
 vec2 r=vec2(1e5,1);int first=int(gM[0].z+.5)-8;
 for(int i=first;i<55;i++)if(i<4||(i>=10&&i<=17)||(i>=20&&i<=28)||i==30||(i>=40&&i<=44)||i>=50)r=opU(r,kfPart(p,i));
 return r;
}
