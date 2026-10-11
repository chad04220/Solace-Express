// Validation must still reject NaN/Infinity in production fast-math builds.
#pragma once
#include <cstdint>
#include <cstring>
#include <limits>

namespace floatValidation {
inline bool finite(float value) {
  static_assert(sizeof(float)==sizeof(uint32_t) && std::numeric_limits<float>::is_iec559,
                "Float validation requires IEEE-754 binary32");
  uint32_t bits;
  std::memcpy(&bits,&value,sizeof bits);
  return (bits & 0x7f800000u)!=0x7f800000u;
}
}
