//! kUpFS
#version 330 core
in vec2 vUV; out vec4 oColor; uniform sampler2D uTex; uniform vec2 uTexel;
void main(){
  vec3 c = texture(uTex, vUV).rgb*4.0;
  c += (texture(uTex, vUV + vec2(uTexel.x, 0.0)).rgb + texture(uTex, vUV - vec2(uTexel.x, 0.0)).rgb
      + texture(uTex, vUV + vec2(0.0, uTexel.y)).rgb + texture(uTex, vUV - vec2(0.0, uTexel.y)).rgb)*2.0;
  c += texture(uTex, vUV + uTexel).rgb + texture(uTex, vUV - uTexel).rgb
     + texture(uTex, vUV + vec2(uTexel.x, -uTexel.y)).rgb + texture(uTex, vUV + vec2(-uTexel.x, uTexel.y)).rgb;
  oColor = vec4(c/16.0, 1.0);
}
