//! kEnemyNeedle
vec2 ccNeedle(vec3 p,vec4 state){
 const vec2 hull[8]=vec2[8](vec2(0,-6.6),vec2(1.18,-3.7),vec2(1.48,.5),vec2(.84,4.5),vec2(0,5.3),vec2(-.84,4.5),vec2(-1.48,.5),vec2(-1.18,-3.7));
 float fus=ccPlate(p,hull,8,.58,.10);
 fus=max(fus,(p.y-.15-.075*(p.z+6.6))*.997199);
 fus=max(fus,(-p.y-.12-.065*(p.z+6.6))*.997894);
 vec2 hit=vec2(fus,110.);
 // Continuous armoured spine, its copper gasket visible between raised tiles.
 vec3 spine=p-vec3(0,.50,-.5);
 spine.yz=mat2(.997559,.069829,-.069829,.997559)*spine.yz;
 const vec2 armor[8]=vec2[8](vec2(0,-4.7),vec2(.78,-2.5),vec2(.84,.5),vec2(.52,3.6),vec2(0,4.1),vec2(-.52,3.6),vec2(-.84,.5),vec2(-.78,-2.5));
 ccAdd(hit,ccPlate(spine,armor,8,.24,.065),112.);
 spine.y-=.045;ccAdd(hit,ccPlate(spine,armor,8,.25,.035),111.);
 vec3 q=p;q.x=abs(q.x);
 const vec2 wing[8]=vec2[8](vec2(.82,-2.4),vec2(1.8,-2.7),vec2(5.95,.8),vec2(5.65,2.15),vec2(4.4,2.95),vec2(1.05,2.15),vec2(.82,-2.4),vec2(.82,-2.4));
 ccAdd(hit,ccPlate(q-vec3(0,-.06,0),wing,6,.18,.055),110.);
 const vec2 inset[8]=vec2[8](vec2(1.45,-1.7),vec2(1.9,-1.8),vec2(5.43,.93),vec2(5.20,1.55),vec2(4.2,2.20),vec2(1.4,1.7),vec2(1.45,-1.7),vec2(1.45,-1.7));
 ccAdd(hit,ccPlate(q-vec3(0,.105,0),inset,6,.070,.02),111.);
 ccUnion(hit,ccPod(q,vec3(3.5,-.12,1.2),.86,2.35));
 // Dorsal knife fin grows from the rear spine; tapered closed prism.
 vec3 fin=p-vec3(0,.64,2.8);
 float f=ccBox(fin,vec3(0,.31,0),vec3(.13,.72,1.65),.055);
 f=max(f,(fin.y-.40*(fin.z+1.65))*.928477);
 ccAdd(hit,f,111.);
 // Narrow embedded visor with armoured brow, not a transparent cabin.
 ccAdd(hit,ccBox(spine,vec3(0,.251,-3.15),vec3(.52,.025,.25),.020),113.);
 ccAdd(hit,ccBox(spine,vec3(0,.278,-3.15),vec3(.44,.013,.17),.010),114.);
 ccAdd(hit,ccBox(q,vec3(1.36,-.02,-2.3),vec3(.20,.20,1.25),.06),113.);
 ccAdd(hit,ccBox(q,vec3(1.36,-.02,-3.50),vec3(.13,.105,.065),.03),116.);
 // Family three-bar rank mark, physically rooted on dorsal plating.
 for(int k=0;k<3;k++)ccAdd(hit,ccBox(spine,vec3(0,.27,.8+float(k)*.23),vec3(.36-.05*float(k),.022,.048),.015),112.);
 return hit;
}
