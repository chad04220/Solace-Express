// Included inside entity_mesh.cpp anonymous namespace; shared MB helpers are available.
void plate(MB& mb, vec3 r0, vec3 r1, vec3 t1, vec3 t0, vec3 off, int part) {
  vec3 A[4] = {r0, r1, t1, t0};
  vec3 c = (r0 + r1 + t0 + t1) * 0.25f;
  mb.quad(A[0] + off, A[1] + off, A[2] + off, A[3] + off, part, c - off * 4.f);
  mb.quad(A[0] - off, A[1] - off, A[2] - off, A[3] - off, part, c + off * 4.f);
  for (int i = 0; i < 4; i++) { int j = (i + 1) & 3; mb.quad(A[i] + off, A[j] + off, A[j] - off, A[i] - off, part, c); }
}
// half-elliptic arch (Quonset) along z: half width hw, height h; smooth shell plus flat end walls
void archShell(MB& mb, float hw, float h, float z0, float z1, int segs, int part, int endPart, bool ends) {
  for (int i = 0; i < segs; i++) {
    float a0 = PI * i / segs, a1 = PI * (i + 1) / segs;
    vec3 p0(cosf(a0) * hw, sinf(a0) * h, 0), p1(cosf(a1) * hw, sinf(a1) * h, 0);
    vec3 n0 = normalize(vec3(cosf(a0) / hw, sinf(a0) / h, 0)), n1 = normalize(vec3(cosf(a1) / hw, sinf(a1) / h, 0));
    mb.vert(vec3(p0.x, p0.y, z0), n0, part); mb.vert(vec3(p1.x, p1.y, z0), n1, part); mb.vert(vec3(p1.x, p1.y, z1), n1, part);
    mb.vert(vec3(p0.x, p0.y, z0), n0, part); mb.vert(vec3(p1.x, p1.y, z1), n1, part); mb.vert(vec3(p0.x, p0.y, z1), n0, part);
    if (ends) for (int e = 0; e < 2; e++) {
      float z = e ? z1 : z0;
      mb.tri(vec3(0, 0, z), vec3(p0.x, p0.y, z), vec3(p1.x, p1.y, z), endPart, vec3(0, h * 0.3f, (z0 + z1) * 0.5f));
    }
  }
}
// Arch end walls share the roof's actual polygonal profile. The front has a real door opening,
// avoiding depth competition between a large door quad and an opaque fan behind it.
void airportArchEnds(MB& mb,float hw,float h,float z0,float z1,int segments,float doorHalf,float doorTop) {
  std::vector<std::pair<float,float>> outline;
  for(int i=0;i<=segments;++i){float a=PI*i/segments;outline.push_back({cosf(a)*hw,std::max(0.f,sinf(a)*h)});}
  for(int i=0;i<segments;++i)mb.tri({0,0,z0},{outline[i].first,outline[i].second,z0},{outline[i+1].first,outline[i+1].second,z0},P_WALL,{0,h*.3f,(z0+z1)*.5f});
  std::sort(outline.begin(),outline.end());std::vector<float> cuts;for(auto p:outline)cuts.push_back(p.first);cuts.push_back(-doorHalf);cuts.push_back(doorHalf);
  std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
  auto roofY=[&](float x){size_t j=0;while(j+2<outline.size() && x>outline[j+1].first)++j;float t=clampf((x-outline[j].first)/(outline[j+1].first-outline[j].first),0,1);return lerpf(outline[j].second,outline[j+1].second,t);};
  for(size_t i=0;i+1<cuts.size();++i){float a=cuts[i],b=cuts[i+1],lo=std::abs((a+b)*.5f)<doorHalf?doorTop:0.f,ya=roofY(a),yb=roofY(b);vec3 A(a,lo,z1),B(b,lo,z1),C(b,yb,z1),D(a,ya,z1),inside(0,h*.3f,(z0+z1)*.5f);
    if(ya-lo<.00001f)mb.tri(A,B,C,P_WALL,inside);else if(yb-lo<.00001f)mb.tri(A,B,D,P_WALL,inside);else mb.quad(A,B,C,D,P_WALL,inside);
  }
}
// Close-range structural cues stay inside the established airport-building envelopes.
// Long facade details are strips, not hundreds of individual window boxes.
void airportAccents(MB& mb, int kind, int lod) {
  if (lod != 0 && lod != 3) return;
  switch(kind) {
    case EK_HANGAR:
      gableFascia(mb,-20,20,-16,16,10,13,true,.6f);
      for(float x : {-19.7f,19.7f}) mb.box({x-.12f,.2f,16.12f},{x+.12f,8.8f,16.25f},P_METAL,1|2|32,.8f);
      for(float x : {-13.06f,-6.53f,0.f,6.53f,13.06f}) mb.box({x-.04f,.3f,16.122f},{x+.04f,8.75f,16.13f},P_DARK,32);
      break;
    case EK_ARCH_HANGAR:
      for(float z : {-7.7f,7.7f}) archShell(mb,9.025f,7.495f,z-.065f,z+.065f,12,P_METAL,P_METAL,false);
      mb.box({-.07f,.15f,12.105f},{.07f,5.6f,12.12f},P_METAL,32);
      break;
    case EK_T_HANGAR:
      mb.box({-24.4f,4.62f,7.38f},{24.4f,4.8f,7.4f},P_TRIM,32,.8f);
      for(int i=0;i<4;++i) {float x=-18.f+12.f*i;
        mb.box({x-.04f,.2f,7.11f},{x+.04f,3.65f,7.12f},P_DARK,32);
        mb.box({x-.46f,4.05f,7.012f},{x+.46f,4.5f,7.025f},P_SIGN,32);
      }
      break;
    case EK_TERMINAL:
      mb.box({-59.f,5.68f,19.71f},{59.f,5.86f,19.92f},P_METAL,8|32,.82f);
      mb.box({-59.f,.3f,19.72f},{59.f,.55f,19.9f},P_TRIM,8|32,.8f);
      // Recessed entrance frames under the landside drop-off canopy.
      for(float x : {-18.f,-6.f,6.f,18.f}) {
        mb.box({x-.10f,.3f,-20.18f},{x+.10f,4.2f,-20.1f},P_METAL,1|2|16);
      }
      break;
    case EK_CTRL_TOWER:
      for(int i=0;i<8;++i) {
        float a=PI/8+i*2*PI/8;
        vec3 r(cosf(a),0,sinf(a)),side(-sinf(a),0,cosf(a));
        vec3 lo=r*4.305f+vec3(0,27,0),hi=r*4.705f+vec3(0,30.6f,0);
        mb.quad(lo-side*.055f,lo+side*.055f,hi+side*.055f,hi-side*.055f,P_METAL,{0,28,0});
        // A low perimeter rail and uprights make the cab's maintenance balcony legible.
        float b=PI/8+(i+1)*2*PI/8;vec3 p(cosf(a)*5.12f,28.02f,sinf(a)*5.12f),q(cosf(b)*5.12f,28.02f,sinf(b)*5.12f);
        mb.quad(p,q,q+vec3(0,.07f,0),p+vec3(0,.07f,0),P_METAL,{0,28,0});
        mb.quad(r*5.12f+vec3(0,27.05f,0)-side*.025f,r*5.12f+vec3(0,27.05f,0)+side*.025f,
          p+side*.025f,p-side*.025f,P_METAL,{0,28,0});
      }
      break;
    case EK_FBO:
      for(float x : {-6.f,-2.5f,1.f,4.5f}) mb.box({x-.045f,.5f,7.085f},{x+.045f,3.4f,7.11f},P_METAL,32);
      for(float x : {-3.75f,3.75f}) mb.box({x-.07f,.3f,8.75f},{x+.07f,3.2f,8.9f},P_METAL,1|2|16|32);
      break;
    default: break;
  }
}

// wheel: a short cylinder across x
void wheel(MB& mb, vec3 c, float r, float w, int segs, bool innerCap = true) {
  const size_t start=mb.o.size();
  mb.cyl(c-vec3(w*.5f,0,0),c+vec3(w*.5f,0,0),r,r,segs,P_WHEEL0+mb.wheelSlot++,innerCap || c.x>0,innerCap || c.x<0);
  // Wheel vertices alone use the auxiliary fields as radius + pivot Y/Z. Ent and EVert strides stay unchanged.
  for(size_t i=start;i<mb.o.size();++i) { mb.o[i].ao=r;mb.o[i].u=c.y;mb.o[i].v=c.z; }
}

// Parametric construction patches use smooth derivative normals. This produces curved bodywork
// and fitted glazing, rather than increasing the tessellation of a flat box.
template<class F>
void airportPatch(MB& mb,int nu,int nv,F P,int part,vec3 inside,float ao=1.f,bool glassUV=false) {
  auto V=[&](float u,float v){
    const float e=.0008f;vec3 p=P(u,v);
    vec3 du=P(std::min(1.f,u+e),v)-P(std::max(0.f,u-e),v);
    vec3 dv=P(u,std::min(1.f,v+e))-P(u,std::max(0.f,v-e));
    vec3 n=normalize(cross(normalize(du),normalize(dv)));if(dot(n,p-inside)<0)n=-n;
    mb.vert(p,n,part,ao,glassUV?1.f+u:0.f,glassUV?v:0.f);
  };
  for(int j=0;j<nv;++j)for(int i=0;i<nu;++i){float a=float(i)/nu,b=float(i+1)/nu,c=float(j)/nv,d=float(j+1)/nv;
    V(a,c);V(b,c);V(b,d);V(a,c);V(b,d);V(a,d);
  }
}

// A narrow band following an arbitrary construction curve, e.g. weather seals or roof trim.
template<class F>
void airportCurve(MB& mb,int n,F P,float radius,int part,int sides=5,float ao=1.f) {
  for(int i=0;i<n;++i)mb.cyl(P(float(i)/n),P(float(i+1)/n),radius,radius,sides,part,false,false,ao,ao);
}

void airportRoundedPanel(MB& mb,float x0,float x1,float y0,float y1,float z,float corner,int part,bool front=true) {
  vec3 c((x0+x1)*.5f,(y0+y1)*.5f,z),n(0,0,front?1.f:-1.f);
  vec3 p[16];int k=0;
  for(int q=0;q<4;++q){float cx=(q==0||q==3)?x1-corner:x0+corner,cy=q<2?y1-corner:y0+corner;
    for(int j=0;j<4;++j){float a=(q*.5f+float(j)/3*.5f)*PI;p[k++]={cx+corner*cosf(a),cy+corner*sinf(a),z};}}
  for(int i=0;i<16;++i){int j=(i+1)%16;mb.vert(c,n,part);mb.vert(p[i],n,part);mb.vert(p[j],n,part);}
}

// Wheel geometry remains one animated part slot: radius and Y/Z pivot keep their original ABI.
// Round shoulders/sidewalls surround an actual recessed alloy rim, spokes, hub and wheel bolts.
void airportRoadWheel(MB& mb,vec3 c,float r,float w,bool twoFaces=false,int segments=24) {
  const size_t start=mb.o.size();const int part=P_WHEEL0+mb.wheelSlot++;
  const float out=c.x<0?-1.f:1.f;
  const float xx[7]={-.5f,-.43f,-.25f,.25f,.43f,.49f,.50f};
  const float rr[7]={.66f,.88f,1.f,1.f,.94f,.78f,.62f};
  for(int j=0;j<6;++j){float slope=(rr[j+1]-rr[j])*r/std::max((xx[j+1]-xx[j])*w,.0001f);
    for(int i=0;i<segments;++i){float a=i*2*PI/segments,b=(i+1)*2*PI/segments;
      auto v=[&](int ring,float t){vec3 p=c+vec3(out*xx[ring]*w,sinf(t)*r*rr[ring],cosf(t)*r*rr[ring]);
        vec3 n=normalize(vec3(-out*slope,sinf(t),cosf(t)));mb.vert(p,n,part);};
      v(j,a);v(j+1,a);v(j+1,b);v(j,a);v(j+1,b);v(j,b);
    }
  }
  float face=c.x+out*w*.497f,well=c.x+out*w*.34f;
  // Recessed brake disc, never a painted spoke pattern.
  mb.cyl({well-out*.008f,c.y,c.z},{well,c.y,c.z},r*.47f,r*.47f,segments,part,true,twoFaces);
  auto ring=[&](float radius0,float radius1,float x0,float x1){for(int i=0;i<segments;++i){float a=i*2*PI/segments,b=(i+1)*2*PI/segments;
    vec3 A(x0,c.y+sinf(a)*radius0,c.z+cosf(a)*radius0),B(x0,c.y+sinf(b)*radius0,c.z+cosf(b)*radius0),C(x1,c.y+sinf(b)*radius1,c.z+cosf(b)*radius1),D(x1,c.y+sinf(a)*radius1,c.z+cosf(a)*radius1);
    mb.quad(A,B,C,D,part,c-vec3(out*r,0,0));}};
  ring(r*.62f,r*.54f,face,face-out*.012f);
  ring(r*.54f,r*.51f,face-out*.012f,well);
  // Five forged spokes have width, taper and a shallow dished profile.
  for(int i=0;i<5;++i){float a=i*2*PI/5+.12f;vec3 rad(0,sinf(a),cosf(a)),tan(0,cosf(a),-sinf(a));
    vec3 root=c+vec3(out*w*.505f,0,0)+rad*(r*.14f),tip=c+vec3(out*w*.475f,0,0)+rad*(r*.535f);
    vec3 a0=root-tan*(r*.072f),b0=root+tan*(r*.072f),a1=tip-tan*(r*.11f),b1=tip+tan*(r*.11f);
    mb.quad(a0,b0,b1,a1,part,c-vec3(out*r,0,0));
    mb.quad(a0,a1,a1-vec3(out*.012f,0,0),a0-vec3(out*.012f,0,0),part,c);
    mb.quad(b0,b1,b1-vec3(out*.012f,0,0),b0-vec3(out*.012f,0,0),part,c);
    vec3 bolt=c+vec3(out*w*.505f,0,0)+rad*(r*.225f);
    mb.cyl(bolt-vec3(out*.005f,0,0),bolt,r*.026f,r*.026f,5,part,true,false);
  }
  mb.cyl({face-out*.004f,c.y,c.z},{face+out*.001f,c.y,c.z},r*.15f,r*.15f,12,part,true,false);
  for(size_t i=start;i<mb.o.size();++i){mb.o[i].ao=r;mb.o[i].u=c.y;mb.o[i].v=c.z;}
}

