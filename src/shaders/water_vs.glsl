//! kWaterVS
//! The sea: a radial grid around the camera on y = 0 (rings growing geometrically out to the horizon), so the vertex
//! density follows the perspective and the panoramic projection stays smooth. World-space shading, so nothing swims.
layout(location = 0) in vec2 aXZ;   // the grid around the origin
uniform mat4 uVP; uniform vec2 uJit; uniform float uLogC; uniform vec3 uCamPos;
uniform mat4 uPanoView; uniform vec2 uPano;
out vec3 vW;
void main(){
  vec3 wp = vec3(uCamPos.x + aXZ.x, 0.0, uCamPos.z + aXZ.y);
  vW = wp;
  gl_Position = uVP*vec4(wp, 1.0);
  bool behind = false;
  if (uPano.x > 0.0) {
    vec3 c = (uPanoView*vec4(wp, 1.0)).xyz; float a = atan(c.x, -c.z), dd = length(c);
    gl_Position = vec4(a/uPano.x*dd, c.y/max(length(c.xz), 1e-3)/uPano.y*dd, 0.0, dd); behind = abs(a) > 1.9;
  }
  gl_Position.xy -= 2.0*uJit*gl_Position.w;
  gl_Position.z = (log2(max(1e-6, 1.0 + gl_Position.w))*uLogC - 1.0)*gl_Position.w;
  if (behind) gl_Position.z = 2.0*gl_Position.w;
}
