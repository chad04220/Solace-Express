//! kPropDiscVS
//! Six vertices for one bounded AI prop disc. The real camera-relative disc is intersected only inside this bound.
#version 330 core
uniform vec4 uDiscBounds;
void main(){
  const vec2 corners[6] = vec2[6](vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,0), vec2(1,1), vec2(0,1));
  gl_Position = vec4(mix(uDiscBounds.xy, uDiscBounds.zw, corners[gl_VertexID]), 0.0, 1.0);
}
