//! kPostFS
#version 330 core
in vec2 vUV; out vec4 oColor;
uniform sampler2D uScene; uniform sampler2D uBloom; uniform float uExposure; uniform float uTime; uniform vec2 uRes;
uniform float uRainLens; uniform vec2 uSunScreen; uniform float uSunVisible; uniform float uFade; uniform float uVignette; uniform float uGLoad;
uniform sampler2D uDepthTex; uniform vec2 uDepthUVS;   // scene depth (in the render resolution's corner of its target): the sun glow and lens ghosts only appear when the sun itself is unobstructed
uniform float uBloomK; uniform sampler2D uRays; uniform vec3 uRayK;   // bloom mix; light-shaft tint and strength
vec3 aces(vec3 x){ const float a=2.51,b=0.03,c=2.43,d=0.59,e=0.14; return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0); }
float h21(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }
void main(){
  float sunVis = uSunVisible;
  if (sunVis > 0.0) {
    float open = 0.0;
    for (int k = 0; k < 5; k++) {
      vec2 o = k == 0 ? vec2(0.0) : vec2(k == 1 ? 1.0 : (k == 2 ? -1.0 : 0.0), k == 3 ? 1.0 : (k == 4 ? -1.0 : 0.0))*0.006;
      vec2 su = clamp(uSunScreen + o, vec2(0.001), vec2(0.999));
      open += step(9e5, texture(uDepthTex, su*uDepthUVS).r);
    }
    bool onScreen = all(greaterThan(uSunScreen, vec2(0.0))) && all(lessThan(uSunScreen, vec2(1.0)));
    sunVis *= onScreen ? open/5.0 : 1.0;
  }
  vec2 uv = vUV;
  // raindrops on the lens
  if (uRainLens > 0.0) {
    vec2 g = uv*vec2(uRes.x/uRes.y, 1.0)*9.0; vec2 id = floor(g); vec2 f = fract(g) - 0.5;
    float r = h21(id); float life = fract(uTime*0.15 + r);
    vec2 o = vec2(h21(id+1.3)-0.5, h21(id+2.7)-0.5)*0.6;
    float d = length(f - o);
    float drop = smoothstep(0.14, 0.06, d) * step(0.55, r) * (1.0 - life) * uRainLens;
    uv += (f - o)*drop*0.08;
  }
  // contrast-adaptive sharpening (no FXAA: TAA already anti-aliases, and a second AA pass only softens the image).
  // Works in a reversible tonemapped space so HDR highlights don't ring; the weight backs off where contrast is high.
  vec2 tp = 1.0/vec2(textureSize(uScene, 0));
  vec3 cM = texture(uScene, uv).rgb;
  vec3 tM = cM/(1.0 + cM);
  vec3 tN = texture(uScene, uv + vec2(0.0, tp.y)).rgb, tS = texture(uScene, uv - vec2(0.0, tp.y)).rgb;
  vec3 tE = texture(uScene, uv + vec2(tp.x, 0.0)).rgb, tW = texture(uScene, uv - vec2(tp.x, 0.0)).rgb;
  tN /= 1.0 + tN; tS /= 1.0 + tS; tE /= 1.0 + tE; tW /= 1.0 + tW;
  vec3 mn = min(tM, min(min(tN, tS), min(tE, tW))), mx = max(tM, max(max(tN, tS), max(tE, tW)));
  vec3 amp = sqrt(clamp(min(mn, 1.0 - mx)/max(mx, 1e-4), 0.0, 1.0));
  vec3 wgt = -amp/6.5;   // CAS sharpness ~0.5
  if (abs(texture(uScene, uv).a - 0.55) < 0.02) wgt = vec3(0.0);   // cockpit displays: already anti-aliased, sharpening only makes them crunchy
  vec3 sh = clamp((tM + (tN + tS + tE + tW)*wgt)/(1.0 + 4.0*wgt), 0.0, 0.99995);
  vec3 scene = sh/(1.0 - sh);
  vec3 c = mix(scene, texture(uBloom, uv).rgb/6.0, uBloomK) + texture(uRays, uv).rgb*uRayK;   // 6 bloom levels summed
  // subtle sun glare / lens flare ghosts
  if (uSunVisible > 0.0) {
    vec2 sd = uv - uSunScreen; sd.x *= uRes.x/uRes.y;
    c += vec3(1.0,0.85,0.6)*exp(-dot(sd,sd)*12.0)*0.35*sunVis;
    for (int i=1;i<4;i++){ vec2 gp = mix(uSunScreen, vec2(0.5), float(i)*0.55); vec2 gd = (uv - gp)*vec2(uRes.x/uRes.y,1.0);
      c += vec3(0.4,0.6,1.0)*smoothstep(0.05*float(i), 0.0, length(gd))*0.03*sunVis; }
  }
  c *= uExposure;
  c = aces(c);
  c = pow(c, vec3(1.0/2.2));
  vec2 vv = vUV - 0.5; c *= 1.0 - dot(vv,vv)*uVignette;
  // g-force tunnel: a red rim that deepens to dark red and then black at the screen edge and closes in as g builds
  if (uGLoad > 0.002) {
    float asp = uRes.x/uRes.y;
    float r = length(vv*vec2(asp, 1.0))/length(vec2(0.5*asp, 0.5));       // 0 centre .. 1 corner
    float g = uGLoad*(1.0 + 0.04*sin(uTime*7.5)*uGLoad);                 // a faint heartbeat pulse at high g
    float reach = mix(0.95, 0.12, g);
    float k = clamp((r - reach)/max(1.05 - reach, 0.05), 0.0, 1.0);    // depth into the band
    vec3 band = mix(vec3(0.7, 0.03, 0.02), vec3(0.22, 0.0, 0.0), smoothstep(0.0, 0.45, k));
    band = mix(band, vec3(0.0), smoothstep(0.35, 0.85, k));
    float a = smoothstep(0.0, 0.3, k)*clamp(0.2 + 0.9*g, 0.0, 1.0);
    c = mix(c*mix(vec3(1.0), vec3(1.0, 0.62, 0.58), g*0.35), band, a);
  }
  c += (h21(vUV*uRes + fract(uTime)*100.0) - 0.5)/255.0*2.0;
  c *= uFade;
  oColor = vec4(c, 1.0);
}
