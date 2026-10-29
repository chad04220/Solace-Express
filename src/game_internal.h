// Somnium Volandi - what the game's translation units (game*.cpp) share among themselves and nothing else does
#pragma once
#include "game.h"

// a file in a folder ("" folder: the file alone)
inline std::string joinPath(const std::string& d, const char* f) { return d.empty() ? std::string(f) : d + "/" + f; }
// the map's wrap for the combat's displacements (world.h WRAP_SPAN), 0 in a region that doesn't repeat
inline float combatWrap() { return g_world.repeating ? WRAP_SPAN : 0.f; }

// a Plane as the renderer draws it (game_frame.cpp)
void fillPlaneVisual(PlaneVisual& pv, const Plane& p, float propAngle, bool inside);
// XR-30 nozzle swivel (0 aft .. 90 deg down) plus pitch vectoring; mirrored by mapJet and the exhaust plumes
inline float jetNozzleAngle(const Plane& p) { return p.nozzle * 0.5f * PI - clampf(p.ctl.pitch + p.ctl.trim * 0.3f, -1, 1) * 0.5f; }
// the research craft's exhausts (game_effects.cpp): exits and jet directions in body coordinates, and each one's share
// of the thrust (1 = the XR-30's); the XR-30 two 2D nozzles, the XR-40 four pods, the XR-50 two nozzles and the rocket
int jetExhausts(const Plane& p, vec3* pos, vec3* dir, float* strength);
// XR-30 wingtip (body frame) matching mapJet's cranked delta in shaders.h; the XR-40's
static const vec3 kJetWingTip(5.62f, -0.38f, 4.4f);
static const vec3 kSomnusWingTip(6.2f, -0.24f, 2.0f);

// the main menu's tour of the islands (game_cameras.cpp): one shot every kMenuShotLen seconds
struct MenuShot { const char* ap; int craft; float tod, cloud; int cam; float alt, turn; };
static const float kMenuShotLen = 16.f;
const MenuShot& menuShot(float t);
// the hub's camera round its field: radius, radians a second
static const float kHubOrbitR = 2200.f, kHubOrbitRate = 0.035f;
// the preview stage (Game::previewStage): the circuit flown over it, how far round the stand its scenery is kept,
// and the time of day it is lit for
static const float kStageCircuitR = 800.f, kStageCircuitAgl = 110.f, kStageKeepR = 2600.f, kStageTimeOfDay = 15.8f;

// the research terminal's register (game_research_ui.cpp)
int resCraftCount(); int resCraftAt(int k);
// alpha sprites sorted back to front for the camera (game_frame.cpp)
void sortAlphaQuads(std::vector<SpriteVert>& v, vec3 cam);
