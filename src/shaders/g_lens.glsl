//! kGLens
//! The g-force lens (uGLoad, Game::update gTunnel): a red, bloodshot lens closing over the view as the load nears what breaks the airframe. The post pass draws it over the scene and the UI pass over the flight HUD, the same shape and colour.
// Its clear middle shrinks from past the corners to a small window as the load builds; past the edge of that the
// lens is dense and dark red. Its veins are a fixed tree round the view's centre: nine trunks come in from the edges,
// each forks into two branches and each branch into two twigs whose tips reach in towards the middle; the lens shows
// more of them the further it closes. Positions are in units of the half-diagonal (0 centre, 1 corner).
const float GV_TAU = 6.2831853, GV_SLOT = GV_TAU/9.0;
float gvH(float n){ return fract(sin(n*12.9898 + 4.1414)*43758.5453); }
float gvWrap(float a){ return a - GV_TAU*floor(a/GV_TAU + 0.5); }
float gvH3(vec3 p){ return fract(sin(dot(p, vec3(127.1, 311.7, 74.7)))*43758.5453); }
float gvN3(vec3 x){ vec3 i = floor(x), f = fract(x); f = f*f*(3.0 - 2.0*f);   // value noise (the lens's ragged edge)
  return mix(mix(mix(gvH3(i), gvH3(i + vec3(1, 0, 0)), f.x), mix(gvH3(i + vec3(0, 1, 0)), gvH3(i + vec3(1, 1, 0)), f.x), f.y),
             mix(mix(gvH3(i + vec3(0, 0, 1)), gvH3(i + vec3(1, 0, 1)), f.x), mix(gvH3(i + vec3(0, 1, 1)), gvH3(i + vec3(1, 1, 1)), f.x), f.y), f.z); }
