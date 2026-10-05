// Exact game geometry/materials rendered on Linux software OpenGL. No AI image generation.
#include "candidates.h"
#include "game.h"
#include "renderer.h"
#include <dlfcn.h>
#include <filesystem>

static bool checkGL(const char* where) {
  const GLenum e = glGetError();
  if (!e) return true;
  printf("OpenGL error 0x%x at %s\n", unsigned(e), where); return false;
}

static void* lib;
static void* (*eglProc)(const char*);
static void* proc(const char* n) { void* p = eglProc(n); return p ? p : dlsym(lib, n); }
template<class F> static F sym(const char* n) { return reinterpret_cast<F>(dlsym(lib, n)); }
static bool initGL(int w, int h) {
  lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL); if (!lib) return false;
  eglProc = sym<void*(*)(const char*)>("eglGetProcAddress"); if (!eglProc) return false;
  auto getDisplay = reinterpret_cast<void*(*)(unsigned, void*, const int*)>(eglProc("eglGetPlatformDisplayEXT"));
  if (!getDisplay) return false;
  auto display = getDisplay(0x31DD, nullptr, nullptr); int major, minor;
  if (!sym<unsigned(*)(void*, int*, int*)>("eglInitialize")(display, &major, &minor)) return false;
  if (!sym<unsigned(*)(unsigned)>("eglBindAPI")(0x30A2)) return false;
  const int ca[] = {0x3033, 1, 0x3040, 8, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3038}; void* config; int count;
  if (!sym<unsigned(*)(void*, const int*, void**, int, int*)>("eglChooseConfig")(display, ca, &config, 1, &count) || !count) return false;
  const int sa[] = {0x3057, w, 0x3056, h, 0x3038};
  auto surface = sym<void*(*)(void*, void*, const int*)>("eglCreatePbufferSurface")(display, config, sa);
  const int xa[] = {0x3098, 3, 0x30FB, 3, 0x30FD, 1, 0x3038};
  auto context = sym<void*(*)(void*, void*, void*, const int*)>("eglCreateContext")(display, config, nullptr, xa);
  const char* missing = nullptr;
  return context && sym<unsigned(*)(void*, void*, void*, void*)>("eglMakeCurrent")(display, surface, surface, context) && glLoad(proc, &missing);
}

