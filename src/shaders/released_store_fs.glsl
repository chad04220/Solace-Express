//! kReleasedStoreFS
// The very same indexed PartMesh as the carried store, now at its independent pose.
in vec3 vB,vW,vN;
flat in float vId;
in float vIdS,vAo;
uniform int uStoreKind;
uniform mat3 uRot;
void main(){
 int mid=int((abs(vIdS-vId)>.001?releasedStoreField(vB).y:vId)+.5);
 vec3 n=normalize(vN),localNormal=transpose(uRot)*n;
 Mat m;m.alb=vec3(.1);m.rough=.4;m.metal=0.;m.nrm=vec3(0,0,1);m.emit=vec3(0);
 // Texture coordinates equal its bay-local authored coordinates, including veins.
 shadeWraithStore(m,mid,vB+vec3(0.,-.305,.1),localNormal);
 n=applyTS(n,m.nrm,.12);
 gbWrite(length(vW),n,GB_ENEMY,m,vAo);
 oG3.y=enemyShadowMaps(uCamPos+vW,n)*storeShadowMaps(uCamPos+vW,n);
 oG3.w=float(GBF_MOVING)/255.;
}
