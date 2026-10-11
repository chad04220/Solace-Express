// Included inside entity_mesh.cpp anonymous namespace; shared MB helpers are available.
void plinth(MB& mb, float hx, float hz, float top = 0.45f) { mb.box(vec3(-hx - 0.12f, -3.f, -hz - 0.12f), vec3(hx + 0.12f, top, hz + 0.12f), P_TRIM, 0x3B); }
void door(MB& mb, float x, float z, float w, float h, float y0 = 0.45f, bool frontZ = true) {
  if (frontZ) mb.box(vec3(x - w * 0.5f, y0, z - 0.02f), vec3(x + w * 0.5f, y0 + h, z + 0.06f), P_DOOR, 0x3B);
  else mb.box(vec3(z - 0.02f, y0, x - w * 0.5f), vec3(z + 0.06f, y0 + h, x + w * 0.5f), P_DOOR, 0x3B);
}

// Thin roof-edge boards follow the actual roof pitch. Six strips (12 triangles) give
// eaves thickness, readable construction and a shadow line without tiled geometry.
void gableFascia(MB& mb, float x0, float x1, float z0, float z1, float e, float r, bool alongZ, float ov) {
  float half = alongZ ? (x1 - x0) * 0.5f : (z1 - z0) * 0.5f;
  float eo = e - ov * (r - e) / half;
  vec3 ref((x0+x1)*0.5f, e-1.f, (z0+z1)*0.5f), down(0, -0.16f, 0);
  auto strip = [&](vec3 a, vec3 b) { mb.quad(a, b, b+down, a+down, P_TRIM, ref, 0.8f); };
  if (alongZ) {
    for (float x : {x0-ov, x1+ov}) strip({x,eo,z0-ov}, {x,eo,z1+ov});
    for (float z : {z0-ov, z1+ov}) { strip({x0-ov,eo,z}, {(x0+x1)*0.5f,r,z}); strip({(x0+x1)*0.5f,r,z}, {x1+ov,eo,z}); }
  } else {
    for (float z : {z0-ov, z1+ov}) strip({x0-ov,eo,z}, {x1+ov,eo,z});
    for (float x : {x0-ov, x1+ov}) { strip({x,eo,z0-ov}, {x,r,(z0+z1)*0.5f}); strip({x,r,(z0+z1)*0.5f}, {x,eo,z1+ov}); }
  }
}

// Construction accents are bounded by existing roofs, plinths and porches. Windows remain
// shader-driven so their per-instance scale/layout cannot disagree with fixed mesh panes.
void buildingAccents(MB& mb, int kind, int lod) {
  if (lod != 0) return;
  switch (kind) {
    case EK_HOUSE:
      gableFascia(mb,-4.5f,4.5f,-5.5f,5.5f,5.6f,8.6f,true,0.5f);
      for (float z : {-5.512f,5.512f}) mb.box({-.48f,6.6f,z-.005f},{.48f,7.25f,z+.005f},P_DARK,z<0?16:32);
      // Three treads, each with just its top and outward riser.
      for (int i=0;i<3;++i) { float y=.15f*(i+1),z=6.7f-.27f*i; mb.box({-2.35f,y-.15f,z-.27f},{-.85f,y,z},P_TRIM,8|32,.8f); }
      break;
    case EK_HOUSE_HIP:
      for (int side=-1;side<=1;side+=2) {
        mb.box({-5.6f,3.09f,side*5.6f-.005f},{5.6f,3.25f,side*5.6f+.005f},P_TRIM,side<0?16:32,.8f);
        mb.box({side*5.6f-.005f,3.09f,-5.6f},{side*5.6f+.005f,3.25f,5.6f},P_TRIM,side<0?1:2,.8f);
      }
      break;
    case EK_HOUSE_L:
      gableFascia(mb,-6,6,-6,.5f,5.2f,8.2f,false,.45f);
      gableFascia(mb,-6,-.6f,.5f,6,5.2f,7.9f,true,.45f);
      break;
    case EK_FARMHOUSE:
      gableFascia(mb,-5.5f,5.5f,-4.5f,4.5f,6.2f,9,false,.5f);
      // Porch beam and two rails, leaving the centre doorway clear.
      mb.box({-4.9f,2.64f,5.9f},{4.9f,2.84f,6.08f},P_TRIM,1|2|4|32,.8f);
      for (int side=-1;side<=1;side+=2) {
        float x0=side<0?-4.75f:1.f,x1=side<0?-1.f:4.75f;
        mb.box({x0,1.37f,5.94f},{x1,1.48f,6.04f},P_WOOD,8|16|32);
        for(int i=0;i<4;++i) { float x=lerpf(x0,x1,i/3.f);mb.box({x-.035f,.55f,5.98f},{x+.035f,1.4f,6.02f},P_TRIM,16|32); }
      }
      break;
    case EK_TOWNHOUSE:
      // Raised party-wall piers and a continuous lintel establish each narrow frontage.
      for(float x : {-8.85f,-3.f,3.f,8.85f}) mb.box({x-.10f,.6f,5.5f},{x+.10f,8.9f,5.6f},P_TRIM,1|2|32,.82f);
      break;
    case EK_WAREHOUSE:
      gableFascia(mb,-12,12,-9,9,6.6f,8.5f,false,.4f);
      for(float x : {-7.18f,-2.82f,.82f,5.18f}) mb.box({x-.08f,1.1f,9.05f},{x+.08f,5.35f,9.16f},P_METAL,1|2|32);
      mb.box({-8.f,5.4f,9.f},{7.f,5.56f,10.45f},P_METAL,8|4|32,.85f);
      break;
    case EK_BARN:
      // Cross-braced timber doors, a broad visual cue at approach distance.
      for (int side=-1;side<=1;side+=2) {
        float x=side*2.05f;
        mb.quad({x,.55f,9.15f},{x-side*.12f,.55f,9.15f},{side*.06f,3.9f,9.15f},{side*.18f,3.9f,9.15f},P_TRIM,{0,2,9});
      }
      break;
    case EK_CHURCH:
      gableFascia(mb,-5,5,-13,5,7,13,true,.4f);
      for(int side=-1;side<=1;side+=2) for(float z : {-9.6f,-3.2f,3.2f}) {
        float x0=side<0?-5.36f:5.f,x1=side<0?-5.f:5.36f;
        mb.box({x0,.5f,z-.28f},{x1,5.8f,z+.28f},P_TRIM,0x3B,.82f);
      }
      break;
    default: break;
  }
}

