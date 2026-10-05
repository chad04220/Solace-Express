//! kKestrelFX27Material
//! Fleet-native painted slate, warm ivory identification panels, copper datum trim; no source PBR assets.
vec3 kfAlbedo(int id,vec3 p){
 if(id==1||id==2||id==3||id==5){
  vec3 base=vec3(.24,.32,.37);if(id==5)base*=.76;
  if(id==1&&p.z< -6.50)base=vec3(.095,.13,.16);
  if((id==2&&abs(p.x)>5.8)||(id==3&&p.y>2.25))base=vec3(.73,.70,.60);
  if((id==2&&abs(abs(p.x)-5.62)<.10)||(id==3&&p.y>2.08&&p.y<2.18))base=vec3(.69,.32,.13);
  float seams=max(1.-smoothstep(.003,.012,abs(mod(p.z+.55,1.1)-.55)),1.-smoothstep(.002,.009,abs(mod(abs(p.x)+.6,1.2)-.6)));
  return base*(1.-.13*seams);
 }
 if(id==6||id==61||id==13)return vec3(.025,.032,.035);
 if(id==8||id==119)return vec3(.45,.48,.48);
 if(id==110)return vec3(.60,.34,.17);
 if(id==113)return vec3(.12,.16,.17);
 if(id==114)return vec3(.27,.20,.14);
 if(id==115)return vec3(.42,.85,.62);
 if(id==116)return vec3(.67,.61,.43);
 if(id==117)return vec3(.025,.10,.13);
 if(id==17)return vec3(.12,.09,.07);
 if(id==21)return vec3(.10,.12,.13);
 return vec3(.4);
}
void kfGatlingMaterial(inout Mat m,int mid,vec3 p,vec3 n){
 if(mid==125){m.alb=vec3(.36,.39,.40);m.metal=.82;m.rough=.29;}
 if(mid==126){m.alb=vec3(.055,.075,.083);m.metal=.48;m.rough=.48;}
 if(mid==127){m.alb=vec3(.85,.39,.09);m.metal=.18;m.rough=.32;m.emit=vec3(1.,.34,.035)*(.15+3.*clamp(gWr[1].x,0.,1.));}
}
void kestrelFX27Material(inout Mat m,int mid,vec3 p,vec3 n,inout bool interior){
 m.alb=kfAlbedo(mid,p);m.metal=(mid==8||mid==110||mid==119)? .65:0.;m.rough=mid==6?.82:mid==8?.30:.47;m.nrm=vec3(0,0,1);
 interior=(mid>=113&&mid<=119)||mid==13||mid==61;
 if(mid==115)m.emit=m.alb*.9;
 if(mid==117){
  vec2 uv=abs(p.x)>.1?vec2((p.x-sign(p.x)*.30)/.21,(p.y-.78)/.16):vec2(p.x/.095,(p.y-.60)/.10);
  int page=p.x<-.1?1:p.x>.1?2:0;
  m.alb=vec3(.01,.02,.025);m.emit=n.z>.4?pageTex(page,uv,.001)*1.3:vec3(.01);m.rough=.15;
 }
 kfGearMaterial(m,mid,p,n);
 kfGatlingMaterial(m,mid,p,n);
}
