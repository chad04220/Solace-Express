//! kKestrelFX27Gear
//! FX27 gear: native fleet wheel-motion contract, fixed contact/tire envelopes and phase-independent field.
//! Apply wheel roll only to the material rest frame; the existing hull bake has no wheel-phase channel.
//! 120 rim/axle,121 brake rotor,122 fixed caliper,123 painted leg,124 polished oleo.
float kfGearExtension(){return 1.85*smoothstep(.18,1.,clamp(gPS.x,0.,1.));}
float kfGearSteer(){return gPS.z*smoothstep(.55,.90,clamp(gPS.x,0.,1.));}
vec3 kfGearCenter(int n){return vec3(n==0?-1.85:n==1?1.85:0.,-.08-kfGearExtension(),n==2?-5.80:.70);}
// The steered frame is shared verbatim by geometry and finish. Roll is nested underneath it.
vec3 kfGearLocal(vec3 p,int n){vec3 v=p-kfGearCenter(n);if(n==2)v.xz=rot2(v.xz,-kfGearSteer());return v;}
vec3 kfGearRollFrame(vec3 v,int n){v.yz=rot2(v.yz,-gWheel[n]);return v;}

vec2 kfDetailedWheel(vec3 v,float h,float roundness,float rimRadius,float inboard,bool braked){
 const float radius=.38;
 float bore=rimRadius-.017;
 float tire=max(sdRoundCylX(v,radius,h,roundness),-sdCylX(v,bore,h+.020));
 vec2 r=vec2(tire,6);
 // Actual deep counterbore, rolled rim lip, recessed wheel face and raised axle cap.
 // All hub geometry fits the original extent: .153 main / .079 nose.
 float hubEnd=h+(braked?.008:.007);
 float lip=max(sdRoundCylX(v,rimRadius,hubEnd,.004),-sdCylX(v,rimRadius-.031,hubEnd+.012));
 float face=sdRoundCylX(v,rimRadius-.012,h-.026,.004);
 float cap=sdRoundCylX(v,.074,hubEnd,.007);
 r=opU(r,vec2(min(lip,min(face,cap)),120));
 // Inboard rotor remains inside the old hub envelope; the fixed caliper is on the fork.
 if(braked)r=opU(r,vec2(sdRoundCylX(v-vec3(inboard*h,0,0),.247,.008,.003),121));
 return r;
}

// p is in body space; the original translation/door sequence is reproduced by kfGearLocal.
vec2 kfGearAssembly(vec3 p,int n){
 vec3 v=kfGearLocal(p,n);float side=n==0?-1.:1.;vec2 r=vec2(1e5,123);
 if(n==2){
  for(int i=-1;i<=1;i+=2)r=opU(r,kfDetailedWheel(v-vec3(float(i)*.15,0,0),.072,.030,.190,0.,false));
  // Twin-wheel axle and a centre fork fit between the two tires; only steering turns this assembly.
  r=opU(r,vec2(sdCylX(v,.043,.221),124));
  r=opU(r,vec2(sdCapsule(v,vec3(0,.025,0),vec3(0,.405,0),.040),123));
  r=opU(r,vec2(sdCylX((v-vec3(0,.405,0)).yxz,.058,.031),123));
 }else{
  float x=-side*.205;
  r=kfDetailedWheel(v,.145,.045,.205,-side,true);
  // The inboard fork runs outside the tire, then crosses above its crown. Neither fork nor caliper spins.
  r=opU(r,vec2(sdCylX(v-vec3(-side*.063,0,0),.051,.165),124));
  r=opU(r,vec2(sdCapsule(v,vec3(x,0,0),vec3(x,.415,0),.025),123));
  r=opU(r,vec2(sdCapsule(v,vec3(x,.415,0),vec3(0,.415,0),.025),123));
  r=opU(r,vec2(sdCylX((v-vec3(0,.417,0)).yxz,.048,.023),123));
  r=opU(r,vec2(sdCapsule(v,vec3(x,.115,0),vec3(x,.105,.230),.018),123));
  vec3 cal=v-vec3(-side*.196,.075,.230);
  r=opU(r,vec2(sdRoundBox(cal,vec3(.033,.085,.052),.012),122));
  // Visible opposed caliper fasteners, still in the steered/deployed but unspun frame.
  for(int j=-1;j<=1;j+=2)r=opU(r,vec2(sdCylX(cal-vec3(-side*.032,float(j)*.050,0),.010,.005),124));
 }
 return r;
}

float kfGearTube(vec3 v,float lo,float hi,float radius){
 float h=max(.006,.5*abs(hi-lo));return sdRoundCylX(vec3(v.y-.5*(lo+hi),v.x,v.z),radius,h,min(.004,h*.45));
}
// Three nested stages telescope instead of stretching the collars; torque links articulate with extension.
// The upper mount's flat cap is fixed at body y=.38, touching the permanent bay ceiling.
vec2 kfGearOleo(vec3 p,int n){
 float travel=kfGearExtension();vec3 c=kfGearCenter(n)+vec3(0,travel,0),v=p-c;
 float upper=.425-.33*travel,lower=.425-.67*travel;
 vec2 r=vec2(kfGearTube(v,.415-.33*travel,.46,.071),123);
 r=opU(r,vec2(kfGearTube(v,.415-.70*travel,.425-.30*travel,.054),124));
 r=opU(r,vec2(kfGearTube(v,.415-travel,.425-.67*travel,.041),124));
 r=opU(r,vec2(kfGearTube(v,upper-.020,upper+.020,.078),123));
 r=opU(r,vec2(kfGearTube(v,lower-.018,lower+.018,.060),123));
 vec3 a=vec3(0,upper,.054),b=vec3(0,lower,.044);
 vec3 elbow=vec3(0,.5*(upper+lower),.115+.095*smoothstep(0.,1.,travel));
 for(int i=-1;i<=1;i+=2){
  vec3 offset=vec3(float(i)*.027,0,0);
  float link=min(sdCapsule(v,a+offset,elbow+offset,.012),sdCapsule(v,elbow+offset,b+offset,.012));
  r=opU(r,vec2(link,123));
 }
 r=opU(r,vec2(sdCylX(v-elbow,.022,.047),124));
 r=opU(r,vec2(sdCylX(v-a,.018,.050),124));
 r=opU(r,vec2(sdCylX(v-b,.017,.041),124));
 return r;
}
