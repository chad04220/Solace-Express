//! kUIFS
#version 330 core
in vec2 vUV; in vec4 vCol; in float vMode; in vec2 vHalf; in float vP; out vec4 oColor;
uniform sampler2D uFont; uniform sampler2D uImg;
float sdRR(vec2 p, vec2 h, float r){ vec2 q = abs(p) - h + r; return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }
void main(){
  if (vMode < 0.5) { oColor = vCol; }
  else if (vMode < 1.5) {
    float d = texture(uFont, vUV).r;
    float w = fwidth(d)*0.75;
    float a = smoothstep(0.5 - w, 0.5 + w, d);
    oColor = vec4(vCol.rgb, vCol.a*a);
  } else if (vMode < 2.5) {
    float d = texture(uFont, vUV).r;  // soft shadow for text
    oColor = vec4(0.0, 0.0, 0.0, vCol.a*smoothstep(0.25, 0.55, d)*0.6);
  } else if (vMode < 3.5) { oColor = texture(uImg, vUV)*vCol; }
  else if (vMode < 5.0) { float d = sdRR(vUV, vHalf, (vMode - 4.0)*1000.0); oColor = vec4(vCol.rgb, vCol.a*clamp(0.5 - d, 0.0, 1.0)); }
  else if (vMode < 6.0) { float d = sdRR(vUV, vHalf, (vMode - 5.0)*1000.0); oColor = vec4(vCol.rgb, vCol.a*clamp(0.5*vP + 0.5 - abs(d + 0.5*vP), 0.0, 1.0)); }
  else if (vMode < 7.5) { float d = sdRR(vUV, vHalf, (vMode - 6.0)*1000.0); float k = clamp(1.0 - max(d, 0.0)/max(vP, 1.0), 0.0, 1.0);
    oColor = vec4(vCol.rgb, vCol.a*k*k*step(0.0, d)); }
  else {
    // AA capsule: half.x is segment half-length, half.y is stroke radius.
    // The CPU pads the quad so coverage reaches zero before its raster edge.
    vec2 q = vec2(max(abs(vUV.x) - vHalf.x, 0.0), vUV.y);
    float d = length(q) - vHalf.y;
    float aa = max(fwidth(d), 1.0);
    oColor = vec4(vCol.rgb, vCol.a*clamp(0.5 - d/aa, 0.0, 1.0));
  }
}