struct GameTest {
  static FrameParams frame(int index, const std::string& requested) {
    std::string view=requested;
#ifdef CANDIDATE_WHEEL_ANIMATION
    float wheelAngle=0;
    if(view.rfind("wheel-",0)==0) {
      wheelAngle=view.find("quarter")!=std::string::npos ? .5f*PI : 0.f;
      if(view.find("frame-")!=std::string::npos) wheelAngle=atoi(view.substr(view.find("frame-")+6).c_str())*5.f*DEG;
      view=std::string("gear-")+(view.find("nose")!=std::string::npos?"nose":"main")+(view.find("half")!=std::string::npos?"-half":"");
    }
#endif
    Game g; g.initHeadless(); g.debugScene("air");
    const auto& A = g_world.airports[g_world.findAirport("NPT")];
    g.plane.reset(&kAircraft[index], A.pos() - A.dir() * 180.f, A.heading, kAircraft[index].maxFuel * .6f, 100, false);
    g.plane.engineRunning = true; g.plane.engineSpool = .4f;
    g.plane.rpm = 0; g.plane.n1 = 40; g.plane.ctl.throttle = .4f;
    if (view.find("half") != std::string::npos && kAircraft[index].retract) g.plane.gear = .55f;
    if (view == "gear-nose-turn") g.plane.ctl.yaw = .6f;
    g.camQ = g.plane.q; g.realTime = 25; g.timeOfDay = 11.5f;
    g.wx.cloudCover = 0; g.wx.precip = 0; g.wx.visibility = 60000;
    const bool inside = view.rfind("cockpit", 0) == 0;
    g.camMode = inside ? 1 : 2; g.propAngle = .32f; g.hudOn = false;
    g.lookYaw = view == "cockpit-right" ? 50.f * DEG : view == "cockpit-left" ? -60.f*DEG : 0;
    g.lookPitch = view == "cockpit-down" ? -.50f : view == "cockpit-left" ? -20.f*DEG : -8.f*DEG;
#ifdef CANDIDATE_WHEEL_ANIMATION
    const auto& spec=kAircraft[index];const auto& model=kModels[index];
    float r=spec.special?.38f:model.wheelR,nr=spec.special?.33f:spec.taildragger?.10f:model.gear==3?r*.75f:r*.85f;
    g.plane.wheelMotion[0].step(1,-wheelAngle*r,true);
    g.plane.wheelMotion[1].step(1,-wheelAngle*r,true);
    g.plane.wheelMotion[2].step(1,-wheelAngle*nr,true);
#endif
    g.updateCamera(0); FrameParams fp = g.buildFrame();
    fp.trafficN = 0; fp.ufoOn = false; fp.feedRig = 0; fp.cloudCover = 0; fp.plN = 0;
    fp.rainLens = 0; fp.fade = 1; fp.vignette = .15f; fp.exposure = 1;
    fp.sunDir = normalize(vec3(-.55f, .75f, -.40f)); fp.sunCol = vec3(1.15f, 1.07f, .94f); fp.night = 0;
    fp.plane.Pr[1] = 0; // stopped propeller exposes its real blades, hub and spinner
    if (!inside) {
      const float L = kAircraft[index].fusLen;
      vec3 off = (view == "rear" || view == "controls") ? vec3(.9f, .34f, 1.05f) :
                 view == "top" ? vec3(.05f, 1.5f, -.25f) :
                 view == "side" ? vec3(1.55f, .20f, .10f) : vec3(-.92f, .34f, -1.02f);
      fp.camPos = fp.plane.pos + g.plane.q.rotate(off * L);
      const vec3 target = fp.plane.pos + g.plane.q.rotate(vec3(0, .05f, -.10f * L));
      const vec3 f = normalize(target - fp.camPos);
      fp.camBack = -f; fp.camRight = normalize(cross(f, vec3(0, 1, 0))); fp.camUp = cross(fp.camRight, f);
      fp.fovY = 43.f * DEG; fp.dispMode = 0;
    }
    if (view.rfind("gav-", 0) == 0) {
      float yaw = 120, pitch = 10; sscanf(view.c_str() + 4, "%f-%f", &yaw, &pitch);
      const float d = kAircraft[index].span*1.15f;
      const vec3 off(sinf(yaw*DEG)*cosf(pitch*DEG)*d, sinf(pitch*DEG)*d, -cosf(yaw*DEG)*cosf(pitch*DEG)*d);
      fp.camPos = fp.plane.pos + g.plane.q.rotate(off);
      const vec3 f = normalize(fp.plane.pos - fp.camPos);
      fp.camBack = -f; fp.camRight = normalize(cross(f, vec3(0, 1, 0))); fp.camUp = cross(fp.camRight, f);
      fp.fovY = 43.f*DEG;
    }
    if (view.rfind("canopy-", 0) == 0) {
      const vec3 target(0, .5f, -4.6f);
      const vec3 off = view == "canopy-side" ? vec3(-6.4f, .9f, -.2f) : vec3(-3.2f, 1.3f, -5.6f);
      fp.camPos = fp.plane.pos + g.plane.q.rotate(target + off);
      const vec3 f = normalize(fp.plane.pos + g.plane.q.rotate(target) - fp.camPos);
      fp.camBack = -f; fp.camRight = normalize(cross(f, vec3(0, 1, 0))); fp.camUp = cross(fp.camRight, f);
      fp.fovY = 37.f*DEG;
    }
    if (view == "flight" || view == "gear-half" || view == "controls") {
      fp.plane.pos.y += 300; fp.camPos.y += 300;
      fp.plane.PS[0] = view == "flight" ? (kAircraft[index].retract ? 0.f : 1.f) : view == "gear-half" ? .55f : 1.f;
    }
    if (view == "controls") { fp.plane.PS[1] = 1; fp.plane.Ctl[0] = .75f; fp.plane.Ctl[1] = .70f; fp.plane.Ctl[2] = -.70f; }
    if (view.rfind("gear-main", 0) == 0 || view.rfind("gear-nose", 0) == 0) {
      const auto& md = kModels[index]; const bool nose = view.rfind("gear-nose", 0) == 0;
      const bool special = md.engine >= 5;
      const float track = fp.plane.M[72], wr = special ? .38f : md.wheelR;
      const float gh = g.plane.gearHeight();
      const float top = special ? -.30f : md.gear >= 4 ? md.wing[4] + track * tanf(md.wing[6] * DEG) - .10f :
                        md.gear == 3 ? md.nacY : -kAircraft[index].fusRad * .65f;
      const float bottom = -gh + wr;
      const float extent = std::max(1.f, top - bottom + wr * 1.3f);
      vec3 target(nose ? 0.f : track, (top + bottom) * .5f, nose ? fp.plane.M[75] : fp.plane.M[74]);
      if (nose && kAircraft[index].taildragger) { target.z = fp.plane.M[77]; target.y = -gh + .11f * kAircraft[index].fusLen + .10f; }
      vec3 cameraOffset(extent*1.70f,extent*.13f,-extent*.75f);
      if (special && view.find("half") != std::string::npos) {
        // Inspect a folding research wheel from outside the wide body/pods, above the terrain.
        float r=nose?.33f:.38f, belly=md.engine==6?.19f:.50f;
        target.y=-gh+r+(1.f-g.plane.gear)*(gh-belly);
        fp.plane.pos.y+=100.f;
        cameraOffset=vec3(extent*1.70f,-extent*.75f,-extent*.5f);
      } else if (view.find("half") != std::string::npos) target.y += extent * .25f;
      fp.camPos = fp.plane.pos + g.plane.q.rotate(target + cameraOffset);
      const vec3 f = normalize(fp.plane.pos + g.plane.q.rotate(target) - fp.camPos);
      fp.camBack = -f; fp.camRight = normalize(cross(f, vec3(0, 1, 0))); fp.camUp = cross(fp.camRight, f);
      fp.fovY = 39.f * DEG;
    }
    return fp;
  }
};

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc < 2) { puts("render_probe OUTPUT_DIR [width height] [views-comma-list]"); return 2; }
  const std::filesystem::path out = argv[1]; std::filesystem::create_directories(out);
  const int w = argc > 2 ? atoi(argv[2]) : 960, h = argc > 3 ? atoi(argv[3]) : 600;
  const std::string list = (argc > 4 ? argv[4] : "front,rear,top,side,flight,gear-half,controls,cockpit,cockpit-right,cockpit-down") + std::string(",");
  if (!initGL(w, h)) { puts("No surfaceless EGL OpenGL context"); return 2; }
  printf("renderer=%s\n", glGetString(GL_RENDERER));
  if (getenv("SHADERCACHE")) { g_shaderCacheDir = getenv("SHADERCACHE"); std::filesystem::create_directories(g_shaderCacheDir); }
  g_world.build(); buildStory(); g_audio.init(48000);
  g_ren.quality = 0; g_ren.renderScale = 1; g_ren.entSync = true;
  // Guides are acceleration aids, not the visible models. Skip first-use hull bakes for this gallery.
  g_ren.hullOff = true;
  if (!g_ren.init(w, h)) { printf("renderer init: %s\n", g_ren.error.c_str()); return 1; }
  if (!checkGL("renderer init")) return 1;
  printf("programs linked, shader-cache hits=%d misses=%d\n", g_shaderCacheHits.load(), g_shaderCacheMisses.load());