void airportSedanDetailed(MB& mb,int lod) {
  const bool hero=lod==3, near=lod==0;
  const int arches=hero?16:near?6:3, crossSteps=hero?6:near?4:2;
  // Real body stations: rounded bumpers, wider wheel shoulders, narrowed waist and a crowned hood.
  const float zs[13]={-2.185f,-2.08f,-1.85f,-1.45f,-1.0f,-.55f,0,.55f,1.0f,1.45f,1.85f,2.025f,2.12f};
  const float ws[13]={.66f,.77f,.835f,.86f,.838f,.83f,.831f,.841f,.852f,.86f,.827f,.775f,.73f};
  const float hs[13]={.77f,.80f,.83f,.895f,.916f,.918f,.92f,.92f,.894f,.867f,.816f,.793f,.777f};
  auto value=[&](const float* a,float z){int i=0;while(i<11 && z>zs[i+1])++i;float h=zs[i+1]-zs[i],t=clampf((z-zs[i])/h,0,1);
    auto slope=[&](int k){if(k==0)return (a[1]-a[0])/(zs[1]-zs[0]);if(k==12)return (a[12]-a[11])/(zs[12]-zs[11]);float dl=(a[k]-a[k-1])/(zs[k]-zs[k-1]),dr=(a[k+1]-a[k])/(zs[k+1]-zs[k]);return dl*dr<=0?0.f:2*dl*dr/(dl+dr);};
    float m0=slope(i),m1=slope(i+1);
    return (2*t*t*t-3*t*t+1)*a[i]+(t*t*t-2*t*t+t)*h*m0+(-2*t*t*t+3*t*t)*a[i+1]+(t*t*t-t*t)*h*m1;
  };
  auto lower=[](float z){float y=.285f;for(float wz:{-1.35f,1.35f}){float d=z-wz;if(fabsf(d)<.365f)y=std::max(y,.31f+sqrtf(.365f*.365f-d*d));}return y;};
  const float wx[7]={0,.45f,.80f,.96f,1.f,.993f,.97f};
  auto shell=[&](float z,float u,int side){float f=u*6;int j=std::min(5,int(f));float t=f-j,w=value(ws,z),h=value(hs,z),lo=lower(z);
    float ys[7]={h,h-.008f,h-.044f,h-.105f,lerpf(h-.105f,lo,.40f),lerpf(h-.105f,lo,.78f),lo};
    auto curve=[&](const float* a,bool flatCentre){auto slope=[&](int k){if(k==0)return flatCentre?0.f:a[1]-a[0];if(k==6)return a[6]-a[5];float dl=a[k]-a[k-1],dr=a[k+1]-a[k];return dl*dr<=0?0.f:2*dl*dr/(dl+dr);};float m0=slope(j),m1=slope(j+1);
      return (2*t*t*t-3*t*t+1)*a[j]+(t*t*t-2*t*t+t)*m0+(-2*t*t*t+3*t*t)*a[j+1]+(t*t*t-t*t)*m1;};
    return vec3(side*w*curve(wx,false),curve(ys,true),z);};
  std::vector<float> stations;for(int i=0;i<13;i+=(!hero&&!near)?2:1)stations.push_back(zs[i]);
  for(float wz:{-1.35f,1.35f})for(int i=0;i<=arches;++i)stations.push_back(wz+.365f*cosf(i*PI/arches));
  std::sort(stations.begin(),stations.end());stations.erase(std::unique(stations.begin(),stations.end(),[](float a,float b){return fabsf(a-b)<.00001f;}),stations.end());
  for(int side:{-1,1})for(size_t i=0;i+1<stations.size();++i){float z0=stations[i],z1=stations[i+1];
    airportPatch(mb,crossSteps,1,[&](float u,float v){return shell(lerpf(z0,z1,v),u,side);},P_PAINT,{0,.55f,0});
  }
  // Closed painted underside of the nose shares the side-shell station edges too.
  for(size_t i=0;i+1<stations.size();++i)if(stations[i]>=1.8499f)mb.quad(shell(stations[i],1,-1),shell(stations[i],1,1),shell(stations[i+1],1,1),shell(stations[i+1],1,-1),P_PAINT,{0,.48f,2.f});
  for(float z:{zs[0]}){std::vector<vec3>ring;for(int j=6;j>=0;--j)ring.push_back(shell(z,j/6.f,-1));for(int j=1;j<=6;++j)ring.push_back(shell(z,j/6.f,1));
    vec3 centre(0,.48f,z);for(size_t j=0;j<ring.size();++j)mb.tri(centre,ring[j],ring[(j+1)%ring.size()],P_PAINT,{0,.5f,0});}
  mb.box({-.65f,.23f,-1.95f},{.65f,.285f,1.95f},P_DARK,4|16|32,.65f);
  // Open arches are real holes in the side shell. A lip and recessed liner reveal depth behind each tire.
  for(int side:{-1,1})for(float wz:{-1.35f,1.35f}){
    airportPatch(mb,arches,1,[&](float u,float v){float a=u*PI,z=wz+.365f*cosf(a),y=.31f+.365f*sinf(a);float x=value(ws,z)*.97f;
      return vec3(side*(x-.13f*v),y,z);},P_DARK,{0,.31f,wz},.6f);
    if(hero) airportCurve(mb,16,[&](float u){float a=u*PI,z=wz+.365f*cosf(a);return vec3(side*value(ws,z)*.973f,.31f+.365f*sinf(a),z);},.010f,P_PAINT,5);
  }
  auto roof=[&](float u,float v){float x=u*2-1,z=lerpf(-.74f,.56f,v),width=.635f+.04f*sinf(PI*v),h=1.429f+.031f*sinf(PI*v);
    return vec3(x*width,h-.077f*x*x,z);};
  airportPatch(mb,hero?12:near?4:2,hero?8:near?3:2,roof,P_PAINT,{0,.85f,0});
  auto front=[&](float u,float v){float x=u*2-1;vec3 a(x*.795f,.894f-.035f*x*x,1.19f-.015f*x*x),b=roof(u,1);
    return a*(1-v)+b*v+vec3(0,.012f*sinf(PI*v)*(1-x*x),.014f*sinf(PI*v)*(1-x*x));};
  auto back=[&](float u,float v){float x=u*2-1;vec3 a(x*.793f,.907f-.03f*x*x,-1.34f+.018f*x*x),b=roof(u,0);
    return a*(1-v)+b*v+vec3(0,.01f*sinf(PI*v)*(1-x*x),-.013f*sinf(PI*v)*(1-x*x));};
  for(int end=0;end<2;++end){auto P=[&](float u,float v){return end?front(u,v):back(u,v);};
    airportPatch(mb,hero?10:3,hero?7:2,P,P_PAINT,{0,.8f,0});
    if(hero) airportPatch(mb,10,7,[&](float u,float v){vec3 p=P(lerpf(.035f,.965f,u),lerpf(.065f,.935f,v));p.z+=end?.0025f:-.0025f;return p;},P_DARK,{0,.8f,0});
    airportPatch(mb,hero?10:3,hero?7:2,[&](float u,float v){vec3 p=P(lerpf(.047f,.953f,u),lerpf(.082f,.918f,v));p.z+=end?.0045f:-.0045f;return p;},P_GLASS,{0,.8f,0},1,true);
  }
  for(int side:{-1,1}){
    auto flank=[&](float u,float v){vec3 a=vec3(side*lerpf(.793f,.795f,u),lerpf(.877f,.859f,u),lerpf(-1.34f,1.175f,u));vec3 b=roof(side>0?1.f:0.f,u);return a*(1-v)+b*v;};
    airportPatch(mb,hero?12:3,hero?5:2,flank,P_PAINT,{0,.8f,0});
    for(int pane=0;pane<2;++pane){float u0=pane?.535f:.035f,u1=pane?.965f:.495f;
      if(hero) airportPatch(mb,5,4,[&](float u,float v){vec3 p=flank(lerpf(u0,u1,u),lerpf(.10f,.915f,v));p.x+=side*.003f;return p;},P_DARK,{0,.8f,0});
      airportPatch(mb,hero?5:2,hero?4:2,[&](float u,float v){vec3 p=flank(lerpf(u0+.012f,u1-.012f,u),lerpf(.125f,.892f,v));p.x+=side*.005f;return p;},P_GLASS,{0,.8f,0},1,true);
    }
    if(hero){
    airportCurve(mb,12,[&](float u){vec3 p=flank(u,.03f);p.x+=side*.004f;return p;},.008f,P_METAL,4);
    // Door shut-lines follow the curved surface, not a rectangular decal hanging in space.
    for(float z:{-.20f,.92f})airportCurve(mb,5,[&](float u){float y=lerpf(.38f,value(hs,z)-.045f,u);return vec3(side*(value(ws,z)+.002f),y,z);},.004f,P_DARK,3);
    for(float z:{-.70f,.58f}){float x=side*(value(ws,z)+.004f);mb.cyl({x,.795f,z-.075f},{x,.795f,z+.075f},.013f,.013f,6,P_METAL,true,true);}
    vec3 mirror(side*.81f,1.035f,.83f);
    blob(mb,1,[&](vec3 d){return mirror+vec3(d.x*.085f,d.y*.046f,d.z*.115f);},[](vec3,vec3){return .95f;},P_PAINT);
    mb.quad(mirror+vec3(-.06f,-.026f,-.095f),mirror+vec3(.06f,-.026f,-.095f),mirror+vec3(.055f,.026f,-.095f),mirror+vec3(-.055f,.026f,-.095f),P_GLASS,mirror);
    }
  }
  auto sedanPanel=[&](MB& out,float x0,float x1,float y0,float y1,float z,float corner,int part,bool front=true){
    if(hero||near)airportRoundedPanel(out,x0,x1,y0,y1,z,corner,part,front);
    else out.quad({x0,y0,z},{x1,y0,z},{x1,y1,z},{x0,y1,z},part,{0,(y0+y1)*.5f,z+(front?-1.f:1.f)});
  };
  // The bumper is a curved skin with actual apertures, not a flat face under white stickers.
  const float xs[]={-.67f,-.66f,-.33f,-.29f,.29f,.33f,.66f,.67f};
  const float ys[]={.30f,.33f,.445f,.515f,.65f,.66f};
  auto fascia=[](float x,float y){float z=2.192f-.01f*(x/.67f)*(x/.67f)-.006f*((y-.48f)/.18f)*((y-.48f)/.18f);return vec3(x,y,z);};
  // Join two explicit boundary polylines. Subdivided collar edges stay on both original surfaces.
  const int frontU=hero?2:1,frontV=hero?2:1;
  std::vector<float> frontX{xs[0]},frontY{ys[0]};
  for(int i=0;i<7;++i)for(int j=1;j<=frontU;++j)frontX.push_back(lerpf(xs[i],xs[i+1],float(j)/frontU));
  for(int i=0;i<5;++i)for(int j=1;j<=frontV;++j)frontY.push_back(lerpf(ys[i],ys[i+1],float(j)/frontV));
  std::vector<vec3> perimeter,previousPerimeter,frontPerimeter;
  for(int j=crossSteps;j>=0;--j){perimeter.push_back(shell(zs[12],float(j)/crossSteps,-1));previousPerimeter.push_back(shell(zs[12]-.0008f,float(j)/crossSteps,-1));}
  for(int j=1;j<=crossSteps;++j){perimeter.push_back(shell(zs[12],float(j)/crossSteps,1));previousPerimeter.push_back(shell(zs[12]-.0008f,float(j)/crossSteps,1));}
  for(float x:frontX)frontPerimeter.push_back(fascia(x,ys[0]));
  for(size_t j=1;j<frontY.size();++j)frontPerimeter.push_back(fascia(xs[7],frontY[j]));
  for(int j=int(frontX.size())-2;j>=0;--j)frontPerimeter.push_back(fascia(frontX[j],ys[5]));
  for(int j=int(frontY.size())-2;j>0;--j)frontPerimeter.push_back(fascia(xs[0],frontY[j]));
  std::vector<float> angles;vec3 fc(0,.48f,zs[12]);
  for(vec3 p:perimeter)angles.push_back(atan2f(p.y-fc.y,p.x));
  for(vec3 p:frontPerimeter)angles.push_back(atan2f(p.y-fc.y,p.x));
  std::sort(angles.begin(),angles.end());angles.erase(std::unique(angles.begin(),angles.end(),[](float a,float b){return fabsf(a-b)<.00001f;}),angles.end());angles.push_back(angles.front()+2*PI);
  auto rim=[&](float a,bool outer,vec3* previous=nullptr){float dx=cosf(a),dy=sinf(a),best=1e6f;vec3 result=fc;const auto& polygon=outer?perimeter:frontPerimeter;
    for(size_t i=0;i<polygon.size();++i){vec3 A=polygon[i]-fc,B=polygon[(i+1)%polygon.size()]-fc,E=B-A;float det=dx*E.y-dy*E.x;if(fabsf(det)<.000001f)continue;
      float ray=(A.x*E.y-A.y*E.x)/det,edge=(A.x*dy-A.y*dx)/det;if(ray>0 && ray<best && edge>=-.00001f && edge<=1.00001f){best=ray;result=polygon[i]+E*clampf(edge,0,1);if(outer && previous)*previous=previousPerimeter[i]+(previousPerimeter[(i+1)%polygon.size()]-previousPerimeter[i])*clampf(edge,0,1);}}
    return result;
  };
  // Cubic collar controls match the body longitudinal tangent and the front-panel tangent.
  // Angle derivatives are global, so adjacent collar sectors share smooth normals rather than faceted fans.
  auto collar=[&](float a,float v){vec3 previous,outer=rim(a,true,&previous),inner=rim(a,false),delta=inner-outer;float arm=length(delta)*.45f;
    vec3 t0=normalize(outer-previous),t1=normalize(vec3(delta.x,delta.y,0));
    t1.z=(-.02f*inner.x/(.67f*.67f))*t1.x+(-.012f*(inner.y-.48f)/(.18f*.18f))*t1.y;t1=normalize(t1);
    vec3 c0=outer+t0*arm,c1=inner-t1*arm;float w=1-v;return outer*(w*w*w)+c0*(3*w*w*v)+c1*(3*w*v*v)+inner*(v*v*v);
  };
  auto collarVertex=[&](float a,float v){vec3 p=collar(a,v);const float e=.0003f;
    vec3 du=collar(a+e,v)-collar(a-e,v),dv=collar(a,std::min(1.f,v+e))-collar(a,std::max(0.f,v-e));
    vec3 n=normalize(cross(normalize(du),normalize(dv)));if(dot(n,p-vec3(0,.48f,1.9f))<0)n=-n;mb.vert(p,n,P_PAINT);
  };
  const int collarSteps=hero?5:near?2:1;
  for(size_t i=0;i+1<angles.size();++i)for(int j=0;j<collarSteps;++j){float a=angles[i],b=angles[i+1],v=float(j)/collarSteps,w=float(j+1)/collarSteps;
    collarVertex(a,v);collarVertex(b,v);collarVertex(b,w);collarVertex(a,v);collarVertex(b,w);collarVertex(a,w);
  }
  auto pocket=[&](float a,float b,float y0,float y1,float depth){std::vector<vec3> q;
    for(float x:frontX)if(x>=a-.00001f && x<=b+.00001f)q.push_back(fascia(x,y0));
    for(float y:frontY)if(y>y0+.00001f && y<=y1+.00001f)q.push_back(fascia(b,y));
    for(int j=int(frontX.size())-1;j>=0;--j)if(frontX[j]>=a-.00001f && frontX[j]<b-.00001f)q.push_back(fascia(frontX[j],y1));
    for(int j=int(frontY.size())-1;j>=0;--j)if(frontY[j]>y0+.00001f && frontY[j]<y1-.00001f)q.push_back(fascia(a,frontY[j]));
    vec3 centre((a+b)*.5f,(y0+y1)*.5f,(2.185f+depth)*.5f);
    mb.quad({a,y0,depth},{b,y0,depth},{b,y1,depth},{a,y1,depth},P_DARK,centre-vec3(0,0,1));
    for(size_t k=0;k<q.size();++k){vec3 p0=q[k],p1=q[(k+1)%q.size()],r0(p0.x,p0.y,depth),r1(p1.x,p1.y,depth),face=(p0+p1+r0+r1)*.25f;mb.quad(p0,p1,r1,r0,P_DARK,face*2-centre);}
  };
  for(int j=0;j<5;++j)for(int i=0;i<7;++i){bool lamp=(i==1||i==5)&&j==3,upper=i==3&&j==3,lower=(i>=2&&i<=4)&&j==1;if(lamp||upper||lower)continue;
    airportPatch(mb,frontU,frontV,[&](float u,float v){return fascia(lerpf(xs[i],xs[i+1],u),lerpf(ys[j],ys[j+1],v));},P_PAINT,{0,.48f,0});}
  for(int side:{-1,1}){float a=side<0?-.66f:.33f,b=side<0?-.33f:.66f;
    pocket(a,b,.515f,.65f,2.105f);
    if(hero){
      // Twin projector optics sit fully inside the closed, fitted housing.
      for(int lamp=0;lamp<2;++lamp){float cx=lerpf(a,b,lamp?.73f:.29f),cy=.582f,r=lamp?.047f:.055f;
        airportPatch(mb,16,3,[&](float u,float v){float t=u*2*PI,rr=lerpf(r,r*.38f,v);return vec3(cx+cosf(t)*rr,cy+sinf(t)*rr,lerpf(2.152f,2.113f,v));},P_METAL,{cx,cy,2.0f});
        mb.cyl({cx,cy,2.111f},{cx,cy,2.143f},r*.40f,r*.34f,12,P_GLASS,true,false);
        airportCurve(mb,16,[&](float u){float t=u*2*PI;return vec3(cx+cosf(t)*r,cy+sinf(t)*r,2.153f);},.0035f,P_METAL,4);
      }
      // Slim outer lens edge follows the recess, leaving actual optics visible within it.
      mb.box({a+.018f,.622f,2.146f},{b-.018f,.630f,2.151f},P_METAL,32);
    }else sedanPanel(mb,a+.03f,b-.03f,.545f,.62f,2.125f,.018f,P_METAL);
    float ra=side<0?-.60f:.30f,rb=side<0?-.30f:.60f;
    sedanPanel(mb,ra,rb,.535f,.66f,-2.191f,.029f,P_DARK,false);
    sedanPanel(mb,ra+.012f,rb-.012f,.547f,.648f,-2.195f,.023f,P_STRIPE,false);
  }
  pocket(-.29f,.29f,.515f,.65f,2.13f);
  pocket(-.33f,.33f,.33f,.445f,2.125f);
  if(hero){for(int i=0;i<3;++i){float y=.544f+i*.038f;mb.box({-.255f,y,2.151f},{.255f,y+.008f,2.157f},P_METAL,32);}
    for(float x:{-.27f,-.135f,0.f,.135f,.27f})mb.box({x-.007f,.345f,2.139f},{x+.007f,.43f,2.147f},P_DARK,32);
    sedanPanel(mb,-.20f,.20f,.45f,.508f,2.198f,.009f,P_TRIM);
    sedanPanel(mb,-.20f,.20f,.408f,.50f,-2.198f,.009f,P_TRIM,false);
  }
  for(int i=0;i<4;++i){vec3 c((i&1)?.80f:-.80f,.31f,(i&2)?1.35f:-1.35f);
    if(hero) airportRoadWheel(mb,c,.31f,.19f,false,24);
    else wheel(mb,c,.31f,.19f,near?12:8,false);
  }

}