// Architectural construction belongs to the closest mesh only. The three original
// meshes retain their inexpensive silhouette; this mesh uses openings in the wall,
// not dark panels laid on top of an unbroken surface. Distances are nominal metres.
// P_WALL aux.u=1 disables the procedural window grid. Fitted P_GLASS uses pane-local
// aux.u=1..2 / aux.v=0..1, shared with the building material shader.
struct BFace {
  vec3 o, t, n;
  BFace(vec3 origin, vec3 tangent, vec3 normal) : o(origin), t(tangent), n(normal) {}
  vec3 p(float u, float y, float d=0) const { return o+t*u+vec3(0,y,0)+n*d; }
  void panel(MB& mb,float u0,float y0,float u1,float y1,float d,int part,float ao=1,bool pane=false) const {
    if(u1-u0<1e-5f||y1-y0<1e-5f)return;
    vec3 p0=p(u0,y0,d),p1=p(u1,y0,d),p2=p(u1,y1,d),p3=p(u0,y1,d);
    vec3 q[4]={p0,p1,p2,p3};int ix[6]={0,1,2,0,2,3};
    for(int k:ix)mb.vert(q[k],n,part,ao,pane?((k==1||k==2)?2.f:1.f):(part==P_WALL?1.f:0.f),pane&&(k>=2)?1.f:0.f);
  }
  void block(MB& mb,float u0,float y0,float d0,float u1,float y1,float d1,int part,float ao=1,bool back=false) const {
    vec3 a=p(u0,y0,d0),b=p(u1,y0,d0),c=p(u1,y1,d0),d=p(u0,y1,d0);
    vec3 e=p(u0,y0,d1),f=p(u1,y0,d1),g=p(u1,y1,d1),h=p(u0,y1,d1),ref=(a+g)*.5f;
    mb.quad(a,e,h,d,part,ref,ao);mb.quad(f,b,c,g,part,ref,ao);
    mb.quad(a,b,f,e,part,ref,ao);mb.quad(d,h,g,c,part,ref,ao);
    panel(mb,u0,y0,u1,y1,d1,part,ao);
    if(back)mb.quad(a,d,c,b,part,ref,ao);
  }
};
BFace bFront(float z) { return BFace({0,0,z},{1,0,0},{0,0,1}); }
BFace bBack(float z) { return BFace({0,0,z},{1,0,0},{0,0,-1}); }
BFace bRight(float x) { return BFace({x,0,0},{0,0,1},{1,0,0}); }
BFace bLeft(float x) { return BFace({x,0,0},{0,0,1},{-1,0,0}); }
struct BOpening { float u0,y0,u1,y1;int type; }; // 0 sash, 1 door, 2 louvre, 3 sectional door, 4 shop glazing
struct BWall { BFace f;float u0,y0,u1,y1;int part;std::vector<BOpening> openings; };
void bOpening(BWall& w,float u,float y,float width,float height,int type=0) {
  w.openings.push_back({u-width*.5f,y,u+width*.5f,y+height,type});
}
void bRow(BWall& w,float first,float step,int count,float y,float width,float height,int type=0) {
  for(int i=0;i<count;++i)bOpening(w,first+i*step,y,width,height,type);
}
void bBorder(MB& mb,const BFace& f,float x0,float y0,float x1,float y1,float thickness,float depth,int part,float ao=1) {
  f.panel(mb,x0,y0,x0+thickness,y1,depth,part,ao);f.panel(mb,x1-thickness,y0,x1,y1,depth,part,ao);
  f.panel(mb,x0+thickness,y0,x1-thickness,y0+thickness,depth,part,ao);f.panel(mb,x0+thickness,y1-thickness,x1-thickness,y1,depth,part,ao);
}
void bFitOpening(MB& mb,const BFace& f,const BOpening& a) {
  const float x0=a.u0,x1=a.u1,y0=a.y0,y1=a.y1,depth=a.type==1?.19f:.16f;
  // Four plaster/stone reveal returns terminate at a genuinely inset sash.
  mb.quad(f.p(x0,y0),f.p(x0,y1),f.p(x0,y1,-depth),f.p(x0,y0,-depth),P_TRIM,f.p(x0-.1f,(y0+y1)*.5f,-depth*.5f),.69f);
  mb.quad(f.p(x1,y0),f.p(x1,y1),f.p(x1,y1,-depth),f.p(x1,y0,-depth),P_TRIM,f.p(x1+.1f,(y0+y1)*.5f,-depth*.5f),.75f);
  mb.quad(f.p(x0,y1),f.p(x1,y1),f.p(x1,y1,-depth),f.p(x0,y1,-depth),P_TRIM,f.p((x0+x1)*.5f,y1+.1f,-depth*.5f),.60f);
  mb.quad(f.p(x0,y0),f.p(x1,y0),f.p(x1,y0,-depth),f.p(x0,y0,-depth),P_TRIM,f.p((x0+x1)*.5f,y0-.1f,-depth*.5f),.82f);
  float sash=a.type==4?.045f:.055f;
  int frame=a.type==4||a.type==3?P_METAL:P_TRIM;
  if(a.type!=5)bBorder(mb,f,x0-.045f,y0-.045f,x1+.045f,y1+.045f,.045f,.009f,P_TRIM,.93f);
  bBorder(mb,f,x0,y0,x1,y1,sash,-depth+.025f,frame,.8f);
  const float ix0=x0+sash,ix1=x1-sash,iy0=y0+sash,iy1=y1-sash;
  if(a.type==0||a.type==4||a.type==5||a.type==6) {
    f.panel(mb,ix0,iy0,ix1,iy1,-depth,P_GLASS,.85f,true);
    if(a.type==6) {
      // Stone-filled corner pieces turn the rectangular structural opening into a
      // true round-arched lancet. Glazing remains behind the exposed arch ring.
      float xm=(x0+x1)*.5f,r=(x1-x0)*.5f,spring=y1-r;
      for(int i=0;i<10;++i) {
        float a0=i*PI/10,a1=(i+1)*PI/10;
        vec3 p0=f.p(xm+cosf(a0)*r,spring+sinf(a0)*r),p1=f.p(xm+cosf(a1)*r,spring+sinf(a1)*r);
        vec3 q0=f.p(xm+cosf(a0)*(r-.07f),spring+sinf(a0)*(r-.07f),-.10f),q1=f.p(xm+cosf(a1)*(r-.07f),spring+sinf(a1)*(r-.07f),-.10f);
        mb.quad(p0,p1,q1,q0,P_TRIM,f.p(xm,spring,-.2f),.83f);
        if(i!=4&&i!=5)mb.quad(p1,p0,f.p(xm+cosf(a0)*r,y1),f.p(xm+cosf(a1)*r,y1),P_WALL,f.p(xm,spring,-.2f));
        else {vec3 top=f.p(xm,y1);if(i==4)mb.tri(p0,f.p(xm+cosf(a0)*r,y1),top,P_WALL,f.p(xm,spring,-.2f));else mb.tri(p1,top,f.p(xm+cosf(a1)*r,y1),P_WALL,f.p(xm,spring,-.2f));}
      }
      f.panel(mb,xm-.022f,iy0,xm+.022f,spring+.18f,-depth+.045f,P_METAL,.81f);
    }
    if(a.type==0) {
      float ym=lerpf(iy0,iy1,.50f);
      f.block(mb,ix0,ym-.022f,-depth+.005f,ix1,ym+.022f,-depth+.052f,frame,.83f);
      if(x1-x0>1.65f)f.block(mb,(x0+x1)*.5f-.025f,iy0,-depth+.005f,(x0+x1)*.5f+.025f,iy1,-depth+.052f,frame,.82f);
      // A stone sill has an exposed, pitched water-shedding top and dark drip edge.
      f.block(mb,x0-.085f,y0-.075f,-.03f,x1+.085f,y0-.025f,.10f,P_TRIM,.86f);
      f.panel(mb,x0-.07f,y0-.084f,x1+.07f,y0-.075f,.075f,P_DARK,.73f);
    }
  } else if(a.type==2) {
    f.panel(mb,ix0,iy0,ix1,iy1,-depth,P_DARK,.65f);
    int slats=std::max(3,(int)((iy1-iy0)/.13f));
    for(int i=0;i<slats;++i) { float y=lerpf(iy0+.04f,iy1-.04f,i/float(slats-1));
      mb.quad(f.p(ix0,y-.025f,-.09f),f.p(ix1,y-.025f,-.09f),f.p(ix1,y+.028f,-.13f),f.p(ix0,y+.028f,-.13f),P_METAL,f.p((x0+x1)*.5f,y-.1f,-.15f),.82f); }
  } else if(a.type==3||a.type==7) {
    f.panel(mb,ix0,iy0,ix1,iy1,-depth,P_DOOR,.83f);
    if(a.type==3)for(float y=iy0+.48f;y<iy1;y+=.48f)f.panel(mb,ix0,y-.012f,ix1,y+.012f,-depth+.012f,P_METAL,.70f);
    f.block(mb,x0-.07f,y0,-.01f,x0-.025f,y1+.10f,.07f,P_METAL,.8f);
    f.block(mb,x1+.025f,y0,-.01f,x1+.07f,y1+.10f,.07f,P_METAL,.8f);
  } else {
    f.panel(mb,ix0,iy0,ix1,iy1,-depth,P_DOOR,.84f);
    float xm=(x0+x1)*.5f, width=ix1-ix0;
    for(int row=0;row<2;++row)for(int col=0;col<2;++col) {
      float px0=ix0+.09f+col*width*.5f,px1=ix0+(col+1)*width*.5f-.09f;
      float py0=iy0+.15f+row*(iy1-iy0)*.43f,py1=py0+(iy1-iy0)*.32f;
      if(px1>px0)bBorder(mb,f,px0,py0,px1,py1,.035f,-depth+.014f,P_TRIM,.55f);
    }
    // A small transom, lever and threshold provide human-scale construction cues.
    if(y1-y0>2.1f)f.panel(mb,ix0+.09f,iy1-.34f,ix1-.09f,iy1-.10f,-depth+.008f,P_GLASS,.82f,true);
    vec3 h=f.p(x1-.20f,y0+1.02f,-depth+.07f);
    mb.cyl(h-f.t*.08f,h+f.t*.055f,.022f,.022f,6,P_METAL,true,true,.8f,.9f);
    f.block(mb,x0-.025f,y0-.025f,-.10f,x1+.025f,y0+.025f,.10f,P_TRIM,.9f);
    (void)xm;
  }
}
void bBuildWall(MB& mb,const BWall& w) {
  std::vector<float> cuts={w.y0,w.y1};
  for(const auto&a:w.openings) { cuts.push_back(std::max(w.y0,a.y0));cuts.push_back(std::min(w.y1,a.y1)); }
  std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
  for(size_t i=1;i<cuts.size();++i) {
    float lo=cuts[i-1],hi=cuts[i];if(hi-lo<1e-5f)continue;
    std::vector<std::pair<float,float>> spans;
    for(const auto&a:w.openings)if(a.y0<hi-1e-5f&&a.y1>lo+1e-5f)spans.push_back({a.u0,a.u1});
    std::sort(spans.begin(),spans.end());float x=w.u0;
    for(auto ab:spans) { w.f.panel(mb,x,lo,std::min(ab.first,w.u1),hi,0,w.part);x=std::max(x,ab.second); }
    w.f.panel(mb,x,lo,w.u1,hi,0,w.part);
  }
  for(const auto&a:w.openings)bFitOpening(mb,w.f,a);
}
// A thin formed metal rain gutter follows the eave instead of a solid oversized tube.
void bGutter(MB& mb,vec3 a,vec3 b,vec3 out) {
  vec3 up(0,.065f,0), lip=out*.105f,ref=(a+b)*.5f-out*.1f;
  mb.quad(a,b,b+lip,a+lip,P_METAL,ref,.80f);
  mb.quad(a+lip,b+lip,b+lip+up,a+lip+up,P_METAL,ref,.85f);
  mb.quad(a,a+up,b+up,b,P_METAL,ref,.74f);
}
void bDownpipe(MB& mb,vec3 top,vec3 base,vec3 out) {
  vec3 bend(base.x,top.y-.3f,base.z),foot=base+out*.09f;
  mb.cyl(top,bend,.047f,.047f,6,P_METAL,false,false,.85f,.8f);
  mb.cyl(bend,base,.047f,.047f,6,P_METAL,false,false,.8f,.7f);
  mb.cyl(base,foot+vec3(0,-.08f,0),.047f,.047f,6,P_METAL,true,false,.7f,.7f);
  for(float s:{.28f,.76f}) { vec3 p=lerp(bend,base,s);mb.cyl(p-vec3(0,.025f,0),p+vec3(0,.025f,0),.06f,.06f,6,P_TRIM,false,false,.8f,.8f); }
}
void bGableConstruction(MB& mb,float x0,float x1,float z0,float z1,float e,float r,bool alongZ,float ov) {
  gableFascia(mb,x0,x1,z0,z1,e,r,alongZ,ov);
  float half=alongZ?(x1-x0)*.5f:(z1-z0)*.5f,eo=e-ov*(r-e)/half;
  if(alongZ) {
    for(float x:{x0-ov,x1+ov}) { float sign=x<x0?-1.f:1.f;vec3 out(sign,0,0);
      mb.quad({x,eo-.16f,z0-ov},{x,eo-.16f,z1+ov},{x-sign*ov,e-.16f,z1+ov},{x-sign*ov,e-.16f,z0-ov},P_TRIM,{(x0+x1)*.5f,e,z0},.68f);
      // Keep the trough inside the established roof footprint.
      bGutter(mb,{x-sign*.12f,eo-.07f,z0-.10f},{x-sign*.12f,eo-.07f,z1+.10f},out);
      float z=z0+.45f;bDownpipe(mb,{x-sign*.12f,eo-.07f,z},{x-sign*(ov-.09f),.38f,z},out);
    }
    // Low-profile folded ridge cover sits on the existing ridge; no height increase.
    for(int s:{-1,1})mb.quad({(x0+x1)*.5f,r,z0-ov},{(x0+x1)*.5f,r,z1+ov},{(x0+x1)*.5f+s*.12f,r-.064f,z1+ov},{(x0+x1)*.5f+s*.12f,r-.064f,z0-ov},P_METAL,{(x0+x1)*.5f,r-.3f,(z0+z1)*.5f},.88f);
  } else {
    for(float z:{z0-ov,z1+ov}) { float sign=z<z0?-1.f:1.f;vec3 out(0,0,sign);
      mb.quad({x0-ov,eo-.16f,z},{x1+ov,eo-.16f,z},{x1+ov,e-.16f,z-sign*ov},{x0-ov,e-.16f,z-sign*ov},P_TRIM,{x0,e,(z0+z1)*.5f},.68f);
      bGutter(mb,{x0-.10f,eo-.07f,z-sign*.12f},{x1+.10f,eo-.07f,z-sign*.12f},out);
      float x=x0+.45f;bDownpipe(mb,{x,eo-.07f,z-sign*.12f},{x,.38f,z-sign*(ov-.09f)},out);
    }
    for(int s:{-1,1})mb.quad({x0-ov,r,(z0+z1)*.5f},{x1+ov,r,(z0+z1)*.5f},{x1+ov,r-.064f,(z0+z1)*.5f+s*.12f},{x0-ov,r-.064f,(z0+z1)*.5f+s*.12f},P_METAL,{(x0+x1)*.5f,r-.3f,(z0+z1)*.5f},.88f);
  }
}
void bChimneyCap(MB& mb,float x0,float x1,float z0,float z1,float y) {
  // Stone coping and a covered dark flue remain below the original chimney top.
  mb.box({x0-.04f,y-.28f,z0-.04f},{x1+.04f,y-.20f,z1+.04f},P_TRIM,0x3B,.88f);
  mb.box({x0+.12f,y-.20f,z0+.12f},{x1-.12f,y-.08f,z1-.12f},P_DARK,0x33,.70f);
  mb.box({x0-.02f,y-.08f,z0-.02f},{x1+.02f,y,z1+.02f},P_METAL,0x3F,.85f);
}
void bFlatCoping(MB& mb,float x0,float x1,float z0,float z1,float deck,float top) {
  // Inside parapet faces and separate weatherproof coping resolve a formerly hollow box.
  float t=.18f;
  mb.box({x0,deck,z0},{x0+t,top,z1},P_TRIM,2|8,.78f);
  mb.box({x1-t,deck,z0},{x1,top,z1},P_TRIM,1|8,.78f);
  mb.box({x0+t,deck,z0},{x1-t,top,z0+t},P_TRIM,8|32,.78f);
  mb.box({x0+t,deck,z1-t},{x1-t,top,z1},P_TRIM,8|16,.78f);
  for(int s:{-1,1}) { float z=s<0?z0:z1-.22f;mb.box({x0,top-.045f,z},{x1,top,z+.22f},P_METAL,0x3B,.9f); }
  for(int s:{-1,1}) { float x=s<0?x0:x1-.22f;mb.box({x,top-.045f,z0+.22f},{x+.22f,top,z1-.22f},P_METAL,0x3B,.9f); }
}
void bServiceUnit(MB& mb,float x0,float x1,float z0,float z1,float y0,float y1) {
  mb.box({x0,y0,z0},{x1,y1-.025f,z1},P_METAL,0x3B,.89f);
  BFace f=bFront(z1);f.panel(mb,x0+.12f,y0+.12f,x1-.12f,y1-.10f,.008f,P_DARK,.70f);
  for(float y=y0+.17f;y<y1-.09f;y+=.11f) {
    mb.quad(f.p(x0+.12f,y-.025f,.038f),f.p(x1-.12f,y-.025f,.038f),f.p(x1-.12f,y+.025f,.008f),f.p(x0+.12f,y+.025f,.008f),P_METAL,f.p((x0+x1)*.5f,y-.1f),.88f);
  }
  float r=std::min(x1-x0,z1-z0)*.31f;vec3 c((x0+x1)*.5f,y1-.016f,(z0+z1)*.5f);
  mb.cyl(c-vec3(0,.008f,0),c,r,r,12,P_DARK,true,false,.8f,.8f);
  for(int i=0;i<4;++i) { float a=i*PI*.5f;vec3 v(cosf(a)*r*.78f,.002f,sinf(a)*r*.78f),w(-sinf(a)*r*.14f,0,cosf(a)*r*.14f);mb.tri(c+w,c+v,c-w,P_METAL,c-vec3(0,.1f,0),.9f); }
}
void buildBuilding(MB& mb,int kind,int lod);
void buildDetailedBuilding(MB& mb,int kind) {
  const size_t begin=mb.o.size();
  std::vector<BWall> walls;
  auto wall=[&](BFace f,float u0,float u1,float y0,float y1,int part=P_WALL)->BWall& { walls.push_back({f,u0,y0,u1,y1,part,{}});return walls.back(); };
  switch(kind) {
    case EK_HOUSE: {
      auto&f=wall(bFront(5.5f),-4.5f,4.5f,.45f,5.6f);
      bOpening(f,-1.6f,.45f,1.1f,2.2f,1);bOpening(f,-3.45f,1.22f,1.15f,1.35f);bOpening(f,1.75f,1.22f,1.9f,1.35f);bRow(f,-2.9f,2.9f,3,3.82f,1.35f,1.35f);
      auto&b=wall(bBack(-5.5f),-4.5f,4.5f,.45f,5.6f);for(float y:{1.22f,3.82f})bRow(b,-2.9f,2.9f,3,y,1.35f,1.35f);
      for(int s:{-1,1}) { auto&w=wall(s<0?bLeft(-4.5f):bRight(4.5f),-5.5f,5.5f,.45f,5.6f);for(float y:{1.22f,3.82f})bRow(w,-3.7f,2.5f,4,y,1.25f,1.35f); }
      break;
    }
    case EK_HOUSE_HIP: {
      auto&f=wall(bFront(5),-5,5,.45f,3.4f);bOpening(f,1.5f,.45f,1,2.1f,1);bOpening(f,-2.7f,1.15f,2.35f,1.35f);bOpening(f,3.55f,1.15f,1.15f,1.35f);
      for(int s=0;s<3;++s) { auto&w=wall(s==0?bBack(-5):s==1?bLeft(-5):bRight(5),-5,5,.45f,3.4f);bRow(w,-3.2f,3.2f,3,1.15f,1.5f,1.35f); }
      break;
    }
    case EK_HOUSE_L: {
      auto&f=wall(bFront(.5f),-.6f,6,.45f,5.2f);bOpening(f,1.4f,.45f,1.1f,2.2f,1);bOpening(f,4.25f,.45f,2.65f,2.3f,3);bRow(f,1.35f,2.9f,2,3.55f,1.35f,1.3f);
      auto&b=wall(bBack(-6),-6,6,.45f,5.2f);for(float y:{1.12f,3.55f})bRow(b,-4.45f,2.95f,4,y,1.35f,1.3f);
      for(int s:{-1,1}) { auto&w=wall(s<0?bLeft(-6):bRight(6),-6,.5f,.45f,5.2f);for(float y:{1.12f,3.55f})bRow(w,-4.1f,2.7f,2,y,1.35f,1.3f); }
      auto&g=wall(bFront(6),-6,-.6f,.45f,5.2f);for(float y:{1.12f,3.55f})bRow(g,-4.65f,2.7f,2,y,1.35f,1.3f);
      for(int s:{-1,1}) { auto&w=wall(s<0?bLeft(-6):bRight(-.6f),.5f,6,.45f,5.2f);for(float y:{1.12f,3.55f})bRow(w,1.95f,2.65f,2,y,1.35f,1.3f); }
      break;
    }
    case EK_FARMHOUSE: {
      auto&f=wall(bFront(4.5f),-5.5f,5.5f,.45f,6.2f);bOpening(f,0,.55f,1.1f,2.3f,1);bOpening(f,-3.2f,1.05f,1.35f,1.6f);bOpening(f,3.2f,1.05f,1.35f,1.6f);bRow(f,-3.3f,3.3f,3,3.95f,1.15f,1.6f);
      auto&b=wall(bBack(-4.5f),-5.5f,5.5f,.45f,6.2f);for(float y:{1.05f,3.95f})bRow(b,-3.3f,3.3f,3,y,1.15f,1.6f);
      for(int s:{-1,1}) { auto&w=wall(s<0?bLeft(-5.5f):bRight(5.5f),-4.5f,4.5f,.45f,6.2f);for(float y:{1.05f,3.95f}) { bOpening(w,-2.65f,y,1.2f,1.6f);bOpening(w,2.65f,y,1.2f,1.6f); } }
      break;
    }
    case EK_TOWNHOUSE: {
      float heights[3]={9,10.5f,9.6f};
      for(int unit=0;unit<3;++unit) { float x0=-9+unit*6,h=heights[unit];
        for(int side:{-1,1}) { auto&w=wall(side>0?bFront(5.5f):bBack(-5.5f),x0,x0+6,.6f,h);
          if(side>0) { bOpening(w,x0+1.6f,.6f,1.1f,2.4f,1);bOpening(w,x0+4.35f,1.15f,1.3f,1.7f); }
          else bRow(w,x0+1.6f,2.8f,2,1.15f,1.2f,1.7f);
          for(float y:{4.f,6.9f})bRow(w,x0+1.6f,2.8f,2,y,1.2f,1.7f);
        }
      }
      for(int s:{-1,1}) { auto&w=wall(s<0?bLeft(-9):bRight(9),-5.5f,5.5f,.6f,s<0?9:9.6f);for(float y:{1.15f,4.f,6.9f})bRow(w,-3.6f,3.6f,3,y,1.2f,1.7f); }
      break;
    }
    case EK_SHOP: {
      auto&f=wall(bFront(7),-7,7,.3f,4.6f);bOpening(f,0,.4f,1.8f,2.4f,1);for(float x:{-4.55f,-2.1f,2.1f,4.55f})bOpening(f,x,.65f,2.15f,2.4f,4);
      auto&b=wall(bBack(-7),-7,7,.3f,4.6f);bOpening(b,3.8f,.3f,1.1f,2.25f,1);bOpening(b,-3.8f,2.6f,2.4f,.7f,2);
      for(int s:{-1,1})wall(s<0?bLeft(-7):bRight(7),-7,7,.3f,4.6f);
      break;
    }
    case EK_APARTMENT: {
      for(int side=0;side<4;++side) { auto&w=wall(side==0?bFront(7):side==1?bBack(-7):side==2?bLeft(-9):bRight(9),side<2?-9:-7,side<2?9:7,.5f,20.8f);
        for(int floor=0;floor<6;++floor) {
          float y=1.4f+floor*3.4f;
          if(side==0) { for(int b=0;b<3;++b) { float x=-5.8f+b*5.6f;
              if(floor==0&&b==1)continue;
              bOpening(w,x,floor==0?y:.55f+floor*3.4f,2.65f,floor==0?1.65f:2.35f,floor==0?0:4); }
          } else bRow(w,side==1?-7.f:-4.7f,side==1?2.8f:3.1f,side==1?6:4,y,1.5f,1.65f);
        }
        if(side==0)bOpening(w,0,.5f,2.2f,2.6f,1);
      }
      break;
    }
    case EK_OFFICE: {
      for(int s=0;s<4;++s) { auto&w=wall(s==0?bFront(8.6f):s==1?bBack(-8.6f):s==2?bLeft(-8.6f):bRight(8.6f),-8.6f,8.6f,.3f,4,P_GLASS);
        bRow(w,-6.65f,2.65f,6,.5f,2.47f,3.1f,4);
      }
      break;
    }
    case EK_TOWER: {
      for(int tier=0;tier<3;++tier) { float half=tier==0?10:tier==1?8:6,base=tier==0?.3f:tier==1?31:57,top=tier==0?30:tier==1?56:74;
        int columns=tier==0?8:tier==1?6:4, floors=tier==0?8:tier==1?6:4;
        for(int s=0;s<4;++s) { auto&w=wall(s==0?bFront(half):s==1?bBack(-half):s==2?bLeft(-half):bRight(half),-half,half,base,top);
          for(int floor=0;floor<floors;++floor) {
            if(tier==0&&s==0&&floor==0) {for(int col=0;col<columns;++col)if(col!=3&&col!=4)bOpening(w,-(columns-1)*1.13f+col*2.26f,base+1.f,1.5f,2.1f,5);bOpening(w,0,.3f,2.9f,3.2f,4);}
            else bRow(w,-(columns-1)*1.13f,2.26f,columns,base+1.0f+floor*3.6f,1.5f,2.1f,5);
          }
        }
      }
      break;
    }
    case EK_SKYSCRAPER: {
      for(int s=0;s<4;++s) { auto&w=wall(s==0?bFront(10.48f):s==1?bBack(-10.48f):s==2?bLeft(-10.48f):bRight(10.48f),-10.5f,10.5f,.3f,7,P_TRIM);bRow(w,-7.5f,3,6,.55f,2.55f,5.3f,4); }
      break;
    }
    case EK_WAREHOUSE: {
      auto&f=wall(bFront(9),-12,12,.6f,6.6f);bOpening(f,-5,1.1f,4,4.1f,3);bOpening(f,3,1.1f,4,4.1f,3);bOpening(f,9,1.1f,1,2.2f,1);
      for(int s=0;s<3;++s) { auto&w=wall(s==0?bBack(-9):s==1?bLeft(-12):bRight(12),s==0?-12:-9,s==0?12:9,.6f,6.6f);bRow(w,s==0?-9.f:-6.f,3, s==0?7:5,5.05f,2.35f,.7f,4); }
      break;
    }
    case EK_BARN: {
      auto&f=wall(bFront(9),-6,6,.3f,4.5f,P_WOOD);bOpening(f,0,.3f,4.4f,4,7);bOpening(f,-4.4f,2.35f,1.25f,1.25f);bOpening(f,4.4f,2.35f,1.25f,1.25f);
      auto&b=wall(bBack(-9),-6,6,.3f,4.5f,P_WOOD);bOpening(b,0,.3f,3.6f,3.7f,7);
      for(int s:{-1,1}) { auto&w=wall(s<0?bLeft(-6):bRight(6),-9,9,.3f,4.5f,P_WOOD);bRow(w,-6.3f,4.2f,4,2.15f,1.5f,1.35f); }
      break;
    }
    case EK_CHURCH: {
      auto&f=wall(bFront(5),-5,5,.5f,7);bOpening(f,-3.7f,2.05f,1.15f,3.35f,6);bOpening(f,3.7f,2.05f,1.15f,3.35f,6);
      auto&b=wall(bBack(-13),-5,5,.5f,7);bOpening(b,0,2.05f,1.7f,3.35f,6);
      for(int s:{-1,1}) { auto&w=wall(s<0?bLeft(-5):bRight(5),-13,5,.5f,7);bRow(w,-10.6f,4.4f,4,2.05f,1.25f,3.35f,6); }
      for(int s=0;s<4;++s) { auto&w=wall(s==0?bFront(10.2f):s==1?bBack(5):s==2?bLeft(-2.6f):bRight(2.6f),s<2?-2.6f:5,s<2?2.6f:10.2f,.5f,18);
        float u=s<2?0:7.6f;if(s==0)bOpening(w,u,.5f,1.8f,3.2f,1);bOpening(w,u,13.2f,1.8f,2.6f,2);
      }
      break;
    }
    case EK_LIGHTHOUSE: {
      auto&f=wall(bFront(4),-6,-3.4f,-1,4);bOpening(f,-4.7f,.15f,.95f,2.2f,1);
      auto&b=wall(bBack(-4),-6,-3.4f,-1,4);bOpening(b,-4.7f,1.2f,1.1f,1.4f);
      auto&l=wall(bLeft(-6),-4,4,-1,4);bRow(l,-2.2f,4.4f,2,1.2f,1.1f,1.4f);
      break;
    }
    case EK_GASSTATION: {
      auto&f=wall(bFront(-3.2f),-4.5f,4.5f,.15f,3.6f);bOpening(f,2.5f,.15f,1.6f,2.3f,1);bOpening(f,-2.6f,.6f,2.6f,2.15f,4);bOpening(f,.2f,.6f,2.6f,2.15f,4);
      wall(bBack(-7),-4.5f,4.5f,.15f,3.6f);wall(bLeft(-4.5f),-7,-3.2f,.15f,3.6f);wall(bRight(4.5f),-7,-3.2f,.15f,3.6f);
      break;
    }
    default:break;
  }
  // Keep all existing roof profiles, foundations, porch positions, balconies and
  // civic silhouettes. Replace only the façade faces represented above.
  std::vector<EVert> base;MB low(base);buildBuilding(low,kind,0);
  // Accents are selectively rebuilt below, avoiding doubled coplanar fascia.
  for(size_t i=0;i<base.size();i+=3) {
    int part=(int)base[i].part;bool remove=part==P_DOOR&&!walls.empty();
    for(const auto&w:walls)if(!remove&&(part==w.part||part==P_GLASS)) {
      bool match=true;
      for(int j=0;j<3;++j) { const auto&v=base[i+j];vec3 p(v.px,v.py,v.pz),delta=p-w.f.o;
        float u=dot(delta,w.f.t),d=dot(delta,w.f.n);
        if(fabsf(d)>.09f||u<w.u0-.001f||u>w.u1+.001f||v.py<w.y0-.001f||v.py>w.y1+.001f) { match=false;break; }
      }
      remove=match;
    }
    // The rear half of the L-house's front wall is internal to the intersecting wing.
    if(kind==EK_HOUSE_L&&part==P_WALL) {
      bool internal=true;for(int j=0;j<3;++j)if(fabsf(base[i+j].pz-.5f)>.001f||base[i+j].py>5.201f)internal=false;
      if(internal)remove=true;
    }
    if(kind==EK_HOUSE&&part==P_WALL) { bool gable=true;for(int j=0;j<3;++j)if(base[i+j].py<5.599f)gable=false;if(gable)remove=true; }
    if(kind==EK_HOUSE&&part==P_ROOF) {bool porch=true;for(int j=0;j<3;++j)if(base[i+j].py<2.849f||base[i+j].py>3.001f)porch=false;if(porch)remove=true;}
    if(kind==EK_FARMHOUSE&&part==P_WOOD) {bool deck=true;for(int j=0;j<3;++j)if(base[i+j].py>.551f)deck=false;if(deck)remove=true;}
    if((kind==EK_TOWNHOUSE||kind==EK_WAREHOUSE)&&part==P_TRIM) {bool stoop=true;for(int j=0;j<3;++j)if(base[i+j].py<-.001f||base[i+j].py>(kind==EK_TOWNHOUSE?.601f:1.101f)||base[i+j].pz<(kind==EK_TOWNHOUSE?5.499f:8.999f))stoop=false;if(stoop)remove=true;}
    if(kind==EK_SILO&&(part==P_METAL||part==P_DARK))remove=true;
    if(kind==EK_SHOP&&part==P_METAL&&base[i].py>=4.599f)remove=true;
    if(kind==EK_APARTMENT&&part==P_METAL&&base[i].py>=20.799f)remove=true;
    if(kind==EK_LIGHTHOUSE&&part==P_WALL) {bool tower=true;for(int j=0;j<3;++j)if(base[i+j].py<1.499f||base[i+j].py>22.001f||fabsf(base[i+j].px)>3.1f||fabsf(base[i+j].pz)>3.1f)tower=false;if(tower)remove=true;}
    if(!remove)for(int j=0;j<3;++j) { EVert v=base[i+j];if(part==P_WALL)v.u=1;
      if(kind==EK_TOWNHOUSE&&part==P_WALL&&v.ny>.5f)v.part=P_DARK;
      // Seat porch posts inside the front beam, safely below the sloping roof.
      if(kind==EK_FARMHOUSE&&part==P_TRIM&&fabsf(v.py-2.9f)<.001f&&v.pz>=5.899f&&v.pz<=6.081f)v.py=2.82f;
      if(part==P_BRICK) {float top=kind==EK_HOUSE?9.4f:kind==EK_HOUSE_HIP?6.4f:kind==EK_HOUSE_L?9.f:kind==EK_FARMHOUSE?10.f:-1; if(fabsf(v.py-top)<.001f)v.py-=.28f;}
      mb.o.push_back(v); }
  }
  for(const auto&w:walls)bBuildWall(mb,w);
  switch(kind) {
    case EK_HOUSE: {
      bGableConstruction(mb,-4.5f,4.5f,-5.5f,5.5f,5.6f,8.6f,true,.5f);
      bChimneyCap(mb,2.1f,2.9f,-1.4f,-.6f,9.4f);
      auto roofPoint=[](float x,float z){return vec3(x,8.6f-x*(2.f/3.f)+.012f,z);};
      for(float z:{-1.46f,-.6f})mb.quad(roofPoint(2.03f,z),roofPoint(2.97f,z),roofPoint(2.97f,z+.06f),roofPoint(2.03f,z+.06f),P_METAL,{2.5f,6,-1},.80f);
      for(float x:{2.03f,2.9f})mb.quad(roofPoint(x,-1.4f),roofPoint(x+.07f,-1.4f),roofPoint(x+.07f,-.6f),roofPoint(x,-.6f),P_METAL,{2.5f,6,-1},.80f);
      // Gable wall is triangulated around the vent, not left behind the grille.
      for(int s:{-1,1}) { BFace f=s<0?bBack(-5.5f):bFront(5.5f);float ys[4]={5.6f,6.6f,7.25f,8.6f};
        for(int i=0;i<3;++i) {float y0=ys[i],y1=ys[i+1],w0=(8.6f-y0)*1.5f,w1=(8.6f-y1)*1.5f;vec3 ref=f.p(0,6,-1);
          if(i==1) {mb.quad(f.p(-w0,y0),f.p(-.45f,y0),f.p(-.45f,y1),f.p(-w1,y1),P_WALL,ref);mb.quad(f.p(.45f,y0),f.p(w0,y0),f.p(w1,y1),f.p(.45f,y1),P_WALL,ref);}
          else if(i==2)mb.tri(f.p(-w0,y0),f.p(w0,y0),f.p(0,y1),P_WALL,ref);
          else mb.quad(f.p(-w0,y0),f.p(w0,y0),f.p(w1,y1),f.p(-w1,y1),P_WALL,ref);
        }
        bFitOpening(mb,f,{-.45f,6.6f,.45f,7.25f,2});
      }
      // A pitched tiled porch roof has a connected underside and load-bearing
      // front beam. Step flashing closes the uphill seam against the plaster.
      vec3 a(-2.7f,3.5f,5.5f),b(-.5f,3.5f,5.5f),c(-.5f,3.f,6.7f),d(-2.7f,3.f,6.7f),down(0,-.15f,0),inside(-1.6f,3.175f,6.1f);
      mb.quad(a,b,c,d,P_ROOF,inside);
      mb.quad(a+down,d+down,c+down,b+down,P_TRIM,inside,.66f);
      mb.quad(d,c,c+down,d+down,P_TRIM,inside,.81f);
      mb.quad(a,d,d+down,a+down,P_TRIM,inside,.78f);mb.quad(c,b,b+down,c+down,P_TRIM,inside,.81f);
      bFront(5.5f).panel(mb,-2.73f,3.47f,-.47f,3.58f,.012f,P_METAL,.8f);
      bGutter(mb,{-2.62f,2.93f,6.58f},{-.58f,2.93f,6.58f},{0,0,1});
      mb.box({-2.7f,.12f,5.5f},{-.5f,.3f,6.17f},P_TRIM,0x3B,.85f);
      // Porch beam, post bases, real treads and a restrained wall lantern.
      mb.box({-2.65f,2.68f,6.44f},{-.55f,2.85f,6.67f},P_TRIM,0x3F,.80f);
      for(float x:{-2.525f,-.675f}) {mb.box({x-.10f,0,6.48f},{x+.10f,.65f,6.68f},P_TRIM,0x3B,.84f);mb.box({x-.11f,2.57f,6.48f},{x+.11f,2.73f,6.68f},P_TRIM,0x3F,.83f);}
      for(int i=0;i<2;++i) {float y=.15f*(i+1),z=6.7f-.27f*i;mb.box({-2.35f,y-.15f,z-.27f},{-.85f,y,z},P_TRIM,8|32,.83f);}
      BFace f=bFront(5.5f);f.block(mb,-.78f,2.0f,.025f,-.62f,2.34f,.095f,P_METAL,.84f);f.panel(mb,-.75f,2.04f,-.65f,2.27f,.097f,P_GLASS,.85f,true);
      break;
    }
    case EK_HOUSE_HIP: {
      bChimneyCap(mb,-3.2f,-2.5f,1.2f,1.9f,6.4f);
      for(int s:{-1,1}) {float a=s*5.6f;vec3 n(0,0,(float)s);bGutter(mb,{-5.45f,3.16f,a-s*.13f},{5.45f,3.16f,a-s*.13f},n);
        mb.box({-5.6f,3.05f,a-.002f},{5.6f,3.25f,a+.002f},P_TRIM,s<0?16:32,.82f);
        mb.box({a-.002f,3.05f,-5.6f},{a+.002f,3.25f,5.6f},P_TRIM,s<0?1:2,.82f);
        bDownpipe(mb,{4.65f,3.16f,a-s*.13f},{4.65f,.4f,s*5.08f},n);
      }
      mb.box({1.0f,.15f,5.05f},{2.f,.30f,5.58f},P_TRIM,0x3B,.86f);mb.box({.92f,.30f,5.02f},{2.08f,.45f,5.37f},P_TRIM,0x3B,.88f);
      // The original raised patio needs a visible load path and a usable side stair.
      mb.box({-5.f,.25f,6.35f},{-1.5f,.45f,6.55f},P_WOOD,0x3B,.84f);
      for(float x:{-4.82f,-3.25f,-1.68f}) {
        mb.box({x-.065f,.03f,6.355f},{x+.065f,.28f,6.485f},P_WOOD,0x3B,.78f);
        mb.box({x-.11f,-.05f,6.31f},{x+.11f,.06f,6.53f},P_TRIM,0x3B,.83f);
      }
      for(int i=0;i<3;++i) {float x=-.6f-.3f*i;mb.box({x-.3f,0,5.3f},{x,.2f*(i+1),6.2f},P_WOOD,2|8|16|32,.85f);}
      break;
    }
    case EK_HOUSE_L:
      bGableConstruction(mb,-6,6,-6,.5f,5.2f,8.2f,false,.45f);bGableConstruction(mb,-6,-.6f,.5f,6,5.2f,7.9f,true,.45f);
      bChimneyCap(mb,3.2f,4,-4.2f,-3.4f,9);
      mb.box({.7f,.22f,.52f},{2.1f,.45f,1.04f},P_TRIM,0x3B,.84f);
      break;
    case EK_FARMHOUSE: {
      bGableConstruction(mb,-5.5f,5.5f,-4.5f,4.5f,6.2f,9,false,.5f);
      bChimneyCap(mb,-5.9f,-5.5f,-.5f,.5f,10);bChimneyCap(mb,5.5f,5.9f,-.5f,.5f,10);
      // A recessed central stair leaves a proper landing at the front door.
      mb.box({-5,.3f,4.5f},{-.8f,.55f,6.2f},P_WOOD,0x3F,.84f);
      mb.box({.8f,.3f,4.5f},{5,.55f,6.2f},P_WOOD,0x3F,.84f);
      mb.box({-.8f,.3f,4.5f},{.8f,.55f,5.6f},P_WOOD,0x3F,.84f);
      for(int i=0;i<3;++i) {float y=.55f*(i+1)/3,z=6.4f-.27f*i;mb.box({-.8f,0,z-.27f},{.8f,y,z},P_WOOD,1|2|8|32,.83f);}
      mb.box({-4.99f,2.62f,5.89f},{4.99f,2.84f,6.09f},P_TRIM,0x3F,.79f);
      for(int s:{-1,1}) { float x0=s<0?-4.75f:1.f,x1=s<0?-1.f:4.75f;
        mb.box({x0,1.37f,5.94f},{x1,1.48f,6.04f},P_WOOD,0x3F,.86f);
        mb.box({x0,.69f,5.96f},{x1,.75f,6.02f},P_TRIM,0x3F,.82f);
        for(float x=x0+.10f;x<x1;x+=.42f)mb.box({x-.025f,.73f,5.98f},{x+.025f,1.39f,6.02f},P_TRIM,0x33,.87f);
      }
      // Roof underside and front edge have thickness, rather than a single floating sheet.
      mb.quad({-5.2f,2.72f,6.4f},{5.2f,2.72f,6.4f},{5.2f,2.85f,6.4f},{-5.2f,2.85f,6.4f},P_TRIM,{0,2.7f,5},.8f);
      mb.quad({-5.2f,3.16f,4.5f},{5.2f,3.16f,4.5f},{5.2f,2.72f,6.4f},{-5.2f,2.72f,6.4f},P_TRIM,{0,3.4f,5},.62f);
      break;
    }
    case EK_TOWNHOUSE: {
      float heights[3]={9,10.5f,9.6f};
      for(int u=0;u<3;++u) {float x0=-9+u*6,h=heights[u];
        bFlatCoping(mb,x0+.05f,x0+5.95f,-5.45f,5.45f,h-.15f,h);
        BFace f=bFront(5.5f);for(float y:{3.65f,6.55f})f.block(mb,x0,y-.045f,.005f,x0+6,y+.045f,.09f,P_TRIM,.83f);
        f.block(mb,x0+.02f,.62f,.005f,x0+.15f,h-.04f,.09f,P_TRIM,.84f);
        // Three full-width treads replace the old 60 cm solid stoop.
        for(int i=0;i<3;++i) {float y=.2f*(i+1),z=6.4f-.3f*i;mb.box({x0+.9f,0,z-.3f},{x0+2.3f,y,z},P_TRIM,1|2|8|32,.86f);}
        for(float x:{x0+.94f,x0+2.25f}) {mb.cyl({x,.2f,6.29f},{x,1.08f,6.29f},.024f,.024f,5,P_METAL,true,false);mb.cyl({x,1.48f,5.55f},{x,1.08f,6.29f},.026f,.026f,5,P_METAL,true,true);}
      }
      break;
    }
    case EK_SHOP:
      bFlatCoping(mb,-7,7,-7,7,4.6f,5.5f);
      bServiceUnit(mb,-3.5f,-1.5f,-3,-1,4.6f,5.6f);bServiceUnit(mb,1.5f,3,-4,-2.6f,4.6f,5.3f);
      // Folded valance and end arms give the canvas awning a supported edge.
      mb.quad({-6.8f,2.85f,8.7f},{6.8f,2.85f,8.7f},{6.8f,2.69f,8.7f},{-6.8f,2.69f,8.7f},P_AWNING,{0,3,7},.9f);
      for(float x:{-6.5f,0.f,6.5f})mb.cyl({x,2.45f,7.06f},{x,2.81f,8.57f},.025f,.025f,5,P_METAL,true,false,.75f,.9f);
      break;
    case EK_APARTMENT: {
      bFlatCoping(mb,-9,9,-7,7,20.8f,21.6f);
      bServiceUnit(mb,-7,-4.8f,-4,-2.5f,20.8f,21.6f);bServiceUnit(mb,4.8f,7,-4,-2.5f,20.8f,21.6f);
      for(int floor=1;floor<6;++floor)for(int bay=0;bay<3;++bay) {float x0=-7.6f+bay*5.6f,y=.5f+floor*3.4f;
        // The balcony is reached through a real floor-level sliding door.
        BFace face=bFront(7);face.block(mb,x0+1.77f,y+.1f,-.15f,x0+1.83f,y+2.35f,-.095f,P_METAL,.82f);
        mb.cyl({x0+1.70f,y+.95f,6.93f},{x0+1.70f,y+1.22f,6.93f},.018f,.018f,5,P_METAL,true,true,.8f,.8f);
        // Slender stainless caps, side returns and posts make the glazing a railing.
        mb.box({x0,y+.98f,8.19f},{x0+3.6f,y+1.04f,8.3f},P_METAL,0x3B,.85f);
        for(float x:{x0+.035f,x0+1.8f,x0+3.565f})mb.box({x-.025f,y,8.22f},{x+.025f,y+1.0f,8.29f},P_METAL,0x33,.8f);
        for(float x:{x0+.035f,x0+3.565f})mb.box({x-.025f,y+.97f,7.05f},{x+.025f,y+1.035f,8.27f},P_METAL,0x3B,.82f);
      }
      mb.box({-1.35f,.30f,7},{1.35f,.5f,7.32f},P_TRIM,0x3B,.85f);
      for(int i=0;i<3;++i) {float y=(i+1)*.5f/3,z=8.3f-.33f*i;mb.box({-1.35f,0,z-.33f},{1.35f,y,z},P_TRIM,1|2|8|32,.85f);}
      for(float x:{-8.92f,8.92f})bFront(7).block(mb,x-.035f,.52f,.005f,x+.035f,20.7f,.08f,P_TRIM,.8f);
      break;
    }
    case EK_OFFICE: {
      // Curtain wall is already an inset glass volume. Thin extruded pressure caps
      // follow its actual floor modules, rather than heavy concrete corner boxes.
      for(int s=0;s<4;++s) { BFace f=s==0?bFront(9):s==1?bBack(-9):s==2?bLeft(-9):bRight(9);
        for(float u=-7.75f;u<8;u+=1.55f)f.block(mb,u-.025f,4.4f,.006f,u+.025f,36,.065f,P_METAL,.85f);
        for(float y=7.7f;y<36;y+=3.7f)f.block(mb,-8.9f,y-.035f,.006f,8.9f,y+.035f,.07f,P_METAL,.78f);
        for(float u:{-8.1f,8.1f})f.block(mb,u-.11f,.3f,-.38f,u+.11f,3.9f,.06f,P_TRIM,.82f);
      }
      bFlatCoping(mb,-9.25f,9.25f,-9.25f,9.25f,36.4f,37.4f);
      bFitOpening(mb,bFront(4.17f),{-3.8f,37.65f,3.8f,39.5f,2});
      for(float x:{-1.35f,1.3f}) {bFront(8.6f).block(mb,x-.026f,.55f,-.15f,x+.026f,3.55f,-.085f,P_METAL,.86f);
        for(float d:{-.12f,.12f})mb.cyl({x+d,1.36f,8.53f},{x+d,1.75f,8.53f},.022f,.022f,6,P_METAL,true,true,.85f,.85f);}

      break;
    }
    case EK_TOWER: {
      for(int tier=0;tier<3;++tier) {float h=tier==0?30:tier==1?56:74,r=tier==0?10.3f:tier==1?8.3f:6.4f;
        bFlatCoping(mb,-r,r,-r,r,h,h+(tier==2?1.2f:1.f));
      }
      BFace front=bFront(10);front.block(mb,-.027f,.35f,-.15f,.027f,3.44f,-.095f,P_METAL,.84f);
      front.block(mb,-1.68f,3.48f,.005f,1.68f,3.66f,.22f,P_TRIM,.83f);
      for(float x:{-.12f,.12f})mb.cyl({x,1.25f,9.93f},{x,1.66f,9.93f},.022f,.022f,6,P_METAL,true,true,.85f,.85f);
      // Setback roof drains, low lift-room louvers and bundled aerial stays.
      for(int s:{-1,1})mb.cyl({s*.36f,75.25f,0},{s*.12f,79.7f,0},.028f,.022f,5,P_METAL,false,false);
      break;
    }
    case EK_SKYSCRAPER: {
      // Octagonal façade caps follow the real eight planar faces and remain within
      // the original crown radius. This avoids a fake rectangular overlay grid.
      for(float x:{-1.5f,1.5f}) {BFace front=bFront(10.48f);front.block(mb,x-.027f,.6f,-.15f,x+.027f,2.85f,-.095f,P_METAL,.84f);
        front.panel(mb,x-1.23f,2.82f,x+1.23f,2.89f,-.095f,P_METAL,.84f);
        for(float d:{-.12f,.12f})mb.cyl({x+d,1.35f,10.4f},{x+d,1.75f,10.4f},.022f,.022f,6,P_METAL,true,true,.85f,.85f);}
      const int seg=8;
      for(int i=0;i<seg;++i) {float a=PI/8+i*2*PI/seg,b=a+2*PI/seg;vec3 p0(cosf(a)*9.6f,0,sinf(a)*9.6f),p1(cosf(b)*9.6f,0,sinf(b)*9.6f);
        vec3 tangent=normalize(p1-p0),normal=normalize((p0+p1)*.5f);BFace f((p0+p1)*.5f,tangent,normal);float half=length(p1-p0)*.5f;
        for(float u=-half+1.35f;u<half-.3f;u+=1.45f)f.block(mb,u-.021f,7,.004f,u+.021f,118,.058f,P_METAL,.85f);
        for(float y=10.6f;y<118;y+=3.7f)f.panel(mb,-half+.025f,y-.055f,half-.025f,y+.055f,.055f,P_METAL,.8f);
        f.block(mb,-half+.025f,116.9f,.005f,half-.025f,117.15f,.085f,P_METAL,.83f);
      }
      break;
    }
    case EK_WAREHOUSE: {
      bGableConstruction(mb,-12,12,-9,9,6.6f,8.5f,false,.4f);
      // Cargo bays remain at truck-bed height; personnel get their own stair.
      mb.box({-9,0,9},{8.1f,1.1f,10.6f},P_TRIM,0x3B,.84f);
      mb.box({8.1f,.7f,9},{9.6f,1.1f,9.41f},P_TRIM,0x3B,.84f);
      for(int i=0;i<6;++i) {float y=(i+1)*1.1f/6,z=10.6f-.2f*i;mb.box({8.45f,0,z-.2f},{9.55f,y,z},P_TRIM,1|2|8|32,.85f);}
      // Structural end piers, impact bumpers and a thin dock shelter establish use.
      for(float x:{-7.18f,-2.82f,.82f,5.18f})mb.box({x-.07f,1.1f,9.035f},{x+.07f,5.3f,9.16f},P_METAL,0x3B,.83f);
      mb.box({-7.25f,5.3f,9.02f},{5.25f,5.48f,10.4f},P_METAL,0x3F,.8f);
      for(float x:{-6.4f,-3.6f,1.6f,4.4f})mb.box({x-.14f,.25f,10.47f},{x+.14f,.95f,10.59f},P_DARK,0x3B,.76f);
      for(int s:{-1,1})for(float z:{-8.3f,-2.8f,2.8f,8.3f})(s<0?bLeft(-12):bRight(12)).block(mb,z-.04f,.65f,.005f,z+.04f,6.35f,.07f,P_METAL,.82f);
      break;
    }
    case EK_BARN: {
      // Gambrel fascias are continuous bent profiles, not rectangular roof slabs.
      vec2 p[5]={{-6.4f,4.25f},{-4.6f,7.6f},{0,10},{4.6f,7.6f},{6.4f,4.25f}};
      for(float z:{-9.35f,9.35f})for(int i=0;i<4;++i)mb.quad({p[i].x,p[i].y,z},{p[i+1].x,p[i+1].y,z},{p[i+1].x,p[i+1].y-.15f,z},{p[i].x,p[i].y-.15f,z},P_TRIM,{0,5,0},.85f);
      for(int s:{-1,1})bGutter(mb,{s*6.26f,4.17f,-9.1f},{s*6.26f,4.17f,9.1f},{(float)s,0,0});
      for(float x:{-5.92f,5.92f})bFront(9).block(mb,x-.065f,.35f,.008f,x+.065f,4.48f,.09f,P_TRIM,.9f);
      mb.box({-1.1f,0,9.12f},{1.1f,.15f,9.35f},P_TRIM,0x3B,.83f);
      bFitOpening(mb,bFront(9.17f),{-.9f,6,.9f,7.6f,7});
      for(int s:{-1,1}) {vec3 a(s*2.04f,.52f,9.04f),b(-s*2.04f,3.95f,9.04f),d(.07f,0,0);mb.quad(a-d,a+d,b+d,b-d,P_TRIM,{0,2,8.9f},.9f);}
      for(int s:{-1,1}) {vec3 a(s*1.62f,.50f,-9.04f),b(-s*1.62f,3.72f,-9.04f),d(.065f,0,0);mb.quad(a-d,a+d,b+d,b-d,P_TRIM,{0,2,-8.9f},.9f);}
      break;
    }
    case EK_SILO: {
      mb.cyl({0,.4f,0},{0,16,0},3,3,32,P_METAL,false,false);
      for(int ring=0;ring<6;++ring) {float a=ring*PI/12,b=(ring+1)*PI/12;mb.cyl({0,16+2.6f*sinf(a),0},{0,16+2.6f*sinf(b),0},3.05f*cosf(a),ring==5?0:3.05f*cosf(b),32,P_METAL,false,false);}
      // Circular seam bands and a service ladder follow the corrugated vessel.
      const int seg=24;
      for(float y=1.9f;y<16;y+=1.75f)mb.cyl({0,y,0},{0,y+.045f,0},3.035f,3.035f,seg,P_METAL,false,false,.86f,.86f);
      for(float z:{-.22f,.22f})mb.cyl({3.13f,.5f,z},{3.13f,17,z},.035f,.035f,6,P_METAL,true,false,.78f,.9f);
      for(float y=.8f;y<16.8f;y+=.36f)mb.cyl({3.14f,y,-.22f},{3.14f,y,.22f},.025f,.025f,5,P_METAL,true,true,.84f,.84f);
      mb.cyl({0,18.15f,0},{0,18.55f,0},.24f,.24f,12,P_METAL,true,false,.9f,.9f);
      break;
    }
    case EK_CHURCH: {
      bGableConstruction(mb,-5,5,-13,5,7,13,true,.4f);
      for(int s:{-1,1})for(float z:{-12.5f,-8.2f,-3.8f,.6f,4.5f}) {float x0=s<0?-5.34f:5.f,x1=s<0?-5.f:5.34f;
        mb.box({x0,.5f,z-.22f},{x1,4.75f,z+.22f},P_TRIM,0x3B,.83f);
        mb.quad({x0,4.75f,z-.22f},{x1,4.75f,z-.22f},{x1,5.15f,z+.05f},{x0,5.15f,z+.05f},P_TRIM,{0,4,z},.9f);
      }
      for(float y:{6.65f,12.6f,17.1f}) {bFront(10.2f).block(mb,-2.59f,y,0,2.59f,y+.16f,.11f,P_TRIM,.83f);for(int s:{-1,1})(s<0?bLeft(-2.6f):bRight(2.6f)).block(mb,5,y,0,10.2f,y+.16f,.11f,P_TRIM,.83f);}
      break;
    }
    case EK_WATERTOWER: {
      // Open maintenance handrail and two-way tension bracing keep daylight through
      // the support structure; no broad solid rings masquerading as a railing.
      const int n=24;float r=5.72f;
      for(int i=0;i<n;++i) {float a=i*2*PI/n,b=(i+1)*2*PI/n;vec3 p(cosf(a)*r,19.14f,sinf(a)*r),q(cosf(b)*r,19.14f,sinf(b)*r);
        mb.cyl(p,q,.035f,.035f,5,P_METAL,false,false,.85f,.85f);
        if(i%2==0)mb.cyl({p.x,18.18f,p.z},p,.035f,.035f,5,P_METAL,true,false,.8f,.9f);
      }
      for(int i=0;i<4;++i) {float a=PI*.25f+i*PI*.5f,b=a+PI*.5f;
        for(int j=0;j<2;++j) {float y0=2+j*8.f,y1=y0+7,r0=lerpf(3.6f,2.6f,y0/19),r1=lerpf(3.6f,2.6f,y1/19);
          mb.cyl({cosf(b)*r0,y0,sinf(b)*r0},{cosf(a)*r1,y1,sinf(a)*r1},.045f,.045f,4,P_METAL,false,false,.8f,.85f);
        }
      }
      for(float y:{18.35f,24.82f})mb.cyl({0,y,0},{0,y+.075f,0},5.22f,5.22f,28,P_METAL,false,false,.83f,.83f);
      break;
    }
    case EK_LIGHTHOUSE: {
      const int seg=32;float ys[]={1.5f,3.7f,6.f,7.1f,12.f,13.1f,18.f,19.1f,22.f};
      auto radius=[](float y){return 3.f-(y-1.5f)*.9f/20.5f;};
      for(int i=0;i<seg;++i) {float a=i*2*PI/seg,b=(i+1)*2*PI/seg;vec3 d0(cosf(a),0,sinf(a)),d1(cosf(b),0,sinf(b));
        for(int j=0;j<8;++j) {float y0=ys[j],y1=ys[j+1];bool entry=j==0&&(i==7||i==8),slot=(j==2||j==4||j==6)&&(i%8==7);
          if(entry)continue;
          if(slot) {float mid=(y0+y1)*.5f,r=radius(mid);vec3 center=(d0+d1)*(r*.5f);BFace f(center,normalize(d1-d0),normalize(center));float w=length(d1-d0)*r;
            bFitOpening(mb,f,{-w*.5f,y0,w*.5f,y1,5});
          } else mb.quad(d0*radius(y0)+vec3(0,y0,0),d1*radius(y0)+vec3(0,y0,0),d1*radius(y1)+vec3(0,y1,0),d0*radius(y1)+vec3(0,y1,0),P_WALL,{0,(y0+y1)*.5f,0});
        }
      }
      bFitOpening(mb,bFront(2.92f),{-.55f,1.5f,.55f,3.7f,1});
      // The raised tower foundation is reached by an exterior side stair within
      // the established keeper-house footprint, not a 1.5 m jump at the door.
      for(int i=0;i<8;++i) {float x=-3.2f+i*.4f,y=(i+1)*1.5f/8;mb.box({x,0,3.35f},{x+.4f,y,4.25f},P_TRIM,2|8|32,.85f);}
      mb.box({-.4f,1.30f,2.94f},{.7f,1.5f,4.25f},P_TRIM,0x3B,.86f);
      mb.cyl({-3.08f,1.05f,4.18f},{.12f,2.4f,4.18f},.028f,.028f,5,P_METAL,true,true,.85f,.85f);
      for(int i:{0,3,7}) {float x=-3.08f+i*.4f,y=(i+1)*1.5f/8;mb.cyl({x,y,4.18f},{x,1.05f+(x+3.08f)*1.35f/3.2f,4.18f},.026f,.026f,5,P_METAL,true,false,.8f,.86f);}
      // Lantern cage and gallery guardrail reveal the scale of the light chamber.
      for(int i=0;i<16;++i) {float a=i*2*PI/16,b=(i+1)*2*PI/16;vec3 p(cosf(a)*2.76f,23.32f,sinf(a)*2.76f),q(cosf(b)*2.76f,23.32f,sinf(b)*2.76f);
        mb.cyl(p,q,.028f,.028f,5,P_METAL,false,false,.85f,.85f);if(i%2==0)mb.cyl({p.x,22.44f,p.z},p,.026f,.026f,5,P_METAL,true,false,.82f,.9f);
        if(i%2==0)mb.cyl({cosf(a)*1.61f,22.46f,sinf(a)*1.61f},{cosf(a)*1.61f,25.05f,sinf(a)*1.61f},.042f,.042f,6,P_METAL,false,false,.8f,.9f);
      }
      for(float y:{22.54f,24.94f})mb.cyl({0,y,0},{0,y+.075f,0},1.64f,1.64f,24,P_METAL,false,false,.85f,.85f);
      bGableConstruction(mb,-6,-3.4f,-4,4,4,5.4f,true,.3f);
      break;
    }
    case EK_GASSTATION: {
      bFront(-3.2f).block(mb,-4.5f,3.38f,.006f,4.5f,3.52f,.07f,P_METAL,.88f);
      for(int i=0;i<3;++i) {float x=-4+i*4.f;
        mb.box({x-.73f,.15f,2.02f},{x+.73f,.28f,2.98f},P_TRIM,0x3B,.82f);
        BFace f=bFront(2.8f);f.panel(mb,x-.29f,1.08f,x+.29f,1.49f,.011f,P_DARK,.87f);
        f.panel(mb,x-.23f,1.28f,x+.23f,1.43f,.014f,P_GLASS,.88f,true);
        f.panel(mb,x-.23f,.62f,x+.23f,.9f,.014f,P_SIGN,.8f);
        // Hose is a low-sided curved sweep, hung from the actual dispenser flank.
        vec3 last(x+.43f,1.52f,2.51f);
        for(int j=1;j<=8;++j) {float t=j/8.f;vec3 next(x+.43f+sinf(t*PI)*.24f,1.52f-sinf(t*PI*.82f)*1.03f,2.51f);mb.cyl(last,next,.031f,.031f,5,P_DARK,false,false,.83f,.83f);last=next;}
        mb.cyl(last,last+vec3(-.10f,.2f,0),.05f,.038f,6,P_METAL,true,true);
      }
      for(float x:{-5.5f,5.5f})for(float z:{.4f,4.6f})mb.box({x-.34f,.15f,z-.34f},{x+.34f,.28f,z+.34f},P_TRIM,0x3B,.8f);
      break;
    }
    default:break;
  }
  // Newly formed gables, arches and curved wall returns use the same material contract.
  for(size_t i=begin;i<mb.o.size();++i)if((int)mb.o[i].part==P_WALL)mb.o[i].u=1;
  for(size_t i=begin;i+2<mb.o.size();i+=3)if((int)mb.o[i].part==P_TRIM&&std::max({mb.o[i].py,mb.o[i+1].py,mb.o[i+2].py})<=.61f)for(int j=0;j<3;++j)mb.o[i+j].u=2;
}

