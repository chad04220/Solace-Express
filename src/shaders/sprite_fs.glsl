//! kSpriteFS
#version 330 core
in vec2 vUV; in vec4 vCol; in float vDist; in vec2 vKind; in vec3 vWorld;
out vec4 oColor;
uniform sampler2D uDepth; uniform vec2 uRes; uniform vec2 uUVS; uniform vec3 uSunDir; uniform vec3 uSunCol; uniform vec3 uAmb; uniform float uFogB; uniform float uTime;
uniform vec3 uCamPos; uniform vec3 uCamR; uniform vec3 uCamU;   // (the vertex shader's: a billboard's sides in the world)
// the billows of smoke, dust and fire: the clouds' baked, tileable fbm (four octaves, 16 units across) - one fetch
// (three octaves of hashed value noise worked out in every pixel of the big overlapping puffs tripled the sprites' cost)
uniform sampler2D uSpriteNoise;
float sFbm(vec2 p){ return textureLod(uSpriteNoise, p*(1.0/16.0), 0.0).r*0.9375; }
void main(){
  float sceneT = texture(uDepth, gl_FragCoord.xy/uRes*uUVS).r;
  int kind = int(vKind.x + 0.5);
  float seed = clamp((vKind.x - float(kind))/0.4, 0.0, 1.0);   // (the sprite's own value, in its kind's fraction)
  float soft = clamp((sceneT - vDist)/max(vKind.y, 0.05), 0.0, 1.0);
  if (soft <= 0.0) discard;
  vec2 c = vUV*2.0 - 1.0; float r2 = dot(c,c);
  vec4 o;
  if(kind==10){ // opaque ordnance, rendered independently of the bounded beam effects
    if(r2>1.)discard;
    float nz=sqrt(max(0.,1.-r2));
    vec3 n=normalize(vec3(c,nz));
    float light=.22+.78*max(0.,dot(n,normalize(vec3(.35,.65,.7))));
    o=vec4(vCol.rgb*(uAmb+uSunCol*light)+vec3(.08)*pow(nz,30.),vCol.a);
  } else if (kind == 0) {        // smoke / dust / spray puff: a billow - its edge and its density broken by noise of its
    // own, turning slowly as it drifts - lit as a ball of it would be: brighter on the sun's side, its thin edges glowing
    // when the sun is behind it, the sky's light in its folds
    float ang = seed*6.2832 + uTime*0.25*(seed - 0.5);
    vec2 cr = mat2(cos(ang), -sin(ang), sin(ang), cos(ang))*c;
    float n = sFbm(cr*2.1 + seed*37.0);
    float a = smoothstep(1.0, 0.15, r2 + (n - 0.5)*0.75); a *= a*(0.7 + 0.45*n);
    vec3 back = cross(uCamR, uCamU);
    vec3 nw = normalize(uCamR*c.x + uCamU*c.y + back*sqrt(max(1.0 - r2, 0.0)));
    float diff = 0.5 + 0.5*max(dot(nw, normalize(uSunDir + vec3(0.0, 0.3, 0.0))), 0.0);
    float fwd = pow(max(dot(normalize(vWorld - uCamPos), uSunDir), 0.0), 6.0)*(1.15 - a);
    float sunUp = max(uSunDir.y + 0.1, 0.0);
    o = vec4(vCol.rgb*(uSunCol*sunUp*(diff*(0.95 + 0.35*n) + fwd*0.7)*1.2 + uAmb*(0.8 + 0.3*n)), vCol.a*min(a, 1.0));
  } else if (kind == 1) { // additive glow light
    float a = (exp(-r2*6.0) + 0.15*exp(-r2*1.5) - 0.0335)*(1.0 - smoothstep(0.6, 1.0, r2)); a = max(a, 0.0);
    o = vec4(vCol.rgb*a*vCol.a, 0.0);
  } else if (kind == 2) { // checkpoint gate: segmented counter-rotating bands, sweeping highlight, inward pulses
    float r = sqrt(r2), ang = atan(c.y, c.x), act = vCol.a, T = uTime;
    float rim = smoothstep(0.03, 0.0, abs(r - 0.86)) + 0.45*exp(-abs(r - 0.86)*22.0);
    float outer = smoothstep(0.022, 0.0, abs(r - 0.935))*step(0.38, fract(ang*12.0/6.2832 + T*0.22));
    float inner = smoothstep(0.014, 0.0, abs(r - 0.77))*step(0.55, fract(ang*36.0/6.2832 - T*0.45))*0.8;
    float sweep = pow(max(cos(ang - T*2.4), 0.0), 18.0)*smoothstep(0.12, 0.0, abs(r - 0.86))*1.8;
    float sweep2 = pow(max(cos(ang + T*1.7 + 3.1416), 0.0), 30.0)*smoothstep(0.05, 0.0, abs(r - 0.935))*1.2;
    float pw = fract(T*0.55);
    float pulse = smoothstep(0.025, 0.0, abs(r - mix(0.84, 0.15, pw)))*(1.0 - pw)*0.7;
    float ticks = smoothstep(0.03, 0.0, abs(r - 0.70))*step(0.9, fract(ang*4.0/6.2832 + 0.125))*1.2;
    float film = 0.07*smoothstep(0.86, 0.3, r)*(0.55 + 0.45*sin(r*34.0 - T*5.0));
    float a = rim*1.25 + outer + inner + (sweep + sweep2 + pulse + ticks + film)*act;
    a *= 1.0 - smoothstep(0.97, 1.0, r);
    o = vec4(vCol.rgb*a*(0.35 + 0.65*act), 0.0);
  } else if (kind == 6) { // expanding shockwave / halo ring
    float r = sqrt(r2);
    float a = smoothstep(0.05, 0.0, abs(r - 0.88)) + 0.4*exp(-abs(r - 0.88)*12.0);
    a *= 1.0 - smoothstep(0.97, 1.0, r);
    o = vec4(vCol.rgb*a*vCol.a, 0.0);
  } else if (kind == 7) { // ember: a motion-blurred streak (x along the motion), brightest at its head
    float a = exp(-c.y*c.y*5.0)*smoothstep(1.0, 0.75, abs(c.x))*mix(0.45, 1.0, smoothstep(-1.0, 0.8, c.x));
    a = max(a - 0.02, 0.0);
    o = vec4(vCol.rgb*a*vCol.a, 0.0);
  } else if (kind == 9) { // fireball billow: alpha-blended and self-lit (overlaps read as dense fire, not white), its
    // edge torn by noise rolling up and out, hotter where it is thickest
    float ang = seed*6.2832 + uTime*0.6*(seed - 0.5);
    vec2 cr = mat2(cos(ang), -sin(ang), sin(ang), cos(ang))*c;
    float n = sFbm(cr*2.4 + vec2(seed*23.0, -uTime*1.3));
    float rr = r2 + (n - 0.5)*0.8;
    float a = smoothstep(1.0, 0.15, rr);
    float core = smoothstep(0.55, 0.0, rr);
    o = vec4(vCol.rgb*(0.7 + 0.7*core + 0.25*n), vCol.a*a);
  } else if (kind == 8) { // vapour ribbon (lit): soft across its width (v), continuous along its length
    float a = smoothstep(1.0, 0.0, abs(c.y)); a *= a;
    float wisp = 0.85 + 0.15*sin(vWorld.x*0.9 + vWorld.z*1.3 + vWorld.y*0.7 + uTime*0.8);
    vec3 n = normalize(vec3(0.0, c.y, sqrt(max(1.0 - c.y*c.y, 0.0))));
    float l = 0.6 + 0.4*max(dot(n, normalize(uSunDir + vec3(0,0.3,0))), 0.0);
    o = vec4(vCol.rgb*(uSunCol*l*1.2*max(uSunDir.y+0.1,0.0) + uAmb), vCol.a*a*wisp);
  } else if (kind == 11) { // checkpoint route (alpha-blended, so it keeps its colour over a bright sky): a ribbon from
    // gate to gate (u: along it, in chevron spacings; v across). A dark translucent body, a soft core and a fine neon
    // rail at each edge, chevrons flowing on toward the next gate between them - a sharp bright front, a fading tail -
    // and a slow wave of light running the same way; far off, a few pixels across, one soft line. Every edge is
    // anti-aliased by its own pixel size, and the chevrons fade as they crowd into a pixel. Layered front to back
    // (premultiplied), then divided out for the blend
    float y = abs(c.y), px = max(fwidth(c.y), 1e-4);   // (px: a pixel, in half-widths)
    float detail = 1.0 - smoothstep(0.18, 0.45, px);
    float wave = 0.8 + 0.2*sin(vUV.x*0.4 - uTime*2.2);
    float th = max(0.04, 0.6*px), rail = (1.0 - smoothstep(th - 0.5*px, th + 0.5*px, abs(y - 0.86)))*min(1.0, 0.07/th);
    float ph = vUV.x + 0.55*y - uTime*1.4, pw = max(fwidth(ph), 1e-4), band = fract(ph);
    float chev = smoothstep(0.42, 0.9, band)*(1.0 - smoothstep(0.92 - pw, 0.92 + pw, band))*(1.0 - smoothstep(0.64, 0.74, y));
    chev *= (1.0 - smoothstep(0.18, 0.4, pw))*detail;
    vec3 hue = vCol.rgb;
    vec4 P = vec4(hue*0.06, 1.0)*0.24*(1.0 - smoothstep(0.84, 0.9, y))*detail;                        // body
    float ac = exp(-y*y*9.0)*mix(0.85, 0.3, detail); P = vec4(hue*1.5*wave, 1.0)*ac + P*(1.0 - ac);   // core (the far line)
    float ar = rail*0.95*detail; P = vec4(hue*2.2*wave, 1.0)*ar + P*(1.0 - ar);                       // rails
    float ah = chev*0.95; P = vec4(mix(hue, vec3(1.0), 0.3)*2.6*wave, 1.0)*ah + P*(1.0 - ah);          // chevrons
    P *= 1.0 - smoothstep(0.93, 1.0, y);
    o = vec4(P.rgb/max(P.a, 1e-4), P.a*vCol.a);
  } else if (kind == 12) { // lightning channel (x along it, y across): a white-hot core - never under a pixel and a half
    // wide - in a violet-blue glow, the segment's ends soft so its neighbours join it seamlessly
    float y = abs(c.y), cw = max(0.06, 1.5*fwidth(c.y));
    float core = exp(-y*y/(cw*cw)) + 0.35*exp(-y*y/(9.0*cw*cw));
    float halo = 0.22*exp(-y*y*4.0);
    float ends = smoothstep(1.0, 0.8, abs(c.x));
    o = vec4((vec3(1.0)*core + vec3(0.5, 0.55, 1.0)*halo)*vCol.rgb*vCol.a*ends, 0.0);
  } else if (kind == 13 || kind == 14) { // a raindrop's splash (seed: how far through its tenth of a second it is):
    // 13 a ring spreading on the ground or the water, 14 the crown of droplets it throws up; lit as the rain
    float ph = seed, a = 0.0;
    if (kind == 13) a = exp(-pow((sqrt(r2) - mix(0.2, 0.92, ph))/0.09, 2.0))*(1.0 - ph);
    else for (int k = 0; k < 5; k++) {
      float dx = (float(k) - 2.0)*0.32, h = 4.0*ph*(1.0 - ph);   // (each droplet out and up, then down)
      vec2 dp = c - vec2(dx*(0.4 + 1.2*ph), -0.7 + 1.3*h*(1.0 - 0.25*abs(float(k) - 2.0)));
      a += exp(-dot(dp, dp)*55.0);
    }
    o = vec4(min(vCol.rgb, vec3(1.0))*(uAmb*2.0 + 0.1)*smoothstep(0.06, 0.2, uAmb.y) + max(vCol.rgb - vec3(1.0), vec3(0.0)), vCol.a*min(a, 1.0));
  } else if (kind == 3) { // rain streak (a colour past 1 is light on it: a landing light's beam, lightning)
    float a = smoothstep(1.0, 0.0, abs(c.x)) * smoothstep(1.0, 0.6, abs(c.y));
    // (by daylight; at night hardly seen but where a light catches them)
    o = vec4(min(vCol.rgb, vec3(1.0))*(uAmb*2.0 + 0.1)*smoothstep(0.06, 0.2, uAmb.y) + max(vCol.rgb - vec3(1.0), vec3(0.0)), vCol.a*a);
  } else if (kind == 4) { // fire (additive): tongues of flame licking up through it (noise rising with time), a hotter,
    // paler heart
    float n = sFbm(vec2(c.x*2.3 + seed*17.0, c.y*1.6 - uTime*(2.2 + 1.5*seed)));
    float rr = r2 + (n - 0.5)*0.9 - c.y*0.15;
    float a = smoothstep(1.0, 0.0, rr), core = smoothstep(0.45, 0.0, rr);
    o = vec4((vCol.rgb*a*a*3.0 + vec3(1.0, 0.8, 0.5)*core*core*dot(vCol.rgb, vec3(0.6)))*vCol.a, 0.0);
  } else {                // snow flake (lit as the rain)
    float a = smoothstep(1.0, 0.2, r2);
    o = vec4(min(vCol.rgb, vec3(1.0))*(uAmb*2.5 + uSunCol*0.3)*mix(0.15, 1.0, smoothstep(0.06, 0.2, uAmb.y)) + max(vCol.rgb - vec3(1.0), vec3(0.0)), vCol.a*a);
  }
  float fog = exp(-vDist*uFogB*0.5);
  o.rgb *= fog; o.a *= mix(1.0, fog, 0.5);
  o.a *= soft; if (kind == 1 || kind == 2 || kind == 4 || kind == 6 || kind == 7 || kind == 12) o.rgb *= soft;
  if (kind == 1 || kind == 4 || kind == 7) o.rgb *= smoothstep(0.8, 4.0, vDist);   // glows right at the lens don't fill the view
  oColor = o;
}
