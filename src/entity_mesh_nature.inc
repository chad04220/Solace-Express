// Included in entity_mesh.cpp's anonymous namespace. Close detail is LOD 3;
// 0/1/2 retain the near/middle/far slots used by the scenery distance policy.
// All branching, weathering and leaf placement is deterministic and in metres.

// A continuously joined, gently fluted stem. Shared rings avoid the visible gaps
// produced by putting independent tapered cylinders end to end.
template<class Path, class Radius>
void natureStem(MB& mb, Path path, Radius radius, int rings, int sides, float seed, float ao0=.7f) {
  std::vector<vec3> p((rings+1)*sides), n(p.size());
  for(int j=0;j<=rings;++j) {
    float t=float(j)/rings;
    vec3 c=path(t), axis=normalize(path(std::min(1.f,t+.002f))-path(std::max(0.f,t-.002f)));
    vec3 side=normalize(cross(axis,fabsf(axis.y)<.92f?vec3(0,1,0):vec3(1,0,0))), front=cross(axis,side);
    for(int i=0;i<sides;++i) {
      float a=i*2*PI/sides;
      // Buttress flutes diminish along the stem instead of making a perfectly round pole.
      float flute=1.f+(.045f+.12f*powf(1-t,4.f))*sinf(a*5.f+seed);
      vec3 d=side*cosf(a)+front*sinf(a);
      p[j*sides+i]=c+d*(radius(t)*flute);
      n[j*sides+i]=normalize(d+axis*.018f);
    }
  }
  for(int j=0;j<rings;++j) for(int i=0;i<sides;++i) {
    int ids[6]={j*sides+i,j*sides+(i+1)%sides,(j+1)*sides+(i+1)%sides,j*sides+i,(j+1)*sides+(i+1)%sides,(j+1)*sides+i};
    for(int id:ids) mb.vert(p[id],n[id],P_BARK,lerpf(ao0,.94f,float(id/sides)/rings));
  }
}

void natureLimb(MB& mb, vec3 a, vec3 bend, vec3 tip, float radius, int sides, int rings=3) {
  auto path=[&](float t){return a*((1-t)*(1-t))+bend*(2*t*(1-t))+tip*(t*t);};
  natureStem(mb,path,[&](float t){return lerpf(radius,std::max(.006f,radius*.13f),t);},rings,sides,a.x+a.y*1.3f,.63f);
}

// Two intersecting tapered ribbons suffice for terminal wood a few millimetres
// thick; large limbs above always use actual joined tubes.
void natureTwig(MB& mb,vec3 a,vec3 b,float radius,float ao=.8f) {
  vec3 axis=normalize(b-a),side=normalize(cross(axis,fabsf(axis.y)<.95f?vec3(0,1,0):vec3(1,0,0))),up=cross(axis,side);
  for(vec3 d:{side,up}) {
    vec3 n=normalize(cross(d,axis));
    mb.vert(a-d*radius,n,P_BARK,ao);mb.vert(a+d*radius,n,P_BARK,ao);mb.vert(b,n,P_BARK,.93f);
  }
}

void natureRoots(MB& mb,float radius,int count,float seed) {
  for(int i=0;i<count;++i) {
    float a=i*2*PI/count+.3f*hash2i(i,int(seed*719)),r=radius*(2.2f+.8f*hash2i(i,31));
    vec3 d(cosf(a),0,sinf(a));
    natureLimb(mb,vec3(0,radius*1.7f,0),d*(r*.34f)+vec3(0,.08f,0),d*r+vec3(0,-.12f,0),radius*.4f,4,2);
  }
}

// A small spray, never a crown-sized card. u's integer part is a stable pattern
// variant; its fractional part and v retain the shared alpha-mask/shadow contract.
void natureSpray(MB& mb,vec3 root,vec3 tip,vec3 side,float width,vec3 crown,float ao,int tag) {
  vec3 axis=tip-root;
  side=normalize(side-axis*(dot(side,axis)/std::max(dot(axis,axis),1e-6f)));
  vec3 normal=normalize(cross(side,axis));
  vec3 outward=normalize((root+tip)*.5f-crown+vec3(0,.14f,0));
  if(dot(normal,outward)<0) normal=-normal;
  normal=normalize(normal*.72f+outward*.28f);
  vec3 p[4]={root-side*(width*.5f),root+side*(width*.5f),tip+side*(width*.5f),tip-side*(width*.5f)};
  const int ids[6]={0,1,2,0,2,3};
  const float uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
  for(int id:ids) mb.vert(p[id],normal,P_LEAFCARD,ao,uv[id][0]+float(tag%8),uv[id][1]);
}