// Beveled service enclosures and vehicle cabins have true rounded edges, not normal-map-only bevels.
template<class F>
void airportRoundBoxWarp(MB& mb,vec3 lo,vec3 hi,float radius,int divisions,int part,F warp) {
  vec3 c=(lo+hi)*.5f,h=(hi-lo)*.5f;radius=std::min(radius,std::min(h.x,std::min(h.y,h.z))*.8f);
  for(int axis=0;axis<3;++axis)for(int sign:{-1,1})airportPatch(mb,divisions,divisions,[&](float u,float v){
    auto edgeSample=[&](float t,float extent){
      // Spend subdivisions on the physical corner radius instead of unused coplanar face interiors.
      float a=fabsf(t*2-1),sgn=t<.5f?-1.f:1.f;
      float q=a<.5f?(a*2)*(extent-radius):(extent-radius)+(a-.5f)*2*radius;
      return sgn*q;
    };
    vec3 p;if(axis==0)p={sign*h.x,edgeSample(u,h.y),edgeSample(v,h.z)};else if(axis==1)p={edgeSample(u,h.x),sign*h.y,edgeSample(v,h.z)};else p={edgeSample(u,h.x),edgeSample(v,h.y),sign*h.z};
    vec3 q(clampf(p.x,-h.x+radius,h.x-radius),clampf(p.y,-h.y+radius,h.y-radius),clampf(p.z,-h.z+radius,h.z-radius));
    return warp(c+q+normalize(p-q)*radius);
  },part,c);
}
void airportRoundBox(MB& mb,vec3 lo,vec3 hi,float radius,int divisions,int part){airportRoundBoxWarp(mb,lo,hi,radius,divisions,part,[](vec3 p){return p;});}

// Shared formed-head silhouette across hero/near/mid; only sampling density changes with distance.
void airportTankerShell(MB& mb,int radial,int axial) {
  airportPatch(mb,radial,1,[&](float u,float v){float a=u*2*PI;return vec3(cosf(a)*1.1f,1.95f+sinf(a)*1.1f,lerpf(-3.5f,1.65f,v));},P_METAL,{0,1.95f,0});
  for(int end=0;end<2;++end)airportPatch(mb,radial,axial,[&](float u,float v){float a=u*2*PI,t=lerpf(.0181828f,PI*.5f,v),r=1.1f*sinf(t);return vec3(cosf(a)*r,1.95f+sinf(a)*r,end?1.65f+.75f*cosf(t):-3.5f-.8f*cosf(t));},P_METAL,{0,1.95f,end?1.65f:-3.5f});
  // The small nonzero polar ring avoids degenerate loft triangles; matched end discs seal its pinholes.
  const float tip=.0181828f,r=1.1f*sinf(tip);
  for(int end=0;end<2;++end){float z=end?1.65f+.75f*cosf(tip):-3.5f-.8f*cosf(tip);vec3 centre(0,1.95f,z),inside(0,1.95f,end?1.65f:-3.5f);
    for(int i=0;i<radial;++i){float a=i*2*PI/radial,b=(i+1)*2*PI/radial;mb.tri(centre,{cosf(a)*r,1.95f+sinf(a)*r,z},{cosf(b)*r,1.95f+sinf(b)*r,z},P_METAL,inside);}
  }
}

void airportTruckDetailed(MB& mb) {
  airportRoundBox(mb,{-1.14f,.45f,-4.4f},{1.14f,.75f,4.40f},.06f,2,P_DARK);
  // High mounted cab is shaped around the front axle; the rounded fender actually clears the tire.
  auto cabWarp=[](vec3 p){float t=clampf((p.y-1.72f)/1.18f,0,1);p.x*=1-.08f*t;if(p.z>3.6f)p.z-=.18f*t;return p;};
  airportRoundBoxWarp(mb,{-1.24f,1.0f,2.62f},{1.24f,2.9f,4.48f},.12f,7,P_PAINT,cabWarp);
  airportRoundBox(mb,{-1.2f,.65f,4.24f},{1.2f,1.03f,4.50f},.065f,3,P_PAINT);
  for(int side:{-1,1}){
    airportPatch(mb,18,2,[&](float u,float v){float a=u*PI,z=3.4f+.53f*cosf(a),y=.48f+.53f*sinf(a);return vec3(side*lerpf(1.08f,1.24f,v),y,z);},P_PAINT,{0,.48f,3.4f});
    airportRoundBox(mb,{side<0?-1.24f:1.02f,.54f,2.77f},{side<0?-1.02f:1.24f,.66f,3.1f},.025f,2,P_METAL);
    airportRoundBox(mb,{side<0?-1.24f:1.06f,.76f,2.75f},{side<0?-1.06f:1.24f,.89f,3.11f},.025f,2,P_METAL);
    auto window=[&](float u,float v){float y=lerpf(1.92f,2.72f,v),z=lerpf(2.91f,4.17f-.14f*v,u);float t=clampf((y-1.72f)/1.18f,0,1);return vec3(side*(1.24f*(1-.08f*t)+.002f),y,z);};
    airportPatch(mb,7,5,window,P_DARK,{0,2,3.5f});
    airportPatch(mb,7,5,[&](float u,float v){vec3 p=window(lerpf(.045f,.955f,u),lerpf(.05f,.95f,v));p.x+=side*.003f;return p;},P_GLASS,{0,2,3.5f},1,true);
    airportCurve(mb,5,[&](float u){return cabWarp({side*1.244f,lerpf(1.06f,1.83f,u),2.9f});},.006f,P_DARK,3);
    mb.cyl(cabWarp({side*1.228f,1.80f,2.95f}),cabWarp({side*1.228f,1.80f,3.13f}),.022f,.022f,6,P_METAL,true,true);
    // Mirrors fit within the established maximum truck width.
    airportRoundBox(mb,{side<0?-1.245f:1.10f,2.16f,4.08f},{side<0?-1.10f:1.245f,2.48f,4.21f},.035f,3,P_DARK);
    mb.box({side<0?-1.237f:1.113f,2.19f,4.072f},{side<0?-1.113f:1.237f,2.45f,4.08f},P_GLASS,16);
    mb.cyl({side*.93f,.8f,2.50f},{side*.93f,2.82f,2.50f},.05f,.05f,10,P_METAL,false,true);
  }
  auto windscreen=[&](float u,float v){float x=(u*2-1)*1.02f,y=lerpf(1.93f,2.70f,v);return vec3(x,y,4.48f-.18f*clampf((y-1.72f)/1.18f,0,1)+.003f+.014f*(1-x*x));};
  airportPatch(mb,14,7,windscreen,P_DARK,{0,2,3.4f});
  airportPatch(mb,14,7,[&](float u,float v){vec3 p=windscreen(lerpf(.025f,.975f,u),lerpf(.035f,.965f,v));p.z+=.003f;return p;},P_GLASS,{0,2,3.4f},1,true);
  for(int side:{-1,1})airportCurve(mb,3,[&](float t){vec3 p=windscreen(lerpf(side<0?.2f:.55f,side<0?.40f:.80f,t),lerpf(.12f,.26f,t));p.z+=.006f;return p;},.014f,P_DARK,4);
  airportRoundedPanel(mb,-.96f,.96f,1.04f,1.62f,4.486f,.065f,P_DARK);
  for(int i=0;i<7;++i){float y=1.09f+i*.075f;mb.box({-.86f,y,4.487f},{.86f,y+.015f,4.491f},P_METAL,32);}
  for(int side:{-1,1}){float a=side<0?-1.14f:.73f,b=side<0?-.73f:1.14f;
    airportRoundedPanel(mb,a,b,.79f,.97f,4.506f,.04f,P_DARK);
    for(int lamp=0;lamp<2;++lamp){float x=lerpf(a,b,lamp?.73f:.28f);airportPatch(mb,16,3,[&](float u,float v){float t=u*2*PI,r=lerpf(.066f,.029f,v);return vec3(x+cosf(t)*r,.88f+sinf(t)*r,lerpf(4.535f,4.508f,v));},P_METAL,{x,.88f,4.40f});
      mb.cyl({x,.88f,4.508f},{x,.88f,4.531f},.029f,.025f,12,P_GLASS,true,false);}
  }
  airportRoundedPanel(mb,-.30f,.30f,.69f,.84f,4.513f,.012f,P_TRIM);
  airportTankerShell(mb,32,11);
  for(float z:{-2.9f,.9f})mb.cyl({0,1.95f,z-.045f},{0,1.95f,z+.045f},1.104f,1.104f,32,P_DARK,false,false);
  // Chassis plumbing, support saddles and the rear manifold are functional-size details.
  for(float z:{-2.9f,.9f})airportRoundBox(mb,{-.9f,.76f,z-.15f},{.9f,1.06f,z+.15f},.055f,2,P_METAL);
  for(int side:{-1,1})mb.cyl({side*.86f,.94f,-3.8f},{side*.86f,.94f,1.7f},.055f,.055f,8,P_METAL,true,true);
  mb.cyl({-.42f,1.45f,-4.28f},{.42f,1.45f,-4.28f},.07f,.07f,10,P_METAL,true,true);
  for(float x:{-.28f,.28f})mb.cyl({x,1.45f,-4.28f},{x,1.20f,-4.28f},.047f,.047f,8,P_DARK,true,true);
  mb.box({-.32f,3.04f,-.58f},{.32f,3.05f,.04f},P_DARK,8);
  for(int i=0;i<6;++i)airportRoadWheel(mb,{(i&1)?1.f:-1.f,.48f,i<2?3.4f:i<4?-2.4f:-3.6f},.48f,.32f,false,24);
}

