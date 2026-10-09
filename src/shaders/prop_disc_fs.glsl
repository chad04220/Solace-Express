//! kPropDiscFS
//! Camera-relative, scene-depth-tested AI propellers. One disc per draw, never a fullscreen loop over traffic.
#version 330 core
out vec4 oColor;
uniform sampler2D uSceneDepth;
uniform vec2 uRes, uJit, uPano;
uniform float uTanHalf, uAspect, uFogB;
uniform vec3 uDiscCentre, uDiscRight, uDiscUp;  // camera-space basis and camera-relative hub
uniform vec4 uDisc;  // radius, this traffic aircraft's angle, blur, blade count
uniform vec3 uPropLight;
uniform int uClassOnly;
void main(){
  vec2 ndc = (gl_FragCoord.xy/uRes + uJit)*2.0 - 1.0;
  vec3 rd = uPano.x > 0.0 ? normalize(vec3(sin(ndc.x*uPano.x), ndc.y*uPano.y, -cos(ndc.x*uPano.x)))
                             : normalize(vec3(ndc.x*uTanHalf*uAspect, ndc.y*uTanHalf, -1.0));
  vec3 normal = cross(uDiscRight, uDiscUp);
  float denom = dot(rd, normal);
  if (abs(denom) < 1e-5) discard;
  float t = dot(uDiscCentre, normal)/denom;
  if (t <= 0.0 || t >= texelFetch(uSceneDepth, ivec2(gl_FragCoord.xy), 0).r) discard;
  vec3 hit = rd*t - uDiscCentre;
  vec2 q = vec2(dot(hit, uDiscRight), dot(hit, uDiscUp))/uDisc.x;
  float radius = length(q), edge = max(fwidth(radius), .001);
  float rim = 1.0 - smoothstep(1.0 - edge, 1.0, radius);
  if (rim <= 0.0) discard;
  float ang = atan(q.y, q.x) - uDisc.y;
  float wave = cos(uDisc.w*ang), aa = min(max(fwidth(wave), .015), .5);
  float blade = smoothstep(.90 - aa, .90 + aa, wave)*(1.0 - smoothstep(.9, 1.0, radius));
  float running = .10 + .08*smoothstep(.6, 1.0, wave) + .25*smoothstep(.95, 1.0, radius);
  float opacity = mix(blade, running, uDisc.z)*rim*.85*exp(-t*max(uFogB, 0.0)*.5);
  if (opacity <= .003) discard;
  if (uClassOnly == 1) { oColor = vec4(0.0, 0.0, 0.0, .2); return; }
  vec3 colour = vec3(.04)*uPropLight + vec3(.6, .6, .1)*smoothstep(.9, 1.0, radius)*.3;
  oColor = vec4(colour, opacity);
}