void natureLeafCluster(MB& mb,vec3 c,vec3 crown,float size,int cards,float seed,float ao) {
  for(int i=0;i<cards;++i) {
    float a=i*2.399963f+seed*4.31f,z=hash2i(i*7+1,int(seed*7331))*1.7f-.8f;
    vec3 dir=normalize(vec3(cosf(a),z,sinf(a))),side=normalize(cross(dir,vec3(.17f,1.f,.13f)));
    float roll=hash2i(i*3+9,int(seed*1951))*2*PI;
    side=side*cosf(roll)+cross(dir,side)*sinf(roll);
    float sz=size*(.82f+.3f*hash2i(i+31,int(seed*2917)));
    vec3 p=c+dir*(sz*.12f);
    natureSpray(mb,p-dir*(sz*.48f),p+dir*(sz*.48f),side,sz,crown,ao,i+int(seed*17));
  }
}

// Needle sprays follow wood: a central shoot with short secondary laterals and
// overlapping, differently rolled needles. There are no opaque pillows at close range.
void natureNeedleShoot(MB& mb,vec3 a,vec3 tip,vec3 side,float width,vec3 crown,int tag,bool detailed) {
  vec3 axis=tip-a,dir=normalize(axis),up=normalize(cross(side,dir));
  natureSpray(mb,a,tip,side,width,crown,.8f,tag);
  natureSpray(mb,a+axis*.08f,tip+up*(width*.13f),normalize(side*.32f+up),width*.86f,crown,.87f,tag+3);
  if(detailed) {
    natureSpray(mb,a+axis*.03f,tip+up*(width*.09f),normalize(side*.72f-up*.7f),width*.92f,crown,.83f,tag+5);
    natureTwig(mb,a,tip,.009f,.73f);
  }
}

void conifer(MB& mb,int lod,float H,float R,float yBase,int tiers,float taper,float seed) {
  const bool close=lod==3,spruce=H>17.f;
  if(lod==2) {
    // Keep the inexpensive 12-triangle distant silhouette and its old envelope.
    mb.cyl({0,yBase*.6f,0},{0,H,0},R*.95f,0,6,P_NEEDLE,false,true,.8f,.95f);
    return;
  }
  auto stem=[&](float t){return vec3(.065f*sinf(t*3.2f+seed)*t,H*t,.07f*sinf(t*4.1f)*t);};
  float br=R*.075f+.07f;
  natureStem(mb,[&](float t){vec3 p=stem(t);if(t==0)p.y=-1.f;return p;},[&](float t){return lerpf(br,.017f,powf(t,.72f));},close?12:lod==0?6:2,close?8:lod==0?5:3,seed);
  if(close) natureRoots(mb,br,5,seed);
  int levels=close?(spruce?16:12):lod==0?(spruce?10:8):(spruce?6:5);
  int radial=close?6:lod==0?6:5;
  for(int l=0;l<levels;++l) {
    for(int j=0;j<radial;++j) {
      int branch=l*radial+j,id=l*19+j;
      // Branch insertion rises in an irregular phyllotactic spiral. Six limbs
      // at one identical height made rigid, disconnected shelves at every view.
      float t=(l+(j+.15f+.6f*hash2i(id,92))/radial)/levels;
      // Reserve room for the top secondary sprays while the live leader still
      // reaches H. Random upward shoots must not grow beyond the retained crown.
      float y=yBase+(H-yBase-(close?.32f:0.f))*t;
      float r=R*powf(1-t,spruce?.83f:.73f)*(.89f+.11f*hash2i(l,int(seed*519)));
      float az=branch*2.399963f+seed+.42f*(hash2i(id,21)-.5f);
      vec3 dir(cosf(az),0,sinf(az)),side(-dir.z,0,dir.x);
      float reach=r*(.81f+.19f*hash2i(id,71));
      vec3 root=stem(y/H),tip=root+dir*reach+vec3(0,-reach*(spruce?.28f:.13f),0);
      vec3 bend=root+dir*(reach*.47f)+vec3(0,reach*.065f,0);
      if(close) {
        natureLimb(mb,root,bend,tip,lerpf(.065f,.018f,t),3,2);
        int pairs=3;
        for(int k=0;k<pairs;++k) for(int s:{-1,1}) {
          float u=.17f+.33f*k+.12f*(hash2i(id*7+k,s+53)-.5f);
          // Anchor to the actual two-segment branch surface, rather than
          // to an unsampled curve that can miss thin wood between its rings.
          vec3 mid=root*.25f+bend*.5f+tip*.25f;
          vec3 attach=u<.5f?lerp(root,mid,u*2):lerp(mid,tip,(u-.5f)*2);
          float length=(reach*(.27f+.07f*hash2i(id,k+9))+.36f+.24f*hash2i(id+k,s+27))*(1.f-.18f*u);
          float pitch=(spruce?-.95f:-.75f)+1.02f*hash2i(id+k*7,s+43);
          vec3 end=attach+normalize(dir*(.31f+.37f*hash2i(id,k+87))+side*(s*(.5f+.4f*hash2i(id,k+66)))+vec3(0,pitch,0))*length;
          float width=std::max(.28f,std::min(.49f,length*.62f));
          float roll=(hash2i(id*3+k,s+81)-.5f)*2*PI;
          vec3 spraySide=side*cosf(roll)+vec3(0,1,0)*sinf(roll);
          natureNeedleShoot(mb,attach,end,spraySide,width,vec3(0,H*.53f,0),id+k*7,true);
        }
        natureNeedleShoot(mb,tip-dir*std::min(.7f,reach*.36f),tip+dir*.13f+vec3(0,.21f,0),side,std::max(.32f,reach*.17f),vec3(0,H*.53f,0),id+5,true);
      } else {
        // Individual layered boughs, with empty space between them, replace cones
        // and round masses even in the medium-distance forest.
        vec3 shoulder=bend+vec3(0,.07f,0),left=shoulder-side*(reach*.25f),right=shoulder+side*(reach*.22f);
        float ao=.62f+.3f*t;
        vec3 n=normalize(vec3(0,1,0)+dir*.35f);
        for(vec3 p:{root,left,tip,root,tip,right}) mb.vert(p,n,P_NEEDLE,ao);
        if(lod==0) {
          natureTwig(mb,root,tip,.025f*(1-t)+.007f,.72f);
          natureNeedleShoot(mb,bend,tip+dir*.12f,side,std::max(.2f,reach*.4f),vec3(0,H*.53f,0),id,false);
          natureSpray(mb,bend-side*(reach*.1f),tip+side*(reach*.22f),normalize(side+vec3(0,.5f,0)),std::max(.18f,reach*.24f),vec3(0,H*.53f,0),.82f,id+2);
        }
      }
    }
  }
  // Short leafy terminal shoots root into the continuous live stem.
  int leaders=close?5:3;
  for(int i=0;i<leaders;++i) {
    float a=i*2.399963f+seed;
    vec3 side(cosf(a),0,sinf(a));
    natureSpray(mb,stem(.935f),stem(1.f)+side*.035f,side,close?.28f:.36f,vec3(0,H*.7f,0),.94f,i);
  }
}