// Fitted pane plus metal frame, with pane coordinates for the glass material.
void airportPane(MB& mb,vec3 a,vec3 b,vec3 c,vec3 d,float frame=.045f) {
  vec3 centre=(a+b+c+d)*.25f, normal=normalize(cross(b-a,d-a));
  if(dot(normal,centre)<0)normal=-normal;
  airportPatch(mb,2,2,[&](float u,float v){return (a*(1-u)+b*u)*(1-v)+(d*(1-u)+c*u)*v;},P_GLASS,centre-normal,1,true);
  for(auto edge : {std::pair<vec3,vec3>(a,b),{b,c},{c,d},{d,a}})mb.cyl(edge.first,edge.second,frame,frame,6,P_METAL,false,false);
}
void airportHeroAccents(MB& mb,int kind) {
  switch(kind){
    case EK_HANGAR:
      // Raised standing seams continue down each roof pitch; the sliding leaves have guides and rollers.
      for(int side:{-1,1})for(int i=1;i<17;++i){float x=side*i*1.18f,y=13.f-fabsf(x)*.15f;
        mb.cyl({x,y+.025f,-16.5f},{x,y+.025f,16.5f},.026f,.026f,5,P_METAL,false,false);}
      for(int i=0;i<12;++i){float x=-18.9f+i*3.43f;mb.box({x-.035f,.35f,16.14f},{x+.035f,8.62f,16.24f},P_METAL,1|2|32);
        mb.cyl({x,8.76f,16.25f},{x,8.76f,16.43f},.16f,.16f,12,P_DARK,true,true);}
      for(int side:{-1,1})for(int i=0;i<8;++i){float z=-13.4f+i*3.75f;float x=side*20.012f;
        airportPane(mb,{x,7.8f,z},{x,7.8f,z+2.8f},{x,9.15f,z+2.8f},{x,9.15f,z},.038f);}
      mb.box({-19.5f,.22f,16.27f},{19.5f,.29f,16.48f},P_METAL,8|32);
      break;
    case EK_ARCH_HANGAR:
      for(float z:{-11.7f,-6.f,0.f,6.f,11.7f})airportCurve(mb,32,[&](float t){float a=PI*t;return vec3(cosf(a)*9.025f,sinf(a)*7.465f,z);},.028f,P_METAL,5);
      for(float x:{-4.95f,-2.5f,0.f,2.5f,4.95f})mb.box({x-.025f,.20f,12.125f},{x+.025f,5.5f,12.18f},P_METAL,1|2|32);
      for(float y:{1.4f,2.8f,4.2f})mb.box({-4.97f,y,12.122f},{4.97f,y+.045f,12.16f},P_METAL,32);
      airportPane(mb,{6.08f,2.48f,12.10f},{7.32f,2.48f,12.10f},{7.32f,3.32f,12.10f},{6.08f,3.32f,12.10f},.026f);
      break;
    case EK_T_HANGAR:
      for(int i=0;i<4;++i){float x=-18.f+i*12;
        for(int j=1;j<7;++j)mb.box({x-5.45f,.18f+j*.49f,7.125f},{x+5.45f,.20f+j*.49f,7.15f},P_METAL,32);
        for(float dx:{-5.52f,5.52f})mb.box({x+dx-.07f,.2f,7.15f},{x+dx+.07f,3.69f,7.26f},P_METAL,1|2|32);
        mb.box({x-.27f,.7f,7.14f},{x+.27f,.76f,7.20f},P_DARK,32);}
      for(int i=0;i<32;++i){float x=-23.8f+i*1.53f;mb.cyl({x,4.013f,-7.35f},{x,4.78f,7.32f},.016f,.016f,4,P_METAL,false,false);}
      break;
    case EK_TERMINAL:
      for(int row=0;row<2;++row)for(int i=0;i<36;++i){float x=-58.95f+i*3.275f,y=row?5.99f:.63f;
        airportPane(mb,{x,y,19.73f},{x+3.16f,y,19.73f},{x+3.16f,y+5.17f,19.73f},{x,y+5.17f,19.73f},.036f);}
      for(int i=0;i<12;++i){float x=-24.85f+i*4.14f;airportPane(mb,{x,.4f,-20.12f},{x+4.03f,.4f,-20.12f},{x+4.03f,4.05f,-20.12f},{x,4.05f,-20.12f},.043f);}
      for(int i=0;i<8;++i){float x=-23.8f+i*6.8f;mb.box({x-.07f,4.43f,-26.8f},{x+.07f,4.68f,-20.2f},P_METAL,1|2|4);}
      for(int i=0;i<4;++i){float x=-46.f+i*26;
        for(float dx:{-.95f,.95f}){mb.cyl({x+dx,14.302f,-10.f},{x+dx,14.37f,-10.f},.70f,.70f,20,P_DARK,true,false);
          for(int b=0;b<5;++b){float a=b*2*PI/5;vec3 c(x+dx,14.38f,-10.f),r(cosf(a)*.59f,0,sinf(a)*.59f),t(-sinf(a)*.09f,0,cosf(a)*.09f);mb.quad(c-t,c+t,c+r+t,c+r-t,P_METAL,c-vec3(0,1,0));}}}
      break;
    case EK_CTRL_TOWER:
      for(int i=0;i<8;++i){float a=PI/8+i*2*PI/8,b=a+2*PI/8;vec3 A(cosf(a),0,sinf(a)),B(cosf(b),0,sinf(b));
        for(int j=0;j<2;++j){float u0=.02f+j*.5f,u1=.48f+j*.5f;vec3 l=A*(1-u0)+B*u0,r=A*(1-u1)+B*u1;
          airportPane(mb,l*4.315f+vec3(0,27.16f,0),r*4.315f+vec3(0,27.16f,0),r*4.69f+vec3(0,30.44f,0),l*4.69f+vec3(0,30.44f,0),.038f);}
        for(float y:{27.50f,28.03f})mb.cyl(A*5.1f+vec3(0,y,0),B*5.1f+vec3(0,y,0),.036f,.036f,6,P_METAL,false,false);}
      for(float y:{9.f,12.f,15.f,18.f,21.f,24.f})mb.cyl({0,y,0},{0,y+.065f,0},lerpf(2.5f,2.2f,(y-7.5f)/18.5f)+.015f,lerpf(2.5f,2.2f,(y-7.5f)/18.5f)+.015f,24,P_TRIM,false,false);
      break;
    case EK_FBO:
      for(int i=0;i<7;++i){float x=-8.43f+i*2.05f;airportPane(mb,{x,.59f,7.115f},{x+1.94f,.59f,7.115f},{x+1.94f,3.3f,7.115f},{x,3.3f,7.115f},.029f);}
      for(int side:{-1,1}){float x=side*9.8f;mb.cyl({x,4.18f,-6.7f},{x,.18f,-6.7f},.067f,.067f,8,P_METAL,false,false);}
      mb.cyl({3.f,6.10f,-3.f},{3.f,6.16f,-3.f},.64f,.64f,20,P_DARK,true,false);
      for(int i=0;i<10;++i)mb.box({2.08f,5.15f+i*.078f,-1.987f},{3.92f,5.17f+i*.078f,-1.978f},P_DARK,32);
      break;
    case EK_FUEL_TANK:
      for(float y:{2.3f,4.55f,6.8f})mb.cyl({0,y,0},{0,y+.05f,0},4.508f,4.508f,48,P_METAL,false,false);
      for(float x:{-.30f,.30f})mb.cyl({x,.15f,4.78f},{x,8.60f,4.78f},.04f,.04f,8,P_METAL,false,false);
      for(int i=0;i<25;++i){float y=.3f+i*.32f;mb.cyl({-.3f,y,4.78f},{.3f,y,4.78f},.025f,.025f,6,P_METAL,false,false);}
      for(float y:{2.4f,3.9f,5.4f,6.9f})airportCurve(mb,16,[&](float t){float a=t*PI;return vec3(.44f*cosf(a),y,4.78f+.44f*sinf(a));},.022f,P_METAL,5);
      mb.cyl({-4.7f,.6f,2.f},{-4.7f,2.7f,2.f},.13f,.13f,12,P_METAL,true,false);
      airportCurve(mb,16,[&](float t){float a=t*2*PI;return vec3(-4.7f+.26f*cosf(a),1.8f+.26f*sinf(a),2.15f);},.027f,P_DARK,5);
      break;
    case EK_FUEL_PUMP:
      airportRoundBox(mb,{-.43f,.16f,1.04f},{.43f,1.69f,1.46f},.06f,4,P_METAL);
      airportPane(mb,{-.32f,1.12f,1.475f},{.32f,1.12f,1.475f},{.32f,1.43f,1.475f},{-.32f,1.43f,1.475f},.017f);
      airportCurve(mb,22,[&](float t){return vec3(lerpf(.35f,.43f,t)+.23f*sinf(2*PI*t),1.32f-1.02f*sinf(PI*t),1.52f+.065f*sinf(PI*t));},.022f,P_DARK,6);
      mb.cyl({.36f,1.28f,1.54f},{.43f,1.47f,1.54f},.035f,.028f,8,P_METAL,true,true);
      for(float x:{-1.25f,1.25f})mb.cyl({x-.035f,1.65f,-.2f},{x+.035f,1.65f,-.2f},.955f,.955f,28,P_DARK,false,false);
      break;
    case EK_WINDSOCK:
      airportCurve(mb,28,[&](float t){float a=t*2*PI;return vec3(.15f,6.f+.354f*cosf(a),.354f*sinf(a));},.008f,P_METAL,5);
      for(float z:{-.29f,.29f})mb.cyl({0,6.26f,0},{.15f,6.f,z},.012f,.012f,5,P_METAL,false,false);
      break;
    case EK_BEACON:
      for(float x:{-.18f,.18f})mb.cyl({x,.3f,.64f},{x,13.5f,.35f},.022f,.022f,6,P_METAL,false,false);
      for(int i=0;i<39;++i){float y=.4f+i*.33f,z=lerpf(.64f,.35f,y/13.5f);mb.cyl({-.18f,y,z},{.18f,y,z},.016f,.016f,5,P_METAL,false,false);}
      mb.cyl({0,13.77f,0},{0,13.91f,0},.49f,.49f,24,P_METAL,true,true);
      for(int side:{-1,1})mb.cyl({0,14.23f,side*.36f},{0,14.23f,side*.46f},.255f,.24f,20,P_BEACON,true,true);
      break;
    case EK_JETBRIDGE:
      for(int side:{-1,1})for(int i=0;i<10;++i){float z=-7.3f+i*1.55f,x=side*1.359f;airportPane(mb,{x,4.28f,z},{x,4.28f,z+1.4f},{x,5.71f,z+1.4f},{x,5.71f,z},.032f);}
      for(float z:{-3.8f,1.8f}){mb.box({-1.40f,3.48f,z-.07f},{1.40f,3.64f,z+.07f},P_METAL);for(int side:{-1,1})mb.box({side*1.39f-.035f,3.5f,z-.07f},{side*1.39f+.035f,6.03f,z+.07f},P_METAL);}
      for(int i=0;i<12;++i){float z=8.65f+i*.115f;for(int side:{-1,1})mb.box({side*1.61f-.017f,3.38f,z},{side*1.61f+.017f,6.16f,z+.038f},P_DARK,1|2|32);}
      for(int side:{-1,1}){mb.cyl({side*.87f,.3f,5.14f},{side*.87f,3.35f,5.14f},.12f,.12f,12,P_METAL,false,false);wheel(mb,{side*1.1f,.28f,5.15f},.28f,.22f,16);}
      break;
    case EK_FENCE:
      for(float x:{-9.7f,-.3f,.3f,9.7f})mb.cyl({x,.15f,.014f},{x,2.3f,.014f},.009f,.009f,5,P_METAL,false,false);
      for(float y:{.12f,1.18f,2.31f})mb.cyl({-9.98f,y,.015f},{9.96f,y,.015f},.008f,.008f,5,P_METAL,false,false);
      break;
    case EK_LOCALIZER:
      for(int i=0;i<14;++i){float x=-16.f+i*32/13;mb.cyl({x,.3f,-.12f},{x,2.61f,-.12f},.024f,.024f,6,P_DARK,false,false);
        for(float y:{2.40f,2.64f,2.87f})mb.box({x-.43f,y-.009f,.351f},{x+.43f,y+.009f,.354f},P_METAL,32);}
      break;
    case EK_RADAR:
      for(int side:{-1,1})for(int i=0;i<4;++i){float y=1.5f+i*3.3f,y1=y+3.1f,r=lerpf(3.6f,1.6f,y/15),r1=lerpf(3.6f,1.6f,y1/15);
        mb.cyl({-r,y,side*r},{r1,y1,side*r1},.045f,.045f,6,P_METAL,false,false);
        mb.cyl({side*r,y,-r},{side*r1,y1,r1},.045f,.045f,6,P_METAL,false,false);}
      mb.cyl({0,15.61f,0},{0,15.78f,0},2.08f,2.08f,32,P_METAL,false,false);
      break;
    case EK_MAST:
      airportRoundBox(mb,{-1.38f,.05f,-1.49f},{1.38f,2.48f,-.31f},.07f,3,P_WALL);
      for(int i=0;i<6;++i)mb.box({-.65f,.55f+i*.16f,-1.503f},{.65f,.59f+i*.16f,-1.5f},P_DARK,16);
      mb.cyl({-.22f,.2f,.38f},{-.14f,11.7f,.38f},.024f,.024f,6,P_DARK,false,false);
      for(float y:{4.f,7.f,10.f})mb.cyl({-.32f,y,.50f},{.32f,y,.50f},.04f,.04f,8,P_METAL,true,true);
      break;
    case EK_FLOODMAST:
      for(int i=0;i<4;++i){float x=-1.05f+i*.7f;
        airportRoundBox(mb,{x-.32f,19.05f,-.33f},{x+.32f,19.87f,.33f},.052f,3,P_METAL);
        for(int j=0;j<7;++j)mb.box({x-.28f,19.1f+j*.1f,-.345f},{x+.28f,19.13f+j*.1f,-.33f},P_DARK,16);}
      airportRoundBox(mb,{-.19f,.4f,-.23f},{.19f,1.15f,.23f},.03f,3,P_METAL);
      break;
    default:break;
  }
}

