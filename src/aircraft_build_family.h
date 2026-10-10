// Aircraft shader/build families. Geometry and behavior stay with their existing owners.
#pragma once
namespace aircraftBuild {
enum class Family { Fleet, Specter, Wraith };
inline Family familyOf(const float* packed) {
  const int engine = (int)(packed[2] + 0.5f);
  return engine == 6 ? Family::Wraith : engine == 5 ? Family::Specter : Family::Fleet;
}
inline const char* shaderDefine(Family family) {
  return family == Family::Wraith ? "#define AF_WRAITH\n" : family == Family::Specter ? "#define AF_JET\n" : "#define AF_LIGHT\n";
}
} // namespace aircraftBuild