void natureBroadleaf(MB& mb,int kind,int lod) {
  const bool close=lod==3,birch=kind==EK_BIRCH,bush=kind==EK_BUSH;
  const float H=kEntInfo[kind].h;
  vec3 crown=bush?vec3(0,.8f,0):birch?vec3(0,9.f,0):vec3(0,7.f,0);
  if(lod==2) {
    if(bush) clump(mb,crown,1.5f,.6f,0,P_LEAF,crown,-.2f,1.8f,.7f);
    else {
      clump(mb,crown,birch?2.5f:4.75f,birch?1.55f:.78f,0,P_LEAF,crown,birch?5.f:3.f,H,birch?.4f:.2f);
      trunk(mb,{0,-1,0},{0,birch?7.f:4.f,0},birch?.2f:.45f,birch?.12f:.3f,3);
    }
    return;
  }
  const vec3 oakC[7]={{0,8.4f,0},{2.55f,6.7f,.4f},{-2.45f,6.9f,.9f},{.6f,6.4f,2.7f},{-.5f,6.8f,-2.7f},{1.8f,7.8f,-1.8f},{-1.8f,7.9f,-1.5f}};
  const float oakR[7]={2.65f,2.25f,2.35f,2.2f,2.25f,1.95f,1.95f};
  const vec3 birchC[9]={{.2f,11.4f,-.1f},{-1.f,9.f,-.4f},{1.1f,9.9f,.5f},{.4f,7.4f,1.1f},{-.3f,10.4f,-1.1f},{-1.2f,7.8f,.6f},{1.2f,8.f,-.8f},{.5f,6.4f,-.3f},{-.4f,12.1f,.5f}};
  const float birchR[9]={1.13f,1.25f,1.3f,1.19f,1.16f,1.1f,1.15f,.96f,.77f};
  const vec3 bushC[4]={{0,1.05f,0},{.71f,.65f,.2f},{-.61f,.68f,.43f},{.1f,.57f,-.72f}};
  const float bushR[4]={.81f,.66f,.7f,.67f};
  int cells=bush?4:birch?9:7;
  auto stem=[&](float t){return vec3((birch?.17f:.11f)*t*t,H*(birch?.965f:.39f)*t,-.09f*t*t);};
  if(!bush) {
    float r=birch?.21f:.52f;
    natureStem(mb,[&](float t){vec3 p=stem(t);if(t==0)p.y=-1.f;return p;},[&](float t){return r*lerpf(1.f,birch?.12f:.52f,t);},close?(birch?14:7):lod==0?5:2,close?10:lod==0?6:4,.61f);
    if(close) natureRoots(mb,r,birch?4:6,.41f);
  }
  for(int i=0;i<cells;++i) {
    vec3 c=bush?bushC[i]:birch?birchC[i]:oakC[i];
    float r=bush?bushR[i]:birch?birchR[i]:oakR[i],squash=birch?1.02f:bush?.75f:.71f;
    float seed=.17f+i*.719f+kind*.11f;
    vec3 a=bush?vec3(0,-.08f,0):birch?stem(clampf((c.y-1.8f)/(H*.965f),0,1)):stem(.64f+.052f*i);
    vec3 b=c+vec3(0,-r*.4f,0);
    if(close||lod==0) natureLimb(mb,a,(a+b)*.5f+vec3(0,birch?.42f:.65f,0),b,bush?.045f:birch?.052f:.17f,close?5:3,close?3:1);
    if(close) {
      int arms=bush?7:birch?7:9;
      for(int j=0;j<arms;++j) {
        float az=j*2.399963f+i*.89f,dy=lerpf(-1.35f,1.85f,hash2i(j+13,i+97));
        vec3 d=normalize(vec3(cosf(az),dy,sinf(az))),side(-sinf(az),0,cosf(az));
        vec3 fork=c+vec3(d.x*r*.58f,d.y*r*squash*.86f,d.z*r*.58f);
        natureLimb(mb,b,(b+fork)*.5f+vec3(0,r*.12f,0),fork,bush?.016f:birch?.025f:.055f,3,2);
        for(int k=0;k<3;++k) {
          float s=float(k-1);
          vec3 tip=fork+vec3(d.x*r*.21f,d.y*r*squash*.3f,d.z*r*.21f)+side*(s*r*.22f);
          if(birch) tip.y-=r*(.13f+.14f*hash2i(j,k+33)); // silver birch's hanging fine twigs
          natureTwig(mb,fork,tip,bush?.008f:.012f,.7f);
          // Five leaves across these 0.3-0.9 m cards yield centimetre-scale leaves,
          // with genuine holes through the crown and branch-supported outer sprays.
          float sz=bush?.38f:birch?.59f:.88f;
          natureLeafCluster(mb,tip,crown,sz,bush?5:birch?4:6,seed+j*.17f+k*.39f,.68f+.22f*hash2i(j,k+i*7));
          // Live foliage also grows along the outer branch, not only in isolated
          // balls at the twig ends. Interleaved interior sprays close the crown
          // naturally while leaving the branch junctions readable.
          natureLeafCluster(mb,lerp(b,tip,.64f),crown,sz*.93f,bush?3:birch?2:4,seed+j*.31f+k*.47f+.13f,.68f+.14f*hash2i(j,k+i*11));
        }
      }
    } else if(lod==0) {
      // A small dark interior backs many finer sprays. It is under half the visible
      // crown radius, so the outer silhouette remains leaves, not a polygon balloon.
      clump(mb,c,r*.43f,squash,0,P_LEAF,crown,bush?-.2f:birch?5.f:3.8f,H,seed,.12f);
      leafCards(mb,c,r*.82f,squash,bush?22:birch?27:38,crown,bush?-.2f:birch?5.f:3.8f,H,seed,bush?.73f:birch?.62f:.43f);
    } else {
      // Fewer lobes only at the distance where leaf blades are subpixel. Keeping
      // every canopy centre preserves the characteristic species silhouette.
      clump(mb,c,r*.96f,squash,0,P_LEAF,crown,bush?-.2f:birch?5.f:3.8f,H,seed,.2f);
    }
  }
}

