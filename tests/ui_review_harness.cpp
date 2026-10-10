// Production UI review capture on surfaceless EGL; no display server required.
// ui modes intentionally omit the 3D scene and print this limitation to stdout.
#include "../src/game.h"
#include "../src/menu_layout.h"
#include "../src/hangar_catalog.h"
#include "../src/models.h"
#include <dlfcn.h>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <chrono>
static void* lib; static void* (*eglProc)(const char*);
static void* proc(const char* n) { void* p = eglProc(n); return p ? p : dlsym(lib,n); }
template<class F> static F sym(const char*n){return reinterpret_cast<F>(dlsym(lib,n));}
static bool initGL(int W,int H){
  lib=dlopen("libEGL.so.1",RTLD_NOW|RTLD_GLOBAL);if(!lib)return false;
  eglProc=sym<void*(*)(const char*)>("eglGetProcAddress");if(!eglProc)return false;
  auto gd=reinterpret_cast<void*(*)(unsigned,void*,const int*)>(eglProc("eglGetPlatformDisplayEXT"));if(!gd)return false;
  void*d=gd(0x31DD,nullptr,nullptr);int ma,mi;
  if(!sym<unsigned(*)(void*,int*,int*)>("eglInitialize")(d,&ma,&mi))return false;
  sym<unsigned(*)(unsigned)>("eglBindAPI")(0x30A2);
  const int ca[]={0x3033,1,0x3040,8,0x3024,8,0x3023,8,0x3022,8,0x3038};void*c;int n;
  if(!sym<unsigned(*)(void*,const int*,void**,int,int*)>("eglChooseConfig")(d,ca,&c,1,&n)||!n)return false;
  const int sa[]={0x3057,W,0x3056,H,0x3038},at[]={0x3098,3,0x30FB,3,0x30FD,1,0x3038};
  void*s=sym<void*(*)(void*,void*,const int*)>("eglCreatePbufferSurface")(d,c,sa);
  void*x=sym<void*(*)(void*,void*,void*,const int*)>("eglCreateContext")(d,c,nullptr,at);const char*m=nullptr;
  return x&&sym<unsigned(*)(void*,void*,void*,void*)>("eglMakeCurrent")(d,s,s,x)&&glLoad(proc,&m);
}
struct GameTest {
  static void checkInteractions(Game& g) {
    auto require=[](bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(3);}};
    auto frame=[&](){g_ren.uiBegin();FrameParams fp{};g.drawResearch(fp);g_ren.uiEnd();g.in=Input{};};
    const int ids[]={kNightjar,kMantis,kResearchJet,kWraith};
    require(g.resCraft==kResearchJet,"original research default is XR-30");
    g.debugScene("research10");
    for(int cycle=0;cycle<3;cycle++) for(int i=0;i<4;i++){
      require(g.resCraft==ids[i],"research keyboard sequence");
      g.in.pressed[K_TAB]=true;frame();
    }
    // Exercise the original terminal's adaptive cards, not the discarded redesign's fixed rows.
    auto L=g.researchLayout();const float s=g.S();
    int nCardsMax=0;
    for(int craft:ids){int n=0;for(int i=0;i<Game::kNumResCards;i++)if(Game::kResCards[i].craft==craft)n++;nCardsMax=std::max(nCardsMax,n);}
    const float cardY=L.top+22*s,listH=(30+18*(nCardsMax+1)+40)*s;
    const float cardH=clampf((L.bot-cardY-12*s*3-listH)/4,64*s,150*s);
    for(int cycle=0;cycle<3;cycle++) for(int i=0;i<4;i++){
      g.in.mx=L.lx+20*s;g.in.my=cardY+i*(cardH+12*s)+cardH*.5f;g.in.mPressed[0]=true;frame();
      require(g.resCraft==ids[i],"research mouse airframe selection");
    }
    g.resCard=0;g.resCraft=kWraith;frame();require(g.resCard==-1,"incompatible research test cleared");
    g.resAirport=0;g.in.pressed[K_LEFT]=true;frame();require(g.resAirport==(int)g_world.airports.size()-1,"site wraps left");
    g.in.pressed[K_RIGHT]=true;frame();require(g.resAirport==0,"site wraps right");
    const float x1=g_ren.W-24*s,hs=s*std::clamp((x1-L.lx)/(1232.f*s),.55f,1.f);
    const float c2=L.lx+(16+112+290+40)*hs,c3=c2+182*hs,chipY=L.bot+(16+12+16+17)*s;
    g.in.mx=c2+(90+38)*hs;g.in.my=chipY;g.in.mPressed[0]=true;frame();require(!g.resAirborne,"runway start hit target");
    g.in.mx=c2+43*hs;g.in.my=chipY;g.in.mPressed[0]=true;frame();require(g.resAirborne,"airborne start hit target");
    for(int i=0;i<3;i++){g.in.mx=c3+(80*i+38)*hs;g.in.my=chipY;g.in.mPressed[0]=true;frame();require(g.resWx==i,"weather hit target");}
    g.in.pressed[K_ESC]=true;frame();require(g.screen==SCR_MENU,"research escape returns to menu");
    printf("PASS original research XR-30 default, repeated keyboard/mouse selections, incompatible test reset, site wrapping, start position, weather, escape\n");
  }
  static void checkScrollFocus(Game& g) {
    auto require=[](bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(3);}};
    g.career.money=10000000;g.career.license=LIC_ATP;
    g.career.fleet.push_back({0,g.career.location,50,.5f});g.screen=SCR_HUB;
    for(int tab:{TAB_HANGAR,TAB_AIRLINE}){
      g.hubTab=tab;g.selHangar=1;g.focusNav=false;g.focusList.clear();
      g_ren.uiBegin();g.drawHub();g_ren.uiEnd();
      require(!g.focusList.empty(),"scroll region registers buttons");
      std::vector<Game::Focusable> inspector;for(const auto& f:g.focusList)if(f.x>g_ren.W*.5f)inspector.push_back(f);require(!inspector.empty(),"inspector registers economic actions");
      auto target=*std::max_element(inspector.begin(),inspector.end(),[](const Game::Focusable& a,const Game::Focusable& b){return a.y<b.y;});
      g.focusId=target.id;g.focusNav=true;
      for(int frame=0;frame<3;frame++){
        g.focusList.clear();g_ren.uiBegin();g.drawHub();g_ren.uiEnd();
        auto it=std::find_if(g.focusList.begin(),g.focusList.end(),[&](const Game::Focusable& f){return f.id==target.id;});
        require(it!=g.focusList.end()&&g.focusId==target.id&&g.focusNav,"scroll preserves keyboard focus identity");
        if(frame==2)require(it->y>=0&&it->y+it->h<=g_ren.H,"focused bottom action scrolls on screen");
      }
      printf("PASS tab %d offscreen action focus identity and reachability\n",tab);
    }
  }
  static void checkLoading(Game& g) {
    auto require=[](bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(3);}};
    const auto oldPending=g_ren.entPending;
    require(!g_ren.tshRequiredPending(1.f),"missing optional shadow shader does not block readiness");
    require(!g_ren.tshRequiredPending(-.1f),"night shadow does not block readiness");
    g_ren.entPending=0;
    g.in=Input{};g.loadFrames=2;g.loadObservedFrame=-1;g.loadStableFrames=0;g.loadReadyT=-1;g.loadShown=0;g.loadBakeSeen=g_ren.bakeCount;
    for(int i=0;i<30;i++)g.updateLoading(1.f/30);
    require(g.loadReadyT<0&&g.loadStableFrames==1&&g.loadShown<1,"repeated updates without a rendered frame cannot complete loading");
    g.loadFrames=3;g.updateLoading(1.f/30);require(g.loadReadyT>=0&&g.loadShown==1,"distinct stable completed frame permits readiness");
    g_ren.entPending=oldPending;
    printf("PASS loading requires distinct completed frames; absent optional shadow shader cannot deadlock\n");
  }
  static void checkHangarFraming(Game& g) {
    for(int row=0;row<hangarCatalogCount();row++){
      const int i=hangarSpecAt(row);g.selHangar=i;FrameParams fp{};g.hangarPreviewCamera(fp);
      const float s=g.S(),W=g_ren.W,H=g_ren.H;
      const auto L=hangarLayout(24*s,126*s,W-48*s,H-146*s,s);
      const auto& a=kAircraft[i];float th=tanf(fp.fovY*.5f);
      const auto& md=kModels[i];const auto gst=gearStations(a);const float gh=fp.plane.M[19*4],wr=a.special?.38f:md.wheelR;
      const float nr=a.special?.33f:a.taildragger?.1f:md.gear==3?wr*.75f:wr*.85f;
      const vec3 centres[]={vec3(-gst.track,wr-gh,gst.mainZ),vec3(gst.track,wr-gh,gst.mainZ),vec3(0,a.taildragger?-gh+.11f*a.fusLen+.1f:nr-gh,a.taildragger?gst.tailZ:gst.noseZ)};
      for(int wheel=0;wheel<3;wheel++){const auto c=centres[wheel];float bottom=fp.plane.pos.y+fp.plane.rot[1]*c.x+fp.plane.rot[4]*c.y+fp.plane.rot[7]*c.z-(wheel==2?nr:wr);if(fabsf(bottom)>.0001f){fprintf(stderr,"FAIL parked tyre contact aircraft%d wheel%d bottom%.6f\n",i,wheel,bottom);exit(3);}}
      if(fp.plane.propCount>0 && (fp.plane.Pr[1]!=0||fp.plane.Pr[0]!=0)){fprintf(stderr,"FAIL parked prop has motion blur/spin\n");exit(3);}

      for(vec3 b:{vec3(a.span*.5f,0,0),vec3(-a.span*.5f,0,0),vec3(0,0,a.fusLen*.5f),vec3(0,0,-a.fusLen*.5f),vec3(0,a.fusRad*3,0)}){
        const float* r=fp.plane.rot;
        vec3 p=fp.plane.pos+vec3(r[0]*b.x+r[3]*b.y+r[6]*b.z,r[1]*b.x+r[4]*b.y+r[7]*b.z,r[2]*b.x+r[5]*b.y+r[8]*b.z)-fp.camPos;
        float z=-dot(p,fp.camBack),x=W*.5f+dot(p,fp.camRight)/z/th*H*.5f,y=H*.5f-dot(p,fp.camUp)/z/th*H*.5f;
        if(z<=0||x<L.previewX||x>L.previewX+L.previewWidth||y<L.previewY||y>L.previewY+L.previewHeight){fprintf(stderr,"FAIL hangar %d dimension endpoint outside preview: %.1f %.1f\n",i,x,y);exit(3);}
      }
    }
    printf("PASS all %d catalog aircraft dimension endpoints fit hangar preview; tyres grounded within0.1mm and props stationary\n",hangarCatalogCount());
  }
  static void checkCatalog(Game& g) {
    auto require=[](bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(3);}};
    g.screen=SCR_HUB;g.hubTab=TAB_HANGAR;g.selHangar=0;g.in=Input{};g.career.money=10000000;g.career.license=LIC_ATP;
    const auto money=g.career.money;const auto fleet=g.career.fleet.size();
    auto id=[](int spec){uint32_t h=2166136261u;auto mix=[&](uint32_t v){h^=v;h*=16777619u;};mix(0);mix(spec);for(char c:std::string("hangar-catalog-airframe"))mix((uint8_t)c);return h;};
    auto frame=[&](){g.gamepadMenus(1.f/60.f);g.focusNavigate();g_ren.uiBegin();g.drawHub();g_ren.uiEnd();g.in=Input{};};
    frame();g.focusId=id(0);g.focusNav=true;
    for(int row=0;row<hangarCatalogCount();row++){
      if(row){g.in.pressed[K_DOWN]=true;frame();}
      const int spec=hangarSpecAt(row);require(g.focusId==id(spec),"keyboard traverses13-row catalog in designation order");
      g.in.pressed[K_ENTER]=true;frame();require(g.selHangar==spec,"Enter selects focused catalog row");
      if(hangarResearchLocked(spec)){
        const float ss=g.S();const auto L=hangarLayout(24*ss,126*ss,g_ren.W-48*ss,g_ren.H-146*ss,ss);
        for(const auto& f:g.focusList)require(!(f.x>=L.rightX&&f.y>=126*ss),"classified inspector exposes no economic controls");
      }
    }
    for(int row=hangarCatalogCount()-2;row>=0;row--){g.in.pad=true;g.in.buttonsPressed=PAD_UP;frame();const int spec=hangarSpecAt(row);require(g.focusId==id(spec),"D-pad traverses catalog in reverse order");g.in.pad=true;g.in.buttonsPressed=PAD_A;frame();require(g.selHangar==spec,"gamepad A selects focused catalog row");}
    for(int row=kNumAircraft;row<hangarCatalogCount();row++){int spec=hangarSpecAt(row);g.focusId=id(spec);g.focusNav=true;frame();auto it=std::find_if(g.focusList.begin(),g.focusList.end(),[&](const Game::Focusable& f){return f.id==id(spec);});require(it!=g.focusList.end()&&it->y>=0&&it->y+it->h<=g_ren.H,"classified row scrolls on screen");g.in.mx=it->x+it->w*.5f;g.in.my=it->y+it->h*.5f;g.in.mPressed[0]=true;frame();require(g.selHangar==spec,"mouse selects all4classified rows");}
    require(g.career.money==money&&g.career.fleet.size()==fleet,"catalog selection never mutates economy");
    puts("PASS complete catalog keyboard/Enter and D-pad/A traversal, all4classified mouse targets, no classified economic controls");
  }
  static void checkFreeFlightUI(Game& g) {
    auto require=[](bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(3);}};
    auto id=[](int value,const char* label){uint32_t h=2166136261u;auto mix=[&](uint32_t v){h^=v;h*=16777619u;};mix(0);mix(value);for(const char* c=label;*c;c++)mix((uint8_t)*c);return h;};
    const auto money=g.career.money;const auto fleet=g.career.fleet.size();const int location=g.career.location,story=g.career.storyIndex;
    g.beginFreeFlightSetup();g.freeCraft=0;g.freeAirport=0;g.in=Input{};
    auto frame=[&](){g.gamepadMenus(1.f/60);g.focusNavigate();g_ren.uiBegin();FrameParams fp{};g.drawFreeFlightSetup(fp);g_ren.uiEnd();g.in=Input{};};
    frame();g.focusId=id(0,"free-flight-airframe");g.focusNav=true;
    for(int i=0;i<kNumAircraft;i++){if(i){g.in.pressed[K_DOWN]=true;frame();}require(g.focusId==id(careerSpecAt(i),"free-flight-airframe"),"free-flight keyboard reaches every career aircraft");g.in.pressed[K_ENTER]=true;frame();require(g.freeCraft==careerSpecAt(i),"free-flight selects each aircraft without licence gate");g.selHangar=kWraith;FrameParams preview{};g.hangarPreviewCamera(preview);require(preview.plane.model==careerSpecAt(i)&&preview.hangarPreview&&!preview.hangarClassified,"free-flight actual preview uses freeCraft, never career/classified selection");}
    g.focusId=id(0,"free-flight-airport");g.focusNav=true;frame();
    for(int i=0;i<(int)g_world.airports.size();i++){if(i){g.in.pressed[K_DOWN]=true;frame();}require(g.focusId==id(i,"free-flight-airport"),"free-flight keyboard reaches every airport");g.in.pressed[K_ENTER]=true;frame();require(g.freeAirport==i,"free-flight selects each airport");}
    const float ss=g.S();auto L=hangarLayout(24*ss,126*ss,g_ren.W-48*ss,g_ren.H-146*ss,ss);float px=L.rightX+18*ss,iw=L.rightWidth-36*ss,yy=g_ren.H-189*ss;
    g.in.mx=px+iw*.75f;g.in.my=yy+40*ss;g.in.mPressed[0]=true;frame();require(g.freeAirborne,"airborne setup hit target");
    g.in.mx=px+iw*.25f;g.in.my=yy+40*ss;g.in.mPressed[0]=true;frame();require(!g.freeAirborne,"runway setup hit target");
    g.in.pressed[K_ESC]=true;frame();require(g.screen==SCR_MENU,"free-flight escape returns main menu");
    require(g.career.money==money&&g.career.fleet.size()==fleet&&g.career.location==location&&g.career.storyIndex==story,"free-flight UI leaves career untouched");
    printf("PASS free-flight %d aircraft + %zu airport keyboard selection, runway/airborne and Escape, career unchanged\n",kNumAircraft,g_world.airports.size());
  }
  static void setup(Game& g,const std::string& sc,float scale) {
    g.realTime=30;g.uiDt=1.f;g.set.uiScale=scale;g.assetDir="assets";g.saveDir=(std::filesystem::temp_directory_path()/"solace-ui-review-settings").string();std::filesystem::create_directories(g.saveDir);
    if(!g.iconTex){int w,h;std::vector<uint8_t> px;if(readImage(getenv("UI_ICON")?getenv("UI_ICON"):"assets/icon.png",w,h,px))g.iconTex=g_ren.makeTexture(px.data(),w,h);}
    if(sc.rfind("freeflight",0)==0){g.beginFreeFlightSetup();g.freeCraft=atoi(sc.c_str()+10);g.freeAirborne=sc.find("airborne")!=std::string::npos;}
    if(sc.rfind("research",0)==0||sc=="menuT5")g.debugScene(sc);
    if(sc.rfind("hangar",0)==0){g.screen=SCR_HUB;g.hubTab=TAB_HANGAR;g.selHangar=atoi(sc.c_str()+6);}
    if(sc.find("airline_active")!=std::string::npos){g.career.money=1000000;g.career.license=LIC_ATP;g.career.storyIndex=(int)g_story.size();for(int i=0;i<5;i++){g.career.fleet.push_back({i,g.career.location,50,.9f});g.career.airline.pilots.push_back({"Review pilot "+std::to_string(i+1),2,200});}}
    if(sc=="logbook"){g.screen=SCR_HUB;g.hubTab=TAB_LOGBOOK;}
    if(sc=="menu_saved"||sc=="menu_overwrite"){g.hasSave=true;g.confirmNew=sc=="menu_overwrite";}
    if(sc=="contracts"){g.screen=SCR_HUB;g.hubTab=TAB_CONTRACTS;}
    if(sc.rfind("airline",0)==0){g.screen=SCR_HUB;g.hubTab=TAB_AIRLINE;}
    if(sc=="settings"||sc=="controls"){g.screen=SCR_HUB;g.hubTab=TAB_SETTINGS;g.settingsPage=sc=="controls"?1:0;}
    if(sc.rfind("loading",0)==0){
      g.contract=g_story[0];g.plane.spec=&kAircraft[0];g.wx=g.contract.wx;
      g.loadT=5;g.loadShown=.57f;g.loadMap=true;g.screen=SCR_LOADING;
      sscanf(sc.c_str(),"loading_%f_%f",&g.loadShown,&g.loadT);
      if(sc.find("ready")!=std::string::npos){g.loadReadyT=g.loadT;g.loadShown=1;}
      // No map shader is initialized in UI-only mode. Real packaged photographs remain available.
    }
  }
  static void scrollSetup(Game& g,const std::string& sc,int frame) {
    g.focusList.clear();
    if(frame==1 && sc.find("scroll")!=std::string::npos){g.in.mx=g_ren.W*.85f;g.in.my=g_ren.H*.5f;g.in.wheel=-100;}
  }
  static void draw(Game& g,const std::string& sc) {
    if(sc.rfind("menu",0)==0)g.drawMenu();
    else if(sc=="pause")g.drawPause();
    else if(sc.rfind("freeflight",0)==0){FrameParams fp{};g.drawFreeFlightSetup(fp);}
    else if(sc.rfind("intro",0)==0){float p=.4f,t=5;sscanf(sc.c_str(),"intro_%f_%f",&p,&t);g.shaderFirstRun=true;g.drawIntro(p,fmt("Preparing GPU shader cache: Terrain program 4 of %d | Building islands",Renderer::kProgramCount),t,g.iconTex,1.f);}
    else if(sc.rfind("loading",0)==0)g.drawLoading();
    else if(sc.rfind("research",0)==0){FrameParams fp{};g.drawResearch(fp);}
    else g.drawHub();
  }
};
int main(int argc,char**argv){
  setvbuf(stdout,nullptr,_IONBF,0);
  if(argc<3){fprintf(stderr,"Usage: ui_review_harness scenes(comma separated) output-dir [W H ui-scale scene-mode]\n");return 2;}
  const int W=argc>3?atoi(argv[3]):1920,H=argc>4?atoi(argv[4]):1080;
  const float scale=argc>5?atof(argv[5]):1;const bool full=argc>6&&std::string(argv[6])=="scene";
#ifdef UI_REVIEW_FLEET_ONLY
  {std::istringstream scenes(argv[1]);std::string sc;
    while(std::getline(scenes,sc,',')){bool fleetHangar=sc.rfind("hangar",0)==0&&atoi(sc.c_str()+6)>=0&&atoi(sc.c_str()+6)<kAircraftCount;bool freeSetup=sc.rfind("freeflight",0)==0&&atoi(sc.c_str()+10)>=0&&isCareerAircraft(atoi(sc.c_str()+10));bool allowed=fleetHangar||freeSetup||sc=="research10"||sc=="research20"||sc=="menuT5";if(!full||!allowed){fprintf(stderr,"Fleet diagnostic supports career hangar, research10/20, or fixed menuT5 only in scene mode.\n");return 2;}}}
  printf("DIAGNOSTIC: exact standard fleet render passes; eight unused generic/research/marched startup programs omitted; pre-baked actual mesh with runtime no-march assertions. Not full-startup validation.\n");
#endif
  std::filesystem::create_directories(argv[2]);
  if(!initGL(W,H)){puts("surfaceless EGL init failed");return 1;}
  printf("GL: %s; %dx%d; UI scale %.2f; %s\n",glGetString(GL_RENDERER),W,H,scale,full?"actual full scene":"LAYOUT PREVIEW: production UI, 3D backdrop omitted");
  const std::string cache=getenv("SHADERCACHE")?getenv("SHADERCACHE"):"";
  if(!cache.empty())std::filesystem::create_directories(cache);
  g_world.build(cache.empty()?"":cache+"/world.bin","ui-review-world-v1");buildStory();
  g_shaderCacheDir=cache;g_ren.matDir="assets/materials";g_ren.renderScale=1;g_ren.quality=1;g_ren.entSync=true;
  if(!(full?g_ren.init(W,H,[](float p,const std::string& stage){printf("INIT %.1f%% %s\n",p*100,stage.c_str());}):g_ren.initUI(W,H))){fprintf(stderr,"renderer init: %s\n",g_ren.error.c_str());return 1;}
  if(!g_shaderNotes.empty())printf("PRODUCTION SHADER NOTES: %s\n",g_shaderNotes.c_str());
  g_ren.bakeYield=[](){static auto last=std::chrono::steady_clock::now();auto now=std::chrono::steady_clock::now();if(std::chrono::duration<double>(now-last).count()>10){puts("Production mesh bake batch completed; continuing");last=now;}};
  std::istringstream input(argv[1]);std::string sc;
  while(std::getline(input,sc,',')){
    Game* g=new Game();g->initHeadless();GameTest::setup(*g,sc,scale);
    if(sc=="interactions"){GameTest::checkInteractions(*g);GameTest::checkScrollFocus(*g);GameTest::checkLoading(*g);GameTest::checkHangarFraming(*g);GameTest::checkCatalog(*g);GameTest::checkFreeFlightUI(*g);delete g;continue;}
    for(int f=0;f<(full?(getenv("FRAMES")?atoi(getenv("FRAMES")):1):3);f++){
      GameTest::scrollSetup(*g,sc,f);
      if(full){g->render();}else{
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,W,H);glClearColor(.014f,.025f,.042f,1);glClear(GL_COLOR_BUFFER_BIT);
        g_ren.uiBegin();GameTest::draw(*g,sc);g_ren.uiEnd();
      }
    }
    glFinish();const std::string path=std::string(argv[2])+"/"+sc+".png";
    if(!g_ren.screenshotPNG(path.c_str()))return 1;
    printf("WROTE %s\n",path.c_str());delete g;
  }
  return 0;
}
