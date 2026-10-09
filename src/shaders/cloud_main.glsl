//! kCloudMain

uniform sampler2D uSceneDepth; uniform int uFrame;
void main(){
  ivec2 full = ivec2(uRes);   // (the view's size: a camera feed uses a corner of larger targets)
  ivec2 fp2 = min(ivec2(gl_FragCoord.xy)*2 + ivec2(uFrame & 1, (uFrame >> 1) & 1), full - 1);
  float d = texelFetch(uSceneDepth, fp2, 0).r;
  vec2 uv = (vec2(fp2) + 0.5)/vec2(full);
  vec2 ndc = (uv + uJit)*2.0 - 1.0;
  vec3 rd = camRay(ndc);
  float jitter = fract(52.9829189*fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) + uSeed);
  oColor = traceClouds(uCamPos, rd, d, jitter);
  oDepth = d;
  oCloudMask = gCloudW > 1e-4 ? gCloudWT/gCloudW : -1.0;   // (location 2 here: the clouds' own distance, -1 none - kCloudAccFS)
}