void naturePine(MB& mb,int lod) {
  const bool close=lod==3;const float H=kEntInfo[EK_PINE].h;
  vec3 crown(.25f,H*.8f,.12f);
  auto path=[&](float t){return vec3(.25f*t*t+.08f*sinf(t*4),H*t,.16f*t*t);};
  if(lod==2) {
    trunk(mb,{0,-1,0},path(.7f),.3f,.18f,3);
    clump(mb,path(.86f),2.5f,.55f,0,P_NEEDLE,crown,crown.y-3,H,.2f,.3f);
    clump(mb,path(.7f)+vec3(.8f,0,1.2f),2.35f,.5f,0,P_NEEDLE,crown,crown.y-3,H,.5f,.3f);
    clump(mb,path(.72f)+vec3(-.9f,0,-1.5f),2.25f,.5f,0,P_NEEDLE,crown,crown.y-3,H,.8f,.3f);
    return;
  }
  natureStem(mb,[&](float t){vec3 p=path(t*.98f);if(t==0)p.y=-1;return p;},[](float t){return lerpf(.37f,.035f,powf(t,.8f));},close?14:lod==0?7:3,close?9:lod==0?6:4,.7f);
  if(close) natureRoots(mb,.37f,5,.9f);
  int whorls=close?6:lod==0?5:4,per=close?4:3;
  for(int w=0;w<whorls;++w) for(int j=0;j<per;++j) {
    int branch=w*per+j;
    // A light-demanding pine keeps its lower bole bare, but its live crown is
    // irregular, not six identical horizontal rings of terminal pom-poms.
    float t=(w+(j+.15f+.5f*hash2i(branch,93))/per)/whorls;
    float y=H*lerpf(.50f,.91f,t),az=branch*2.399963f+.45f*(hash2i(branch,71)-.5f);
    float L=lerpf(2.8f,1.1f,t)*(.87f+.18f*hash2i(w*9+j,21));
    vec3 a=path(y/H),dir(cosf(az),0,sinf(az)),side(-dir.z,0,dir.x),b=a+dir*L+vec3(0,lerpf(.5f,.25f,t),0);
    vec3 bend=a+dir*(L*.5f)+vec3(0,-.12f,0);
    if(close||lod==0) natureLimb(mb,a,bend,b,.105f*(1-.45f*t),close?4:3,close?3:1);
    if(close) {
      vec3 joints[4]={a,a*(4.f/9)+bend*(4.f/9)+b*(1.f/9),a*(1.f/9)+bend*(4.f/9)+b*(4.f/9),b};
      for(int q=0;q<7;++q) {
        float u=.35f+.085f*q+.06f*(hash2i(branch,q+29)-.5f);
        float along=u*3;int segment=std::min(int(along),2);
        // Root each secondary shoot on a sampled main-limb segment. The same
        // seven shoots now fill its outer length instead of sharing one tip.
        vec3 attach=lerp(joints[segment],joints[segment+1],along-segment);
        float sign=q%2?1.f:-1.f;
        vec3 d=normalize(dir*(.45f+.35f*hash2i(q,branch+53))+side*(sign*(.45f+.3f*hash2i(branch,q+37)))+vec3(0,.15f+.72f*hash2i(branch,q+87),0));
        vec3 tip=attach+d*(.70f+.24f*hash2i(q,branch+37)+(1-u)*.2f);
        natureTwig(mb,attach,tip,.014f,.72f);
        for(int k=0;k<3;++k) {
          float roll=k*PI/3+hash2i(q,branch+19)*PI;
          vec3 u=normalize(cross(d,vec3(0,1,0))),v=cross(d,u);
          natureSpray(mb,attach+d*.02f,tip+d*.17f,u*cosf(roll)+v*sinf(roll),.62f,crown,.76f+.05f*k,q+j*3+k);
        }
      }
    } else if(lod==0) {
      clump(mb,b,.53f,.53f,0,P_NEEDLE,crown,crown.y-3,H,.1f*w+.03f*j,.18f);
      leafCards(mb,b,.86f,.65f,13,crown,crown.y-3,H,.11f*w+.03f*j,.57f);
    } else clump(mb,b,1.05f,.60f,0,P_NEEDLE,crown,crown.y-3,H,.11f*w+.03f*j,.25f);
  }
  if(close||lod==0) {
    for(int j=0;j<(close?12:6);++j) {
      float a=j*2.399963f;vec3 d(cosf(a)*.38f,.45f,sinf(a)*.38f);
      natureNeedleShoot(mb,path(.94f),path(.94f)+d,normalize(vec3(-d.z,0,d.x)),.37f,crown,j,close);
    }
  } else clump(mb,path(.95f),1.1f,.7f,0,P_NEEDLE,crown,crown.y-3,H,.9f,.2f);
}

