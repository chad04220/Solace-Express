//! kFarRegionFS
//! Coarse land goes through the normal deferred terrain light and atmospheric perspective, never a sky overlay.
in vec3 vColour, vNormal, vPosition;
in float vHeight;
in vec2 vLocal;
uniform vec3 uCamPos;
uniform float uRegionCoverage;
uniform float uClipHalf;   // (> 0: nothing drawn inside the active chart's square of this half side - it is drawn there)
layout(location=0) out vec4 oG0;
layout(location=1) out vec4 oG1;
layout(location=2) out vec4 oG2;
layout(location=3) out vec4 oG3;
vec2 octEncode(vec3 n) {
  n /= abs(n.x) + abs(n.y) + abs(n.z);
  return n.y >= 0.0 ? n.xz : (1.0 - abs(n.zx))*vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0);
}
// Kept identical in both terrain fragment passes: the two coverages partition every screen sample.
float regionLodDither() {
  ivec2 q = ivec2(gl_FragCoord.xy) & ivec2(7);
  int b0 = ((q.x & 1) ^ (q.y & 1))*2 + (q.y & 1);
  int b1 = (((q.x >> 1) & 1) ^ ((q.y >> 1) & 1))*2 + ((q.y >> 1) & 1);
  int b2 = (((q.x >> 2) & 1) ^ ((q.y >> 2) & 1))*2 + ((q.y >> 2) & 1);
  return (float(b0*16 + b1*4 + b2) + 0.5)/64.0;
}
void main() {
  if (uRegionCoverage <= 0.0 || regionLodDither() >= uRegionCoverage) discard;
  if (vHeight < 0.0) discard;
  if (uClipHalf > 0.0 && max(abs(vLocal.x), abs(vLocal.y)) < uClipHalf) discard;
  oG0 = vec4(length(vPosition - uCamPos), octEncode(normalize(vNormal)), 1.0);
  oG1 = vec4(sqrt(clamp(vColour, 0.0, 1.0)), 0.9);
  oG2 = vec4(0.0);
  oG3 = vec4(1.0, 1.0, 1.0, 0.0);
}
