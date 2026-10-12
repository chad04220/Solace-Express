// Real showroom UI/camera regression; no window, GL context or aircraft mesh bake.
#include "../src/game.h"
#include "../src/hangar_catalog.h"
#include "../src/hangar_preview.h"
#include "../src/menu_layout.h"
#include "../src/models.h"
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
    g.resYaw = .57f; g.resPitch = .31f; g.resZoom = 1.23f;
    g.resCraft = kWraith; g.militarySelection = 1;
    const auto originalGun = g.practiceForward;
    const auto originalBomb = g.practiceBomb;
    const int originalWave = g.practiceWave;
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("solace_hangar_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
        check(near(g.hangarView.yaw,0) && near(g.hangarView.zoom,1),context+" selecting a model resets its view");
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
        click(0); check(near(g.hangarView.yaw,-15*DEG),context+" mouse rotate left");
        click(1); check(near(g.hangarView.yaw,0),context+" mouse rotate right");
        click(2); check(near(g.hangarView.zoom,.9f),context+" mouse zoom out");
        click(3); check(near(g.hangarView.zoom,1),context+" mouse zoom in");
        click(0); click(2); click(4);
        check(near(g.hangarView.yaw,0) && near(g.hangarView.zoom,1),context+" mouse reset");
        g.in = Input{}; g.focusNav = true; g.focusId = controls[0].id; g.in.pressed[K_ENTER] = true; frame();
        check(near(g.hangarView.yaw,-15*DEG),context+" keyboard Enter rotates");
        g.in.pressed[K_RIGHT] = true; frame();
        check(g.focusId==controls[1].id,context+" arrow focus reaches adjacent rotate button");
        g.in.pressed[' '] = true; frame();
        check(near(g.hangarView.yaw,0),context+" keyboard Space rotates");
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
        check(near(g.hangarView.yaw,0) && near(g.hangarView.zoom,1),context+" keyboard reset");
        g.hangarView.rotate(15*DEG); g.hangarView.magnify(.1f);
        g.focusId = controls[4].id; g.in.pad = true; g.in.buttonsPressed = g.in.buttons = PAD_A; frame();
        check(near(g.hangarView.yaw,0) && near(g.hangarView.zoom,1),context+" controller reset");
        g.in = Input{}; g.focusNav = false;
        FrameParams base{}; g.hangarPreviewCamera(base);
        const auto room = hangarPreview::geometry(base.hangarSize);
        bool clear = true, framed = true, grounded = true, safeEye = true, classified = true;
        for (float zoom : {.75f,1.f,1.35f}) for (int turn = 0; turn < 24; ++turn) {
          g.hangarView.yaw = turn*15*DEG; g.hangarView.zoom = zoom;
          FrameParams fp{}; g.hangarPreviewCamera(fp);
          safeEye &= length(fp.camPos-base.camPos)<.0001f && fp.camPos.y<fp.hangarSize*.60f && std::fabs(fp.camPos.x)<fp.hangarSize*1.2f;
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
        check(clear,context+" all yaw/zoom room triangles leave aircraft sightlines clear");
        check(framed,context+" all yaw/zoom model bounds fit stage");
        check(grounded,context+" all yaw/zoom tyres stay grounded and props still");
        check(safeEye,context+" optical zoom never moves eye into walls or roof");
        check(classified,context+" silhouette lighting remains private");
        check(g.screen==(freeFlight?SCR_FREE_FLIGHT:SCR_HUB) && !g.freeFlight && g.freeAirport==airport && g.freeAirborne==airborne && g.selHangar==careerSelection,
          context+" viewing never launches, changes departure setup or changes career selection");
      }
    }
    // Free Flight uses the same room but never inherits the career viewer state.
    g.hangarView.yaw=1;g.hangarView.zoom=1.35f;
    g.screen=SCR_FREE_FLIGHT;g.freeCraft=0;FrameParams free{};g.hangarPreviewCamera(free);
    check(near(g.hangarView.yaw,0) && near(g.hangarView.zoom,1) && free.plane.model==0 && !free.hangarClassified,"Free Flight resets career view and selects its own unlocked craft");
    g.hangarView.yaw=1;g.hangarView.zoom=.75f;g.screen=SCR_HUB;g.hangarPreviewCamera(free);
    check(near(g.hangarView.yaw,0) && near(g.hangarView.zoom,1),"return to career resets Free Flight view");
    check(near(g.resYaw,.57f) && near(g.resPitch,.31f) && near(g.resZoom,1.23f) && g.resCraft==kWraith && g.militarySelection==1,
      "preview controls preserve research and military selection/camera");
    check(g.practiceForward==originalGun && g.practiceBomb==originalBomb && g.practiceWave==originalWave,"preview controls preserve practice equipment");
    check(before!="SAVE FAILED" && snapshot("after.sav")==before,"all viewing controls preserve complete career save");
    std::error_code ec; fs::remove_all(root,ec);
    printf("Hangar preview controls: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
  }
};
int main() { return GameTest::run(); }