struct AirportBodyStation {float z,rx,ry,cy;};
// A C1-continuous elliptical loft preserves the nose and tail silhouette while adding real longitudinal curvature.
vec3 airportBodyPoint(const AirportBodyStation* p,int count,float z,float angle) {
  int i=0;while(i<count-2 && z>p[i+1].z)++i;float h=p[i+1].z-p[i].z,t=clampf((z-p[i].z)/h,0,1);
  auto get=[&](int component){auto field=[&](int j){return component==0?p[j].rx:component==1?p[j].ry:p[j].cy;};
    float a=field(i),b=field(i+1),m0=(field(i+1)-field(std::max(0,i-1)))/(p[i+1].z-p[std::max(0,i-1)].z),m1=(field(std::min(count-1,i+2))-field(i))/(p[std::min(count-1,i+2)].z-p[i].z);
    return (2*t*t*t-3*t*t+1)*a+(t*t*t-2*t*t+t)*h*m0+(-2*t*t*t+3*t*t)*b+(t*t*t-t*t)*h*m1;};
  return {cosf(angle)*get(0),get(2)+sinf(angle)*get(1),z};
}
void airportBodyLoft(MB& mb,const AirportBodyStation* p,int count,int radial,int longitudinal,int part) {
  for(int i=0;i<count-1;++i)airportPatch(mb,radial,longitudinal,[&](float u,float v){return airportBodyPoint(p,count,lerpf(p[i].z,p[i+1].z,v),u*2*PI);},part,{0,p[i].cy,p[i].z});
  for(int i:{0,count-1}){vec3 c(0,p[i].cy,p[i].z);for(int j=0;j<radial;++j)mb.tri(c,airportBodyPoint(p,count,p[i].z,j*2*PI/radial),airportBodyPoint(p,count,p[i].z,(j+1)*2*PI/radial),part,{0,p[i].cy,(p[0].z+p[count-1].z)*.5f});}
}
// Symmetric aerofoil section with a rounded leading edge, tapered thickness and a fine trailing edge.
void airportWing(MB& mb,vec3 rootLE,vec3 rootTE,vec3 tipLE,vec3 tipTE,float thick,int chord,int span,int part) {
  for(int face:{-1,1})airportPatch(mb,chord,span,[&](float u,float v){vec3 le=rootLE*(1-v)+tipLE*v,te=rootTE*(1-v)+tipTE*v;float th=thick*(.2969f*sqrtf(std::max(u,.000001f))-.126f*u-.3516f*u*u+.2843f*u*u*u-.1036f*u*u*u*u)/.12f;
    return le*(1-u)+te*u+vec3(0,face*th*(1-.48f*v),0);},part,(rootLE+rootTE+tipLE+tipTE)*.25f-vec3(0,face*.2f,0));
}
void airportGADetailed(MB& mb) {
  const AirportBodyStation body[]={{-3.90f,.10f,.13f,1.5f},{-3.4f,.17f,.20f,1.45f},{-2.f,.30f,.32f,1.39f},{-.3f,.49f,.45f,1.35f},{.2f,.58f,.51f,1.35f},{1.1f,.60f,.56f,1.38f},{2.1f,.60f,.57f,1.37f},{2.95f,.54f,.50f,1.2f},{3.7f,.46f,.43f,1.17f},{4.15f,.25f,.25f,1.15f}};
  airportBodyLoft(mb,body,10,28,3,P_PAINT);
  auto glazing=[&](float z0,float z1,float a0,float a1){airportPatch(mb,7,7,[&](float u,float v){float z=lerpf(z0,z1,u),a=lerpf(a0,a1,v);vec3 p=airportBodyPoint(body,10,z,a);
    // Independent glass/fuselage tessellation can differ by millimetres between their vertices.
    // Seat the pane just outward along the same loft normal so no painted teeth cross its edge.
    vec3 along=normalize(airportBodyPoint(body,10,z+.0005f,a)-airportBodyPoint(body,10,z-.0005f,a));
    vec3 around=normalize(airportBodyPoint(body,10,z,a+.0005f)-airportBodyPoint(body,10,z,a-.0005f));
    p.x*=1.008f;p.y+=.003f;return p+normalize(cross(around,along))*.006f;},P_GLASS,{0,1.2f,1.f},1,true);};
  glazing(1.99f,2.83f,.88f,2.26f);
  for(int side:{-1,1}){float a0=side>0?.14f:PI-.14f,a1=side>0?.93f:PI-.93f;
    glazing(.55f,1.13f,a0,a1);glazing(1.23f,1.93f,a0,a1);
    airportWing(mb,{side*.5f,2.05f,2.1f},{side*.5f,2.05f,.55f},{side*5.5f,2.055f,1.94f},{side*5.5f,2.055f,.70f},.062f,18,10,P_PAINT);
    airportWing(mb,{side*.10f,1.45f,-3.15f},{side*.10f,1.45f,-4.f},{side*1.75f,1.45f,-3.37f},{side*1.75f,1.45f,-3.94f},.027f,14,5,P_PAINT);
    mb.cyl({side*.52f,.98f,1.38f},{side*2.73f,2.028f,1.45f},.027f,.027f,10,P_METAL,false,false);
    mb.cyl({side*.45f,.83f,1.f},{side*1.19f,.29f,1.f},.042f,.031f,10,P_METAL,false,false);
    airportRoadWheel(mb,{side*1.25f,.27f,1.f},.27f,.14f,false,20);
    // Aileron/flap hinges and the door seam stay fine enough to read as sheet-metal construction.
    airportCurve(mb,8,[&](float t){return vec3(side*lerpf(.7f,5.35f,t),2.057f,lerpf(.82f,.88f,t));},.006f,P_DARK,4);
    mb.cyl({side*.594f,1.35f,1.01f},{side*.594f,1.35f,1.19f},.013f,.013f,6,P_METAL,true,true);
  }
  plate(mb,{0,1.5f,-2.9f},{0,1.5f,-4.05f},{0,2.7f,-4.15f},{0,2.7f,-3.55f},{.027f,0,0},P_STRIPE);
  mb.cyl({0,.83f,3.4f},{0,.25f,3.45f},.036f,.036f,10,P_METAL,false,false);
  airportRoadWheel(mb,{0,.22f,3.45f},.22f,.10f,true,20);
  mb.cyl({0,1.15f,4.145f},{0,1.15f,4.195f},.145f,.07f,20,P_METAL,true,false);
  for(int side:{-1,1}){plate(mb,{-.035f,1.15f,4.163f},{.035f,1.15f,4.163f},{.11f,1.15f+side*.91f,4.185f},{.025f,1.15f+side*.93f,4.185f},{0,0,.008f},P_DARK);}
  mb.cyl({-.31f,.90f,3.75f},{-.31f,.78f,3.66f},.028f,.028f,10,P_DARK,true,false);
}
void airportAirlinerDetailed(MB& mb) {
  const AirportBodyStation body[]={{-19.3f,.25f,.25f,4.2f},{-17.6f,.85f,.85f,3.8f},{-15.f,1.55f,1.55f,3.35f},{-12.f,1.98f,1.98f,3.f},{-7.f,1.98f,1.98f,3.f},{7.f,1.98f,1.98f,3.f},{13.5f,1.98f,1.98f,3.f},{16.3f,1.78f,1.78f,2.95f},{18.1f,1.25f,1.25f,2.8f},{19.1f,.60f,.60f,2.65f},{19.55f,.018f,.018f,2.58f}};
  airportBodyLoft(mb,body,11,48,4,P_PAINT);
  for(int side:{-1,1}){
    airportWing(mb,{side*1.7f,1.75f,2.2f},{side*1.7f,1.75f,-4.4f},{side*17.f,2.75f,-8.6f},{side*17.f,2.75f,-10.2f},.115f,24,18,P_METAL);
    airportWing(mb,{side*.4f,3.9f,-14.6f},{side*.4f,3.9f,-17.8f},{side*6.3f,4.3f,-18.3f},{side*6.3f,4.3f,-19.4f},.059f,18,10,P_METAL);
    plate(mb,{side*17.f,2.75f,-8.7f},{side*17.f,2.75f,-10.1f},{side*17.1f,4.3f,-10.9f},{side*17.1f,4.3f,-10.f},{.035f,0,0},P_STRIPE);
    float ex=side*5.8f;
    const float ez[]={-3.f,-2.4f,-1.8f,.7f,1.85f,2.35f,2.4f},er[]={.15f,.40f,.74f,1.0f,1.05f,1.01f,.91f};
    for(int i=0;i<6;++i)airportPatch(mb,40,3,[&](float u,float v){float a=u*2*PI,r=lerpf(er[i],er[i+1],v),z=lerpf(ez[i],ez[i+1],v);return vec3(ex+cosf(a)*r,1.47f+sinf(a)*r,z);},i==0?P_METAL:P_PAINT,{ex,1.47f,0});
    // Rolled inlet lip, curved intake wall and actual pitched fan blades behind the aperture.
    airportPatch(mb,40,5,[&](float u,float v){float a=u*2*PI,b=v*PI,r=.90f+.105f*sinf(b);return vec3(ex+cosf(a)*r,1.47f+sinf(a)*r,2.36f+.08f*cosf(b));},P_METAL,{ex,1.47f,2.1f});
    mb.cyl({ex,1.47f,1.83f},{ex,1.47f,2.31f},.83f,.90f,40,P_DARK,false,true);
    for(int b=0;b<22;++b){float a=b*2*PI/22;vec3 r( sinf(a),cosf(a),0),t(cosf(a),-sinf(a),0),c(ex,1.47f,2.05f);
      mb.quad(c+r*.20f-t*.025f,c+r*.20f+t*.025f+vec3(0,0,.055f),c+r*.80f+t*.045f,c+r*.80f-t*.07f-vec3(0,0,.04f),P_METAL,c-vec3(0,0,.5f));}
    mb.cyl({ex,1.47f,2.02f},{ex,1.47f,2.28f},.22f,.06f,24,P_METAL,true,false);
    airportRoundBox(mb,{ex-.14f,2.18f,-2.f},{ex+.14f,2.45f,1.5f},.035f,3,P_METAL);
    for(int i=0;i<34;++i){float z=-11.6f+i*.79f;float a=side>0?.40f:PI-.40f;
      airportPatch(mb,4,5,[&](float u,float v){float dz=(u*2-1)*.19f,da=(v*2-1)*.14f;vec3 p=airportBodyPoint(body,11,z+dz,a+da);p.x*=1.006f;return p;},P_GLASS,{0,3,z},1,true);}
    for(float z:{-11.5f,11.7f})airportCurve(mb,18,[&](float t){float a=t*2*PI;float y=3.24f+.90f*sinf(a),zz=z+.36f*cosf(a);float x=sqrtf(std::max(0.f,1.98f*1.98f-(y-3)*(y-3)));return vec3(side*(x+.01f),y,zz);},.016f,P_DARK,5);
    mb.cyl({side*3.8f,1.9f,-1.2f},{side*3.8f,.55f,-1.2f},.14f,.12f,14,P_METAL,false,false);
    for(int w:{-1,1})airportRoadWheel(mb,{side*3.8f+w*.4f,.57f,-1.2f},.57f,.35f,true,24);
  }
  for(int side:{-1,1})airportPatch(mb,8,5,[&](float u,float v){float z=lerpf(16.5f,17.76f,u),a=side>0?lerpf(.67f,1.20f,v):lerpf(PI-.67f,PI-1.20f,v);vec3 p=airportBodyPoint(body,11,z,a);p.x*=1.005f;p.y+=.006f;return p;},P_GLASS,{0,3,17},1,true);
  plate(mb,{0,4.5f,-12.6f},{0,4.2f,-18.4f},{0,11.4f,-19.4f},{0,11.4f,-16.9f},{.14f,0,0},P_STRIPE);
  mb.cyl({0,1.4f,14.6f},{0,.4f,14.6f},.10f,.085f,14,P_METAL,false,false);
  for(int side:{-1,1})airportRoadWheel(mb,{side*.22f,.38f,14.6f},.38f,.22f,true,24);
  airportRoundBox(mb,{-1.4f,1.1f,-4.f},{1.4f,1.6f,2.5f},.18f,5,P_PAINT);
}

