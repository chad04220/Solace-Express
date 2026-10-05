//! kSpriteVS
#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUV; layout(location=2) in vec4 aCol; layout(location=3) in vec2 aKind;
layout(location=4) in float aBill;   // > 0: aPos is a billboard's centre, this its half size (it faces this view's camera)
uniform mat4 uViewProj; uniform vec3 uCamPos; uniform vec3 uCamR; uniform vec3 uCamU;
uniform mat4 uPanoView; uniform vec2 uPano;   // a panoramic camera feed (see camRay): projected onto its cylinder
out vec2 vUV; out vec4 vCol; out float vDist; out vec2 vKind; out vec3 vWorld;
void main(){
  vec3 p = aPos + (uCamR*(aUV.x*2.0 - 1.0) + uCamU*(aUV.y*2.0 - 1.0))*aBill;
  vUV = aUV; vCol = aCol; vKind = aKind; vWorld = p; vDist = length(p - uCamPos); gl_Position = uViewProj*vec4(p, 1.0);
  if (uPano.x > 0.0) { vec3 c = (uPanoView*vec4(p, 1.0)).xyz; float a = atan(c.x, -c.z), d = length(c);
    gl_Position = vec4(a/uPano.x*d, c.y/max(length(c.xz), 1e-3)/uPano.y*d, abs(a) > 1.9 ? 2.0*d : 0.98*d, d); }
}
