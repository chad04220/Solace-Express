//! kPlaneMeshVS
//! The aircraft mesh pass (aircraft_mesh.cpp): the static part of the airframe as a mesh baked from its field, in
//! body space with the field's normal, material id and cabin ambient occlusion per vertex. The id is carried twice:
//! flat (the triangle's) and interpolated - they differ only on a triangle whose corners straddle two materials, and
//! the fragment shader asks the field there.
layout(location = 0) in vec3 aPos; layout(location = 1) in vec3 aNrm; layout(location = 2) in float aId; layout(location = 3) in float aAo;
uniform mat4 uVP; uniform vec2 uJit; uniform float uLogC; uniform mat3 uRot; uniform vec3 uPos;
out vec3 vW; out vec3 vN; flat out float vId; out float vIdS; out float vAo;
void main(){
  vW = uRot*aPos + uPos;
  vN = aNrm; vId = aId; vIdS = aId; vAo = aAo;
  gl_Position = uVP*vec4(vW, 1.0);
  gl_Position.xy -= 2.0*uJit*gl_Position.w;   // the TAA's sub-pixel jitter
  gl_Position.z = (log2(max(1e-6, 1.0 + gl_Position.w))*uLogC - 1.0)*gl_Position.w;   // logarithmic depth (the raster passes')
}
