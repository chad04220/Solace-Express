//! kFeedRaysFS
//! A camera feed's light shafts, added over its picture (the main view adds its own in the composite)
#version 330 core
in vec2 vUV; out vec4 oColor; uniform sampler2D uTex; uniform vec2 uUVS; uniform vec3 uRayK;
void main(){ oColor = vec4(texture(uTex, vUV*uUVS).rgb*uRayK, 0.0); }
