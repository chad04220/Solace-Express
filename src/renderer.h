// Air Xpress - renderer interface
#pragma once
#include "common.h"
#include "gl.h"
#include "world.h"

struct SpriteVert { float x, y, z, u, v, r, g, b, a, kind, soft; };
enum SpriteKind { SPR_SMOKE = 0, SPR_GLOW = 1, SPR_RING = 2, SPR_RAIN = 3, SPR_FIRE = 4, SPR_SNOW = 5, SPR_SHOCK = 6, SPR_SPARK = 7 };

struct PlaneVisual {
  bool on = false;
  vec3 pos; float rot[9];  // body->world, column-major
  float M[24 * 4];          // model geometry (models.cpp packModel)
  float PS[4], Ctl[4], Pr[4], I0[4], I1[4], I2[4];  // state, controls, prop, instruments
  vec3 colBase, colStripe;
  float prop[2][4]; int propCount = 0;
  float hud[4] = {0, 0, 0, 0}, hud2[4] = {1, 0, 0, 0}, hudV[3] = {0, 0, -1}, hud3[4] = {0, 0, 0, 1000};  // research jet HUD data
  float flame[4] = {0, 0, 0, 0};  // research jet exhaust: spool, reheat, nozzle vector angle (rad), mach
};

struct WreckVisual {
  int pieces = 0;               // > 0: draw these clipped pieces instead of the intact aircraft
  vec3 pos[5]; float rot[5][9]; // piece centre (world) and body->world rotation
  vec3 C[5], H[5];              // clip box (body coords) of each piece
  int debris = 0;
  float deb[16][4], debQ[16][4];  // chunk centre + size (negative = charred), orientation quaternion (w,x,y,z)
  float crater[4] = {0, 0, 0, 0}; // x, z, radius, depth
};

struct FrameParams {
  vec3 camPos; vec3 camRight, camUp, camBack; float fovY = 1.0f;
  float time = 0;
  vec3 sunDir, sunCol; float night = 0;
  float cloudCover = 0.3f, cloudBase = 1500, fogB = 0.0001f, wet = 0, snow = 0, lightning = 0, storm = 0;
  vec2 windOff;
  PlaneVisual plane;
  WreckVisual wreck;
  vec3 landLightPos, landLightDir; float landLight = 0;
  vec3 flameLightPos, flameLight;  // research jet exhaust light (radiance; zero when off)
  float exposure = 1.0f, rainLens = 0, fade = 1, vignette = 0.6f, gLoad = 0;   // gLoad: g-force tunnel 0..1
};

struct UIVert { float x, y, u, v, r, g, b, a, mode, hx, hy, p; };

class Renderer {
public:
  int W = 0, H = 0;          // window size
  float renderScale = 0.75f;
  int quality = 1;           // 0 low, 1 medium, 2 high
  bool ok = false;
  std::string error;
  GLuint minimapTex = 0;

  bool init(int w, int h);
  void resize(int w, int h);
  void renderScene(const FrameParams& fp, const std::vector<SpriteVert>& alphaSprites, const std::vector<SpriteVert>& addSprites);
  mat4 viewProj(const FrameParams& fp) const;
  bool project(const FrameParams& fp, vec3 p, float& sx, float& sy) const;  // to window pixels

  // ---- immediate-mode 2D UI
  void uiBegin();
  void rect(float x, float y, float w, float h, vec3 c, float a = 1.0f, float radius = 0.0f);
  void line(float x0, float y0, float x1, float y1, float th, vec3 c, float a = 1.0f);
  void rectGrad(float x, float y, float w, float h, vec3 top, vec3 bottom, float a = 1.0f, float radius = 0.0f);
  void rectOutline(float x, float y, float w, float h, vec3 c, float a, float radius, float thickness);
  void glow(float x, float y, float w, float h, vec3 c, float a, float radius, float soft);  // soft halo around a rounded rect
  float text(float x, float y, float size, const std::string& s, vec3 c, float a = 1.0f, int align = 0, bool shadow = true);
  float textWidth(const std::string& s, float size) const;
  void image(GLuint tex, float x, float y, float w, float h, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1, float a = 1.0f);
  void uiEnd();
  void flushUIPublic() { flushUI(); }
  bool screenshot(const char* path);

private:
  GLuint progRT = 0, progSprite = 0, progBright = 0, progBlur = 0, progPost = 0, progUI = 0;
  GLuint vaoEmpty = 0, vaoSprite = 0, vboSprite = 0, vaoUI = 0, vboUI = 0;
  GLuint texHM = 0, texAlb = 0, texNrm = 0, texFont = 0, texMask = 0, texRoadId = 0, texData = 0, texHMax = 0;
  struct V4 { float x, y, z, w; };
  std::vector<V4> townB, townY;
  GLuint fboScene = 0, texColor = 0, texDepth = 0, fboSprite = 0;
  GLuint fboBloom[2] = {0, 0}, texBloom[2] = {0, 0};
  int rw = 0, rh = 0, bw = 0, bh = 0;
  float maxH = 2500;
  std::vector<UIVert> ui;
  GLuint curImg = 0;
  void flushUI();
  void createTargets();
  void genMaterials();
  void genMinimap();
};

extern Renderer g_ren;
