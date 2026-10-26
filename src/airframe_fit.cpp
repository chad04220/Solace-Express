// Somnium Volandi - one airframe as it is fitted: floats in place of its wheels, the mechanic's kits (docs/HANGAR.md)
#include "aircraft.h"
#include "models.h"
#include <map>
#include <memory>
#include <mutex>

// Floats suit a high-winged piston or turboprop light enough to carry them: the singles, the Skerry and the Longshore.
// Not a low wing (its tips meet the water in a bank), nor the Archipel (twelve tonnes), nor the jets; the Kittiwake is
// built on its own.
bool floatKitFor(int type) {
  if (type < 0 || type >= kAircraftCount || isResearchAircraft(type)) return false;
  const AircraftSpec& s = kAircraft[type];
  return !s.floats && s.engineType != ENG_JET && s.wingY > 0.5f && s.emptyMass < 4000.f;
}
// The kits: the engine and the drag for every career type, the weight and the tanks too; a propeller for a propeller,
// the STOL kit for a wing that lands slow (not a jet's)
bool kitFor(int type, int kit) {
  if (type < 0 || type >= kAircraftCount || isResearchAircraft(type) || kit < 0 || kit >= KIT_COUNT) return false;
  const AircraftSpec& s = kAircraft[type];
  if (kit == KIT_PROP || kit == KIT_STOL) return s.engineType != ENG_JET;
  return true;
}

const char* kitName(int kit, const AircraftSpec& t) {
  const bool jet = t.engineType == ENG_JET, piston = t.engineType == ENG_PISTON;
  switch (kit) {
    case KIT_ENGINE: return jet ? "Engine uprate" : piston ? "Engine overhaul and tune" : "Power section upgrade";
    case KIT_PROP: return "Composite propeller";
    case KIT_AERO: return jet ? "Winglets and gap seals" : "Speed kit";
    case KIT_LIGHT: return "Lightweight interior";
    case KIT_TANKS: return jet ? "Auxiliary tanks" : "Long-range tanks";
    case KIT_STOL: return "STOL kit";
    default: return "";
  }
}
const char* kitBlurb(int kit, const AircraftSpec& t) {
  const bool jet = t.engineType == ENG_JET;
  switch (kit) {
    case KIT_ENGINE: return jet ? "6% more thrust: a shorter take-off run, a faster climb. It burns as much more." :
                                  "8% more power: a shorter take-off run, a faster climb, a little more speed. It burns as much more.";
    case KIT_PROP: return "5% more thrust from the same power, most of it at the start of the take-off roll.";
    case KIT_AERO: return jet ? "4% less drag: faster on the same thrust, farther on the same fuel." :
                                "Gap seals, fairings and wheel spats: 6% less drag - faster on the same power, farther on the same fuel.";
    case KIT_LIGHT: return "5% off the empty weight: that much more payload, a shorter roll, a better climb.";
    case KIT_TANKS: return "30% more fuel: the range a long haul needs. Full tanks weigh more, and eat into the payload.";
    case KIT_STOL: return "Vortex generators and a drooped leading edge: 10% more lift, a slower stall, shorter runways.";
    default: return "";
  }
}

namespace {
// Floats in place of the wheels: amphibious floats - the wheels fold up into them, so the airframe still uses a runway -
// as long as the float's own proportions say (aircraft.h kFloat*), each float's deck three-quarters of a tenth of the
// fuselage's length below the belly at the step (the propeller's tips half a metre over the water at rest) and its centre line out past the fuselage's side. A sixth more empty weight for them and
// their struts, the take-off weight unchanged (the floats come out of the useful load), and a twelfth off the cruise for
// their drag (the aerodynamics' own float drag, aero.cpp, flies it; the book speed and range follow). Rough strips are
// no place for small amphibious wheels; a taildragger stands on the floats' bows.
void fitFloats(AircraftSpec& s, int type) {
  const AircraftSpec& row = kAircraft[type];
  const ModelDef& m = kModels[type];
  const GearStations g = gearStations(row);
  float hw = 0, hh = 0, cy = 0; modelSection(m, g.mainZ, hw, hh, cy);
  const float len = kFloatLength * row.fusLen, depth = kFloatDepth * len, gap = 0.075f * row.fusLen;
  if (row.maxTakeoffMass <= 0.f) s.maxTakeoffMass = row.maxMass();
  s.floats = true; s.retract = true; s.taildragger = false; s.roughOK = false;
  s.gearHeightM = 0.30f + gap + depth - (cy - hh);
  s.gearTrackM = std::max(0.092f * row.span, hw + 0.5f * kFloatBeam * len);
  s.emptyMass *= 1.16f;
  s.dragFactor *= 1.04f;   // (their struts, spreader bars and fittings, over the hulls' own drag)
  s.cruise *= 0.92f; s.rangeKm *= 0.92f;
}
}

const AircraftSpec& fitted(int type, Fit fit) {
  if (type < 0 || type >= kAircraftCount) type = 0;
  if (!floatKitFor(type)) fit.floats = false;   // (only what can be fitted)
  for (int k = 0; k < KIT_COUNT; k++) if (!kitFor(type, k)) fit.kits &= ~(1u << k);
  fit.kits &= (1u << KIT_COUNT) - 1u;
  if (fit.stock()) return kAircraft[type];
  static std::mutex mu;
  static std::map<uint32_t, std::unique_ptr<AircraftSpec>> made;
  const uint32_t key = (uint32_t)type << 16 | (fit.floats ? 1u << 15 : 0u) | fit.kits;
  std::lock_guard<std::mutex> lk(mu);
  auto at = made.find(key);
  if (at != made.end()) return *at->second;
  auto spec = std::make_unique<AircraftSpec>(kAircraft[type]);
  AircraftSpec& s = *spec;
  s.fittedFrom = type; s.fit = fit;
  if (s.maxTakeoffMass <= 0.f) s.maxTakeoffMass = kAircraft[type].maxMass();   // (the weight limit is the type's whatever is fitted)
  if (fit.floats) fitFloats(s, type);
  const bool jet = s.engineType == ENG_JET;
  if (fit.has(KIT_ENGINE)) s.power *= jet ? 1.06f : 1.08f;                    // overhauled and tuned, or a turbine's uprate
  if (fit.has(KIT_PROP)) { s.propFactor = 1.05f; s.v0 *= 1.15f; }           // a composite propeller: more thrust, most at the start of the roll
  if (fit.has(KIT_AERO)) s.dragFactor *= jet ? 0.96f : 0.94f;               // gap seals and fairings (a jet's winglets)
  if (fit.has(KIT_LIGHT)) s.emptyMass *= 0.95f;                             // a lightweight interior: the useful load grows
  if (fit.has(KIT_TANKS)) { s.maxFuel *= 1.3f; s.emptyMass *= 1.01f; s.rangeKm *= 1.3f; }   // long-range tanks
  if (fit.has(KIT_STOL)) { s.CLmax *= 1.1f; s.flapCL *= 1.08f; s.dragFactor *= 1.01f; s.runwayM *= 0.88f; }   // vortex generators and a drooped leading edge
  return *made.emplace(key, std::move(spec)).first->second;
}
