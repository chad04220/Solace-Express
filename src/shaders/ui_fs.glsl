//! kUIFS
//! The interface: flat and gradient rectangles, outlines, glows, text and its shadow, images and anti-aliased lines; the flight HUD under the g-force lens (assembled after g_lens.glsl: shaders.h uiFSAssembly).
in vec2 vUV; in vec4 vCol; in float vMode; in vec2 vHalf; in float vP; in float vBehind; out vec4 oColor;
uniform sampler2D uFont; uniform sampler2D uImg;
// The scene's G-buffer (GB0) for what is drawn behind the aircraft: window pixels to its texels (xy), its size (zw;
// 0: no scene drawn)
uniform sampler2D uGB0; uniform vec4 uGBMap;
uniform float uGLoad; uniform float uTime; uniform vec2 uScreen;   // the g-force lens over the HUD (0: none) and its heartbeat's clock
float sdRR(vec2 p, vec2 h, float r){ vec2 q = abs(p) - h + r; return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }
void uiColour(){
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
// How much of this pixel an aircraft covers: the four nearest G-buffer texels, bilinearly, so the cut follows its
// outline smoothly at any render resolution. The classes from GB_PLANE (5) to GB_ENEMY (13) are all airframes: the
// own aircraft, its cabin, pods and displays, debris, the UFO, the wreck, the traffic and the enemies (kGBuffer;
// shader_keyword_test checks the numbers)
float aircraftCover(){
  vec2 g = gl_FragCoord.xy*uGBMap.xy - 0.5, f = fract(g);
  ivec2 i0 = ivec2(floor(g)), hi = ivec2(uGBMap.zw) - 1;
  float c[4];
  for (int k = 0; k < 4; k++) {
    int cls = int(texelFetch(uGB0, clamp(i0 + ivec2(k & 1, k >> 1), ivec2(0), hi), 0).w + 0.5);
    c[k] = cls >= 5 && cls <= 13 ? 1.0 : 0.0;
  }
  return mix(mix(c[0], c[1], f.x), mix(c[2], c[3], f.x), f.y);
}
void main(){
  uiColour();
  if (vBehind > 0.5 && uGBMap.z > 0.0) oColor.a *= 1.0 - aircraftCover();
  if (uGLoad > 0.002) {   // (blended over the scene the post pass already put the lens over: the same colour as if under it)
    float rr, th, beat, px;
    float gX = gLensShape(gl_FragCoord.xy/uScreen, uScreen, uGLoad, uTime, rr, th, beat, px);
    oColor.rgb = gLensColour(oColor.rgb, uGLoad, gX, rr, th, beat, px);
    oColor.a *= 1.0 - gLensCover(uGLoad, gX);   // (hidden under the iris, as the view is)
  }
}
