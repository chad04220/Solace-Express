//! kEnemyCommon
float ccBox(vec3 p,vec3 center,vec3 halfSize,float bevel){
 vec3 q=abs(p-center)-max(halfSize-vec3(bevel),vec3(.001));
 return length(max(q,0.))+min(max(q.x,max(q.y,q.z)),0.)-bevel;
}
float ccCylinderY(vec3 p,float radius,float halfY){
 vec2 q=vec2(length(p.xz)-radius,abs(p.y)-halfY);
 return min(max(q.x,q.y),0.)+length(max(q,0.));
}
// Polygon prism with a conservative 45-degree armor chamfer. The bevel expands
// the footprint by bevel metres; the supplied vertical half-height is final.
float ccPlate(vec3 p,vec2 vertices[8],int count,float halfY,float bevel){
 float d2=1e20;bool inside=false;vec2 q=p.xz;
 for(int i=0;i<8;i++){
  if(i>=count)break;
  int j=i+1;if(j==count)j=0;
  vec2 a=vertices[i],b=vertices[j],e=b-a,v=q-a;
  vec2 w=v-e*clamp(dot(v,e)/max(dot(e,e),1e-8),0.,1.);
  d2=min(d2,dot(w,w));
  if((a.y>q.y)!=(b.y>q.y)){
   float crossX=a.x+(q.y-a.y)*(b.x-a.x)/(b.y-a.y);
   if(q.x<crossX)inside=!inside;
  }
 }
 float planar=sqrt(max(d2,0.))*(inside?-1.:1.);
 vec2 d=vec2(planar,abs(p.y)-max(.001,halfY-bevel));
 float rounded=min(max(d.x,d.y),0.)+length(max(d,0.))-bevel;
 // Broad machined edge facets avoid vertical slab-like armor; normalized plane
 // intersection preserves a conservative signed distance for sphere tracing.
 float chamfer=(planar+abs(p.y)-halfY+min(halfY*.68,.50))*.70710678-bevel*.35;
 return max(rounded,chamfer);
}
void ccAdd(inout vec2 hit,float d,float material){if(d<hit.x)hit=vec2(d,material);}
void ccUnion(inout vec2 hit,vec2 part){if(part.x<hit.x)hit=part;}
// Connected nacelle, never a loose decorative thruster. Two open-bottom wells
// are permanently cut into the casing; emitter caps are solid inset discs.
// radius is outer X half-width; lengthZ is outer Z half-length.
vec2 ccPod(vec3 p,vec3 center,float radius,float lengthZ){
 vec3 q=p-center;
 float shell=ccBox(q,vec3(0),vec3(radius,.56*radius,lengthZ),.22*radius);
 // Swept cheek tapers make the pod part of a directional craft, not a box.
 float taper=.45*radius/(.60*lengthZ);
 shell=max(shell,(abs(q.x)-radius-taper*(q.z+.40*lengthZ))/sqrt(1.+taper*taper));
 float cavities=1e5;
 for(int i=0;i<2;i++){
  vec3 c=q-vec3(0,-.50*radius,(i==0?-1.:1.)*.46*lengthZ);
  cavities=min(cavities,ccCylinderY(c,.56*radius,.33*radius));
 }
 float casingMaterial=110.;
 // Copper is also assigned on the existing flush lip surface. The geometric
 // collar stays embedded; this reveals its finish without expanding the hull.
 for(int i=0;i<2;i++){
  float lipR=length(q.xz-vec2(0,(i==0?-1.:1.)*.46*lengthZ));
  if(q.y<-.48*radius && lipR>.555*radius && lipR<.69*radius)casingMaterial=112.;
 }
 vec2 hit=vec2(max(shell,-cavities),casingMaterial);
 for(int i=0;i<2;i++){
  vec3 c=q-vec3(0,-.21*radius,(i==0?-1.:1.)*.46*lengthZ);
  ccAdd(hit,ccCylinderY(c,.555*radius,.10*radius),115.);
  // Copper lip lies in the casing, inset from its outer silhouette.
  vec3 r=q-vec3(0,-.525*radius,(i==0?-1.:1.)*.46*lengthZ);
  float ring=max(ccCylinderY(r,.67*radius,.026*radius),-ccCylinderY(r,.57*radius,.06*radius));
  ccAdd(hit,ring,112.);
 }
 // Rooted heat-exchanger slots on the dorsal casing, never open holes.
 for(int k=0;k<4;k++){
  float z=(float(k)*.19-.12)*lengthZ;
  ccAdd(hit,ccBox(q,vec3(0,.555*radius,z),vec3(.57*radius,.030*radius,.043*radius),.015*radius),113.);
 }
 // Inset aft drive window backed by solid casing. No flame mesh or hole.
 ccAdd(hit,ccBox(q,vec3(0,.02*radius,lengthZ-.012),vec3(.56*radius,.18*radius,.028),.025*radius),116.);
 return hit;
}
