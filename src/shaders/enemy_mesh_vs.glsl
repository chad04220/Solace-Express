//! kEnemyMeshVS
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aMaterialAO;
uniform mat4 uVP, uPanoView;
uniform mat3 uRot;
uniform vec3 uPos;
uniform vec2 uJit, uPano;
uniform float uLogC;
out vec3 vB, vW, vN;
flat out float vId;
out float vIdS, vAo;
void main(){
 vB=aPos; vW=uRot*aPos+uPos; vN=uRot*aNormal;
 vId=aMaterialAO.x; vIdS=aMaterialAO.x; vAo=aMaterialAO.y;
 gl_Position=uVP*vec4(vW,1.);
 bool behind=false;
 if(uPano.x>0.){
  vec3 c=(uPanoView*vec4(vW,0.)).xyz;
  float a=atan(c.x,-c.z), d=length(c);
  gl_Position=vec4(a/uPano.x*d,c.y/max(length(c.xz),1e-3)/uPano.y*d,0.,d);
  behind=abs(a)>1.9;
 }
 gl_Position.xy-=2.*uJit*gl_Position.w;
 gl_Position.z=(log2(max(1e-6,1.+gl_Position.w))*uLogC-1.)*gl_Position.w;
 if(behind)gl_Position.z=2.*gl_Position.w;
}