void naturePalm(MB& mb,int lod) {
  const bool close=lod==3;
  auto path=[](float t){return vec3(1.3f*t*t,8.8f*t,.25f*t*t);};
  int rings=close?18:lod==0?9:lod==1?4:2,sides=close?10:lod==0?7:4;
  natureStem(mb,[&](float t){vec3 p=path(t);if(t==0)p.y=-1;return p;},[&](float t){return lerpf(.31f,.19f,t)+.012f*sinf(t*80.f);},rings,sides,.31f,.8f);
  vec3 crown=path(1.f);
  int fronds=close?15:lod==0?11:lod==1?7:5,segments=lod==0?8:lod==1?4:2;
  for(int f=0;f<fronds;++f) {
    float a=f*2.399963f+.3f;
    vec3 dir(cosf(a),0,sinf(a)),side(-sinf(a),0,cosf(a));
    float len=4.05f*(.88f+.2f*hash2i(f,77)),lift=1.f+.65f*hash2i(f,13);
    auto F=[&](float t){return crown+dir*(len*t)+vec3(0,lift*t-3.05f*t*t,0);};
    if(close) {
      // Separate, V-folded leaflets and a real rachis. Open slots extend to the
      // midrib; broad, uniformly perforated strips cannot fake this silhouette.
      for(int j=0;j<10;++j) natureTwig(mb,F(float(j)/10),F(float(j+1)/10),lerpf(.022f,.004f,float(j)/10),.85f);
      for(int j=0;j<22;++j) for(int s:{-1,1}) {
        float t=.085f+.88f*(float(j)+.16f*(s==1))/22;
        vec3 root=F(t);
        float length=(.16f+.83f*powf(std::max(0.f,sinf(PI*t)),.8f))*(.9f+.14f*hash2i(j,f+41));
        vec3 tip=root+side*(s*length)+dir*(length*.31f)-vec3(0,length*(.19f+.2f*t),0);
        vec3 mid=root+(tip-root)*.48f+vec3(0,.045f,0),across=dir*(.028f+.014f*(1-t));
        // Negative v explicitly marks geometry leaflets; both passes skip the
        // old alpha mask while the material uses abs(v) for natural tip aging.
        vec3 n=normalize(cross(side*float(s),tip-root+vec3(0,.15f,0)));if(n.y<0)n=-n;
        auto emit=[&](vec3 p,float u){mb.vert(p,n,P_FROND,.82f+.14f*t,u,-t);};
        emit(root,0);emit(mid-across,float(s)*.5f);emit(tip,float(s));
        emit(root,0);emit(tip,float(s));emit(mid+across,float(s)*.5f);
      }
    } else for(int j=0;j<segments;++j) {
      float t0=float(j)/segments,t1=float(j+1)/segments;
      auto width=[](float t){return .09f+.76f*powf(std::max(0.f,sinf(PI*std::min(t*1.07f,1.f))),.7f);};
      float w0=width(t0),w1=width(t1);vec3 c0=F(t0),c1=F(t1),n=normalize(cross(side,c1-c0));if(n.y<0)n=-n;
      vec3 p[4]={c0-side*w0-vec3(0,.25f*w0,0),c0+side*w0-vec3(0,.25f*w0,0),c1+side*w1-vec3(0,.25f*w1,0),c1-side*w1-vec3(0,.25f*w1,0)};
      int ids[6]={0,1,2,0,2,3};float us[4]={-1,1,1,-1},vs[4]={t0,t0,t1,t1};
      for(int id:ids)mb.vert(p[id],n,P_FROND,.7f+.3f*vs[id],us[id],vs[id]);
    }
  }
  if(close||lod==0) for(int i=0;i<5;++i) {
    float a=i*2.399963f;vec3 c=crown+vec3(cosf(a)*.27f,-.25f,sinf(a)*.27f);
    blob(mb,close?1:0,[&](vec3 d){return c+vec3(d.x*.16f,d.y*.23f,d.z*.16f);},[](vec3,vec3){return .73f;},P_BARK);
  }
}

