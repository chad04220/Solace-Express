//! kEnemyShadow
// Dedicated layers follow the aircraft layers; no enemy field enters scene programs.
uniform int uEnemyShOn;
uniform mat4 uEnemyShVP[8];
float enemyShadowMaps(vec3 p,vec3 n){
 if(uEnemyShOn==0)return 1.;
 float s=1.; vec2 ts=1.5/vec2(textureSize(uAfShMap,0).xy);
 for(int k=0;k<8;k++){
  if((uEnemyShOn&(1<<k))==0)continue;
  vec4 q=uEnemyShVP[k]*vec4(p+n*.045,1.);
  vec3 u=q.xyz/q.w*.5+.5;
  if(u.x<=0.||u.x>=1.||u.y<=0.||u.y>=1.||u.z<=0.)continue;
  float z=min(u.z,.999),lit=0.;
  for(int j=0;j<4;j++)lit+=texture(uAfShMap,vec3(u.xy+(vec2(float(j&1),float(j>>1))-.5)*ts,float(16+k))).r>=z-.0008?1.:0.;
  s*=lit*.25;
 }
 return s;
}
// Released rigid stores, their own small sun projections (layers24..31).
uniform int uStoreShOn;
uniform mat4 uStoreShVP[8];
uniform float uStoreShFade[8];
float storeShadowMaps(vec3 p,vec3 n){
 if(uStoreShOn==0)return 1.;
 float s=1.;vec2 ts=1.5/vec2(textureSize(uAfShMap,0).xy);
 for(int k=0;k<8;k++){
  if((uStoreShOn&(1<<k))==0)continue;
  vec4 q=uStoreShVP[k]*vec4(p+n*.004,1.);vec3 u=q.xyz/q.w*.5+.5;
  if(u.x<=0.||u.x>=1.||u.y<=0.||u.y>=1.||u.z<=0.)continue;
  float z=min(u.z,.999),lit=0.;
  for(int j=0;j<4;j++)lit+=texture(uAfShMap,vec3(u.xy+(vec2(float(j&1),float(j>>1))-.5)*ts,float(24+k))).r>=z-.001?1.:0.;
  s*=mix(1.,lit*.25,uStoreShFade[k]);
 }
 return s;
}
