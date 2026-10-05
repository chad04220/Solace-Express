//! kTaaFS
//! Temporal anti-aliasing resolve: reprojects the previous frame (world points through the camera; aircraft pixels
//! through the aircraft's own motion), clamps it to the current neighbourhood and blends. Averages away the per-frame
//! jitter of the ray marcher (clouds, distant terrain, water sparkle) and sub-pixel geometry.
#version 330 core
in vec2 vUV; layout(location=0) out vec4 oHist; layout(location=1) out vec4 oColor;
uniform sampler2D uRaw; uniform sampler2D uDepth; uniform sampler2D uHist; uniform vec2 uRes; uniform float uHistValid;
uniform vec2 uRawRes; uniform vec2 uJit;   // ray-trace resolution (<= uRes: temporal upscaling) and this frame's jitter
uniform vec3 uCamPos; uniform mat3 uCamRot; uniform vec3 uPrevCamPos; uniform mat3 uPrevCamRot; uniform float uTanHalf; uniform float uAspect;
uniform vec3 uPlanePos; uniform mat3 uPlaneRot; uniform vec3 uPrevPlanePos; uniform mat3 uPrevPlaneRot;
vec3 toY(vec3 c){ c = c/(1.0 + max(c.r, max(c.g, c.b))); return vec3(0.25*c.r + 0.5*c.g + 0.25*c.b, 0.5*c.r - 0.5*c.b, -0.25*c.r + 0.5*c.g - 0.25*c.b); }
vec3 fromY(vec3 y){ vec3 c = vec3(y.x + y.y - y.z, y.x + y.z, y.x - y.y - y.z); return c/max(1.0 - max(c.r, max(c.g, c.b)), 1e-3); }
// 5-tap Catmull-Rom history fetch: keeps the accumulated image sharp
vec3 histCR(vec2 uv){
  vec2 sp = uv*uRes, tp = floor(sp - 0.5) + 0.5, f = sp - tp;
  vec2 w0 = f*(-0.5 + f*(1.0 - 0.5*f)), w1 = 1.0 + f*f*(-2.5 + 1.5*f), w2 = f*(0.5 + f*(2.0 - 1.5*f)), w3 = f*f*(-0.5 + 0.5*f);
  vec2 w12 = w1 + w2, t0 = (tp - 1.0)/uRes, t3 = (tp + 2.0)/uRes, t12 = (tp + w2/w12)/uRes;
  vec3 r = texture(uHist, vec2(t12.x, t0.y)).rgb*w12.x*w0.y + texture(uHist, vec2(t0.x, t12.y)).rgb*w0.x*w12.y
         + texture(uHist, t12).rgb*w12.x*w12.y + texture(uHist, vec2(t3.x, t12.y)).rgb*w3.x*w12.y + texture(uHist, vec2(t12.x, t3.y)).rgb*w12.x*w3.y;
  float ws = w12.x*w0.y + w0.x*w12.y + w12.x*w12.y + w3.x*w12.y + w12.x*w3.y;
  return max(r/ws, vec3(0.0));
}
void main(){
  // the raw frame was traced at a lower resolution, offset by this frame's jitter: reconstruct it at this pixel
  bool up = uRawRes.x < uRes.x - 0.5;
  vec2 ruv = vUV - uJit;
  ivec2 ip = up ? clamp(ivec2(floor(ruv*uRawRes)), ivec2(0), ivec2(uRawRes) - 1) : ivec2(gl_FragCoord.xy);
  vec4 cur = texelFetch(uRaw, ip, 0);
  float flag = cur.a;
  if (up) cur.rgb = texture(uRaw, ruv).rgb;
  vec3 m1 = vec3(0.0), m2 = vec3(0.0), cy = toY(cur.rgb);
  float fmin = flag, fmax = flag;   // pixel classes around this one (a silhouette edge holds both)
  for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
    vec4 nb = texelFetch(uRaw, clamp(ip + ivec2(i, j), ivec2(0), ivec2(uRawRes) - 1), 0);
    vec3 y = toY(nb.rgb);
    m1 += y; m2 += y*y;
    fmin = min(fmin, nb.a); fmax = max(fmax, nb.a);
  }
  m1 /= 9.0; vec3 sd = sqrt(max(m2/9.0 - m1*m1, 0.0));
  float t = texelFetch(uDepth, ip, 0).r;
  vec2 ndc = vUV*2.0 - 1.0;
  vec3 rd = normalize(uCamRot*vec3(ndc.x*uTanHalf*uAspect, ndc.y*uTanHalf, -1.0));
  vec3 d;
  if (t > 9e5) d = transpose(uPrevCamRot)*rd;          // sky: a direction, only camera rotation matters
  else {
    vec3 P = uCamPos + rd*t;
    if (flag > 0.4 && flag < 0.6) P = uPrevPlaneRot*(transpose(uPlaneRot)*(P - uPlanePos)) + uPrevPlanePos;
    d = transpose(uPrevCamRot)*(P - uPrevCamPos);
  }
  vec2 puv = d.z < -1e-4 ? vec2(d.x/(-d.z)/(uTanHalf*uAspect), d.y/(-d.z)/uTanHalf)*0.5 + 0.5 : vec2(-1.0);
  bool valid = uHistValid > 0.5 && flag > 0.1 && all(greaterThan(puv, vec2(0.0))) && all(lessThan(puv, vec2(1.0)));
  vec3 res = cur.rgb;
  if (valid) {
    vec3 hy = toY(histCR(puv));
    float k = flag > 0.9 ? 1.25 : 0.9;                  // tighter clamp for moving parts
    hy = clamp(hy, m1 - k*sd - 0.002, m1 + k*sd + 0.002);
    float motion = length((puv - vUV)*uRes);
    float hflag = texture(uHist, puv).a;   // what the history pixel was: aircraft, world or a moving effect
    float a = mix(0.08, 0.3, clamp(motion/12.0, 0.0, 1.0));   // fast motion: lean on the new frame, less smear
    if (flag < 0.4) a = max(a, 0.4);
    // disocclusion (a wing sweeping off the sky): drop the stale history - but only when no neighbour shares the
    // history's class, so jittered silhouette edges keep accumulating and stay anti-aliased
    if (hflag < fmin - 0.25 || hflag > fmax + 0.25) a = max(a, 0.9);
    res = fromY(mix(hy, cy, a));
  }
  oHist = vec4(res, flag);
  oColor = vec4(res, flag);
}