// A city's skyline (v3.48): a stepped supertall, a round glass tower, a residential slab and a mid-rise office block
// with shops at street level. Their windows are the shader's at every detail level (ent_fs2 entityFacadePane and the
// curtain-wall grid, in step with the numbers here), so the close mesh (lod 3) is the near one with its construction
// added - mullion caps, rail caps, balcony screens, sills - never a second set of openings that could disagree.
void buildCityTower(MB& mb, int kind, int lod) {
  const bool hero = lod == 3, d0 = lod == 0 || hero, d1 = lod <= 1 || hero;
  const size_t begin = mb.o.size();
  switch (kind) {
    case EK_SUPERTALL: {
      plinth(mb, 12.f, 12.f, 0.3f);
      mb.box(vec3(-11.4f, 0.3f, -11.4f), vec3(11.4f, 8.4f, 11.4f), P_GLASS, 0x33);   // the double-height lobby, set back
      mb.box(vec3(-12.f, 8.4f, -12.f), vec3(12.f, 9.2f, 12.f), P_TRIM, 0x3F);         // transfer band
      // three shafts stepping back at 140 and 190 m, a ledge at each step and a crown
      const float tier[3][3] = {{10.5f, 9.2f, 140.f}, {8.7f, 141.2f, 190.f}, {6.9f, 191.2f, 216.f}};
      for (int t = 0; t < 3; t++) {
        const float h = tier[t][0], l = t < 2 ? h + 0.4f : 7.3f, top = t < 2 ? tier[t][2] + 1.2f : 219.f;
        mb.box(vec3(-h, tier[t][1], -h), vec3(h, tier[t][2], h), P_GLASS, 0x33);
        mb.box(vec3(-l, tier[t][2], -l), vec3(l, top, l), P_TRIM, 0x3F);
      }
      if (d1) { mb.box(vec3(-4.5f, 219.f, -4.5f), vec3(4.5f, 223.f, 4.5f), P_DARK, 0x3B);
        mb.cyl(vec3(0, 223.f, 0), vec3(0, 236.f, 0), 0.9f, 0.12f, 6, P_METAL, false, false); }
      if (d0) {
        for (int i = 0; i < 4; i++) { const float x = i & 1 ? 10.5f : -10.5f, z = i & 2 ? 10.5f : -10.5f;   // corner fins
          mb.box(vec3(x - 0.35f, 9.2f, z - 0.35f), vec3(x + 0.35f, 140.f, z + 0.35f), P_TRIM); }
        door(mb, 0.f, 11.4f, 3.0f, 3.2f, 0.3f);
      }
      if (hero)   // pressure caps on the mullions: each face's bays measured from its corners (the shader's module)
        for (int t = 0; t < 3; t++) {
          const float h = tier[t][0]; const int bays = (int)roundf(2.f * h / 1.5f);
          for (int s = 0; s < 4; s++) { BFace f = s == 0 ? bFront(h) : s == 1 ? bBack(-h) : s == 2 ? bLeft(-h) : bRight(h);
            for (int b = 1; b < bays; b++) { const float u = -h + b * 2.f * h / bays;
              f.block(mb, u - 0.03f, tier[t][1], 0.004f, u + 0.03f, tier[t][2], 0.07f, P_METAL, 0.85f); } }
        }
      break;
    }
    case EK_ROUNDTOWER: {
      const int sg = d0 ? 24 : d1 ? 16 : 10; const float rot = PI / sg;   // (a face, not a corner, square to the door)
      mb.cyl(vec3(0, -3.f, 0), vec3(0, 0.3f, 0), 9.62f, 9.62f, sg, P_TRIM, true, false, 1, 1, rot);    // plinth
      mb.cyl(vec3(0, 0.3f, 0), vec3(0, 6.1f, 0), 8.8f, 8.8f, sg, P_GLASS, false, false, 1, 1, rot);     // the lobby, set back
      mb.cyl(vec3(0, 6.1f, 0), vec3(0, 6.9f, 0), 9.5f, 9.5f, sg, P_TRIM, true, true, 1, 1, rot);        // canopy band
      mb.cyl(vec3(0, 6.9f, 0), vec3(0, 116.f, 0), 9.1f, 9.1f, sg, P_GLASS, false, false, 1, 1, rot);
      mb.cyl(vec3(0, 116.f, 0), vec3(0, 119.f, 0), 9.4f, 9.4f, sg, P_TRIM, true, true, 1, 1, rot);      // crown ring
      if (d1) { mb.cyl(vec3(0, 119.f, 0), vec3(0, 122.5f, 0), 5.5f, 5.5f, sg / 2, P_DARK, true, false, 1, 1, rot);
        mb.cyl(vec3(0, 122.5f, 0), vec3(0, 124.f, 0), 0.25f, 0.08f, 5, P_METAL, false, false); }
      if (d0) door(mb, 0.f, 8.7f, 2.4f, 3.0f, 0.3f);
      if (hero)   // a cap on each of the 36 mullions (every 10 degrees from +x, as the shader's grid)
        for (int i = 0; i < 36; i++) { const float a = i * 2.f * PI / 36.f; const vec3 d(cosf(a), 0, sinf(a));
          BFace f(d * 9.1f, vec3(-d.z, 0, d.x), d);
          f.block(mb, -0.03f, 6.9f, -0.08f, 0.03f, 116.f, 0.07f, P_METAL, 0.85f); }
      break;
    }
    case EK_SLABTOWER: {
      plinth(mb, 14.f, 7.5f, 0.5f);
      mb.box(vec3(-14.f, 0.5f, -7.5f), vec3(14.f, 61.6f, 7.5f), P_WALL, 0x33);
      mb.box(vec3(-14.f, 61.6f, -7.5f), vec3(14.f, 62.4f, 7.5f), P_TRIM, 0x33);   // parapet
      mb.box(vec3(-14.f, 61.4f, -7.5f), vec3(14.f, 61.6f, 7.5f), P_DARK, 8);      // one roof plane, no coplanar wall cap
      if (d1) mb.box(vec3(-4.f, 61.6f, -2.5f), vec3(4.f, 64.f, 2.5f), P_TRIM, 0x3B);   // lift and stair overrun
      if (d0) {
        // twenty floors of 3 m: a balcony along each above the ground, its glazed rail, plain end bays either side
        for (int f = 1; f < 20; f++) { const float y = 0.5f + f * 3.f;
          mb.box(vec3(-10.5f, y - 0.2f, 7.5f), vec3(10.5f, y, 8.9f), P_TRIM, 0x3F & ~16);
          mb.box(vec3(-10.5f, y, 8.8f), vec3(10.5f, y + 1.05f, 8.9f), P_GLASS, 0x3F & ~4);
        }
        door(mb, 0.f, 7.5f, 2.4f, 2.6f, 0.5f);
      }
      if (hero)   // rail caps, and screens between the three flats on each floor
        for (int f = 1; f < 20; f++) { const float y = 0.5f + f * 3.f;
          mb.box(vec3(-10.5f, y + 1.05f, 8.78f), vec3(10.5f, y + 1.1f, 8.92f), P_METAL, 0x3B, 0.85f);
          for (float x : {-4.5f, 4.5f}) mb.box(vec3(x - 0.06f, y, 7.5f), vec3(x + 0.06f, y + 2.8f, 8.8f), P_TRIM, 1 | 2 | 32, 0.85f);
        }
      break;
    }
    case EK_MIDRISE: {
      plinth(mb, 15.f, 11.f, 0.3f);
      mb.box(vec3(-14.4f, 0.3f, -10.4f), vec3(14.4f, 4.6f, 10.4f), P_GLASS, 0x33);   // shopfronts, under the first floor
      mb.box(vec3(-15.f, 4.6f, -11.f), vec3(15.f, 5.3f, 11.f), P_TRIM, 0x3F & ~8);   // its soffit and band
      mb.box(vec3(-15.f, 5.3f, -11.f), vec3(15.f, 28.6f, 11.f), P_WALL, 0x33);       // six office floors of 3.85 m
      mb.box(vec3(-15.f, 28.6f, -11.f), vec3(15.f, 29.4f, 11.f), P_TRIM, 0x33);      // parapet
      mb.box(vec3(-15.f, 28.4f, -11.f), vec3(15.f, 28.6f, 11.f), P_DARK, 8);
      if (d1) { mb.box(vec3(-6.f, 28.6f, -5.f), vec3(6.f, 31.f, 3.f), P_DARK, 0x3B);
        mb.box(vec3(-14.f, 3.6f, 10.4f), vec3(14.f, 4.4f, 10.5f), P_SIGN, 32); }   // the shops' fascia
      if (d0) {
        door(mb, 0.f, 10.4f, 2.6f, 3.0f, 0.3f);
        for (int i = 0; i < 4; i++) { const float x = i & 1 ? 14.7f : -14.7f, z = i & 2 ? 10.7f : -10.7f;   // corner piers
          mb.box(vec3(x - 0.3f, 0.3f, z - 0.3f), vec3(x + 0.3f, 4.6f, z + 0.3f), P_TRIM, 0x33); }
      }
      if (hero)   // a sill under each floor's ribbon of windows, all round
        for (int f = 0; f < 6; f++) { const float y = 6.2f + f * 3.85f;
          for (int s = 0; s < 4; s++) { BFace w = s == 0 ? bFront(11.f) : s == 1 ? bBack(-11.f) : s == 2 ? bLeft(-15.f) : bRight(15.f);
            const float half = s < 2 ? 14.3f : 10.6f;
            w.block(mb, -half, y - 0.14f, 0.f, half, y - 0.02f, 0.12f, P_TRIM, 0.85f); }
        }
      break;
    }
    default: break;
  }
  // as buildDetailedBuilding's: the street-level stone weathers (P_TRIM aux.u 2 below 0.61 m)
  if (hero) for (size_t i = begin; i + 2 < mb.o.size(); i += 3)
    if ((int)mb.o[i].part == P_TRIM && std::max({mb.o[i].py, mb.o[i + 1].py, mb.o[i + 2].py}) <= .61f) for (int j = 0; j < 3; ++j) mb.o[i + j].u = 2;
}

