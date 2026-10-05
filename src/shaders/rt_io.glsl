//! kRtIO
//! Full-screen scene pass I/O: the pixel outputs, the view (camera ray with the panorama case) and the TAA jitter.
in vec2 vUV;
layout(location=0) out vec4 oColor;
layout(location=1) out float oDepth;
layout(location=2) out float oCloudMask;   // 1: this pixel's clouds are left to the quarter-resolution cloud pass
uniform int uCloudSplit;
uniform vec2 uRes; uniform vec3 uCamPos; uniform mat3 uCamRot; uniform float uTanHalf; uniform float uAspect;
uniform vec2 uPano;   // x > 0: a panoramic camera (a cylinder around it: x the half angle, y the vertical extent at unit distance)
vec3 camRay(vec2 ndc){
  if (uPano.x > 0.0) { float a = ndc.x*uPano.x; return normalize(uCamRot*vec3(sin(a), ndc.y*uPano.y, -cos(a))); }
  return normalize(uCamRot*vec3(ndc.x*uTanHalf*uAspect, ndc.y*uTanHalf, -1.0));
}
uniform vec2 uJit; uniform float uSeed;  // TAA: sub-pixel jitter (uv units) and a per-frame noise seed
