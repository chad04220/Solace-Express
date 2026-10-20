//! kFarRegionFS
//! Coarse land goes through the normal deferred terrain light and atmospheric perspective, never a sky overlay.
in vec3 vColour, vNormal, vPosition;
in float vHeight, vLights;
in vec2 vLocal, vPatch;
uniform vec3 uCamPos;
uniform float uRegionCoverage;
uniform float uNight;      // (0 day .. 1 night: the towns' and cities' lights, sodium-warm, as they come on)
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
float lightSpeck(vec2 p) {   // a town's lights in clumps: a hashed 250 m lattice, its mean where a pixel spans its cells
  vec2 cell = p/250.0;
  float h = fract(sin(dot(floor(cell), vec2(127.1, 311.7)))*43758.5453);
  return mix(step(0.62, h)*2.6, 1.0, clamp(length(fwidth(cell)) - 0.4, 0.0, 1.0));
}
void main() {
  if (uRegionCoverage <= 0.0 || regionLodDither() >= uRegionCoverage) discard;
  if (vHeight < 0.0) discard;
  if (uClipHalf > 0.0 && max(abs(vLocal.x), abs(vLocal.y)) < uClipHalf) discard;
  oG0 = vec4(length(vPosition - uCamPos), octEncode(normalize(vNormal)), 1.0);
  oG1 = vec4(sqrt(clamp(vColour, 0.0, 1.0)), 0.9);
  // (squared: each glow drawn in to its core, not spread over the vertex grid's cells)
  float lit = vLights > 0.0 ? vLights*vLights*lightSpeck(vPatch)*smoothstep(0.05, 0.6, uNight) : 0.0;
  oG2 = vec4(vec3(1.0, 0.72, 0.4)*lit*0.6, 0.0);
  oG3 = vec4(1.0, 1.0, 1.0, 0.0);
}
