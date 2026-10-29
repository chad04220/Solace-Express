// Uniform build interface; the four family implementations remain separate.
#pragma once
#include "aircraft_mesh_build_fleet.h"
#include "aircraft_mesh_build_nocturne.h"
#include "aircraft_mesh_build_somnus.h"
#include "aircraft_mesh_build_morpheus.h"
#include "aircraft_mesh_build_phantasos.h"
namespace aircraftBuild {
inline const MeshBuilder& meshBuilderFor(const float* packed) {
  static const MeshBuilder fleetBuilder{Family::Fleet, fleet::parts, fleet::partPlan, fleet::appendHullStates};
  static const MeshBuilder nocturneBuilder{Family::Nocturne, nocturne::parts, nocturne::partPlan, nocturne::appendHullStates};
  static const MeshBuilder somnusBuilder{Family::Somnus, somnus::parts, somnus::partPlan, somnus::appendHullStates};
  static const MeshBuilder morpheusBuilder{Family::Morpheus, morpheus::parts, morpheus::partPlan, morpheus::appendHullStates};
  static const MeshBuilder phantasosBuilder{Family::Phantasos, phantasos::parts, phantasos::partPlan, phantasos::appendHullStates};
  switch (familyOf(packed)) {
    case Family::Nocturne: return nocturneBuilder;
    case Family::Somnus: return somnusBuilder;
    case Family::Morpheus: return morpheusBuilder;
    case Family::Phantasos: return phantasosBuilder;
    default: return fleetBuilder;
  }
}
inline int partList(const float* packed, bool inside, PartInst* out, int model = -1) {
  return meshBuilderFor(packed).parts(packed, inside, out, model);
}
} // namespace aircraftBuild
