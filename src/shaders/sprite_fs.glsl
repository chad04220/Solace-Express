//! kSpriteFS
#version 330 core
in vec2 vUV; in vec4 vCol; in float vDist; in vec2 vKind; in vec3 vWorld;
out vec4 oColor;
uniform sampler2D uDepth; uniform vec2 uRes; uniform vec3 uSunDir; uniform vec3 uSunCol; uniform vec3 uAmb; uniform float uFogB; uniform float uTime;
void main(){
  float sceneT = texture(uDepth, gl_FragCoord.xy/uRes).r;
  int kind = int(vKind.x + 0.5);
  float soft = clamp((sceneT - vDist)/max(vKind.y, 0.05), 0.0, 1.0);
  if (soft <= 0.0) discard;
  vec2 c = vUV*2.0 - 1.0; float r2 = dot(c,c);
  vec4 o;
  if (kind == 0) {        // soft smoke / dust puff (lit)
    float a = smoothstep(1.0, 0.0, r2); a *= a;
    vec3 n = normalize(vec3(c, sqrt(max(1.0-r2, 0.0))));
    float l = 0.55 + 0.45*max(dot(n, normalize(uSunDir + vec3(0,0.3,0))), 0.0);
    o = vec4(vCol.rgb*(uSunCol*l*1.2*max(uSunDir.y+0.1,0.0) + uAmb), vCol.a*a);
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
  } else if (kind == 9) { // fireball billow: alpha-blended and self-lit (overlaps read as dense fire, not white)
    float n = 0.72 + 0.28*sin(vWorld.x*0.55 + vWorld.y*0.8 + uTime*2.7)*sin(vWorld.z*0.6 - vWorld.y*0.5 + uTime*2.1);
    float rr = r2/max(n, 0.3);
    float a = smoothstep(1.0, 0.2, rr);
    float core = smoothstep(0.6, 0.0, r2);
    o = vec4(vCol.rgb*(0.8 + 0.5*core), vCol.a*a);
  } else if (kind == 8) { // vapour ribbon (lit): soft across its width (v), continuous along its length
    float a = smoothstep(1.0, 0.0, abs(c.y)); a *= a;
    float wisp = 0.85 + 0.15*sin(vWorld.x*0.9 + vWorld.z*1.3 + vWorld.y*0.7 + uTime*0.8);
    vec3 n = normalize(vec3(0.0, c.y, sqrt(max(1.0 - c.y*c.y, 0.0))));
    float l = 0.6 + 0.4*max(dot(n, normalize(uSunDir + vec3(0,0.3,0))), 0.0);
    o = vec4(vCol.rgb*(uSunCol*l*1.2*max(uSunDir.y+0.1,0.0) + uAmb), vCol.a*a*wisp);
  } else if (kind == 3) { // rain streak
    float a = smoothstep(1.0, 0.0, abs(c.x)) * smoothstep(1.0, 0.6, abs(c.y));
    o = vec4(vCol.rgb*(uAmb*2.0 + 0.1), vCol.a*a);
  } else if (kind == 4) { // fire (additive)
    float a = smoothstep(1.0, 0.0, r2);
    o = vec4(vCol.rgb*a*a*vCol.a*3.0, 0.0);
  } else {                // snow flake
    float a = smoothstep(1.0, 0.2, r2);
    o = vec4(vCol.rgb*(uAmb*2.5 + uSunCol*0.3), vCol.a*a);
  }
  float fog = exp(-vDist*uFogB*0.5);
  o.rgb *= fog; o.a *= mix(1.0, fog, 0.5);
  o.a *= soft; if (kind == 1 || kind == 2 || kind == 4 || kind == 6 || kind == 7) o.rgb *= soft;
  if (kind == 1 || kind == 4 || kind == 7) o.rgb *= smoothstep(0.8, 4.0, vDist);   // glows right at the lens don't fill the view
  oColor = o;
}
