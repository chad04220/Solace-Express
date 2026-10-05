//! kUIVS
//! ------------------------------------------------------------------------------------------------
#version 330 core
layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUV; layout(location=2) in vec4 aCol; layout(location=3) in vec4 aMode;
uniform vec2 uScreen; out vec2 vUV; out vec4 vCol; out float vMode; out vec2 vHalf; out float vP;
void main(){ vUV = aUV; vCol = aCol; vMode = aMode.x; vHalf = aMode.yz; vP = aMode.w; gl_Position = vec4(aPos.x/uScreen.x*2.0-1.0, 1.0-aPos.y/uScreen.y*2.0, 0.0, 1.0); }