#ifdef CANDIDATE_WHEEL_ANIMATION
  if(argc>5 && std::string(argv[5])=="ground") {
    const char* names[]={"ground-ga","ground-airliner","ground-car","ground-truck"};
    const int kinds[]={EK_GA_PLANE,EK_AIRLINER,EK_CAR,EK_TRUCK};
    for(int ki=0;ki<4;++ki) for(int pose=0;pose<2;++pose) {
      FrameParams fp=GameTest::frame(candidate::Swift,"gear-main");fp.plane.on=false;
      const auto& apt=g_world.airports[g_world.findAirport("NPT")];
      vec3 base=apt.pos()-apt.dir()*180.f;
      Ent ent{base.x,g_world.height(base.x,base.z),base.z,0,1,1,1,.5f};
      const auto layout=groundWheelLayout(kinds[ki]);
      GroundVehicleMotion motion;float distance=pose*.5f*PI*layout.wheel[0].radius;
      motion.step(kinds[ki],ent,vec3(0,0,distance),0,1);ent.z+=distance;
      fp.groundVehicles.push_back(motion.visual(kinds[ki],ent));
      vec3 target=vec3(ent.x,ent.y,ent.z)+layout.wheel[0].center;
      // Observe the exterior side of the left wheel; the camera follows the actual vehicle displacement.
      float extent=layout.wheel[0].radius*5.f;
      fp.camPos=target+vec3(-extent*1.5f,extent*.22f,extent*.48f);
      const vec3 f=normalize(target-fp.camPos);fp.camRight=normalize(cross(f,vec3(0,1,0)));fp.camUp=cross(fp.camRight,f);fp.camBack=-f;
      fp.fovY=.58f;fp.vignette=0;
      g_ren.resetTemporal();
      for(int k=0;k<4;++k){g_ren.renderScene(fp,{},{});glFinish();if(!checkGL("ground render"))return 1;}
      const auto path=out/(std::string(names[ki])+(pose?"-quarter.png":"-zero.png"));
      if(!g_ren.screenshotPNG(path.string().c_str())||!checkGL("ground readback"))return 1;
      printf("saved %s, GL errors=0\n",path.string().c_str());
    }
    return 0;
  }
#endif
  const std::string selected = argc > 5 ? argv[5] : "candidates";
  const candidate::Entry* first = selected == "candidates" ? std::begin(candidate::entries) : std::begin(candidate::roster);
  const candidate::Entry* last = selected == "candidates" ? std::end(candidate::entries) : std::end(candidate::roster);
  for (auto e = first; e != last; ++e) {
    const auto& entry = *e;
    if (selected != "candidates" && selected != "all" && ("," + selected + ",").find("," + std::string(entry.slug) + ",") == std::string::npos) continue;
    for (size_t a = 0, b; (b = list.find(',', a)) != std::string::npos; a = b + 1) {
      const std::string view = list.substr(a, b - a); if (view.empty()) continue;
      if (view.find("half") != std::string::npos && !kAircraft[entry.index].retract) continue;
      FrameParams fp = GameTest::frame(entry.index, view);
      g_ren.resetTemporal();
      for (int k = 0; k < 4; k++) { g_ren.renderScene(fp, {}, {}); glFinish(); if (!checkGL("render")) return 1; }
      const auto path = out / (std::string(entry.slug) + "-" + view + ".png");
      if (!g_ren.screenshotPNG(path.string().c_str())) return 1;
      if (!checkGL("readback")) return 1;
      printf("saved %s, GL errors=0\n", path.string().c_str());
    }
  }
  return 0;
}
