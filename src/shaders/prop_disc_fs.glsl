//! kPropDiscFS
//! Camera-relative, scene-depth-tested AI propellers. One disc per draw, never a fullscreen loop over traffic.
out vec4 oColor;
uniform sampler2D uSceneDepth;
uniform vec2 uRes, uJit, uPano;
uniform float uTanHalf, uAspect, uFogB;
uniform vec3 uDiscCentre, uDiscRight, uDiscUp;  // camera-space basis and camera-relative hub
uniform vec4 uDisc;  // radius, this traffic aircraft's angle, blur, blade count
uniform vec3 uPropLight;
uniform float uPropHub;  // model spinner radius / disc radius
uniform int uClassOnly;
void main(){
  vec2 ndc = (gl_FragCoord.xy/uRes + uJit)*2.0 - 1.0;
  vec3 rd = uPano.x > 0.0 ? normalize(vec3(sin(ndc.x*uPano.x), ndc.y*uPano.y, -cos(ndc.x*uPano.x)))
                             : normalize(vec3(ndc.x*uTanHalf*uAspect, ndc.y*uTanHalf, -1.0));
  vec3 normal = cross(uDiscRight, uDiscUp);
  float denom = dot(rd, normal);
  if (abs(denom) < 1e-5) discard;
  float t = dot(uDiscCentre, normal)/denom;
  vec3 hit = rd*t - uDiscCentre;
  vec2 q = vec2(dot(hit, uDiscRight), dot(hit, uDiscUp))/uDisc.x;
  vec2 dx = dFdx(q), dy = dFdy(q);
  if (t <= 0.0 || t >= texelFetch(uSceneDepth, ivec2(gl_FragCoord.xy), 0).r || dot(q, q) > 1.03) discard;
  vec4 blade = propellerVisual(q, dx, dy, uDisc.y, uDisc.z, uDisc.w, uPropHub, uPropLight);
  float opacity = blade.a*exp(-t*max(uFogB, 0.0)*.5);
  if (opacity <= .003) discard;
  if (uClassOnly == 1) { oColor = vec4(0.0, 0.0, 0.0, .2); return; }
  oColor = vec4(blade.rgb, opacity);
}
