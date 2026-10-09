//! kCloudAccFS
//! The clouds' own temporal accumulation, at their quarter resolution, between the march and the composite. The march
//! is jittered every frame, and looking towards the sun its silver linings swing hard with each sample's place (the
//! forward lobe, through a light depth that falls off exponentially): the main TAA, leaning on the new frame while
//! the view moves and taking a cloud over the sky for infinitely far, let that through as a shimmer. Here each
//! texel's last value is fetched from where its cloud was (the march's scatter-weighted distance, through last
//! frame's camera), kept to the range of this frame's neighbours (so lightning, a cloud's edge or an aircraft
//! crossing in front never leaves a trail), and blended in an eighth at a time.
#version 330 core
in vec2 vUV; layout(location=0) out vec4 oCloud; layout(location=1) out vec2 oDist;
uniform sampler2D uCur; uniform sampler2D uCurT; uniform sampler2D uCurD;   // this frame: in-scatter + transmittance, cloud distance, scene depth
uniform sampler2D uHist; uniform sampler2D uHistD;                         // the last accumulation: the same, (cloud distance, scene depth)
uniform vec2 uCloudHi;   // this view's last cloud texel
uniform vec2 uHistUVS;   // the cloud texels in use over the history texture's size
uniform vec2 uView;      // the render resolution the clouds were traced for (each texel a 2x2 block of it)
uniform mat3 uCamRot; uniform mat3 uPrevCamRot; uniform vec3 uCamDelta; uniform float uTanHalf; uniform float uAspect;
uniform float uHistValid; uniform float uDt;
vec4 toC(vec4 c){ return vec4(c.rgb/(1.0 + max(c.r, max(c.g, c.b))), c.a); }   // (bright silver linings compressed for the clamp)
vec4 fromC(vec4 c){ return vec4(c.rgb/max(1.0 - max(c.r, max(c.g, c.b)), 1e-3), c.a); }
void main(){
  ivec2 q = ivec2(gl_FragCoord.xy), hi = ivec2(uCloudHi);
  vec4 cur = texelFetch(uCur, q, 0);
  float ct = texelFetch(uCurT, q, 0).r, sd = texelFetch(uCurD, q, 0).r;
  // the range of this frame's 3x3 neighbourhood (mean and spread)
  vec4 m1 = vec4(0.0), m2 = vec4(0.0);
  for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
    vec4 c = toC(texelFetch(uCur, clamp(q + ivec2(i, j), ivec2(0), hi), 0));
    m1 += c; m2 += c*c;
  }
  m1 /= 9.0; vec4 sdv = sqrt(max(m2/9.0 - m1*m1, 0.0));
  // where this texel's cloud was last frame: its ray through the centre of the texel's 2x2 block, out to the cloud
  vec2 ndc = (vec2(q)*2.0 + 1.0)/uView*2.0 - 1.0;
  vec3 rd = normalize(uCamRot*vec3(ndc.x*uTanHalf*uAspect, ndc.y*uTanHalf, -1.0));
  vec3 d = ct > 0.0 ? transpose(uPrevCamRot)*(rd*ct + uCamDelta) : transpose(uPrevCamRot)*rd;   // (no cloud: a direction)
  vec2 puv = d.z < -1e-4 ? vec2(d.x/(-d.z)/(uTanHalf*uAspect), d.y/(-d.z)/uTanHalf)*0.5 + 0.5 : vec2(-1.0);
  vec4 res = cur; vec2 dist = vec2(ct, sd);
  if (uHistValid > 0.5 && all(greaterThan(puv, vec2(0.0))) && all(lessThan(puv, vec2(1.0)))) {
    vec2 huv = puv*uHistUVS;
    vec4 h = toC(texture(uHist, huv));
    vec2 hd = texture(uHistD, huv).rg;
    h = clamp(h, m1 - 1.25*sdv - 0.002, m1 + 1.25*sdv + 0.002);
    float a = 0.125;
    // the ground or an aircraft in front moved (another scene depth here): what was accumulated belongs to other rays
    if (abs(log(max(hd.y, 0.1)) - log(max(sd, 0.1))) > 0.15) a = 1.0;
    a = 1.0 - pow(1.0 - a, clamp(uDt*60.0, 0.05, 4.0));   // (the same smoothing in time at any frame rate)
    res = fromC(mix(h, toC(cur), a));
    // (the distance is kept from the history where both saw cloud: it steadies the next reprojection)
    if (ct > 0.0 && hd.x > 0.0) dist.x = mix(hd.x, ct, max(a, 0.25));
  }
  oCloud = res;
  oDist = dist;
}
