//! kPostFS
//! The composite: the windscreen's rain, sharpening, bloom and light shafts, the sun's glare, tone mapping, the vignette and the g-force lens (assembled after g_lens.glsl: shaders.h postFSAssembly).
in vec2 vUV; out vec4 oColor;
uniform sampler2D uScene; uniform sampler2D uBloom; uniform float uExposure; uniform float uTime; uniform vec2 uRes;
uniform float uRainLens; uniform vec4 uRainFlow; uniform float uGlassMist; uniform vec2 uSunScreen; uniform float uSunVisible; uniform float uFade; uniform float uVignette; uniform float uGLoad;
uniform sampler2D uDepthTex; uniform vec2 uDepthUVS;   // scene depth (in the render resolution's corner of its target): the sun glow and lens ghosts only appear when the sun itself is unobstructed
uniform float uBloomK; uniform sampler2D uRays; uniform vec3 uRayK;   // bloom mix; light-shaft tint and strength
vec3 aces(vec3 x){ const float a=2.51,b=0.03,c=2.43,d=0.59,e=0.14; return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0); }
float h21(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }
float gRainRim = 0.0, gRainBody = 0.0;   // the drops on the glass: their dark rims (the cabin's shadow in them) and their bodies (the sky's light)
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
  // the g-force lens (g_lens.glsl): where this pixel is in it; past the edge of its clear middle it bends the picture
  // outward a touch, as the thicker part of a lens does
  float gL = uGLoad, gX = -1.0, gRR = 0.0, gTh = 0.0, gBeat = 0.0, gPx = 0.0;
  if (gL > 0.002) {
    gX = gLensShape(vUV, uRes, gL, uTime, gRR, gTh, gBeat, gPx);
    uv = 0.5 + (vUV - 0.5)*(1.0 - 0.035*gL*smoothstep(-0.05, 0.4, gX));
  }
  // rain on the windscreen (the cockpit view): only over what is seen through the glass (the cabin's own surfaces
  // are nearer than it). Still, the drops sit beaded on it, running down now and then; in the airflow they are swept
  // back along it - streaming away on screen from where the air meets the glass (uRainFlow), quicker and longer the
  // faster the aircraft goes. Each drop is a small lens; inside a cloud a fine mist of them streams past.
  if (uRainLens > 0.0 || uGlassMist > 0.0) {
    float glass = smoothstep(1.3, 1.8, texture(uDepthTex, uv*uDepthUVS).r);
    if (glass > 0.0) {
      vec2 asp = vec2(uRes.x/uRes.y, 1.0), off = vec2(0.0);
      float speed = uRainFlow.w, tm = mod(uTime, 512.0), rim = 0.0, body = 0.0;
      // beaded drops (blown off the glass as the airflow builds)
      if (uRainLens > 0.0 && speed < 0.95) {
        vec2 g = uv*asp*9.0; vec2 id = floor(g); vec2 f = fract(g) - 0.5;
        float r = h21(id); float life = fract(tm*0.12 + r);
        vec2 o = vec2(h21(id + 1.3) - 0.5, h21(id + 2.7) - 0.5)*0.6 - vec2(0.0, life*life*0.3*step(0.85, h21(id + 4.1)));   // (a few run down)
        float sz = 0.08 + 0.07*h21(id + 5.9);
        float d = length(f - o);
        float drop = smoothstep(sz, sz*0.55, d)*step(1.0 - 0.55*uRainLens, r)*(1.0 - life)*(1.0 - speed);
        off += (f - o)*drop*0.14; rim = max(rim, drop*smoothstep(sz*0.5, sz, d)); body = max(body, drop);
      }
      // swept drops and mist: lanes across the flow, a drop every so often along each moving with it, its trail behind
      float amt = max(uRainLens*smoothstep(0.1, 0.4, speed), uGlassMist);
      if (amt > 0.0) {
        vec2 S = uRainFlow.xy*asp, rel = uv*asp - S;
        float rho = length(rel), rho0 = length(0.5*asp - S);
        vec2 al = rel/max(rho, 1e-4)*uRainFlow.z, ac = vec2(-al.y, al.x);
        float x = atan(rel.y, rel.x)*rho0, sAl = rho*uRainFlow.z;
        for (int k = 0; k < 2; k++) {   // (two lane widths: drops and the mist's finer ones)
          float W = k == 0 ? 0.03 : 0.012, Lc = k == 0 ? 0.22 : 0.09;
          float dens = k == 0 ? uRainLens*smoothstep(0.1, 0.4, speed) : uGlassMist;
          if (dens <= 0.0) continue;
          float xi = floor(x/W), xf = fract(x/W) - 0.5;
          float v = mix(0.2, 1.8, speed)*(0.6 + 0.8*h21(vec2(xi, 7.1 + float(k))));   // screen heights a second
          float sp = sAl - tm*v, si = floor(sp/Lc), sf = fract(sp/Lc);
          float pres = step(1.0 - 0.7*dens, h21(vec2(xi + 0.37*float(k), si)));
          float head = 0.2 + 0.6*h21(vec2(si, xi + 3.3)), xo = (h21(vec2(xi*1.7, si*2.3)) - 0.5)*0.5;
          float rad = (0.2 + 0.16*h21(vec2(si + 1.9, xi)))*W, tail = Lc*mix(0.15, 0.7, speed);
          float dx = (xf - xo)*W, ds = (sf - head)*Lc;
          float tt = clamp(-ds/tail, 0.0, 1.0), rr = rad*mix(1.0, 0.35, tt);
          float dd = ds > 0.0 ? length(vec2(dx, ds)) : (ds < -tail ? length(vec2(dx, ds + tail)) : abs(dx));
          float m = pres*smoothstep(rr, rr*0.4, dd)*mix(1.0, 0.25, tt);
          off += (al*max(ds, 0.0) + ac*dx)/max(rr, 1e-4)*m*(k == 0 ? 0.011 : 0.004)/asp;
          rim = max(rim, m*(k == 0 ? 0.8 : 0.4)*smoothstep(rr*0.3, rr, dd)); body = max(body, m*(k == 0 ? 1.0 : 0.5));
        }
      }
      uv += off*glass;
      gRainRim = rim*glass; gRainBody = body*glass;
    }
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
  if (abs(texture(uScene, uv).a - 0.55) < 0.02) wgt *= 0.5;   // cockpit displays: already anti-aliased - a lighter touch (none at all left them soft after the TAA)
  vec3 sh = clamp((tM + (tN + tS + tE + tW)*wgt)/(1.0 + 4.0*wgt), 0.0, 0.99995);
  vec3 scene = sh/(1.0 - sh);
  vec3 blur = texture(uBloom, uv).rgb/6.0;   // 6 bloom levels summed: the whole picture, blurred
  if (gX > -0.05) {   // through the g-force lens past its clear middle: colour fringes, and the picture going soft
    float k = gL*smoothstep(-0.05, 0.35, gX);
    vec2 ca = (uv - 0.5)*0.012*k;
    scene = vec3(mix(scene.r, texture(uScene, uv + ca).r, k), scene.g, mix(scene.b, texture(uScene, uv - ca).b, k));
    scene = mix(scene, blur, 0.75*k);
  }
  vec3 c = mix(scene, blur, uBloomK) + texture(uRays, uv).rgb*uRayK;
  // subtle sun glare / lens flare ghosts
  if (uSunVisible > 0.0) {
    vec2 sd = uv - uSunScreen; sd.x *= uRes.x/uRes.y;
    c += vec3(1.0,0.85,0.6)*exp(-dot(sd,sd)*12.0)*0.35*sunVis;
    for (int i=1;i<4;i++){ vec2 gp = mix(uSunScreen, vec2(0.5), float(i)*0.55); vec2 gd = (uv - gp)*vec2(uRes.x/uRes.y,1.0);
      c += vec3(0.4,0.6,1.0)*smoothstep(0.05*float(i), 0.0, length(gd))*0.03*sunVis; }
  }
  c = c*(1.0 - 0.35*gRainRim) + (c*0.08 + vec3(0.025))*gRainBody;
  c *= uExposure;
  c = aces(c);
  c = pow(c, vec3(1.0/2.2));
  vec2 vv = vUV - 0.5; c *= 1.0 - dot(vv,vv)*uVignette;
  if (gL > 0.002) c = gLensColour(c, gL, gX, gRR, gTh, gBeat, gPx);
  c += (h21(vUV*uRes + fract(uTime)*100.0) - 0.5)/255.0*2.0;
  c *= uFade;
  oColor = vec4(c, 1.0);
}
