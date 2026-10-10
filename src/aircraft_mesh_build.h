// Uniform build interface; the three family implementations remain separate.
#pragma once
#include "aircraft_mesh_build_fleet.h"
#include "aircraft_mesh_build_specter.h"
#include "aircraft_mesh_build_wraith.h"
namespace aircraftBuild {
inline const MeshBuilder& meshBuilderFor(const float* packed) {
  static const MeshBuilder fleetBuilder{Family::Fleet, fleet::parts, fleet::partPlan, fleet::appendHullStates};
  static const MeshBuilder specterBuilder{Family::Specter, specter::parts, specter::partPlan, specter::appendHullStates};
  static const MeshBuilder wraithBuilder{Family::Wraith, wraith::parts, wraith::partPlan, wraith::appendHullStates};
  switch (familyOf(packed)) {
    case Family::Specter: return specterBuilder;
    case Family::Wraith: return wraithBuilder;
    default: return fleetBuilder;
  }
}
inline int partList(const float* packed, bool inside, PartInst* out, int model = -1) {
  return meshBuilderFor(packed).parts(packed, inside, out, model);
}
} // namespace aircraftBuild
