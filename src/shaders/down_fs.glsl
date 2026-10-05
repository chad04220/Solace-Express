//! kDownFS
//! ------------------------------------------------------------------------------------------------
//! Bloom: a physically based mip chain. Each level is a 13-tap downsample of the one above (the first uses a Karis
//! average per 2x2 block so single bright pixels can't flicker into blobs); the levels are then combined back up with
//! a 3x3 tent filter. The composite mixes it in energy-conserving, with no threshold: every bright source glows by
//! the same physics, a lens-like soft halo that widens with brightness.
#version 330 core
in vec2 vUV; out vec4 oColor; uniform sampler2D uTex; uniform vec2 uTexel; uniform int uFirst;
vec3 s(vec2 o){ return texture(uTex, vUV + o*uTexel).rgb; }
float karis(vec3 c){ return 1.0/(1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722))); }
void main(){
  vec3 a = s(vec2(-2.0, 2.0)), b = s(vec2(0.0, 2.0)), c = s(vec2(2.0, 2.0));
  vec3 d = s(vec2(-2.0, 0.0)), e = s(vec2(0.0)), f = s(vec2(2.0, 0.0));
  vec3 g = s(vec2(-2.0, -2.0)), h = s(vec2(0.0, -2.0)), i = s(vec2(2.0, -2.0));
  vec3 j = s(vec2(-1.0, 1.0)), k = s(vec2(1.0, 1.0)), l = s(vec2(-1.0, -1.0)), m = s(vec2(1.0, -1.0));
  vec3 r;
  if (uFirst == 1) {
    vec3 g0 = (j + k + l + m)*0.25, g1 = (a + b + d + e)*0.25, g2 = (b + c + e + f)*0.25, g3 = (d + e + g + h)*0.25, g4 = (e + f + h + i)*0.25;
    float w0 = karis(g0)*0.5, w1 = karis(g1)*0.125, w2 = karis(g2)*0.125, w3 = karis(g3)*0.125, w4 = karis(g4)*0.125;
    r = (g0*w0 + g1*w1 + g2*w2 + g3*w3 + g4*w4)/(w0 + w1 + w2 + w3 + w4);
  } else r = e*0.125 + (a + c + g + i)*0.03125 + (b + d + f + h)*0.0625 + (j + k + l + m)*0.125;
  float lum = dot(r, vec3(0.2126, 0.7152, 0.0722));
  if (!(lum >= 0.0 && lum < 1e6)) r = vec3(0.0);
  oColor = vec4(min(r, vec3(6e4)), 1.0);
}
