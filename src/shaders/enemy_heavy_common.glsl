//! kEnemyHeavyCommon
void ccHeavySkin(inout vec2 h, vec3 p, float host, vec3 center,
                 vec3 halfSize, float raise, float material) {
  ccAdd(h, max(host - raise, ccBox(p, center, halfSize, 0.018)), material);
}

