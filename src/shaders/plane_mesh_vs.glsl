//! kPlaneMeshVS
//! The aircraft mesh pass (aircraft_mesh.cpp): the static part of the airframe as a mesh baked from its field, in
//! body space with the field's normal, material id and cabin ambient occlusion per vertex. The id is carried twice:
//! flat (the triangle's) and interpolated - they differ only on a triangle whose corners straddle two materials, and
//! the fragment shader asks the field there.
layout(location = 0) in vec3 aPos; layout(location = 1) in vec3 aNrm; layout(location = 2) in float aId; layout(location = 3) in float aAo;
// (uPos: the aircraft relative to the camera, and uVP from the camera at the origin - placed in world metres, a vertex
// was held to the floats' spacing there, 4 mm at the map's edges, and the cockpit swam by pixels as the aircraft flew)
uniform mat4 uVP; uniform vec2 uJit; uniform float uLogC; uniform mat3 uRot; uniform vec3 uPos;
uniform sampler2D uPartPose; uniform int uPartInst;   // a rigid part's instance (its pose: 4 texels from kPartPoseFS), or -1 the airframe
out vec3 vW;   // relative to the camera
out vec3 vN; flat out float vId; out float vIdS; out float vAo;
out vec3 vB;   // body space: what the cabin's cut-outs and panels are decided from
void main(){
  vec3 pos = aPos, nrm = aNrm;
  if (uPartInst >= 0) {
    int b = uPartInst*4;
    mat3 R = mat3(texelFetch(uPartPose, ivec2(b, 0), 0).xyz, texelFetch(uPartPose, ivec2(b + 1, 0), 0).xyz, texelFetch(uPartPose, ivec2(b + 2, 0), 0).xyz);
    pos = R*aPos + texelFetch(uPartPose, ivec2(b + 3, 0), 0).xyz; nrm = transpose(inverse(R))*aNrm;   // (a control surface's pose is affine, not a rotation)
  }
  vW = uRot*pos + uPos; vB = pos;
  vN = nrm; vId = aId; vIdS = aId; vAo = aAo;
  gl_Position = uVP*vec4(vW, 1.0);
  gl_Position.xy -= 2.0*uJit*gl_Position.w;   // the TAA's sub-pixel jitter
  gl_Position.z = (log2(max(1e-6, 1.0 + gl_Position.w))*uLogC - 1.0)*gl_Position.w;   // logarithmic depth (the raster passes')
}
