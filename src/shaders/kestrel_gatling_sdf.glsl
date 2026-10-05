//! kKestrelFX27Gatling
//! Fictional retractable rotary energy gun, rendered by the native field. No clock-driven geometry.
//! wr[0]=(door,carrier,rotorRadians,spool), wr[1].x=flash. Dimensions are visual game art only.
float kfGatlingBay(vec3 p){return sdRoundBox(p-vec3(0,-.60,-1.50),vec3(.32,.50,.92),.035);}
float kfGatlingHousing(vec3 p){return sdRoundBox(p-vec3(0,-.48,-1.50),vec3(.43,.42,1.04),.075);}
vec3 kfGatlingRig(vec3 p,int id){
 if(id==50||id==51){float s=id==50?-1.:1.;return kfRotate(p,vec3(s*.34,-.90,-1.50),vec3(0,0,1),-s*1.45*clamp(gWr[0].x,0.,1.));}
 if(id==52||id==53){p.y+=.75*clamp(gWr[0].y,0.,1.);if(id==53)p=kfRotate(p,vec3(0,-.52,-1.50),vec3(0,0,1),-gWr[0].z);}
 return p;
}
vec3 kfGatlingUnrig(vec3 p,int id){
 if(id==50||id==51){float s=id==50?-1.:1.;return kfRotate(p,vec3(s*.34,-.90,-1.50),vec3(0,0,1),s*1.45*clamp(gWr[0].x,0.,1.));}
 if(id==52||id==53){if(id==53)p=kfRotate(p,vec3(0,-.52,-1.50),vec3(0,0,1),gWr[0].z);p.y-=.75*clamp(gWr[0].y,0.,1.);}
 return p;
}
vec2 kfGatlingPart(vec3 p,int id){
 vec3 q=kfGatlingRig(p,id);vec2 r=vec2(1e5,126);float d;
 if(id==50||id==51){
  float s=id==50?-1.:1.;vec3 c=vec3(s*.17,-.922,-1.50);
  d=sdRoundBox(q-c,vec3(.17,.022,.942),.012);
  // Captured hinge spindle: a rounded journal pocket follows the door, clear of the static pin.
  d=max(d,-sdCylX((q-vec3(s*.34,-.90,-1.50)).zxy,.037,.95));
  r=vec2(d,1);
  // Thin copper panel datum remains inset in its own moving door skin.
  r=opU(r,vec2(sdRoundBox(q-(c+vec3(0,-.022,0)),vec3(.012,.004,.61),.003),110));
 }
 if(id==52){
  vec3 v=q-vec3(0,-.52,0);
  // Stationary carrier cradle with a bore around the rotating drum; the rear bearing is separate.
  d=sdRoundBox(v-vec3(0,0,-.965),vec3(.25,.235,.29),.048);
  d=max(d,-sdCylX((v-vec3(0,0,-.965)).zxy,.189,.34));
  r=vec2(d,126);
  r=opU(r,vec2(sdCylX((v-vec3(0,0,-.645)).zxy,.226,.026),125));
  r=opU(r,vec2(sdCylX((v-vec3(0,0,-.825)).zxy,.049,.17),124));
  // Brackets are solid, attached to the cradle, with a compact amber status inset.
  for(int i=-1;i<=1;i+=2){float s=float(i);
   r=opU(r,vec2(sdRoundBox(q-vec3(s*.205,-.305,-.95),vec3(.045,.070,.21),.018),125));
   r=opU(r,vec2(sdRoundBox(q-vec3(s*.252,-.52,-.96),vec3(.004,.028,.14),.003),127));
   for(int n=0;n<3;n++)r=opU(r,vec2(sdCylX(q-vec3(s*.254,-.42,-1.12+float(n)*.15),.014,.005),124));
  }
 }
 if(id==53){
  vec3 v=q-vec3(0,-.52,0);
  // Rotor drum and a real center bore keep the fixed bearing shaft out of the rotating solid.
  d=sdCylX((v-vec3(0,0,-1.06)).zxy,.170,.225);
  d=max(d,-sdCylX((v-vec3(0,0,-1.06)).zxy,.061,.24));
  r=vec2(d,125);
  for(int k=0;k<6;k++){
   float a=float(k)*1.04719755;vec3 b=v-vec3(.105*cos(a),.105*sin(a),-1.765);
   d=sdRoundCylX(b.zxy,.030,.590,.006);
   // Blind muzzle recess, leaving a closed rear wall and finite sidewall thickness.
   d=max(d,-sdCapsule(b,vec3(0,0,-.65),vec3(0,0,-.37),.016));
   r=opU(r,vec2(d,125));
   r=opU(r,vec2(sdCylX((b-vec3(0,0,-.350)).zxy,.015,.014),126));
  }
  // Rotor collars bind all six barrels while preserving six discrete tube openings.
  for(int j=0;j<2;j++){
   float z=j==0?-2.19:-1.34;
   r=opU(r,vec2(sdTorus((v-vec3(0,0,z)).xzy,vec2(.110,.034)),125));
  }
 }
 if(id==54){
  float drop=.75*clamp(gWr[0].y,0.,1.);
  // Telescopic captive rails: upper faces meet the permanent bay roof at y=-.10.
  // The lower sleeves overlap their rods deliberately, like the landing-gear oleos.
  for(int i=-1;i<=1;i+=2){float s=float(i),x=s*.205;
   r=opU(r,vec2(sdRoundBox(p-vec3(x,-.16,-.95),vec3(.065,.060,.23),.016),126));
   r=opU(r,vec2(sdCapsule(p,vec3(x,-.17,-.95),vec3(x,-.295-drop,-.95),.028),124));
   r=opU(r,vec2(sdCylX((p-vec3(x,-.22-.50*drop,-.95)).yxz,.042,.050+.50*drop),125));
   r=opU(r,vec2(sdCylX((p-vec3(x,-.265-drop,-.95)).yxz,.048,.025),125));
  }
 }
 return r;
}