void buildAirportKind(MB& mb, int kind, int lod) {
  const size_t first=mb.o.size();
  bool d0 = lod == 0 || lod == 3, d1 = lod != 2;
  switch (kind) {
    case EK_HANGAR: {   // steel-clad hangar: low gable roof spanning the door, sliding doors across the whole front
      mb.box(vec3(-20.1f, -2.f, -16.1f), vec3(20.1f, 0.2f, 16.1f), P_TRIM, 0x3B);
      mb.box(vec3(-20.f, 0.2f, -16.f), vec3(20.f, 10.f, 16.f), P_WALL, 0x3B & ~8 & ~32);
      mb.box(vec3(-20.f, 8.9f, 15.9f), vec3(20.f, 10.f, 16.f), P_WALL, 32);   // header over the doors
      mb.gable(-20.f, 20.f, -16.f, 16.f, 10.f, 13.f, true, d1 ? 0.6f : 0.f, P_ROOF, P_WALL);
      mb.box(vec3(-19.6f, 0.2f, 16.f), vec3(19.6f, 8.9f, 16.12f), P_DOOR, 32);
      if (d1) mb.box(vec3(-20.f, 8.8f, 16.f), vec3(20.f, 9.1f, 16.5f), P_METAL, 0x3F);   // door track
      if (d0) {
        mb.box(vec3(20.f, 0.2f, 12.f), vec3(20.06f, 2.3f, 13.f), P_DOOR, 2);   // personnel door on the side
        for (int i = 0; i < 3; i++) mb.box(vec3(-10.f + i * 10.f - 0.6f, 12.f, -8.f), vec3(-10.f + i * 10.f + 0.6f, 12.9f + i * 0.f, -6.8f), P_METAL, 0x3B | 8);   // roof vents
      }
      break;
    }
    case EK_ARCH_HANGAR: {   // Quonset hut hangar
      mb.box(vec3(-9.1f, -2.f, -12.1f), vec3(9.1f, 0.15f, 12.1f), P_TRIM, 0x3B);
      int sg=lod == 3 ? 32 : lod == 0 ? 16 : lod == 1 ? 10 : 6;
      archShell(mb,9.f,7.5f,-12.f,12.f,sg,P_ROOF,P_WALL,false);
      airportArchEnds(mb,9.f,7.5f,-12.f,12.f,sg,5.f,5.6f);
      if(lod==3)for(float x:{-5.f,5.f})mb.box({x-.065f,.15f,12.025f},{x+.065f,5.6f,12.18f},P_METAL,1|2|16|32);
      mb.box(vec3(-5.f, 0.15f, 12.f), vec3(5.f, 5.6f, 12.1f), P_DOOR, 32);
      if (d1) mb.box(vec3(-5.4f, 5.6f, 12.f), vec3(5.4f, 5.9f, 12.25f), P_METAL, 0x3F);
      if (d0) { door(mb, -7.f, -12.f, 0.9f, 2.f, 0.15f); mb.box(vec3(6.f, 2.4f, 12.f), vec3(7.4f, 3.4f, 12.08f), P_GLASS, 32); }
      break;
    }
    case EK_T_HANGAR: {   // row of four nested T-hangar bays: mono-pitch roof, a wide door per bay
      mb.box(vec3(-24.1f, -2.f, -7.1f), vec3(24.1f, 0.15f, 7.1f), P_TRIM, 0x3B);
      mb.box(vec3(-24.f, 0.15f, -7.f), vec3(24.f, 4.f, 7.f), P_WALL, 0x3B & ~8 & ~32);
      mb.quad(vec3(-24.4f, 4.8f, 7.4f), vec3(24.4f, 4.8f, 7.4f), vec3(24.4f, 4.f, -7.4f), vec3(-24.4f, 4.f, -7.4f), P_ROOF, vec3(0, 2, 0));
      for (int s = -1; s <= 1; s += 2) mb.tri(vec3(s * 24.f, 4.f, -7.f), vec3(s * 24.f, 4.8f, 7.f), vec3(s * 24.f, 4.f, 7.f), P_WALL, vec3(0, 2, 0));
      mb.box(vec3(-24.f, 3.7f, 6.95f), vec3(24.f, 4.8f, 7.f), P_WALL, 32);
      for (int b = 0; b < 4; b++) {
        float x0 = -24.f + b * 12.f;
        mb.box(vec3(x0 + 0.5f, 0.15f, 7.f), vec3(x0 + 11.5f, 3.7f, 7.1f), P_DOOR, 32);
        if (d1) mb.box(vec3(x0 - 0.25f, 0.15f, 6.9f), vec3(x0 + 0.25f, 3.75f, 7.2f), P_TRIM, 0x3B);
      }
      break;
    }
    case EK_TERMINAL: {   // passenger terminal: glazed airside facade under a deep roof, landside entrance canopy
      mb.box(vec3(-60.f, -2.f, -20.f), vec3(60.f, 0.3f, 20.f), P_TRIM, 0x3B);
      mb.box(vec3(-60.f, 0.3f, -20.f), vec3(60.f, 11.6f, 20.f), P_WALL, 0x3B & ~8 & ~32);
      mb.box(vec3(-59.f, 0.3f, 19.6f), vec3(59.f, 11.6f, 19.7f), P_GLASS, 32);   // curtain wall
      mb.box(vec3(-63.f, 11.6f, -21.f), vec3(63.f, 12.6f, 25.f), P_TRIM, 0x3F);  // roof slab, oversailing the glass
      mb.box(vec3(-62.f, 12.6f, -20.f), vec3(62.f, 12.65f, 24.f), P_DARK, 8);
      if (d1) {
        mb.box(vec3(-18.f, 12.6f, -14.f), vec3(18.f, 15.f, 8.f), P_WALL, 0x3B);   // upper hall / plant
        mb.box(vec3(-17.5f, 12.8f, 8.f), vec3(17.5f, 14.6f, 8.08f), P_GLASS, 32);
        mb.box(vec3(-26.f, 4.4f, -27.f), vec3(26.f, 5.f, -20.f), P_CANOPY, 0x3F);   // drop-off canopy
        for (int i = 0; i < 5; i++) { float x = -24.f + i * 12.f; mb.box(vec3(x - 0.25f, 0.3f, -26.6f), vec3(x + 0.25f, 4.4f, -26.1f), P_TRIM); }
        mb.box(vec3(-25.f, 0.3f, -20.1f), vec3(25.f, 4.2f, -20.f), P_GLASS, 16);   // landside entrance glazing
        for (int i = 0; i < 9; i++) { float x = -52.f + i * 13.f; mb.box(vec3(x - 0.35f, 0.3f, 19.7f), vec3(x + 0.35f, 11.6f, 20.4f), P_TRIM); }   // facade fins
      }
      if (d0) {
        mb.box(vec3(-9.f, 15.f, 7.8f), vec3(9.f, 16.6f, 8.2f), P_SIGN, 0x3F);
        for (int i = 0; i < 4; i++) mb.box(vec3(-48.f + i * 26.f, 12.65f, -12.f), vec3(-44.f + i * 26.f, 14.3f, -8.f), P_METAL, 0x3B | 8);
      }
      break;
    }
    case EK_CTRL_TOWER: {   // control tower: base block, concrete shaft, splayed glazed cab with a catwalk, antennas
      int sg = lod == 3 ? 32 : lod == 0 ? 16 : lod == 1 ? 10 : 6;
      mb.box(vec3(-5.f, -2.f, -5.f), vec3(5.f, 7.f, 5.f), P_WALL, 0x3B);
      mb.box(vec3(-5.2f, 7.f, -5.2f), vec3(5.2f, 7.5f, 5.2f), P_TRIM, 0x3F);
      mb.cyl(vec3(0, 7.5f, 0), vec3(0, 26.f, 0), 2.5f, 2.2f, sg, P_WALL, false, false);
      mb.cyl(vec3(0, 26.f, 0), vec3(0, 27.f, 0), 3.2f, 4.3f, 8, P_TRIM, false, true, 1, 1, PI / 8, false);
      mb.cyl(vec3(0, 27.f, 0), vec3(0, 30.6f, 0), 4.3f, 4.7f, 8, P_GLASS, false, false, 1, 1, PI / 8, false);
      mb.cyl(vec3(0, 30.6f, 0), vec3(0, 31.4f, 0), 5.1f, 5.1f, 8, P_TRIM, true, true, 1, 1, PI / 8, false);
      if (d1) { mb.cyl(vec3(0, 26.9f, 0), vec3(0, 27.05f, 0), 5.3f, 5.3f, 8, P_METAL, true, true, 1, 1, PI / 8, false);   // catwalk
        mb.cyl(vec3(0, 31.4f, 0), vec3(0, 34.f, 0), 0.07f, 0.05f, 4, P_OBST, false, false); }
      if (d0) { mb.cyl(vec3(2.f, 31.4f, 1.f), vec3(2.f, 33.f, 1.f), 0.05f, 0.05f, 4, P_METAL, false, false);
        mb.box(vec3(-2.5f, 31.4f, -2.f), vec3(-1.f, 32.3f, -0.8f), P_METAL, 0x3B | 8); door(mb, 0.f, 5.f, 1.4f, 2.4f, 0.f); }
      break;
    }
    case EK_FBO: {   // flight centre / club house: single storey, glazed front, flat roof with parapet
      mb.box(vec3(-10.1f, -2.f, -7.1f), vec3(10.1f, 0.3f, 7.1f), P_TRIM, 0x3B);
      mb.box(vec3(-10.f, 0.3f, -7.f), vec3(10.f, 4.2f, 7.f), P_WALL, 0x3B & ~8);
      mb.box(vec3(-10.2f, 4.2f, -7.2f), vec3(10.2f, 5.f, 7.2f), P_TRIM, 0x3B & ~8);
      mb.box(vec3(-9.8f, 4.1f, -6.8f), vec3(9.8f, 4.2f, 6.8f), P_DARK, 8);
      mb.box(vec3(-8.5f, 0.5f, 7.f), vec3(6.f, 3.4f, 7.08f), P_GLASS, 32);
      if (d1) { mb.box(vec3(-4.f, 3.2f, 7.f), vec3(4.f, 3.5f, 9.f), P_TRIM, 0x3F); mb.box(vec3(-6.f, 4.25f, 7.2f), vec3(2.f, 4.95f, 7.3f), P_SIGN, 32); }
      if (d0) { door(mb, 7.8f, 7.f, 1.6f, 2.3f, 0.3f); mb.box(vec3(2.f, 5.f, -4.f), vec3(4.f, 6.1f, -2.f), P_METAL, 0x3B | 8);
        mb.cyl(vec3(-8.f, 5.f, -5.f), vec3(-8.f, 9.f, -5.f), 0.05f, 0.04f, 4, P_METAL, false, false); }
      break;
    }
    case EK_FUEL_TANK: {   // vertical fuel tank inside a bund wall
      int sg = lod == 3 ? 48 : lod == 0 ? 20 : lod == 1 ? 12 : 8;
      if (d1) for (int s = -1; s <= 1; s += 2) { mb.box(vec3(-6.f, -1.f, s * 5.8f - 0.2f), vec3(6.f, 1.f, s * 5.8f + 0.2f), P_TRIM, 0x3F);
        mb.box(vec3(s * 5.8f - 0.2f, -1.f, -6.f), vec3(s * 5.8f + 0.2f, 1.f, 6.f), P_TRIM, 0x3F); }
      mb.cyl(vec3(0, -0.5f, 0), vec3(0, 8.f, 0), 4.5f, 4.5f, sg, P_METAL, false, false);
      mb.cyl(vec3(0, 8.f, 0), vec3(0, 9.f, 0), 4.6f, 0.4f, sg, P_METAL, false, false);
      if (lod==0) { for (int i = 0; i < 6; i++) { float a = i * 0.3f, y = 0.5f + i * 1.3f; mb.box(vec3(cosf(a) * 4.55f - 0.4f, y, sinf(a) * 4.55f - 0.4f), vec3(cosf(a) * 4.55f + 0.4f, y + 0.08f, sinf(a) * 4.55f + 0.4f), P_METAL, 0x3F); }
        mb.cyl(vec3(-4.5f, 0.6f, 2.f), vec3(-6.f, 0.6f, 2.f), 0.18f, 0.18f, 6, P_METAL, false, false); }
      break;
    }
    case EK_FUEL_PUMP: {   // small fuel station: horizontal tank on saddles, dispenser with hose
      mb.box(vec3(-2.5f, -0.5f, -1.6f), vec3(2.5f, 0.12f, 1.6f), P_TRIM, 0x3F);
      for (int s = -1; s <= 1; s += 2) mb.box(vec3(s * 1.2f - 0.15f, 0.12f, -0.75f), vec3(s * 1.2f + 0.15f, 0.8f, 0.75f), P_TRIM, 0x3B);
      mb.cyl(vec3(-2.f, 1.65f, -0.2f), vec3(2.f, 1.65f, -0.2f), 0.95f, 0.95f, lod == 3 ? 32 : lod == 0 ? 14 : 8, P_METAL, true, true);
      mb.box(vec3(-0.45f, 0.12f, 1.f), vec3(0.45f, 1.7f, 1.45f), P_METAL, 0x3B | 8);
      // The close tier has a fitted display; the coarse sign would sit in front of its glass.
      if (d1 && lod != 3) mb.box(vec3(-0.4f, 0.9f, 1.45f), vec3(0.4f, 1.5f, 1.5f), P_SIGN, 32);
      if (lod==0) mb.cyl(vec3(0.35f, 1.3f, 1.45f), vec3(0.6f, 0.3f, 1.6f), 0.025f, 0.025f, 4, P_DARK, false, false);
      break;
    }
    case EK_WINDSOCK: {   // pole, frame ring and a striped sock (the vertex shader turns the sock with the wind)
      mb.box(vec3(-0.4f, -0.5f, -0.4f), vec3(0.4f, 0.1f, 0.4f), P_TRIM, 0x3F);
      mb.cyl(vec3(0, 0.1f, 0), vec3(0, 6.3f, 0), 0.06f, 0.05f, d0 ? 6 : 4, P_OBST, false, false);
      if(lod==3){
        airportPatch(mb,24,24,[](float u,float v){float a=u*2*PI,x=.15f+3.6f*v,r=(.36f-.20f*v)*(1+.025f*sinf(7*a+v*12)*v);return vec3(x,6.f-.32f*v*v+cosf(a)*r,sinf(a)*r);},P_SOCK,{1.95f,6.f,0});
        break;
      }
      int sg = d0 ? 12 : 7;
      for (int i = 0; i < 4; i++) { float x0 = 0.15f + i * 0.9f, x1 = x0 + 0.9f;   // sock along +x, tapering
        mb.cyl(vec3(x0, 6.f, 0), vec3(x1, 6.f, 0), 0.36f - i * 0.05f, 0.36f - (i + 1) * 0.05f, sg, P_SOCK, false, false); }
      break;
    }
    case EK_BEACON: {   // rotating aerodrome beacon on a lattice mast
      for (int i = 0; i < 3; i++) { float a = i * 2.094f; mb.cyl(vec3(cosf(a) * 1.1f, 0, sinf(a) * 1.1f), vec3(cosf(a) * 0.45f, 13.6f, sinf(a) * 0.45f), 0.06f, 0.05f, 4, P_METAL, false, false); }
      if (d0) for (int i = 0; i < 3; i++) for (int k = 0; k < 4; k++) { float a0 = i * 2.094f, a1 = a0 + 2.094f, y0 = k * 3.4f, y1 = y0 + 3.4f;
          float r0 = lerpf(1.1f, 0.45f, y0 / 13.6f), r1 = lerpf(1.1f, 0.45f, y1 / 13.6f);
          mb.cyl(vec3(cosf(a0) * r0, y0, sinf(a0) * r0), vec3(cosf(a1) * r1, y1, sinf(a1) * r1), 0.025f, 0.025f, 3, P_METAL, false, false); }
      mb.box(vec3(-0.8f, 13.6f, -0.8f), vec3(0.8f, 13.75f, 0.8f), P_METAL, 0x3F);
      mb.cyl(vec3(0, 13.75f, 0), vec3(0, 14.6f, 0), 0.42f, 0.42f, d0 ? 12 : 8, P_BEACON, true, false);
      if (d1) mb.cyl(vec3(0, 14.6f, 0), vec3(0, 15.f, 0), 0.3f, 0.05f, 6, P_METAL, false, false);
      break;
    }
    case EK_GA_PLANE: {   // parked high-wing single: nose +z, wing span 11 m
      if(lod==3){airportGADetailed(mb);break;}
      int sg = lod == 0 ? 10 : 6;
      mb.cyl(vec3(0, 1.2f, 3.0f), vec3(0, 1.15f, 4.15f), 0.6f, 0.38f, sg, P_PAINT, false, true);   // cowling
      mb.box(vec3(-0.6f, 0.7f, 0.2f), vec3(0.6f, 1.75f, 3.0f), P_PAINT, 0x3F);                       // cabin
      mb.quad(vec3(-0.58f, 1.75f, 2.95f), vec3(0.58f, 1.75f, 2.95f), vec3(0.55f, 1.98f, 1.9f), vec3(-0.55f, 1.98f, 1.9f), P_GLASS, vec3(0, 1.2f, 1.f));
      mb.box(vec3(-0.6f, 1.75f, 0.2f), vec3(0.6f, 1.98f, 1.9f), P_PAINT, 0x3B | 8);
      for (int s = -1; s <= 1; s += 2) mb.box(vec3(s * 0.6f - 0.02f, 1.2f, 0.4f), vec3(s * 0.6f + 0.02f, 1.7f, 2.7f), P_GLASS, 3);
      mb.cyl(vec3(0, 1.35f, 0.2f), vec3(0, 1.5f, -3.9f), 0.6f, 0.13f, sg, P_PAINT, false, false);   // tail cone
      mb.box(vec3(-5.5f, 1.98f, 0.55f), vec3(5.5f, 2.12f, 2.1f), P_PAINT, 0x3F);                     // wing
      mb.box(vec3(-1.75f, 1.42f, -4.f), vec3(1.75f, 1.48f, -3.15f), P_PAINT, 0x3F);                  // stabiliser
      plate(mb, vec3(0, 1.5f, -2.9f), vec3(0, 1.5f, -4.05f), vec3(0, 2.7f, -4.15f), vec3(0, 2.7f, -3.55f), vec3(0.03f, 0, 0), P_STRIPE);   // fin
      if (d1) {
        for (int s = -1; s <= 1; s += 2) mb.cyl(vec3(s * 0.6f, 0.95f, 1.4f), vec3(s * 2.7f, 1.98f, 1.5f), 0.035f, 0.035f, 4, P_METAL, false, false);   // struts
        for (int s = -1; s <= 1; s += 2) { mb.cyl(vec3(s * 0.45f, 0.75f, 1.0f), vec3(s * 1.2f, 0.3f, 1.0f), 0.04f, 0.04f, 4, P_METAL, false, false); wheel(mb, vec3(s * 1.25f, 0.27f, 1.0f), 0.27f, 0.14f, sg); }
        mb.cyl(vec3(0, 0.75f, 3.4f), vec3(0, 0.25f, 3.45f), 0.04f, 0.04f, 4, P_METAL, false, false); wheel(mb, vec3(0, 0.22f, 3.45f), 0.22f, 0.1f, sg);
        for (int s = -1; s <= 1; s += 2) mb.box(vec3(-0.05f, 1.15f, 4.15f), vec3(0.05f, 1.15f + s * 0.95f, 4.2f), P_DARK, 0x3F);   // propeller
      }
      break;
    }
    case EK_AIRLINER: {   // parked narrow-body airliner: nose +z, 38 m long, 34 m span; fuselage centre 3 m up
      if(lod==3){airportAirlinerDetailed(mb);break;}
      int sg = lod == 0 ? 18 : lod == 1 ? 12 : 8;
      const float R = 1.98f, Y = 3.0f;
      mb.cyl(vec3(0, Y, -12.f), vec3(0, Y, 13.5f), R, R, sg, P_PAINT, false, false);
      float nz[5] = {13.5f, 16.3f, 18.1f, 19.1f, 19.55f}, nr[5] = {R, 1.78f, 1.25f, 0.6f, 0.f}, ny[5] = {Y, Y - 0.05f, Y - 0.2f, Y - 0.35f, Y - 0.42f};
      for (int i = 0; i < 4; i++) mb.cyl(vec3(0, ny[i], nz[i]), vec3(0, ny[i + 1], nz[i + 1]), nr[i], nr[i + 1], sg, P_PAINT, false, false);
      float tz[4] = {-12.f, -15.f, -17.6f, -19.3f}, tr[4] = {R, 1.55f, 0.85f, 0.25f}, ty[4] = {Y, Y + 0.35f, Y + 0.8f, Y + 1.2f};
      for (int i = 0; i < 3; i++) mb.cyl(vec3(0, ty[i], tz[i]), vec3(0, ty[i + 1], tz[i + 1]), tr[i], tr[i + 1], sg, P_PAINT, false, i == 2);
      for (int s = -1; s <= 1; s += 2) {
        plate(mb, vec3(s * 1.7f, 1.75f, 2.2f), vec3(s * 1.7f, 1.75f, -4.4f), vec3(s * 17.f, 2.75f, -10.2f), vec3(s * 17.f, 2.75f, -8.6f), vec3(0, 0.12f, 0), P_METAL);   // wing
        plate(mb, vec3(s * 0.4f, Y + 0.9f, -14.6f), vec3(s * 0.4f, Y + 0.9f, -17.8f), vec3(s * 6.3f, Y + 1.3f, -19.4f), vec3(s * 6.3f, Y + 1.3f, -18.3f), vec3(0, 0.06f, 0), P_METAL);   // stabiliser
        // engine: nacelle on a pylon under the wing
        float ex = s * 5.8f;
        mb.cyl(vec3(ex, 1.45f, 2.4f), vec3(ex, 1.5f, -2.0f), 1.05f, 0.75f, sg, P_PAINT, true, false);
        mb.cyl(vec3(ex, 1.5f, -2.0f), vec3(ex, 1.55f, -3.0f), 0.55f, 0.15f, sg / 2 + 2, P_METAL, false, false);
        if (d0) mb.cyl(vec3(ex, 1.45f, 2.45f), vec3(ex, 1.45f, 2.3f), 0.9f, 0.9f, sg, P_DARK, true, false);   // fan face
        mb.box(vec3(ex - 0.15f, 2.2f, -2.f), vec3(ex + 0.15f, 2.45f, 1.5f), P_METAL, 0x3F);
        if (d1) plate(mb, vec3(s * 17.f, 2.75f, -8.7f), vec3(s * 17.f, 2.75f, -10.1f), vec3(s * 17.1f, 4.3f, -10.9f), vec3(s * 17.1f, 4.3f, -10.f), vec3(0.04f, 0, 0), P_STRIPE);   // winglet
      }
      plate(mb, vec3(0, Y + 1.5f, -12.6f), vec3(0, Y + 1.2f, -18.4f), vec3(0, 11.4f, -19.4f), vec3(0, 11.4f, -16.9f), vec3(0.14f, 0, 0), P_STRIPE);   // fin
      if (d1) {   // undercarriage
        for (int s = -1; s <= 1; s += 2) { mb.cyl(vec3(s * 3.8f, 1.9f, -1.2f), vec3(s * 3.8f, 0.55f, -1.2f), 0.14f, 0.14f, 6, P_METAL, false, false);
          for (int w = -1; w <= 1; w += 2) wheel(mb, vec3(s * 3.8f + w * 0.4f, 0.57f, -1.2f), 0.57f, 0.35f, sg); }
        mb.cyl(vec3(0, 1.4f, 14.6f), vec3(0, 0.4f, 14.6f), 0.1f, 0.1f, 6, P_METAL, false, false);
        for (int w = -1; w <= 1; w += 2) wheel(mb, vec3(w * 0.22f, 0.38f, 14.6f), 0.38f, 0.22f, sg);
        mb.box(vec3(-1.4f, 1.1f, -4.f), vec3(1.4f, 1.6f, 2.5f), P_PAINT, 0x3F);   // wing-to-body fairing
      }
      break;
    }
    case EK_JETBRIDGE: {   // passenger boarding bridge: rotunda at -z (terminal), tunnel, drive column, cab at +z (aircraft door)
      mb.cyl(vec3(0, 0, -9.f), vec3(0, 6.3f, -9.f), 1.9f, 1.9f, lod == 3 ? 32 : lod == 0 ? 12 : 8, P_WALL, true, false);
      mb.box(vec3(-1.35f, 3.5f, -8.f), vec3(1.35f, 6.f, 8.6f), P_WALL, 0x3F);
      mb.box(vec3(-1.6f, 3.3f, 8.6f), vec3(1.6f, 6.2f, 10.f), P_DARK, 0x3F);   // cab bellows
      if (d1) { for (int s = -1; s <= 1; s += 2) mb.box(vec3(s * 0.9f - 0.15f, 0.f, 5.f), vec3(s * 0.9f + 0.15f, 3.5f, 5.3f), P_METAL);
        mb.box(vec3(-1.3f, 0.f, 4.8f), vec3(1.3f, 0.55f, 5.5f), P_DARK, 0x3F); }
      break;
    }
    case EK_CAR: {   // compact island sedan: shaped bonnet/boot, raked glazing and a proper roof line
      if(lod!=2){airportSedanDetailed(mb,lod);break;}
      // Four body stations replace a box. No interior underside is needed above the road.
      const float z[4]={-2.2f,-1.65f,1.55f,2.2f}, w[4]={.70f,.88f,.88f,.73f}, y[4]={.66f,.90f,.91f,.67f};
      for(int i=0;i<3;++i) {
        vec3 a(-w[i],y[i],z[i]),b(w[i],y[i],z[i]),c(w[i+1],y[i+1],z[i+1]),d(-w[i+1],y[i+1],z[i+1]);
        mb.quad(a,b,c,d,P_PAINT,{0,.4f,0});
        for(int side=-1;side<=1;side+=2) mb.quad({side*w[i],.32f,z[i]},{side*w[i+1],.32f,z[i+1]},
          {side*w[i+1],y[i+1],z[i+1]},{side*w[i],y[i],z[i]},P_PAINT,{0,.5f,0});
      }
      for(int i : {0,3}) mb.quad({-w[i],.32f,z[i]},{w[i],.32f,z[i]},{w[i],y[i],z[i]},{-w[i],y[i],z[i]},P_PAINT,{0,.5f,0});
      const vec3 rl(-.64f,1.46f,-.78f),rr(.64f,1.46f,-.78f),fl(-.64f,1.43f,.56f),fr(.64f,1.43f,.56f);
      mb.quad(rl,rr,fr,fl,P_PAINT,{0,1,0});
      mb.quad({-.80f,.91f,1.15f},{.80f,.91f,1.15f},fr,fl,P_GLASS,{0,1,0});
      mb.quad({-.80f,.90f,-1.45f},{.80f,.90f,-1.45f},rr,rl,P_GLASS,{0,1,0});
      for(int side=-1;side<=1;side+=2) {
        // Side glazing is split at a solid B-pillar. The panels stay on the same sloping plane.
        auto sidePoint=[&](float t,float z0,float z1) {
          float lower=lerpf(.90f,.91f,(z0+1.45f)/2.60f),upper=lerpf(1.46f,1.43f,(z1+.78f)/1.34f);
          return vec3(side*lerpf(.80f,.64f,t),lerpf(lower,upper,t),lerpf(z0,z1,t));
        };
        if(d1) {
          mb.quad(sidePoint(0,d0?-1.32f:-1.45f,d0?-.67f:-.78f),sidePoint(0,-.13f,-.13f),sidePoint(1,-.13f,-.13f),sidePoint(1,d0?-1.32f:-1.45f,d0?-.67f:-.78f),P_GLASS,{0,1,0});
          mb.quad(sidePoint(0,-.04f,-.04f),sidePoint(0,d0?1.06f:1.15f,d0?.47f:.56f),sidePoint(1,d0?1.06f:1.15f,d0?.47f:.56f),sidePoint(1,-.04f,-.04f),P_GLASS,{0,1,0});
          mb.quad(sidePoint(0,-.13f,-.13f),sidePoint(0,-.04f,-.04f),sidePoint(1,-.04f,-.04f),sidePoint(1,-.13f,-.13f),P_PAINT,{0,1,0});
        } else mb.quad(sidePoint(0,-1.45f,-.78f),sidePoint(0,1.15f,.56f),sidePoint(1,1.15f,.56f),sidePoint(1,-1.45f,-.78f),P_GLASS,{0,1,0});
        if(d0) {
          // Painted A/C pillars meet the glazing without coplanar overlays.
          mb.quad(sidePoint(0,-1.45f,-.78f),sidePoint(0,-1.32f,-.67f),sidePoint(1,-1.32f,-.67f),sidePoint(1,-1.45f,-.78f),P_PAINT,{0,1,0});
          mb.quad(sidePoint(0,1.06f,.47f),sidePoint(0,1.15f,.56f),sidePoint(1,1.15f,.56f),sidePoint(1,1.06f,.47f),P_PAINT,{0,1,0});
          // Sill and inset front door handles, within the existing 1.8 m body envelope.
          mb.box({side*.876f-.002f,.36f,-1.02f},{side*.876f+.002f,.43f,1.03f},P_DARK,side<0?1:2);
          for(float dz:{.53f}) mb.box({side*.867f-.005f,.79f,dz-.11f},{side*.867f+.005f,.84f,dz+.11f},P_METAL,side<0?1:2);
        }
      }
      if(d1) {
        for(int i=0;i<4;++i) wheel(mb,{(i&1)?.8f:-.8f,.31f,(i&2)?1.35f:-1.35f},.31f,.2f,d0?8:5,false);
        // Lamps are non-emissive lenses on parked cars, with a dark grille separating them.
        for(int side=-1;side<=1;side+=2) {
          float x0=side<0?-.68f:.34f,x1=side<0?-.34f:.68f;
          mb.box({x0,.5f,2.199f},{x1,.62f,2.2f},P_METAL,32);
          mb.box({x0,.48f,-2.2f},{x1,.59f,-2.199f},P_STRIPE,16);
        }
        mb.box({-.29f,.40f,2.199f},{.29f,.55f,2.2f},P_DARK,32);
      }
      break;
    }
    case EK_TRUCK: {   // three-axle fuel bowser: cab-over profile, tank shoulders and visible chassis
      if(lod==3){airportTruckDetailed(mb);break;}
      if(lod==2) {
        // Keep the shaped cab at distance, but collapse tank shoulders and hidden chassis faces.
        // Exactly the previous 56-triangle far budget; far populations do not inherit near detail.
        mb.box({-1.16f,.45f,-4.4f},{1.16f,.78f,4.4f},P_DARK,8|16|32);
        mb.box({-1.25f,.85f,2.6f},{1.25f,1.85f,4.5f},P_PAINT,1|2|16|32);
        vec3 ref(0,1.8f,3.5f);
        mb.quad({-1.25f,1.85f,4.5f},{1.25f,1.85f,4.5f},{1.10f,2.9f,4.28f},{-1.10f,2.9f,4.28f},P_GLASS,ref);
        mb.quad({-1.25f,1.85f,2.6f},{1.25f,1.85f,2.6f},{1.10f,2.9f,2.64f},{-1.10f,2.9f,2.64f},P_PAINT,ref);
        mb.quad({-1.10f,2.9f,2.64f},{1.10f,2.9f,2.64f},{1.10f,2.9f,4.28f},{-1.10f,2.9f,4.28f},P_PAINT,ref);
        for(int side=-1;side<=1;side+=2) mb.quad({side*1.25f,1.85f,2.6f},{side*1.25f,1.85f,4.5f},{side*1.10f,2.9f,4.28f},{side*1.10f,2.9f,2.64f},P_PAINT,ref);
        mb.cyl({0,1.95f,-4.3f},{0,1.95f,2.4f},1.1f,1.1f,8,P_METAL,true,true);
        break;
      }
      mb.box({-1.16f,.45f,-4.4f},{1.16f,.78f,4.4f},P_DARK,0x3B);
      mb.box({-1.25f,.85f,2.6f},{1.25f,1.85f,4.5f},P_PAINT,1|2|16|32);
      // The cab narrows above the belt line. Raked windshield remains readable at every LOD.
      vec3 ref(0,1.8f,3.5f);
      mb.quad({-1.25f,1.85f,4.5f},{1.25f,1.85f,4.5f},{1.10f,2.78f,4.28f},{-1.10f,2.78f,4.28f},P_GLASS,ref);
      mb.quad({-1.25f,1.85f,2.6f},{1.25f,1.85f,2.6f},{1.10f,2.78f,2.64f},{-1.10f,2.78f,2.64f},P_PAINT,ref);
      mb.box({-1.12f,2.78f,2.62f},{1.12f,2.9f,4.3f},P_PAINT,8|1|2|16|32);
      for(int side=-1;side<=1;side+=2) {
        mb.quad({side*1.25f,1.85f,2.6f},{side*1.25f,1.85f,4.5f},{side*1.10f,2.78f,4.28f},{side*1.10f,2.78f,2.64f},P_PAINT,ref);
        if(d1) mb.quad({side*1.237f,1.99f,2.82f},{side*1.237f,1.99f,4.18f},{side*1.122f,2.68f,4.07f},{side*1.122f,2.68f,2.85f},P_GLASS,ref);
      }
      int sg=d0?12:8;
      airportTankerShell(mb,sg,d0?4:3);
      if(d1) {
        for(int i=0;i<6;++i) wheel(mb,{(i&1)?1.f:-1.f,.48f,i<2?3.4f:i<4?-2.4f:-3.6f},.48f,.35f,d0?8:5,false);
        mb.box({-.94f,1.04f,4.501f},{.94f,1.50f,4.52f},P_DARK,32);
        for(int side=-1;side<=1;side+=2) {float x0=side<0?-1.17f:.78f,x1=side<0?-.78f:1.17f;
          mb.box({x0,.89f,4.501f},{x1,1.03f,4.52f},P_METAL,32);}
      }
      if(d0) {
        // Tank straps follow the cylinder rather than adding another full tank surface.
        for(float z:{-2.9f,.9f}) mb.cyl({0,1.95f,z-.055f},{0,1.95f,z+.055f},1.105f,1.105f,sg,P_DARK,false,false);
        for(int side=-1;side<=1;side+=2) { float x0=side<0?-1.24f:1.05f,x1=side<0?-1.05f:1.24f;
          mb.box({x0,.66f,2.75f},{x1,.83f,3.18f},P_METAL,8|16|32);}
        mb.box({-.32f,3.035f,-.6f},{.32f,3.045f,.05f},P_DARK,8);
      }
      break;
    }
    case EK_FENCE: {   // 20 m of chain-link fence: post at -x, top rail, mesh panel
      mb.cyl(vec3(-10.f, -0.3f, 0), vec3(-10.f, 2.45f, 0), 0.045f, 0.04f, 4, P_METAL, true, false);
      if (d1) mb.cyl(vec3(-10.f, 2.35f, 0), vec3(10.f, 2.35f, 0), 0.025f, 0.025f, 4, P_METAL, false, false);
      if (d0) mb.cyl(vec3(0.f, -0.3f, 0), vec3(0.f, 2.45f, 0), 0.035f, 0.03f, 4, P_METAL, false, false);
      mb.quad(vec3(-10.f, 0.f, 0), vec3(10.f, 0.f, 0), vec3(10.f, 2.35f, 0), vec3(-10.f, 2.35f, 0), P_FENCE, vec3(0, 1.f, -1.f));
      break;
    }
    case EK_LOCALIZER: {   // ILS localizer: a row of antenna elements on a low frame, facing the runway (+z)
      mb.box(vec3(-17.f, -0.4f, -0.35f), vec3(17.f, 0.35f, 0.35f), P_METAL, 0x3F);
      int n = lod == 3 ? 14 : lod == 0 ? 14 : lod == 1 ? 7 : 0;
      for (int i = 0; i < n; i++) {
        float x = -16.f + i * 32.f / std::max(n - 1, 1);
        mb.box(vec3(x - 0.05f, 0.35f, -0.05f), vec3(x + 0.05f, 2.6f, 0.05f), P_METAL);
        mb.box(vec3(x - 0.5f, 2.3f, 0.05f), vec3(x + 0.5f, 3.f, 0.35f), P_TRIM, 0x3F);
      }
      if (lod == 2) mb.box(vec3(-16.f, 0.35f, -0.1f), vec3(16.f, 3.f, 0.3f), P_TRIM, 0x3F);
      break;
    }
    case EK_RADAR: {   // radome on a lattice tower
      for (int i = 0; i < 4; i++) { float sx = (i & 1) ? 1.f : -1.f, sz = (i & 2) ? 1.f : -1.f;
        mb.cyl(vec3(sx * 3.6f, -0.5f, sz * 3.6f), vec3(sx * 1.6f, 15.f, sz * 1.6f), 0.14f, 0.11f, 4, P_METAL, false, false); }
      if (d0) for (int k = 0; k < 4; k++) { float y = 2.f + k * 3.3f, r = lerpf(3.6f, 1.6f, y / 15.f);
        mb.box(vec3(-r, y, -r), vec3(r, y + 0.12f, -r + 0.12f), P_METAL); mb.box(vec3(-r, y, r - 0.12f), vec3(r, y + 0.12f, r), P_METAL);
        mb.box(vec3(-r, y, -r), vec3(-r + 0.12f, y + 0.12f, r), P_METAL); mb.box(vec3(r - 0.12f, y, -r), vec3(r, y + 0.12f, r), P_METAL); }
      mb.box(vec3(-2.2f, 15.f, -2.2f), vec3(2.2f, 15.6f, 2.2f), P_TRIM, 0x3F);
      blob(mb, lod == 3 ? 3 : lod == 0 ? 2 : 1, [](vec3 d) { return vec3(d.x * 3.7f, 19.f + d.y * 3.5f, d.z * 3.7f); }, [](vec3, vec3) { return 1.f; }, P_TRIM);
      break;
    }
    case EK_MAST: {   // antenna mast (glide slope / radio): banded tube, antenna panels facing +z, equipment hut
      mb.box(vec3(-1.4f, -0.5f, -1.5f), vec3(1.4f, 2.5f, -0.3f), P_WALL, 0x3B);
      mb.cyl(vec3(0, 0, 0.5f), vec3(0, 12.f, 0.5f), 0.16f, 0.12f, d0 ? 8 : 5, P_OBST, false, true);
      if (d1) for (int i = 0; i < 3; i++) mb.box(vec3(-0.35f, 4.f + i * 3.f, 0.65f), vec3(0.35f, 5.5f + i * 3.f, 0.8f), P_TRIM, 0x3F);
      break;
    }
    case EK_FLOODMAST: {   // apron floodlight mast: lamp head aimed down and forward (+z)
      mb.cyl(vec3(0, -0.5f, 0), vec3(0, 19.f, 0), 0.32f, 0.18f, d0 ? 10 : 6, P_METAL, false, false);
      if(lod!=3) mb.box(vec3(-1.4f, 19.f, -0.35f), vec3(1.4f, 19.9f, 0.35f), P_DARK, 0x3F);
      for (int i = 0; i < 4; i++) { float x = -1.05f + i * 0.7f;
        mb.quad(vec3(x - 0.3f, 18.98f, -0.25f), vec3(x + 0.3f, 18.98f, -0.25f), vec3(x + 0.3f, 18.98f, 0.3f), vec3(x - 0.3f, 18.98f, 0.3f), P_LAMP, vec3(0, 19.5f, 0)); }
      break;
    }
  }
  if(lod==3){airportHeroAccents(mb,kind);for(size_t i=first;i<mb.o.size();++i)if(mb.o[i].part==P_WALL)mb.o[i].u=1.f;}
}
