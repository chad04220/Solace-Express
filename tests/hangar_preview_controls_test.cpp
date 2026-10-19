// Real showroom UI/camera regression; no window, GL context or aircraft mesh bake.
#include "../src/game.h"
#include "../src/hangar_catalog.h"
#include "../src/hangar_preview.h"
#include "../src/menu_layout.h"
#include "../src/models.h"
#include "../src/military_missions.h"
#include "test_world.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>

struct GameTest {
  static bool obstructed(vec3 from, vec3 to, const std::vector<hangarPreview::Vertex>& room) {
    const vec3 d = to - from;
    for (size_t i = 0; i + 2 < room.size(); i += 3) {
      const vec3 e1 = room[i+1].p - room[i].p, e2 = room[i+2].p - room[i].p, h = cross(d,e2);
      const float det = dot(e1,h); if (std::fabs(det) < 1e-6f) continue;
      const float inv = 1.f / det; const vec3 s = from - room[i].p;
      const float u = inv * dot(s,h); if (u < 0 || u > 1) continue;
      const vec3 q = cross(s,e1); const float v = inv * dot(d,q);
      if (v < 0 || u+v > 1) continue;
      const float t = inv * dot(e2,q); if (t > .0001f && t < .9999f) return true;
    }
    return false;
  }
  static vec3 transformed(const PlaneVisual& plane, vec3 p) {
    const float* r = plane.rot;
    return plane.pos + vec3(r[0]*p.x+r[3]*p.y+r[6]*p.z,
      r[1]*p.x+r[4]*p.y+r[7]*p.z,r[2]*p.x+r[5]*p.y+r[8]*p.z);
  }
  static int run() {
    buildTestWorld(); buildStory(); g_audio.init(48000);
    auto game = std::make_unique<Game>(); Game& g = *game;
    g.initHeadless(); g.diskless = true; g.set.traffic = false;
    g.screen = SCR_HUB; g.hubTab = TAB_HANGAR; g.uiDt = 1.f/60.f;
    g.researchView.yaw = .57f; g.researchView.pitch = .31f; g.researchView.zoom = 1.23f;
    g.resCraft = kSomnus; g.militarySelection = 1;
    g.career.flights = Game::kMilitaryFlights;   // (the Military board opens with ten flights logged: drawMilitaryLocked)
    const auto originalGun = g.practiceForward;
    const auto originalBomb = g.practiceBomb;
    const int originalWave = g.practiceWave;
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("somnium_hangar_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    auto snapshot = [&](const char* name) {
      const auto file = root/name; if (!g.career.save(file.string())) return std::string("SAVE FAILED");
      std::ifstream f(file,std::ios::binary); return std::string(std::istreambuf_iterator<char>(f),{});
    };
    const auto before = snapshot("before.sav");
    int checks = 0, failures = 0;
    auto check = [&](bool ok,const std::string& label) {
      ++checks; failures += !ok; if (!ok) fprintf(stderr,"FAIL hangar: %s\n",label.c_str());
    };
    auto near = [](float a,float b) { return std::fabs(a-b) < .0001f; };
    auto frame = [&]() {
      g.gamepadMenus(1.f/60.f); g.focusNavigate(); g_ren.uiBegin();
      FrameParams fp{};
      if (g.screen==SCR_FREE_FLIGHT) g.drawFreeFlightSetup(fp); else g.drawHub();
      g.hangarPreviewCamera(fp); g.in.endFrame();
    };
    const std::array<std::pair<int,int>,5> sizes = {{{1920,1080},{1280,720},{960,540},{800,600},{640,480}}};
    for (const auto size : sizes) for (float scale : {1.f,1.4f}) {
      g_ren.W = size.first; g_ren.H = size.second; g.set.uiScale = scale;
      const float s = g.S(), W = g_ren.W, H = g_ren.H;
      const auto L = hangarLayout(24*s,126*s,W-48*s,H-146*s,s);
      for (bool freeFlight : {false,true}) for (int row = 0; row < (freeFlight?kNumAircraft:hangarCatalogCount()); ++row) {
        const int craft = freeFlight?careerSpecAt(row):hangarSpecAt(row); const auto& a = kAircraft[craft];
        const std::string context = std::string(freeFlight?"free flight ":"career ")+std::to_string(size.first)+"x"+std::to_string(size.second)+" scale"+std::to_string(scale)+" aircraft"+std::to_string(craft);
        g.screen = freeFlight?SCR_FREE_FLIGHT:SCR_HUB;
        if (freeFlight) g.freeCraft = craft; else g.selHangar = craft;
        const int careerSelection = g.selHangar, airport = g.freeAirport;
        const bool airborne = g.freeAirborne;
        g.in = Input{}; g.focusNav = false; frame(); frame();
        check(near(g.hangarView.yaw,.7f) && near(g.hangarView.zoom,1),context+" selecting a model resets its view");
        std::vector<Game::Focusable> controls;
        for (const auto& f : g.focusList) if (f.x >= L.previewX && f.x+f.w <= L.previewX+L.previewWidth+.01f && f.y >= L.previewY+L.previewHeight)
          controls.push_back(f);
        check(controls.size()==5,context+" five native preview controls");
        if (controls.size()!=5) continue;
        bool bounds = true;
        for (const auto& f : controls) bounds &= f.w > 18 && f.h > 14 && f.x >= 0 && f.y >= 0 && f.x+f.w <= W && f.y+f.h <= H;
        check(bounds,context+" controls remain on screen and below aircraft stage");
        auto click = [&](int n) {
          g.in = Input{}; g.in.mx = controls[n].x+controls[n].w*.5f; g.in.my = controls[n].y+controls[n].h*.5f;
          g.in.mPressed[0] = true; frame();
        };
        click(0); check(near(g.hangarView.yaw,.7f-15*DEG),context+" mouse rotate left");
        click(1); check(near(g.hangarView.yaw,.7f),context+" mouse rotate right");
        click(2); check(near(g.hangarView.zoom,.9f),context+" mouse zoom out");
        click(3); check(near(g.hangarView.zoom,1),context+" mouse zoom in");
        click(0); click(2); click(4);
        check(near(g.hangarView.yaw,.7f) && near(g.hangarView.zoom,1),context+" mouse reset");
        g.in = Input{}; g.focusNav = true; g.focusId = controls[0].id; g.in.pressed[K_ENTER] = true; frame();
        check(near(g.hangarView.yaw,.7f-15*DEG),context+" keyboard Enter rotates");
        g.in.pressed[K_RIGHT] = true; frame();
        check(g.focusId==controls[1].id,context+" arrow focus reaches adjacent rotate button");
        g.in.pressed[' '] = true; frame();
        check(near(g.hangarView.yaw,.7f),context+" keyboard Space rotates");
        g.in = Input{}; g.in.pad = true; g.focusNav = true; g.focusId = controls[2].id;
        g.in.buttonsPressed = g.in.buttons = PAD_A; frame();
        check(near(g.hangarView.zoom,.9f),context+" controller A zooms");
        g.in.buttons = 0; g.in.buttonsPressed = PAD_RIGHT; frame();
        check(g.focusId==controls[3].id,context+" D-pad focus reaches adjacent zoom button");
        g.in.buttonsPressed = g.in.buttons = PAD_A; frame();
        check(near(g.hangarView.zoom,1),context+" controller A zooms in");
        g.in = Input{}; g.focusNav = true; g.focusId = controls[3].id;
        for (int i = 0; i < 24; ++i) { g.in.pressed[K_ENTER] = true; frame(); }
        check(near(g.hangarView.zoom,1.35f) && g.focusId==controls[3].id,context+" repeated zoom in clamps and retains focus");
        g.focusId = controls[2].id;
        for (int i = 0; i < 24; ++i) { g.in.pressed[K_ENTER] = true; frame(); }
        check(near(g.hangarView.zoom,.75f) && g.focusId==controls[2].id,context+" repeated zoom out clamps and retains focus");
        g.focusId = controls[4].id; g.in.pressed[K_ENTER] = true; frame();
        check(near(g.hangarView.yaw,.7f) && near(g.hangarView.zoom,1),context+" keyboard reset");
        g.hangarView.rotate(15*DEG); g.hangarView.magnify(.1f);
        g.focusId = controls[4].id; g.in.pad = true; g.in.buttonsPressed = g.in.buttons = PAD_A; frame();
        check(near(g.hangarView.yaw,.7f) && near(g.hangarView.zoom,1),context+" controller reset");
        g.in = Input{}; g.focusNav = false;
        FrameParams base{}; g.hangarPreviewCamera(base);
        const auto room = hangarPreview::geometry(base.hangarSize);
        bool clear = true, framed = true, grounded = true, safeEye = true, classified = true;
        for(float pitch:{-.5f,.15f,1.f}) {
          g.hangarView.pitch=pitch;FrameParams pitchBase{};g.hangarPreviewCamera(pitchBase);
        for (float zoom : {.75f,1.f,1.35f}) for (int turn = 0; turn < 24; ++turn) {
          g.hangarView.yaw = turn*15*DEG; g.hangarView.zoom = zoom;
          FrameParams fp{}; g.hangarPreviewCamera(fp);
          safeEye &= length(fp.camPos-pitchBase.camPos)<.0001f && fp.camPos.y<fp.hangarSize*.60f && std::fabs(fp.camPos.x)<fp.hangarSize*1.2f;
          classified &= fp.hangarClassified==hangarResearchLocked(craft) && (!fp.hangarClassified || (fp.plN==0 && fp.plane.lensN==0 && fp.sealedCockpit));
          const float th = tanf(fp.fovY*.5f);
          for (vec3 p : {vec3(),vec3(a.span*.5f,0,0),vec3(-a.span*.5f,0,0),vec3(0,0,a.fusLen*.5f),vec3(0,0,-a.fusLen*.5f),vec3(0,a.fusRad*3,0)}) {
            const vec3 world = transformed(fp.plane,p), rel = world-fp.camPos;
            clear &= !obstructed(fp.camPos,world,room);
            const float z = -dot(rel,fp.camBack), x = W*.5f+dot(rel,fp.camRight)/z/th*H*.5f, y = H*.5f-dot(rel,fp.camUp)/z/th*H*.5f;
            framed &= z>0 && x>=L.previewX && x<=L.previewX+L.previewWidth && y>=L.previewY && y<=L.previewY+L.previewHeight;
          }
          const auto gs = gearStations(a); const auto& md = kModels[craft];
          const float gh = fp.plane.M[19*4], wr = a.special?.38f:md.wheelR, nr = a.special?.33f:a.taildragger?.1f:md.gear==3?wr*.75f:wr*.85f;
          const vec3 centres[] = {vec3(-gs.track,wr-gh,gs.mainZ),vec3(gs.track,wr-gh,gs.mainZ),vec3(0,a.taildragger?-gh+.11f*a.fusLen+.1f:nr-gh,a.taildragger?gs.tailZ:gs.noseZ)};
          for (int wheel = 0; wheel < 3; ++wheel) grounded &= std::fabs(transformed(fp.plane,centres[wheel]).y-(wheel==2?nr:wr))<.0001f;
          grounded &= fp.plane.propCount==0 || (fp.plane.Pr[0]==0 && fp.plane.Pr[1]==0);
        }
        }
        check(clear,context+" all yaw/pitch/zoom room triangles leave aircraft sightlines clear");
        check(framed,context+" all yaw/pitch/zoom model bounds fit stage");
        check(grounded,context+" all yaw/pitch/zoom tyres stay grounded and props still");
        check(safeEye,context+" optical zoom never moves eye into walls or roof");
        check(classified,context+" silhouette lighting remains private");
        check(g.screen==(freeFlight?SCR_FREE_FLIGHT:SCR_HUB) && !g.freeFlight && g.freeAirport==airport && g.freeAirborne==airborne && g.selHangar==careerSelection,
          context+" viewing never launches, changes departure setup or changes career selection");
      }
    }
    // Free Flight uses the same room but never inherits the career viewer state.
    g.hangarView.yaw=1;g.hangarView.zoom=1.35f;
    g.screen=SCR_FREE_FLIGHT;g.freeCraft=0;FrameParams free{};g.hangarPreviewCamera(free);
    check(near(g.hangarView.yaw,.7f) && near(g.hangarView.zoom,1) && free.plane.model==0 && !free.hangarClassified,"Free Flight resets career view and selects its own unlocked craft");
    g.hangarView.yaw=1;g.hangarView.zoom=.75f;g.screen=SCR_HUB;g.hangarPreviewCamera(free);
    check(near(g.hangarView.yaw,.7f) && near(g.hangarView.zoom,1),"return to career resets Free Flight view");
    check(near(g.researchView.yaw,.57f) && near(g.researchView.pitch,.31f) && near(g.researchView.zoom,1.23f) && g.resCraft==kSomnus && g.militarySelection==1,
      "preview controls preserve research and military selection/camera");
    check(g.practiceForward==originalGun && g.practiceBomb==originalBomb && g.practiceWave==originalWave,"preview controls preserve practice equipment");

    // Cross-menu contract: real production controls, focus routing, camera and
    // input edges. Same stable five-button order, held behaviour and defaults.
    for (const auto size : sizes) for(float scale:{1.f,1.4f}) for(int mode=0;mode<4;++mode) {
      g_ren.W=size.first;g_ren.H=size.second;g.set.uiScale=scale;
      g.in=Input{};g.in.mx=g.in.my=-100;g.focusNav=false;g.set.menuMotion=true;
      if(mode==1) { const Career fixtureCareer=g.career;g.debugScene("research40");g.career=fixtureCareer; }
      else {g.screen=mode==3?SCR_FREE_FLIGHT:SCR_HUB;g.hubTab=mode==2?TAB_CONTRACTS:TAB_HANGAR;g.hubList=mode==2?2:0;}
      g.selHangar=g.freeCraft=0;g.militarySelection=military::LocalSurvey;
      auto& view=mode==1?g.researchView:mode==2?g.militaryView:g.hangarView;
      const uint32_t first=mode==1?0x7E5F0030u:mode==2?0x6D0020u:0x7E6F0030u;
      const std::string context=" shared menu "+std::to_string(mode)+" "+std::to_string(size.first)+" scale"+std::to_string(scale);
      auto camera=[&]() {FrameParams fp{};if(mode==1)g.researchPreviewCamera(fp);else if(mode==2)g.militaryPreviewCamera(fp);else g.hangarPreviewCamera(fp);return fp;};
      auto tick=[&](float dt=1.f/60.f,bool fullUpdate=false) {
        g.uiDt=dt;if(fullUpdate)g.update(dt);else {g.gamepadMenus(dt);g.focusNavigate();}const auto fp=camera();g_ren.uiBegin();
        if(mode==1)g.drawResearch(fp);else if(mode==2)g.drawMilitary(fp);else if(mode==3)g.drawFreeFlightSetup(fp);else g.drawHub();
        g.in.endFrame();
      };
      tick();tick();view.reset();
      float x,y,w,h,uiScale=g.S();
      if(mode==1) {const auto L=g.researchLayout();x=L.px0;y=L.top;w=L.px1-L.px0;h=L.bot-56*uiScale-y;}
      else if(mode==2) {const auto L=g.militaryLayout();uiScale=std::min(uiScale,std::min(g_ren.H/720.f,g_ren.W/1120.f));x=L.px0;y=L.top+79*uiScale;w=L.px1-L.px0;h=L.bot-56*uiScale-y;}
      else {const auto L=hangarLayout(24*uiScale,126*uiScale,g_ren.W-48*uiScale,g_ren.H-146*uiScale,uiScale);x=L.previewX;y=L.previewY;w=L.previewWidth;h=L.previewHeight;}
      auto find=[&](int i) {Game::Focusable out{};for(const auto& f:g.focusList)if(f.id==first+uint32_t(i))out=f;return out;};
      bool controls=true;
      for(int i=0;i<5;++i) {const auto f=find(i);controls&=f.id==first+uint32_t(i)&&f.x>=x&&f.x+f.w<=x+w+.01f&&f.y>=y+h&&f.y+f.h<=g_ren.H&&f.w>14*uiScale;if(i)controls&=f.x>find(i-1).x+find(i-1).w;}
      check(controls,"identical ordered strip outside native aircraft stage"+context);

      if(scale==1.f && (size.first==1920 || size.first==640)) for(bool pad:{false,true}) {
        g.in=Input{};g.in.pad=pad;g.focusNav=false;
        if(pad)g.in.buttonsPressed=PAD_DOWN;else g.in.pressed[K_DOWN]=true;
        tick();std::set<uint32_t> seen;std::vector<uint32_t> queue;
        if(g.focusNav){seen.insert(g.focusId);queue.push_back(g.focusId);}
        for(size_t at=0;at<queue.size() && at<256;++at)for(int direction=0;direction<4;++direction) {
          g.in=Input{};g.in.pad=pad;g.focusNav=true;g.focusId=queue[at];tick();
          if(pad){const unsigned directions[]={PAD_LEFT,PAD_RIGHT,PAD_UP,PAD_DOWN};g.in.buttonsPressed=directions[direction];}
          else {const int directions[]={K_LEFT,K_RIGHT,K_UP,K_DOWN};g.in.pressed[directions[direction]]=true;}
          tick();if(seen.insert(g.focusId).second)queue.push_back(g.focusId);
        }
        bool reachable=true;for(int i=0;i<5;++i)reachable&=seen.count(first+uint32_t(i))!=0;
        check(reachable,std::string(pad?"D-pad":"keyboard")+" navigation reaches every preview button from no focus"+context);
      }
      g.in=Input{};view.reset();
      g.focusNav=true;g.focusId=first;g.in.pressed[K_ENTER]=g.in.down[K_ENTER]=true;tick();
      const float firstYaw=view.yaw;
      for(int i=0;i<10;++i)tick();
      check(near(firstYaw,.7f-15*DEG)&&near(view.yaw,firstYaw),"held keyboard has initial action and repeat delay"+context);
      for(int i=0;i<24;++i)tick();
      check(view.yaw<firstYaw-15*DEG,"held keyboard repeats rotations"+context);
      g.in.down[K_ENTER]=false;tick();const float releasedYaw=view.yaw;for(int i=0;i<24;++i)tick();
      check(near(view.yaw,releasedYaw),"release stops repeats and view remains still"+context);
      view.reset();g.focusNav=false;const auto plus=find(3);g.in.mx=plus.x+plus.w*.5f;g.in.my=plus.y+plus.h*.5f;
      g.in.mPressed[0]=g.in.mDown[0]=true;tick();for(int i=0;i<60;++i)tick();
      check(near(view.zoom,MenuPreviewView::maxZoom),"held mouse zoom reaches shared upper bound"+context);
      g.in.mx=-100;tick();const float leftZoom=view.zoom;for(int i=0;i<24;++i)tick();
      check(view.held==-1&&near(view.zoom,leftZoom),"leaving held button cancels capture"+context);
      g.in=Input{};g.focusNav=true;g.focusId=first+2;view.reset();g.in.pad=true;g.in.buttonsPressed=g.in.buttons=PAD_A;tick();for(int i=0;i<60;++i)tick();
      check(near(view.zoom,MenuPreviewView::minZoom),"held controller A reaches shared lower bound"+context);
      g.focusId=first+4;g.in.buttons=0;tick();g.in.buttonsPressed=g.in.buttons=PAD_A;tick();
      check(near(view.yaw,.7f)&&near(view.pitch,.15f)&&near(view.zoom,1)&&!view.drag,"controller Reset restores common view and stops gestures"+context);
      g.in=Input{};g.focusNav=false;g.in.mx=x+w*.5f;g.in.my=y+h*.5f;g.in.mPressed[0]=g.in.mDown[0]=true;g.in.mdx=25;g.in.mdy=5;tick();
      check(view.drag&&near(view.yaw,.9f)&&near(view.pitch,.18f),"same two-axis drag sensitivity"+context);
      g.in.mDown[0]=false;g.in.wheel=-1;tick();check(near(view.zoom,.9f)&&!view.drag,"same additive wheel zoom"+context);
      g.in=Input{};g.in.pad=true;g.focusNav=true;g.focusId=first;g.in.rx=.8f;g.in.ry=.8f;g.in.rt=.8f;const float stickYaw=view.yaw,stickPitch=view.pitch,stickZoom=view.zoom;tick();
      check(view.yaw>stickYaw&&view.pitch<stickPitch&&view.zoom>stickZoom,"focused right stick rotates and trigger zooms without wheel routing"+context);
      g.in=Input{};g.in.pad=true;g.in.rx=.8f;g.in.ry=.8f;g.in.rt=.8f;g.focusNav=false;g.in.mx=-100;g.in.my=-100;const auto inactive=view;tick();
      check(near(view.yaw,inactive.yaw)&&near(view.pitch,inactive.pitch)&&near(view.zoom,inactive.zoom),"offstage controller preserves view"+context);
      // Real window focus loss stops controller repeats/analog use, and held controls cannot activate on return.
      g.in=Input{};view.reset();g.in.pad=true;g.focusNav=true;g.focusId=first;
      g.in.buttonsPressed=g.in.buttons=PAD_A;tick();
      g.in.down[K_ENTER]=g.in.pressed[K_ENTER]=g.in.mDown[0]=g.in.mPressed[0]=true;
      g.focusLost();const auto unfocused=view;const auto screenBefore=g.screen;const auto tabBefore=g.hubTab;const auto listBefore=g.hubList;
      check(!g.windowFocused && view.held==-1 && !view.drag && !g.in.down[K_ENTER] && !g.in.pressed[K_ENTER] &&
        !g.in.mDown[0] && !g.in.mPressed[0] && g.in.mReleased[0],"window focusLost releases keyboard, mouse and controller gesture state"+context);
      auto rawController=[&](unsigned buttons,float axis,float trigger) {
        g.in.pad=true;g.in.buttons=g.in.buttonsPressed=buttons;g.in.rx=g.in.ry=axis;g.in.rt=trigger;
        g.filterControllerFocus();
      };
      for(int i=0;i<6;++i) {rawController(PAD_A|PAD_START|PAD_RB,.8f,.8f);tick(.1f,true);}
      check(near(view.yaw,unfocused.yaw) && near(view.pitch,unfocused.pitch) && near(view.zoom,unfocused.zoom) &&
        g.screen==screenBefore && g.hubTab==tabBefore && g.hubList==listBefore && !g.in.buttons && !g.in.buttonsPressed,
        "unfocused controller cannot repeat, move camera, change menus or launch"+context);
      g.focusGained();rawController(PAD_A|PAD_START,.8f,.8f);tick(1.f/60.f,true);
      check(near(view.yaw,unfocused.yaw) && near(view.zoom,unfocused.zoom) && g.screen==screenBefore && g.controllerFocusRearm,
        "focus return blocks held controller buttons and analog axes until neutral"+context);
      rawController(0,.8f,.8f);tick(1.f/60.f,true);
      check(near(view.yaw,unfocused.yaw) && near(view.pitch,unfocused.pitch) && near(view.zoom,unfocused.zoom) && g.controllerFocusRearm,
        "releasing buttons alone cannot rearm a still-deflected controller"+context);
      rawController(0,0,0);tick(1.f/60.f,true);
      g.focusNav=true;g.focusId=first;rawController(PAD_A,0,0);tick(1.f/60.f,true);
      check(!g.controllerFocusRearm && !near(view.yaw,unfocused.yaw),
        "a fresh controller activation works after focus return and neutral release"+context);
      g.in=Input{};view.reset();g.focusNav=false;
      // Rendering and stored state share their safe elevation limits. Reversal must move immediately.
      if(mode==0 || mode==3) {
        g.in.mx=x+w*.5f;g.in.my=y+h*.5f;g.in.mPressed[0]=g.in.mDown[0]=true;g.in.mdy=1000;tick();
        const float high=view.pitch;g.in.mdy=-1;tick();
        check(near(high,.35f) && view.pitch<high,"showroom reverses immediately from roof elevation limit"+context);
        g.in.mdy=-1000;tick();const float low=view.pitch;g.in.mdy=1;tick();
        check(near(low,.04f) && view.pitch>low,"showroom reverses immediately from floor elevation limit"+context);
      } else if(mode==1) {
        const bool airborne=g.resAirborne;g.resAirborne=false;
        g.in.mx=x+w*.5f;g.in.my=y+h*.5f;g.in.mPressed[0]=g.in.mDown[0]=true;g.in.mdy=-1000;tick();
        const float low=view.pitch;g.in.mdy=1;tick();
        check(near(low,.04f) && view.pitch>low,"Research runway reverses immediately from ground elevation limit"+context);
        g.resAirborne=airborne;
      }
      g.in=Input{};g.in.mx=g.in.my=-100;view.reset();
      bool fits=true,privateShape=true,clearRoom=true;
      for(float zoom:{.75f,1.f,1.35f})for(float pitch:{-.5f,.15f,1.f})for(int angle=0;angle<24;++angle) {
        view.zoom=zoom;view.pitch=pitch;view.yaw=.7f+angle*15*DEG;const auto fp=camera();const auto& sp=kAircraft[fp.plane.model];
        const float th=tanf(fp.fovY*.5f);
        const auto room=fp.hangarPreview?hangarPreview::geometry(fp.hangarSize):std::vector<hangarPreview::Vertex>{};
        for(vec3 local:{vec3(),vec3(sp.span*.5f,0,0),vec3(-sp.span*.5f,0,0),vec3(0,0,sp.fusLen*.5f),vec3(0,0,-sp.fusLen*.5f),vec3(0,sp.fusRad*3,0)}) {
          const auto point=transformed(fp.plane,local);
          if(fp.hangarPreview)clearRoom&=!obstructed(fp.camPos-fp.hangarOrigin,point-fp.hangarOrigin,room);
          const auto rel=point-fp.camPos;const float z=-dot(rel,fp.camBack);
          const float px=g_ren.W*.5f+dot(rel,fp.camRight)/z/th*g_ren.H*.5f,py=g_ren.H*.5f-dot(rel,fp.camUp)/z/th*g_ren.H*.5f;
          fits&=z>0&&px>=x&&px<=x+w&&py>=y&&py<=y+h;
        }
        if(fp.hangarClassified)privateShape&=fp.plN==0&&fp.sealedCockpit&&fp.plane.lensN==0;
      }
      check(fits,"full yaw pitch zoom envelope fits unobscured stage"+context);
      check(privateShape,"classified geometry retains privacy"+context);
      check(clearRoom,"full elevated orbit has clear room sightlines"+context);
      view.reset();view.rotate(.5f);view.magnify(.2f);
      if(mode==1)g.resCraft=kVigil;else if(mode==2)g.militarySelection=military::RouteRecon;else if(mode==3)g.freeCraft=1;else g.selHangar=1;
      camera();check(near(view.yaw,.7f)&&near(view.pitch,.15f)&&near(view.zoom,1)&&!view.drag,"new selection resets the common view"+context);
      // A page transition while held must not transfer a gesture to another view.
      view.held=0;view.heldSource=2;view.drag=true;g.focusScreen=-1;g.focusNavigate();
      check(view.held==-1&&!view.drag,"page focus transition releases gestures"+context);
    }
    g.screen=SCR_FLIGHT;g.crashed=false;g.paused=false;g.focusLost();
    check(g.paused,"window focus loss still pauses a live flight");
    g.focusGained();g.in=Input{};g.filterControllerFocus();
    check(before!="SAVE FAILED" && snapshot("after.sav")==before,"all viewing controls preserve complete career save");
    std::error_code ec; fs::remove_all(root,ec);
    printf("Hangar preview controls: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
  }
};
int main() { return GameTest::run(); }