void buildTree(MB& mb,int kind,int lod) {
  const EntKindInfo& I=kEntInfo[kind];
  switch(kind) {
    case EK_FIR:conifer(mb,lod,I.h,I.hx,1.6f,8,.86f,.3f);break;
    case EK_SPRUCE:conifer(mb,lod,I.h,I.hx,.9f,11,.9f,1.7f);break;
    case EK_PINE:naturePine(mb,lod);break;
    case EK_OAK:case EK_BIRCH:case EK_BUSH:natureBroadleaf(mb,kind,lod);break;
    case EK_PALM:naturePalm(mb,lod);break;
  }
}

// ------------------------------------------------------------------ rocks
// Rounded fracture intersections, not a sphere pushed towards an axis-aligned
// cube. The same continuous shape is sampled at every LOD, so weathering cannot
// move whole strata or change a boulder's silhouette when its topology changes.
void rockBlob(MB& mb,int sub,vec3 c,vec3 s,float yaw,float tilt,float seed,float noiseAmp,float angular,float strata=0,float topCut=1e9f) {
  float cy=cosf(yaw),sy=sinf(yaw),ct=cosf(tilt),st=sinf(tilt);
  vec3 joint[10];float distance[10];
  for(int j=0;j<10;++j) {
    float y=1.f-2.f*(j+.5f)/10.f,a=j*2.399963f+seed*1.73f,r=sqrtf(1-y*y);
    joint[j]=vec3(cosf(a)*r,y,sinf(a)*r);
    distance[j]=.79f+.15f*hash2i(j+21,int(seed*997));
  }
  blob(mb,sub,[&](vec3 d) {
    float fracture=1.22f;
    for(int j=0;j<10;++j) {
      float projection=dot(d,joint[j]);
      if(projection>.001f) fracture=std::min(fracture,distance[j]/projection);
    }
    float k=lerpf(1.f,fracture,angular);
    vec3 q=d*k;
    float broad=fbm3(q*2.2f+vec3(seed*11,seed*5,-seed*3),3)-.5f;
    float grain=fbm3(q*9.3f+vec3(seed*23,-seed*7,seed*5),2)-.5f;
    // Grain-scale chips ride a much quieter weathered surface than the former
    // inflatable noise. Narrow clefts follow two oblique joint families.
    float seam=fabsf(sinf(dot(q,vec3(2.7f,1.8f,-2.3f))+seed*8));
    float cleft=powf(std::max(0.f,1.f-seam*11.f),3.f);
    k=1.f+noiseAmp*(broad*.92f+grain*.20f)-angular*.042f*cleft;
    vec3 p(q.x*s.x*k,q.y*s.y*k,q.z*s.z*k);
    if(strata>0) {
      float bed=p.y*strata+.18f*p.x+.13f*p.z+seed*9;
      float phase=bed-floorf(bed),ledge=powf(std::max(0.f,1.f-fabsf(phase-.22f)*7.f),2.f);
      float erosion=1.f+.035f*ledge-.019f*powf(std::max(0.f,1.f-fabsf(phase-.32f)*11.f),2.f);
      p.x*=erosion;p.z*=erosion;
    }
    p={p.x,p.y*ct-p.z*st,p.y*st+p.z*ct};
    p={cy*p.x+sy*p.z,p.y,-sy*p.x+cy*p.z};p+=c;
    if(p.y>topCut)p.y=topCut+(p.y-topCut)*.08f;
    return p;
  },[&](vec3,vec3 d){return clampf(.78f+.18f*d.y,.57f,1.f);},P_ROCK,false,nullptr,0,seed,angular*.63f);
}

