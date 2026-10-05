//! kKestrelFX27GearMaterial
//! Detailed wheel finish in exact deployed/steered/rolled coordinates. No clock/airspeed-driven animation.
//! The field stays independent of gWheel so the native static/moving hull split cannot freeze rotating geometry.
float kfGearLine(float distance,float width,float pixel){return 1.-smoothstep(width,width+pixel,abs(distance));}
float kfGearBand(float x,float lo,float hi,float pixel){return smoothstep(lo-pixel,lo+pixel,x)*(1.-smoothstep(hi-pixel,hi+pixel,x));}
// A compact 5x7 numeric alphabet: digits, x and M only. Material coverage, never new geometry.
// Two small integers hold the lower four and upper three rows; no font atlas dependency.
float kfGearSizeGlyph(int code,vec2 cell,float antialias){
 if(any(lessThan(cell,vec2(0)))||any(greaterThanEqual(cell,vec2(5,7))))return 0.;
 int lo=0,hi=0;
 if(code==0){lo=575022;hi=14897;}
 else if(code==1){lo=135310;hi=4292;}
 else if(code==2){lo=266335;hi=14896;}
 else if(code==3){lo=475663;hi=15888;}
 else if(code==4){lo=326920;hi=8586;}
 else if(code==5){lo=508431;hi=31777;}
 else if(code==6){lo=509486;hi=14369;}
 else if(code==7){lo=133186;hi=32264;}
 else if(code==8){lo=476718;hi=14897;}
 else if(code==9){lo=999950;hi=14897;}
 else if(code==10){lo=141856;hi=554;}
 else if(code==11){lo=706097;hi=18293;}
 int bit=int(floor(cell.x))+5*int(floor(cell.y));
 int on=bit<20?((lo>>bit)&1):((hi>>(bit-20))&1);
 float edge=max(abs(fract(cell.x)-.5),abs(fract(cell.y)-.5));
 return float(on)*(1.-smoothstep(.48,.50+antialias,edge));
}
// Nominal model dimensions: outside diameter x full tire width, in millimetres.
// This is a size sequence, not a manufacturer/fitment/rating claim: mains 760x290 MM, nose 760x144 MM.
// Rolled-wheel-local x picks the sidewall face. Reversing the arc on the opposite face keeps text
// left-to-right when viewed from either side, including the mirrored main wheels and both nose tires.
float kfGearSizeText(vec3 q,float halfWidth,float pixel){
 const float cell=.0034,base=.254,arcRadius=.266;
 float face=q.x<0.?-1.:1.;
 float arc=-face*atan(q.z,q.y)*arcRadius;
 vec2 text=vec2((arc+.1003)/cell,(length(q.yz)-base)/cell);
 int column=int(floor(text.x/6.));if(column<0||column>9)return 0.;
 int diameter=int(2.*.38*1000.+.5),width=int(2.*halfWidth*1000.+.5),code=-1;
 if(column<3){int divisor=column==0?100:column==1?10:1;code=(diameter/divisor)%10;}
 else if(column==3)code=10;
 else if(column<7){int divisor=column==4?100:column==5?10:1;code=(width/divisor)%10;}
 else if(column>=8)code=11;
 float ink=kfGearSizeGlyph(code,vec2(text.x-float(column)*6.,text.y),clamp(pixel/cell*.5,.035,.28));
 // Details fade instead of turning into a bright dash at whole-aircraft viewing distances.
 return ink*(1.-smoothstep(cell*.85,cell*1.6,pixel));
}
void kfGearMaterial(inout Mat m,int mid,vec3 p,vec3 normal){
 if(mid==122){m.alb=vec3(.44,.23,.095);m.metal=.65;m.rough=.43;return;}
 if(mid==123){m.alb=vec3(.30,.39,.42);m.metal=.62;m.rough=.31;return;}
 if(mid==124){m.alb=vec3(.67,.72,.73);m.metal=.94;m.rough=.18;return;}
 if(mid!=6&&mid!=120&&mid!=121)return;
 int n=p.z< -3.?2:p.x<0.?0:1;
 vec3 q=kfGearLocal(p,n);float h=n==2?.072:.145;
 if(n==2)q.x-=q.x<0.?-.15:.15;
 q=kfGearRollFrame(q,n);
 float radius=.38,rad=length(q.yz),angle=atan(q.z,q.y);
 // Anti-alias fine surface details once per visible fragment, not per march step.
 float px=max(.0006,max(length(dFdx(p)),length(dFdy(p))));
 if(mid==6){
  float tread=smoothstep(.332,.366,rad);
  float grooves=kfGearLine(abs(q.x)-h*.36,.0035,px)*tread;
  float pitch=.078,arc=angle*radius;
  float slant=arc+.40*abs(q.x),repeat=slant-pitch*floor(slant/pitch+.5);
  float cuts=kfGearLine(repeat,.0037,px)*tread*smoothstep(pitch*.5,pitch*.12,px);
  float sidewall=smoothstep(h-.031,h-.006,abs(q.x));
  float bead=kfGearLine(rad-.289,.0023,px)*sidewall;
  m.alb=mix(vec3(.040,.046,.048),vec3(.008,.011,.012),max(grooves,max(cuts*.82,bead*.72)));
  // Restrained curved white size sequence only; the former radial datum stripe is removed.
  float sizeText=kfGearSizeText(q,h,px)*sidewall;
  m.alb=mix(m.alb,vec3(.72,.73,.69),sizeText);
  // Small sidewall index bars are molded rubber, not a second independent phase.
  float a=angle*18.;float mark=1.-smoothstep(.07,.07+px*40.,abs(sin(a)));
  float lettering=mark*kfGearBand(rad,.304,.318,px)*sidewall;
  m.alb=mix(m.alb,vec3(.10,.115,.116),lettering*.8);m.metal=0.;m.rough=.87;
  return;
 }
 if(mid==121){
  float sector=angle-.523598776*floor(angle/.523598776+.5);
  vec2 hole=rad*vec2(cos(sector),sin(sector))-vec2(.211,0);
  float drill=1.-smoothstep(.008,.008+px,length(hole));
  float rings=kfGearLine(rad-.221,.0015,px)+kfGearLine(rad-.180,.0015,px);
  m.alb=mix(vec3(.255,.275,.29),vec3(.055,.065,.069),max(drill,rings*.24));
  m.metal=.89;m.rough=.43;return;
 }
 // Recessed six-spoke hub, raised axle cap and six bolt heads. All share the tire's physical rolling phase.
 float sector=angle-1.047197551*floor(angle/1.047197551+.5);
 float tangential=abs(rad*sin(sector));
 float spoke=1.-smoothstep(.020,.020+px,tangential);
 float ring=kfGearBand(rad,.083,.165,px);
 m.alb=mix(vec3(.64,.68,.69),vec3(.10,.145,.16),ring*(1.-spoke));m.metal=.91;m.rough=.25;
 vec2 bolt=rad*vec2(cos(sector),sin(sector))-vec2(.128,0);
 float boltHead=1.-smoothstep(.010,.010+px,length(bolt));
 m.alb=mix(m.alb,vec3(.87,.84,.69),boltHead);m.rough=mix(m.rough,.19,boltHead);
 if(rad<.074){m.alb=vec3(.40,.47,.49);float slot=kfGearLine(q.z,.003,px)*kfGearBand(q.y,-.038,.038,px);m.alb*=1.-.55*slot;}
 float lip=kfGearLine(rad-(n==2?.183:.198),.002,px);m.alb=mix(m.alb,vec3(.82),lip*.7);
}
