//! kUIVS
//! ------------------------------------------------------------------------------------------------
#version 330 core
layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUV; layout(location=2) in vec4 aCol; layout(location=3) in vec4 aMode;
uniform vec2 uScreen; out vec2 vUV; out vec4 vCol; out float vMode; out vec2 vHalf; out float vP; out float vBehind;
// (a mode past 16: drawn behind the aircraft - Renderer::uiBehindAircraft)
void main(){ vUV = aUV; vCol = aCol; vBehind = step(15.5, aMode.x); vMode = aMode.x - 16.0*vBehind; vHalf = aMode.yz; vP = aMode.w; gl_Position = vec4(aPos.x/uScreen.x*2.0-1.0, 1.0-aPos.y/uScreen.y*2.0, 0.0, 1.0); }