void rockColumn(MB& mb,vec3 base,int segs,int rings,float H,float R0,float R1,float seed,float strata,float notch,float lean) {
  std::vector<vec3> p((rings+1)*segs);
  for(int j=0;j<=rings;++j) {
    float t=float(j)/rings;
    for(int i=0;i<segs;++i) {
      float a=i*2*PI/segs,ca=cosf(a),sa=sinf(a);
      // Persistent, tall joint faces survive every sampled resolution. Bedding
      // ledges and wave-cut recesses are coherent geology, not random ring sizes.
      float outline=1.f+.095f*sinf(3*a+seed*7)+.055f*cosf(5*a-seed*3);
      float facet=1.f;
      for(int k=0;k<5;++k) {
        float b=k*2*PI/5+seed,projection=ca*cosf(b)+sa*sinf(b);
        if(projection>.01f)facet=std::min(facet,(.89f+.07f*hash2i(k,int(seed*711)))/projection);
      }
      // Bedding is unevenly spaced and tilted across fracture faces. Strong
      // periodic full-circumference ledges made these read as stacked tires.
      float bed=t*H/(seed>.6f?4.7f:3.5f)+.28f*ca-.21f*sa+seed*5;
      bed+=.19f*sinf(t*H*.47f+seed*9)+.10f*sinf(a*3+t*2.7f+seed*13);
      float phase=bed-floorf(bed);
      float ledge=powf(std::max(0.f,1.f-fabsf(phase-.27f)*5.f),2.f);
      float recess=powf(std::max(0.f,1.f-fabsf(phase-.43f)*7.f),2.f);
      float exposed=.18f+.26f*smoothstepf(-.45f,.65f,sinf(a*2.3f+floorf(bed)*1.7f+seed*11));
      float broad=fbm3(vec3(ca*1.4f+seed*7,t*2.2f,sa*1.4f),3)-.5f;
      float joint=fabsf(sinf(a*4.5f+seed*11+.15f*sinf(t*9)));
      float split=powf(std::max(0.f,1.f-joint*9.f),2.f);
      float r=lerpf(R0,R1,t)*outline*facet*(1.f+.28f*broad+strata*exposed*(ledge-.65f*recess)-.045f*split);
      if(t<.12f)r*=1.f-notch*(1.f-t/.12f);
      float rim=(.10f*sinf(a*3+seed*17)+.06f*cosf(a*7+seed*7))*R1*powf(t,12.f);
      p[j*segs+i]=base+vec3(ca*r+lean*t*t*H*.1f,H*t+rim,sa*r*.86f);
    }
  }
  for(int j=0;j<rings;++j)for(int i=0;i<segs;++i) {
    vec3 a=p[j*segs+i],b=p[j*segs+(i+1)%segs],c=p[(j+1)*segs+(i+1)%segs],d=p[(j+1)*segs+i];
    vec3 ref=base+vec3(lean*H*.025f,(a.y+c.y)*.5f-base.y,0);
    mb.quad(a,b,c,d,P_ROCK,ref,.73f+.23f*float(j)/rings);
  }
  vec3 top(0);for(int i=0;i<segs;++i)top+=p[rings*segs+i];top=top/float(segs);
  // The crown is a broken plateau, not the point of a cone.
  top.y-=R1*.035f;
  for(int i=0;i<segs;++i)mb.tri(p[rings*segs+i],p[rings*segs+(i+1)%segs],top,P_ROCK,top-vec3(0,2,0),.96f);
}