// how much of one vessel covers the pixel at (th round the centre, s out from it): the vessel at angle ang there,
// half-width w, running from its tip (inner end) out to its root; px is a pixel in these units (anti-aliasing)
float gvVessel(float th, float s, float ang, float w, float tip, float root, float px){
  float d = abs(gvWrap(th - ang))*s;
  return (1.0 - smoothstep(w - px, w + px, d))*smoothstep(tip, tip + 0.04, s)*(1.0 - smoothstep(root, root + 0.02, s));
}
// a vessel's turn away from its parent: none where it leaves it, easing out to all of it at its tip
float gvPeel(float s, float root, float tip){ float u = clamp((root - s)/(root - tip), 0.0, 1.0); return 1.0 - (1.0 - u)*(1.0 - u); }
float gVeins(float th, float s, float px, float thick){
  th = mod(th, GV_TAU);
  float i0 = floor(th/GV_SLOT), v = 0.0;
  // (a vessel strays under 1.25 slots from its trunk's slot: the neighbouring trunks' trees are all that can reach here)
  for (int di = -1; di <= 1; di++) {
    float i = mod(i0 + float(di), 9.0);
    float tipT = 0.42 + 0.16*gvH(i + 1.7);
    float aT = (i + 0.5 + 0.5*(gvH(i) - 0.5))*GV_SLOT + (0.012*sin(s*9.0 + gvH(i + 7.1)*GV_TAU) + 0.005*sin(s*27.0 + gvH(i + 3.3)*GV_TAU))/max(s, 0.3);
    v = max(v, gvVessel(th, s, aT, (0.0035 + 0.0085*smoothstep(tipT, 1.1, s))*thick, tipT, 9.0, px));
    for (int c = 0; c < 2; c++) {
      float j = i*2.0 + float(c);
      float sb = tipT + 0.06 + 0.22*gvH(j + 11.3), tipB = 0.24 + 0.12*gvH(j + 5.9);   // where it leaves the trunk; its tip
      if (s > sb + 0.02) continue;
      float aB = aT + (float(c)*2.0 - 1.0)*GV_SLOT*(0.25 + 0.2*gvH(j + 2.2))*gvPeel(s, sb, tipB) + 0.02*sin(s*31.0 + gvH(j)*GV_TAU);
      v = max(v, 0.9*gvVessel(th, s, aB, (0.0022 + 0.0045*smoothstep(tipB, sb, s))*thick, tipB, sb, px));
      for (int e = 0; e < 2; e++) {
        float k = j*2.0 + float(e);
        float st = mix(tipB, sb, 0.25 + 0.55*gvH(k + 8.8)), tipW = 0.12 + 0.12*gvH(k + 4.4);   // where it leaves the branch; its tip
        float aW = aB + (float(e)*2.0 - 1.0)*GV_SLOT*(0.15 + 0.15*gvH(k + 6.6))*gvPeel(s, st, tipW) + 0.015*sin(s*47.0 + gvH(k + 1.1)*GV_TAU);
        v = max(v, 0.75*gvVessel(th, s, aW, (0.0014 + 0.0026*smoothstep(tipW, st, s))*thick, tipW, st, px));
      }
    }
  }
  return v;
}
// the lens's shape at a screen point (uv 0..1 on a screen res pixels wide and high) for strength gl at time t: how far
// past the edge of its clear middle the point is (negative inside). That middle is a little wider than tall, its edge
// ragged and swelling in with each heartbeat. Also hands back the point round the centre (rr out, th round), the
// heartbeat (0..1) and a pixel's size in those units, for gLensColour.
float gLensShape(vec2 uv, vec2 res, float gl, float t, out float rr, out float th, out float beat, out float px){
  float asp = res.x/res.y, hd = length(vec2(asp, 1.0));
  vec2 pc = (uv - 0.5)*vec2(asp, 1.0)/(0.5*hd);
  rr = length(pc); th = atan(pc.y, pc.x); px = 2.0/(res.y*hd);
  float ph = fract(t*1.45);   // (a racing pulse, ~87 a minute: a beat and its echo)
  beat = exp(-ph*9.0)*smoothstep(0.0, 0.03, ph) + 0.6*exp(-max(ph - 0.3, 0.0)*11.0)*smoothstep(0.27, 0.31, ph);
  float R = 1.25 - 1.07*pow(gl, 0.8) - 0.03*beat*gl;
  float edge = (gvN3(vec3(pc/max(rr, 1e-4)*2.5, t*0.1)) - 0.5)*0.14*R;
  return length(pc*vec2(0.88, 1.0)) - edge - R;
}
// the lens over a display colour c (after tone mapping): a red film over the whole view, faint at first and the colour
// draining through it as the load builds; past the edge of its clear middle dense and dark red, darkening far out as
// the airframe nears its end; the veins where it is dense and just inside its edge, swelling with each beat; and the
// lens catching the light, a faint glint round its clear middle and a soft sheen along its curve
vec3 gLensColour(vec3 c, float gl, float gX, float rr, float th, float beat, float px){
  float lum = dot(c, vec3(0.299, 0.587, 0.114)), dense = smoothstep(-0.05, 0.12, gX);
  c = mix(c, c*vec3(0.9, 0.3, 0.26) + lum*vec3(0.3, 0.04, 0.03), 0.85*smoothstep(0.0, 0.9, gl));
  c *= 1.0 - 0.3*pow(gl, 1.4);
  c = mix(c, c*vec3(0.62, 0.08, 0.06) + lum*vec3(0.2, 0.012, 0.008), dense);
  c += vec3(0.09, 0.006, 0.004)*(0.35 + 0.65*dense)*smoothstep(0.0, 0.6, gl);   // (its own faint red glow: it shows over a dark night too)
  c *= 1.0 - 0.75*smoothstep(0.06, 0.40 + 0.35*(1.0 - gl), gX)*smoothstep(0.35, 1.0, gl);
  if (gl > 0.1 && gX > -0.07) {
    float v = gVeins(th, rr, px, 1.0 + 0.3*beat*gl)*smoothstep(-0.07, 0.08, gX)*smoothstep(0.1, 0.4, gl);
    c = mix(c, c*0.2 + vec3(0.035, 0.0, 0.0), clamp(v, 0.0, 1.0)*0.9);
  }
  c += vec3(0.5, 0.12, 0.1)*exp(-gX*gX*2500.0)*0.12*gl;
  c += vec3(0.6, 0.32, 0.3)*(1.0 - smoothstep(0.0, 0.2, abs(rr - 0.66)))*(1.0 - smoothstep(0.0, 0.9, abs(gvWrap(th - 2.25))))*0.12*gl;
  return c;
}