void buildBuilding(MB& mb, int kind, int lod) {
  if (kind >= EK_SUPERTALL && kind <= EK_MIDRISE) { buildCityTower(mb, kind, lod); return; }
  if (lod == 3) {
    if (kind == EK_RWYLIGHT || kind == EK_PAPI) { buildBuilding(mb, kind, 0); return; }
    buildDetailedBuilding(mb, kind); return;
  }
  bool d0 = lod == 0, d1 = lod <= 1;
  switch (kind) {
    case EK_HOUSE: {
      plinth(mb, 4.5f, 5.5f);
      mb.box(vec3(-4.5f, 0.45f, -5.5f), vec3(4.5f, 5.6f, 5.5f), P_WALL, 0x3B & ~8);
      mb.gable(-4.5f, 4.5f, -5.5f, 5.5f, 5.6f, 8.6f, true, d1 ? 0.5f : 0.f, P_ROOF, P_WALL);
      if (d1) mb.box(vec3(2.1f, 6.2f, -1.4f), vec3(2.9f, 9.4f, -0.6f), P_BRICK, 0x3B | 8);
      if (d0) { door(mb, -1.6f, 5.5f, 1.1f, 2.2f); mb.box(vec3(-2.7f, 2.85f, 5.5f), vec3(-0.5f, 3.0f, 6.7f), P_ROOF, 0x3F);
        mb.box(vec3(-2.6f, 0.45f, 6.5f), vec3(-2.45f, 2.85f, 6.65f), P_TRIM); mb.box(vec3(-0.75f, 0.45f, 6.5f), vec3(-0.6f, 2.85f, 6.65f), P_TRIM); }
      break;
    }
    case EK_HOUSE_HIP: {
      plinth(mb, 5.f, 5.f);
      mb.box(vec3(-5.f, 0.45f, -5.f), vec3(5.f, 3.4f, 5.f), P_WALL, 0x3B & ~8);
      mb.hip(-5.6f, 5.6f, -5.6f, 5.6f, 3.25f, 5.8f, P_ROOF);
      if (d1) mb.box(vec3(-3.2f, 4.f, 1.2f), vec3(-2.5f, 6.4f, 1.9f), P_BRICK, 0x3B | 8);
      if (d0) { door(mb, 1.5f, 5.f, 1.0f, 2.1f); mb.box(vec3(-5.f, 0.45f, 5.f), vec3(-1.5f, 0.6f, 6.6f), P_WOOD, 0x3F); }
      break;
    }
    case EK_HOUSE_L: {
      plinth(mb, 6.f, 6.f);
      mb.box(vec3(-6.f, 0.45f, -6.f), vec3(6.f, 5.2f, 0.5f), P_WALL, 0x3B & ~8);
      mb.gable(-6.f, 6.f, -6.f, 0.5f, 5.2f, 8.2f, false, d1 ? 0.45f : 0.f, P_ROOF, P_WALL);
      mb.box(vec3(-6.f, 0.45f, 0.5f), vec3(-0.6f, 5.2f, 6.f), P_WALL, 0x3B & ~8 & ~16);
      mb.gable(-6.f, -0.6f, 0.5f, 6.f, 5.2f, 7.9f, true, d1 ? 0.45f : 0.f, P_ROOF, P_WALL);
      if (d1) mb.box(vec3(3.2f, 5.6f, -4.2f), vec3(4.0f, 9.0f, -3.4f), P_BRICK, 0x3B | 8);
      if (d0) { door(mb, 1.6f, 0.5f, 1.1f, 2.2f); door(mb, 4.2f, 0.5f, 2.6f, 2.3f); }
      break;
    }
    case EK_FARMHOUSE: {
      plinth(mb, 5.5f, 4.5f);
      mb.box(vec3(-5.5f, 0.45f, -4.5f), vec3(5.5f, 6.2f, 4.5f), P_WALL, 0x3B & ~8);
      mb.gable(-5.5f, 5.5f, -4.5f, 4.5f, 6.2f, 9.f, false, d1 ? 0.5f : 0.f, P_ROOF, P_WALL);
      if (d1) { mb.box(vec3(-5.9f, 0.45f, -0.5f), vec3(-5.5f, 10.f, 0.5f), P_BRICK, 0x3B | 8); mb.box(vec3(5.5f, 0.45f, -0.5f), vec3(5.9f, 10.f, 0.5f), P_BRICK, 0x3B | 8); }
      if (d1) {   // front porch: deck, lean-to roof on posts
        mb.box(vec3(-5.f, 0.3f, 4.5f), vec3(5.f, 0.55f, 6.2f), P_WOOD, 0x3F);
        mb.quad(vec3(-5.2f, 3.3f, 4.5f), vec3(5.2f, 3.3f, 4.5f), vec3(5.2f, 2.85f, 6.4f), vec3(-5.2f, 2.85f, 6.4f), P_ROOF, vec3(0, 0, 4.5f));
        if (d0) for (int i = 0; i < 4; i++) { float x = -4.8f + i * 3.2f; mb.box(vec3(x - 0.09f, 0.55f, 5.9f), vec3(x + 0.09f, 2.9f, 6.08f), P_TRIM); }
      }
      if (d0) door(mb, 0.f, 4.5f, 1.1f, 2.3f);
      break;
    }
    case EK_TOWNHOUSE: {
      float hs[3] = {9.0f, 10.5f, 9.6f};
      plinth(mb, 9.f, 5.5f, 0.6f);
      for (int u = 0; u < 3; u++) {
        float x0 = -9.f + u * 6.f, x1 = x0 + 6.f, h = hs[u];
        mb.box(vec3(x0, 0.6f, -5.5f), vec3(x1, h, 5.5f), P_WALL, (u == 0 ? 1 : 0) | (u == 2 ? 2 : 0) | 8 | 16 | 32);
        if (u > 0 && hs[u - 1] != h) {   // party wall step
          float lo = std::min(hs[u - 1], h), hi = std::max(hs[u - 1], h);
          mb.quad(vec3(x0, lo, -5.5f), vec3(x0, hi, -5.5f), vec3(x0, hi, 5.5f), vec3(x0, lo, 5.5f), P_WALL, vec3(hs[u - 1] > h ? x0 + 1 : x0 - 1, lo, 0));
        }
        if (d1) mb.box(vec3(x0, h, 5.2f), vec3(x1, h + 0.6f, 5.75f), P_TRIM, 0x3F);   // cornice
        if (d0) { door(mb, x0 + 1.6f, 5.5f, 1.1f, 2.4f, 0.6f); mb.box(vec3(x0 + 0.9f, 0.f, 5.5f), vec3(x0 + 2.3f, 0.6f, 6.4f), P_TRIM, 0x3F); }
      }
      if (d0) mb.box(vec3(-3.f, 10.5f, -3.f), vec3(-1.2f, 11.6f, -1.6f), P_DARK, 0x3F);
      break;
    }
    case EK_SHOP: {
      plinth(mb, 7.f, 7.f, 0.3f);
      mb.box(vec3(-7.f, 0.3f, -7.f), vec3(7.f, 4.6f, 7.f), P_WALL, 0x3B & ~8);
      mb.box(vec3(-7.f, 4.6f, -7.f), vec3(7.f, 5.5f, 7.f), P_TRIM, 0x3B & ~8);   // parapet band
      mb.box(vec3(-7.f, 4.4f, -7.f), vec3(7.f, 4.6f, 7.f), P_DARK, 8);             // one roof plane, no coplanar wall cap
      mb.box(vec3(-6.4f, 0.4f, 7.f), vec3(6.4f, 3.1f, 7.08f), P_GLASS, 32);       // storefront
      if (d1) mb.box(vec3(-6.6f, 3.6f, 7.f), vec3(6.6f, 4.45f, 7.12f), P_SIGN, 32);
      if (d0) {
        mb.quad(vec3(-6.8f, 3.45f, 7.f), vec3(6.8f, 3.45f, 7.f), vec3(6.8f, 2.85f, 8.7f), vec3(-6.8f, 2.85f, 8.7f), P_AWNING, vec3(0, 0, 7.f));
        door(mb, 0.f, 7.08f, 1.8f, 2.4f, 0.4f);
        mb.box(vec3(-3.5f, 4.6f, -3.f), vec3(-1.5f, 5.6f, -1.f), P_METAL, 0x3B | 8); mb.box(vec3(1.5f, 4.6f, -4.f), vec3(3.f, 5.3f, -2.6f), P_METAL, 0x3B | 8);
      }
      break;
    }
    case EK_APARTMENT: {
      plinth(mb, 9.f, 7.f, 0.5f);
      mb.box(vec3(-9.f, 0.5f, -7.f), vec3(9.f, 20.8f, 7.f), P_WALL, 0x3B & ~8);
      mb.box(vec3(-9.f, 20.8f, -7.f), vec3(9.f, 21.6f, 7.f), P_TRIM, 0x3B & ~8);
      mb.box(vec3(-9.f, 20.6f, -7.f), vec3(9.f, 20.8f, 7.f), P_DARK, 8);             // one roof plane, no coplanar wall cap
      if (d1) mb.box(vec3(-2.5f, 20.8f, -2.f), vec3(2.5f, 23.2f, 2.f), P_TRIM, 0x3B | 8);
      if (d0) {
        for (int f = 1; f < 6; f++) {
          float y = 0.5f + f * 3.4f;
          for (int b = 0; b < 3; b++) {
            float x0 = -7.6f + b * 5.6f;
            mb.box(vec3(x0, y - 0.2f, 7.f), vec3(x0 + 3.6f, y, 8.3f), P_TRIM, 0x3F & ~16); // omit the wall-contact face
            mb.box(vec3(x0, y, 8.2f), vec3(x0 + 3.6f, y + 1.0f, 8.3f), P_GLASS, 0x3F & ~4); // underside rests on the slab
          }
        }
        door(mb, 0.f, 7.f, 2.2f, 2.6f, 0.5f);
        // Roof service units and an entrance canopy, funded by the hidden balcony faces above.
        // All remain inside the original apartment bounds and disappear with the other LOD0 details.
        mb.box(vec3(-7.f, 20.8f, -4.f), vec3(-4.8f, 21.6f, -2.5f), P_METAL, 0x3B);
        mb.box(vec3(4.8f, 20.8f, -4.f), vec3(7.f, 21.6f, -2.5f), P_METAL, 0x3B);
        mb.box(vec3(-2.f, 3.1f, 7.f), vec3(2.f, 3.32f, 8.15f), P_TRIM, 0x3F & ~16);
      }
      break;
    }
    case EK_OFFICE: {
      plinth(mb, 9.f, 9.f, 0.3f);
      mb.box(vec3(-8.6f, 0.3f, -8.6f), vec3(8.6f, 4.0f, 8.6f), P_GLASS, 0x33);       // recessed lobby glazing
      mb.box(vec3(-9.f, 3.9f, -9.f), vec3(9.f, 4.4f, 9.f), P_TRIM, 0x3F);
      mb.box(vec3(-9.f, 4.4f, -9.f), vec3(9.f, 36.f, 9.f), P_GLASS, 0x3B);
      mb.box(vec3(-9.3f, 36.f, -9.3f), vec3(9.3f, 37.4f, 9.3f), P_TRIM, 0x3F);
      if (d1) mb.box(vec3(-5.f, 37.4f, -4.f), vec3(5.f, 39.8f, 4.f), P_DARK, 0x3B | 8);
      if (d0) for (int i = 0; i < 4; i++) { float x = i & 1 ? 9.f : -9.f, z = i & 2 ? 9.f : -9.f; mb.box(vec3(x - 0.35f, 4.4f, z - 0.35f), vec3(x + 0.35f, 36.f, z + 0.35f), P_TRIM); }
      break;
    }
    case EK_TOWER: {
      plinth(mb, 10.f, 10.f, 0.3f);
      mb.box(vec3(-10.f, 0.3f, -10.f), vec3(10.f, 30.f, 10.f), P_WALL, 0x3B);
      mb.box(vec3(-10.3f, 30.f, -10.3f), vec3(10.3f, 31.f, 10.3f), P_TRIM, 0x3F);
      mb.box(vec3(-8.f, 31.f, -8.f), vec3(8.f, 56.f, 8.f), P_WALL, 0x3B);
      mb.box(vec3(-8.3f, 56.f, -8.3f), vec3(8.3f, 57.f, 8.3f), P_TRIM, 0x3F);
      mb.box(vec3(-6.f, 57.f, -6.f), vec3(6.f, 74.f, 6.f), P_WALL, 0x3B);
      mb.box(vec3(-6.4f, 74.f, -6.4f), vec3(6.4f, 75.2f, 6.4f), P_TRIM, 0x3F);
      if (d1) mb.cyl(vec3(0, 75.2f, 0), vec3(0, 80.f, 0), 0.5f, 0.12f, 6, P_METAL, false, false);
      break;
    }
    case EK_SKYSCRAPER: {
      plinth(mb, 10.f, 10.f, 0.3f);
      mb.box(vec3(-10.5f, 0.3f, -10.5f), vec3(10.5f, 7.f, 10.5f), P_TRIM, 0x3B);
      int sg = 8;
      mb.cyl(vec3(0, 7.f, 0), vec3(0, 118.f, 0), 9.6f, 9.6f, sg, P_GLASS, false, false, 1, 1, PI / 8, false);
      mb.cyl(vec3(0, 118.f, 0), vec3(0, 127.f, 0), 9.6f, 6.6f, sg, P_GLASS, true, false, 1, 1, PI / 8, false);
      if (d1) mb.cyl(vec3(0, 127.f, 0), vec3(0, 135.f, 0), 1.0f, 0.05f, 6, P_METAL, false, false);
      if (d0) for (int i = 0; i < sg; i++) { float a = PI / 8 + i * 2 * PI / sg; vec3 c(cosf(a) * 9.7f, 0, sinf(a) * 9.7f); mb.box(vec3(c.x - 0.3f, 7.f, c.z - 0.3f), vec3(c.x + 0.3f, 118.f, c.z + 0.3f), P_TRIM); }
      break;
    }
    case EK_WAREHOUSE: {
      plinth(mb, 12.f, 9.f, 0.6f);
      mb.box(vec3(-12.f, 0.6f, -9.f), vec3(12.f, 6.6f, 9.f), P_WALL, 0x3B & ~8);
      mb.gable(-12.f, 12.f, -9.f, 9.f, 6.6f, 8.5f, false, d1 ? 0.4f : 0.f, P_ROOF, P_WALL);
      if (d1) { door(mb, -5.f, 9.f, 4.f, 4.6f, 0.6f); door(mb, 3.f, 9.f, 4.f, 4.6f, 0.6f); }
      if (d0) { mb.box(vec3(-9.f, 0.f, 9.f), vec3(9.f, 1.1f, 10.6f), P_TRIM, 0x3F); door(mb, 9.f, 9.f, 1.0f, 2.2f, 1.1f); }
      break;
    }
    case EK_BARN: {
      plinth(mb, 6.f, 9.f, 0.3f);
      mb.box(vec3(-6.f, 0.3f, -9.f), vec3(6.f, 4.5f, 9.f), P_WOOD, 0x3B & ~8);
      // gambrel roof: profile extruded along z
      float ov = d1 ? 0.35f : 0.f;
      vec2 prof[5] = {vec2(-6.4f, 4.25f), vec2(-4.6f, 7.6f), vec2(0.f, 10.f), vec2(4.6f, 7.6f), vec2(6.4f, 4.25f)};
      vec3 ref(0, 5.f, 0);
      for (int i = 0; i < 4; i++)
        mb.quad(vec3(prof[i].x, prof[i].y, -9.f - ov), vec3(prof[i + 1].x, prof[i + 1].y, -9.f - ov), vec3(prof[i + 1].x, prof[i + 1].y, 9.f + ov), vec3(prof[i].x, prof[i].y, 9.f + ov), P_ROOF, ref);
      for (int s = 0; s < 2; s++) {   // gable ends in planks
        float z = s ? 9.f : -9.f;
        vec3 c(0, 6.f, z);
        vec2 w[5] = {vec2(-6.f, 4.5f), vec2(-4.4f, 7.5f), vec2(0.f, 9.8f), vec2(4.4f, 7.5f), vec2(6.f, 4.5f)};
        for (int i = 0; i < 4; i++) mb.tri(vec3(w[i].x, w[i].y, z), vec3(w[i + 1].x, w[i + 1].y, z), c, P_WOOD, vec3(0, 6.f, 0));
        mb.tri(vec3(-6.f, 4.5f, z), vec3(6.f, 4.5f, z), c, P_WOOD, vec3(0, 6.f, 0));
      }
      if (d1) { door(mb, 0.f, 9.f, 4.4f, 4.0f, 0.3f); door(mb, 0.f, 9.f, 1.8f, 1.6f, 6.0f); }
      if (d0) { mb.box(vec3(-2.2f, 0.3f, 9.06f), vec3(2.2f, 0.5f, 9.14f), P_TRIM); mb.box(vec3(-2.2f, 4.1f, 9.06f), vec3(2.2f, 4.3f, 9.14f), P_TRIM); }
      break;
    }
    case EK_SILO: {
      int sg = lod == 0 ? 14 : lod == 1 ? 9 : 6;
      mb.box(vec3(-3.3f, -2.f, -3.3f), vec3(3.3f, 0.4f, 3.3f), P_TRIM, 0x3B);
      mb.cyl(vec3(0, 0.4f, 0), vec3(0, 16.f, 0), 3.f, 3.f, sg, P_METAL, false, false);
      blob(mb, lod == 0 ? 1 : 0, [&](vec3 d) { return vec3(d.x * 3.05f, 16.f + std::max(d.y, 0.f) * 2.6f, d.z * 3.05f); }, [](vec3, vec3) { return 1.f; }, P_METAL);
      if (d0) mb.box(vec3(2.95f, 0.4f, -0.3f), vec3(3.2f, 17.f, 0.3f), P_DARK, 0x3F);
      break;
    }
    case EK_CHURCH: {
      plinth(mb, 5.f, 13.f, 0.5f);
      mb.box(vec3(-5.f, 0.5f, -13.f), vec3(5.f, 7.f, 5.f), P_WALL, 0x3B & ~8);
      mb.gable(-5.f, 5.f, -13.f, 5.f, 7.f, 13.f, true, d1 ? 0.4f : 0.f, P_ROOF, P_WALL);
      mb.box(vec3(-2.6f, 0.5f, 5.f), vec3(2.6f, 18.f, 10.2f), P_WALL, 0x3B);
      if (d1) mb.box(vec3(-2.8f, 17.6f, 4.8f), vec3(2.8f, 18.3f, 10.4f), P_TRIM, 0x3F);
      // spire: four-sided pyramid
      { vec3 a(-2.6f, 18.3f, 5.f), b(2.6f, 18.3f, 5.f), c(2.6f, 18.3f, 10.2f), d(-2.6f, 18.3f, 10.2f), t(0, 27.4f, 7.6f), ref(0, 19.f, 7.6f);
        mb.tri(a, b, t, P_ROOF, ref); mb.tri(b, c, t, P_ROOF, ref); mb.tri(c, d, t, P_ROOF, ref); mb.tri(d, a, t, P_ROOF, ref); }
      if (d0) { mb.box(vec3(-0.08f, 27.2f, 7.52f), vec3(0.08f, 28.4f, 7.68f), P_METAL); mb.box(vec3(-0.45f, 27.8f, 7.52f), vec3(0.45f, 27.95f, 7.68f), P_METAL);
        door(mb, 0.f, 10.2f, 1.8f, 3.2f, 0.5f); }
      break;
    }
    case EK_WATERTOWER: {
      int sg = lod == 0 ? 14 : lod == 1 ? 9 : 6;
      for (int i = 0; i < 4; i++) { float a = PI * 0.25f + i * PI * 0.5f; vec3 f(cosf(a) * 3.6f, -1.f, sinf(a) * 3.6f), t(cosf(a) * 2.6f, 18.2f, sinf(a) * 2.6f);
        mb.cyl(f, t, 0.28f, 0.22f, lod == 0 ? 5 : 3, P_METAL, false, false); }
      if (d0) for (int i = 0; i < 4; i++) { float a0 = PI * 0.25f + i * PI * 0.5f, a1 = a0 + PI * 0.5f;
        for (int h = 0; h < 2; h++) { float y0 = 2.f + h * 8.f, y1 = y0 + 7.f, r0 = lerpf(3.6f, 2.6f, y0 / 19.f), r1 = lerpf(3.6f, 2.6f, y1 / 19.f);
          mb.cyl(vec3(cosf(a0) * r0, y0, sinf(a0) * r0), vec3(cosf(a1) * r1, y1, sinf(a1) * r1), 0.08f, 0.08f, 3, P_METAL, false, false); } }
      mb.cyl(vec3(0, 18.f, 0), vec3(0, 25.f, 0), 5.2f, 5.2f, sg, P_METAL, false, true);
      mb.cyl(vec3(0, 25.f, 0), vec3(0, 28.f, 0), 5.3f, 0.f, sg, P_METAL, false, false);
      if (d0) mb.cyl(vec3(0, 18.f, 0), vec3(0, 18.2f, 0), 6.f, 6.f, sg, P_DARK, true, true);
      break;
    }
    case EK_LIGHTHOUSE: {
      int sg = lod == 0 ? 16 : lod == 1 ? 10 : 6;
      mb.box(vec3(-3.4f, -2.f, -3.4f), vec3(3.4f, 1.5f, 3.4f), P_TRIM, 0x3B);
      mb.cyl(vec3(0, 1.5f, 0), vec3(0, 22.f, 0), 3.0f, 2.1f, sg, P_WALL, false, false);
      mb.cyl(vec3(0, 22.f, 0), vec3(0, 22.45f, 0), 2.9f, 2.9f, sg, P_DARK, true, true);
      mb.cyl(vec3(0, 22.45f, 0), vec3(0, 25.f, 0), 1.6f, 1.6f, sg, P_LAMP, false, false);
      blob(mb, lod == 0 ? 1 : 0, [&](vec3 d) { return vec3(d.x * 1.75f, 25.f + std::max(d.y, 0.f) * 2.f, d.z * 1.75f); }, [](vec3, vec3) { return 1.f; }, P_METAL);
      if (d0) { mb.box(vec3(-2.6f, 1.5f, 2.6f), vec3(-1.4f, 3.6f, 2.95f), P_DOOR); mb.box(vec3(-6.f, -1.f, -4.f), vec3(-3.4f, 4.f, 4.f), P_WALL, 0x3B & ~8);
        mb.gable(-6.f, -3.4f, -4.f, 4.f, 4.f, 5.4f, true, 0.3f, P_ROOF, P_WALL); }
      break;
    }
    case EK_RWYLIGHT: {   // elevated edge light: base plate, frangible stalk, glass globe over the lamp
      if (lod == 2) break;
      mb.cyl(vec3(0, -0.3f, 0), vec3(0, 0.04f, 0), 0.09f, 0.08f, 8, P_METAL, true, false);
      mb.cyl(vec3(0, 0.04f, 0), vec3(0, 0.3f, 0), 0.022f, 0.02f, 6, P_METAL, false, false);
      mb.cyl(vec3(0, 0.29f, 0), vec3(0, 0.33f, 0), 0.05f, 0.05f, 8, P_DARK, true, false);   // lamp holder
      int seg = d0 ? 10 : 6;
      for (int i = 0; i < 3; i++) {   // the globe, as three stacked frusta
        float y0 = 0.33f + i * 0.03f, y1 = y0 + 0.03f;
        float a0 = i / 3.f, a1 = (i + 1) / 3.f, r0 = 0.06f * sqrtf(std::max(0.f, 1.f - a0 * a0)), r1 = 0.06f * sqrtf(std::max(0.f, 1.f - a1 * a1));
        mb.cyl(vec3(0, y0, 0), vec3(0, y1, 0), r0 + 0.002f, r1 + 0.002f, seg, P_RLAMP, i == 2, false);
      }
      break;
    }
    case EK_PAPI: {   // precision approach path indicator: a light box on two legs, two lenses facing the approach
      if (lod == 2) break;
      for (int s = -1; s <= 1; s += 2) mb.box(vec3(s * 0.3f - 0.03f, -0.3f, -0.03f), vec3(s * 0.3f + 0.03f, 0.4f, 0.03f), P_METAL);
      mb.box(vec3(-0.42f, 0.4f, -0.3f), vec3(0.42f, 0.78f, 0.25f), P_TRIM, 0x3F);
      mb.box(vec3(-0.44f, 0.78f, -0.33f), vec3(0.44f, 0.8f, 0.3f), P_DARK, 0x3F);   // sun hood
      for (int s = -1; s <= 1; s += 2) mb.cyl(vec3(s * 0.2f, 0.59f, 0.25f), vec3(s * 0.2f, 0.59f, 0.27f), 0.11f, 0.11f, d0 ? 14 : 8, P_PAPI, true, false);
      break;
    }
    case EK_GASSTATION: {
      mb.box(vec3(-8.f, -1.f, -7.f), vec3(8.f, 0.15f, 7.f), P_TRIM, 0x3F);                 // forecourt slab
      mb.box(vec3(-4.5f, 0.15f, -7.f), vec3(4.5f, 3.6f, -3.2f), P_WALL, 0x3B);           // kiosk
      mb.box(vec3(-4.f, 0.4f, -3.2f), vec3(4.f, 2.8f, -3.12f), P_GLASS, 32);
      mb.box(vec3(-7.6f, 4.6f, -1.6f), vec3(7.6f, 5.5f, 6.6f), P_CANOPY, 0x3F);           // canopy
      if (d1) for (int i = 0; i < 4; i++) { float x = i & 1 ? 5.5f : -5.5f, z = i & 2 ? 4.6f : 0.4f; mb.box(vec3(x - 0.25f, 0.15f, z - 0.25f), vec3(x + 0.25f, 4.6f, z + 0.25f), P_TRIM); }
      if (d0) {
        for (int i = 0; i < 3; i++) { float x = -4.f + i * 4.f; mb.box(vec3(x - 0.4f, 0.15f, 2.2f), vec3(x + 0.4f, 1.7f, 2.8f), P_METAL, 0x3B | 8); }
        mb.box(vec3(6.6f, 0.15f, 6.4f), vec3(7.0f, 5.f, 6.8f), P_TRIM); mb.box(vec3(5.8f, 5.f, 6.45f), vec3(7.8f, 7.f, 6.75f), P_SIGN, 0x3F);
        door(mb, 2.5f, -3.12f, 1.6f, 2.3f, 0.15f);
      }
      break;
    }
  }
}
// ------------------------------------------------------------------ airport buildings and furniture
// thin plate through four corners (root leading / trailing, tip trailing / leading), thickness 2|off| along off
