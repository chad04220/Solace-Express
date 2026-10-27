// Aircraft shader/build families. Geometry and behavior stay with their existing owners.
#pragma once
namespace aircraftBuild {
// (Morpheus, the XR-50 - engine code 7 - builds its own parts, but its shaders are the jet family's: it flies the XR-30's
// sealed pod, its gear and its research materials, and its own airframe is that build's HAS_MORPHEUS)
enum class Family { Fleet, Nocturne, Somnus, Morpheus };
inline Family familyOf(const float* packed) {
  const int engine = (int)(packed[2] + 0.5f);
  return engine == 7 ? Family::Morpheus : engine == 6 ? Family::Somnus : engine == 5 ? Family::Nocturne : Family::Fleet;
}
inline const char* shaderDefine(Family family) {
  return family == Family::Somnus ? "#define AF_SOMNUS\n" : family == Family::Nocturne || family == Family::Morpheus ? "#define AF_JET\n" : "#define AF_LIGHT\n";
}
} // namespace aircraftBuild