void buildRock(MB& mb,int kind,int lod) {
  const bool close=lod==3;
  int sub=close?3:lod==0?2:lod==1?1:0;
  switch(kind) {
    case EK_BOULDER:rockBlob(mb,sub,{0,.62f,0},{1,.78f,.92f},.3f,.1f,.21f,.26f,.63f);break;
    case EK_BLOCK:rockBlob(mb,sub,{0,.6f,0},{1.05f,.72f,.88f},1.1f,.12f,.57f,.19f,.87f);break;
    case EK_SLAB:rockBlob(mb,sub,{0,.3f,0},{1.55f,.42f,1.15f},.2f,.16f,.83f,.14f,.89f,6);break;
    case EK_OUTCROP:{
      struct B{vec3 c,s;float yaw,tilt;};B blocks[7]={
        {{0,2.4f,0},{4.2f,3.1f,3.6f},.2f,.05f},{{3.6f,1.6f,1.4f},{2.6f,2.2f,2.5f},.32f,.14f},
        {{-3.8f,1.5f,-1},{3,2,2.4f},.12f,.10f},{{.8f,5.6f,-.4f},{2.7f,2.4f,2.4f},.26f,.10f},
        {{-1.6f,4.9f,1.3f},{2.1f,2,1.8f},.18f,.08f},{{4.6f,.7f,-2.8f},{1.7f,1.2f,1.5f},2.2f,.3f},{{-5.6f,.5f,2.4f},{1.4f,1,1.3f},.1f,.25f}};
      int n=close||lod==0?7:lod==1?5:3;
      if(lod==2){blocks[0].c.y=4.3f;blocks[0].s.y=4.8f;}
      for(int i=0;i<n;++i)rockBlob(mb,close?2:lod==0?1:0,blocks[i].c,blocks[i].s,blocks[i].yaw,blocks[i].tilt,.13f*i+.2f,.19f,.77f,i<5?1.7f:0);
      break;
    }
    case EK_SPIRE:{
      rockColumn(mb,{0,-1,0},close?28:lod==0?14:lod==1?9:6,close?42:lod==0?14:lod==1?7:3,lod==2?24.2f:22.5f,4.2f,lod==2?2.5f:1.9f,.37f,.16f,0,.4f);
      if(lod!=2)rockBlob(mb,close?2:lod==0?1:0,{.5f,22.4f,.1f},{3.1f,1.3f,2.6f},.4f,.08f,.61f,.17f,.8f);
      if(close||lod==0)for(int i=0;i<3;++i)rockBlob(mb,close?1:0,{cosf(i*2.2f)*4.2f,.4f,sinf(i*2.2f)*3.8f},{1.4f,1,1.2f},i*1.1f,.2f,.4f+i*.2f,.19f,.72f);
      break;
    }
    case EK_SEASTACK:{
      rockColumn(mb,{0,-1,0},close?36:lod==0?18:lod==1?11:6,close?36:lod==0?11:lod==1?6:2,31,9.6f,7.2f,.71f,.065f,.18f,-.3f);
      rockColumn(mb,{11,-1,4},close?20:lod==0?10:lod==1?7:3,close?24:lod==0?8:lod==1?4:2,16,3.6f,2.6f,.23f,.09f,.2f,.5f);
      if(close||lod==0)for(int i=0;i<5;++i)rockBlob(mb,close?1:0,{cosf(i*1.7f)*10.5f,.3f,sinf(i*1.7f)*9.5f},{2.3f,1.5f,2},i*1.3f,.2f,.9f+i*.1f,.2f,.76f);
      break;
    }
  }
}
