// Common mesh-authoring contract. It contains no aircraft geometry or rendering state.
#pragma once
#include "common.h"
#include "aircraft_build_family.h"
namespace aircraftBuild {
// These values are shared with plane_parts.glsl and serialized mesh part records.
// Never renumber them; aircraft_build_interface_test verifies the shader contract.
enum PartType : int { PT_YOKE_SHAFT = 0, PT_YOKE_WHEEL = 1, PT_PEDAL = 2, PT_THR_KNOB = 3, PT_THR_LEVER = 4, PT_FLAP_LEVER = 5,
          PT_JET_STICK = 6, PT_JET_THR = 7, PT_WR_STICK = 8, PT_WR_THR = 9, PT_WR_PEDAL = 10,
          PT_FLAP = 11, PT_AILERON = 12, PT_ELEVATOR = 13, PT_RUDDER = 14,


          PT_WR_PODF = 15, PT_WR_PODR = 16, PT_WR_FAN = 17, PT_WR_VANEC = 18, PT_WR_VANEO = 19, PT_WR_VANEY = 20,
          PT_WR_PETAL = 21, PT_WR_DOOR = 22, PT_WR_BOMB = 23, PT_WR_HATCH = 24, PT_WR_TURRET = 25, PT_WR_MUZZLE = 26,
          PT_WR_ARM = 27, PT_WR_ELEVON = 28, PT_WR_RUDV = 29,
          PT_JT_ELEVON = 30, PT_JT_CANARD = 31, PT_JT_RUDDER = 32,


          PT_GEAR_MAIN = 33, PT_GEAR_NOSE = 34, PT_GEAR_TAIL = 35, PT_GEAR_MDOOR = 36, PT_GEAR_NDOOR = 37,



          PT_JT_NOZZLE = 38, PT_JT_LEGM = 39, PT_JT_WHEELM = 40, PT_JT_LEGN = 41, PT_JT_WHEELN = 42, PT_JT_DOORM = 43, PT_JT_DOORN = 44,
          PT_WR_ACT = 45, PT_ATLAS_FAN = 46 };
struct PartInst { int type; float sx, sy; };
inline constexpr int kMaxPartInst = 128;
// Fixed bounds receive a two-cell margin. Survey bounds use the original 2 cm grid;
// cockpit controls use the original centered 71-point, 1 cm grid. Empty skips the part.
enum class PartSampling { Fixed, Survey, CockpitSurvey, Empty };
struct PartBakePlan {
  PartSampling sampling = PartSampling::CockpitSurvey;
  vec3 lo, hi;
  float lattice = 0.002f;
};
struct HullState { float ps[4], ctl[4], wr[4] = {0, 0, 0, 0}, wr2[4] = {0, 0, 0, 0}; };
struct MeshBuilder {
  Family family;
  int (*parts)(const float* packed, bool inside, PartInst* out, int model);
  PartBakePlan (*partPlan)(int type, const float* packed);
  void (*appendHullStates)(std::vector<HullState>& states, bool inside, bool meshBake);
};
} // namespace aircraftBuild
