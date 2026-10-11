//! kEnemyBastion
vec2 ccBastion(vec3 p,vec4 state){
 const vec2 hull[8]=vec2[8](vec2(-1.7,-6.2),vec2(1.7,-6.2),vec2(4.6,-3.8),vec2(5.1,2.2),vec2(3.3,5.8),vec2(-3.3,5.8),vec2(-5.1,2.2),vec2(-4.6,-3.8));
 vec2 hit=vec2(ccPlate(p,hull,8,.82,.20),110.);
 vec3 q=p;q.x=abs(q.x);
 const vec2 shield[8]=vec2[8](vec2(.18,-5.7),vec2(1.5,-5.7),vec2(4.1,-3.45),vec2(4.55,.3),vec2(3.4,1.5),vec2(.18,.5),vec2(.18,-5.7),vec2(.18,-5.7));
 ccAdd(hit,ccPlate(q-vec3(0,.77,0),shield,6,.22,.055),112.);
 ccAdd(hit,ccPlate(q-vec3(0,.85,.07),shield,6,.24,.026),111.);
 const vec2 rear[8]=vec2[8](vec2(.18,1.0),vec2(3.4,1.9),vec2(4.15,2.45),vec2(2.95,5.25),vec2(.18,5.25),vec2(.18,1),vec2(.18,1),vec2(.18,1));
 ccAdd(hit,ccPlate(q-vec3(0,.75,0),rear,5,.18,.04),111.);
 const vec2 shoulder[8]=vec2[8](vec2(3.9,-2.9),vec2(6.2,-3.3),vec2(8.1,-.9),vec2(8.0,2.75),vec2(6.5,4.4),vec2(3.8,3.4),vec2(3.9,-2.9),vec2(3.9,-2.9));
 ccAdd(hit,ccPlate(q-vec3(0,-.20,0),shoulder,6,.40,.14),110.);
 ccUnion(hit,ccPod(q,vec3(6.25,-.28,.7),1.32,3.35));
 ccUnion(hit,ccPod(p,vec3(0,-.64,3.35),1.25,1.85));
 // Deep, closed payload cassettes, rooted into belly rather than hanging bombs.
 for(int k=0;k<3;k++){
  float z=-3.3+float(k)*1.7;
  ccAdd(hit,ccBox(q,vec3(2.5,-.86,z),vec3(.87,.28,.63),.12),113.);
  ccAdd(hit,ccBox(q,vec3(2.5,-1.10,z),vec3(.61,.045,.34),.04),116.);
 }
 ccAdd(hit,ccBox(p,vec3(0,.94,-4.1),vec3(1.18,.23,.47),.09),113.);
 ccAdd(hit,ccBox(p,vec3(0,1.17,-4.1),vec3(.98,.045,.30),.035),114.);
 ccAdd(hit,ccBox(p,vec3(0,.92,2.8),vec3(.45,.52,1.4),.10),111.);
 for(int k=0;k<3;k++)ccAdd(hit,ccBox(p,vec3(0,1.445,2.2+float(k)*.30),vec3(.31,.02,.07),.015),112.);
 return hit;
}
