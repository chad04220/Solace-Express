//! kEnemyMeshFS
in vec3 vB,vW,vN;
flat in float vId;
in float vIdS,vAo;
uniform vec4 uEnemyState;
uniform mat3 uRot;
void main(){
 vec3 n=normalize(vN);
 // Query the type's field only at material seams, never ray march its volume.
 float id=abs(vIdS-vId)>.001?mapEnemySurface(vB).y:vId;
 Mat m=enemyCraftMaterial(id,vB,transpose(uRot)*n,uEnemyState);
 gbWrite(length(vW),n,GB_ENEMY,m,vAo);
 oG3.y=enemyShadowMaps(uCamPos+vW,n)*storeShadowMaps(uCamPos+vW,n);
 oG3.z=1.;
 oG3.w=float(GBF_MOVING)/255.;
}
