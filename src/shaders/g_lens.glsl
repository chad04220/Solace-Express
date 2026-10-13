//! kGLens
//! The g-force lens (uGLoad, Game::update gTunnel): the view closing in from its edges as an eye's iris closes, dark red, veined and throbbing with the heartbeat. The post pass draws it over the scene and the UI pass over the flight HUD, the same shape and colour.
// As the load builds, a dark red iris closes in from past the screen's corners towards its middle, the way an iris
// closes round a shrinking pupil. Its tissue stretches inward with it: fine radial fibres, the wavy ring of the
// collarette a little way out from the opening, crypts between the fibres and contraction furrows further out, and a
// dark ruffled rim at the opening itself. Through the opening the world is still seen, reddening and losing its colour
// as the load builds; through the iris only faintly, between its fibres, and towards the screen's edges not at all.
// Blood vessels branch in over it from the edges, their tips reaching just past the opening, so they creep in as it
// closes; with each beat of the heart (a lub and its dub) the iris tightens, the vessels swell and a dark flush runs
// over the view. Positions are in units of the half-diagonal (0 centre, 1 corner).
const float GV_TAU = 6.2831853, GV_TRUNKS = 11.0, GV_SLOT = GV_TAU/GV_TRUNKS;
float gvH(float n){ return fract(sin(n*12.9898 + 4.1414)*43758.5453); }
float gvWrap(float a){ return a - GV_TAU*floor(a/GV_TAU + 0.5); }
float gvH3(vec3 p){ return fract(sin(dot(p, vec3(127.1, 311.7, 74.7)))*43758.5453); }
float gvN3(vec3 x){ vec3 i = floor(x), f = fract(x); f = f*f*(3.0 - 2.0*f);   // value noise
  return mix(mix(mix(gvH3(i), gvH3(i + vec3(1, 0, 0)), f.x), mix(gvH3(i + vec3(0, 1, 0)), gvH3(i + vec3(1, 1, 0)), f.x), f.y),
             mix(mix(gvH3(i + vec3(0, 0, 1)), gvH3(i + vec3(1, 0, 1)), f.x), mix(gvH3(i + vec3(0, 1, 1)), gvH3(i + vec3(1, 1, 1)), f.x), f.y), f.z); }
// one vessel at the pixel (th round the centre, s out from it): at angle ang there, half-width w, from its tip (its inner
// end) out to its root. x: how much of the pixel it covers (anti-aliased over px); y: where across it the pixel lies (1
// its middle, 0 its walls), for its round shading
vec2 gvVessel(float th, float s, float ang, float w, float tip, float root, float px){
  float d = abs(gvWrap(th - ang))*s;
  float along = smoothstep(tip, tip + 0.03, s)*(1.0 - smoothstep(root, root + 0.02, s));
  return vec2((1.0 - smoothstep(w - px, w + px, d))*along, clamp(1.0 - d/max(w, 1e-5), 0.0, 1.0));
}
// a vessel's turn away from its parent: none where it leaves it, easing out to all of it at its tip
float gvPeel(float s, float root, float tip){ float u = clamp((root - s)/(root - tip), 0.0, 1.0); return 1.0 - (1.0 - u)*(1.0 - u); }
vec2 gvKeep(vec2 v, vec2 n){ return n.x > v.x ? n : v; }
// the vessels: eleven trunks in from beyond the screen's edges, each forking into two branches and each branch into two
// twigs, wandering as they go; their tips reach a little way past the opening (R), so they follow it in. thick: their
// swelling with the heartbeat. Hands back the nearest vessel's coverage and where across it the pixel lies
vec2 gVeins(float th, float s, float px, float thick, float R){
  th = mod(th, GV_TAU);
  float i0 = floor(th/GV_SLOT);
  vec2 v = vec2(0.0);
  // (a vessel strays under 1.25 slots from its trunk's slot: the neighbouring trunks' trees are all that can reach here)
  for (int di = -1; di <= 1; di++) {
    float i = mod(i0 + float(di), GV_TRUNKS);
    float tipT = R*(1.05 + 0.35*gvH(i + 1.7)) + 0.05;
    float aT = (i + 0.5 + 0.6*(gvH(i) - 0.5))*GV_SLOT + (0.035*sin(s*5.0 + gvH(i + 7.1)*GV_TAU) + 0.006*sin(s*13.0 + gvH(i + 3.3)*GV_TAU))/max(s, 0.25);
    v = gvKeep(v, gvVessel(th, s, aT, (0.0045 + 0.011*smoothstep(tipT, tipT + 0.9, s))*thick, tipT, 9.0, px));
    for (int c = 0; c < 2; c++) {
      float j = i*2.0 + float(c);
      float sb = tipT + 0.08 + 0.3*gvH(j + 11.3), tipB = R*(0.9 + 0.25*gvH(j + 5.9)) + 0.02;   // where it leaves the trunk; its tip
      if (s > sb + 0.02 || s < tipB - 0.01) continue;
      float aB = aT + (float(c)*2.0 - 1.0)*GV_SLOT*(0.22 + 0.2*gvH(j + 2.2))*gvPeel(s, sb, tipB) + 0.012*sin(s*14.0 + gvH(j)*GV_TAU)/max(s, 0.25);
      v = gvKeep(v, 0.95*gvVessel(th, s, aB, (0.003 + 0.0055*smoothstep(tipB, sb, s))*thick, tipB, sb, px));
      for (int e = 0; e < 2; e++) {
        float k = j*2.0 + float(e);
        float st = mix(tipB, sb, 0.2 + 0.6*gvH(k + 8.8)), tipW = R*(0.86 + 0.2*gvH(k + 4.4));   // where it leaves the branch; its tip
        if (s > st + 0.02 || s < tipW - 0.01) continue;
        float aW = aB + (float(e)*2.0 - 1.0)*GV_SLOT*(0.12 + 0.14*gvH(k + 6.6))*gvPeel(s, st, tipW) + 0.01*sin(s*19.0 + gvH(k + 1.1)*GV_TAU)/max(s, 0.25);
        v = gvKeep(v, 0.85*gvVessel(th, s, aW, (0.0018 + 0.003*smoothstep(tipW, st, s))*thick, tipW, st, px));
      }
    }
  }
  return v;
}
// the heartbeat at time t: a lub and, a third of a beat later, its fainter dub, at 105 a minute (a heart racing under the
// load); 0 between beats, 1 at the lub's peak
float gHeartbeat(float t){
  float ph = fract(t*1.75);
  return exp(-ph*10.0)*smoothstep(0.0, 0.025, ph) + 0.55*exp(-max(ph - 0.3, 0.0)*12.0)*smoothstep(0.28, 0.32, ph);
}
// the opening's radius at strength gl and heartbeat beat: past the corners with no load, a pinhole at the full lens
float gLensRadius(float gl, float beat){ return (1.32 - 1.2*pow(gl, 0.72))*(1.0 - 0.07*beat*gl); }
// the lens's shape at a screen point (uv 0..1 on a screen res pixels wide and high) for strength gl at time t: how far
// past the edge of the opening the point is (negative inside it). The opening is a little wider than tall (the eyes'
// field is), its edge ruffled finely and wandering slowly, tightening with each beat. Also hands back the point round
// the centre (rr out, th round), the heartbeat (0..1) and a pixel's size in those units, for gLensColour.
float gLensShape(vec2 uv, vec2 res, float gl, float t, out float rr, out float th, out float beat, out float px){
  float asp = res.x/res.y, hd = length(vec2(asp, 1.0));
  vec2 pc = (uv - 0.5)*vec2(asp, 1.0)/(0.5*hd);
  rr = length(pc); th = atan(pc.y, pc.x); px = 2.0/(res.y*hd);
  beat = gHeartbeat(t);
  float R = gLensRadius(gl, beat);
  vec2 dir = pc/max(rr, 1e-4);
  float edge = R*((gvN3(vec3(dir*3.0, t*0.12)) - 0.5)*0.06 + (gvN3(vec3(dir*38.0, 1.7)) - 0.5)*0.022);
  return length(pc*vec2(0.9, 1.0)) - edge - R;
}
// the iris's tissue at a point gX past the opening's edge (rr out, th round; the opening's radius R): its colour (rgb)
// and how much of the view behind it it hides (a). Its pattern is laid out from the opening outward in proportion to it
// (rho, the log of the distance over R), so as the iris closes its tissue stretches inward with the opening, as a real
// one's does
vec4 gIris(float gX, float rr, float th, float R, float beat, float gl){
  vec2 dir = vec2(cos(th), sin(th));
  float rho = log(max(rr, 1e-3)/max(R, 1e-3));
  float tw = th + 0.012*sin(rho*7.0 + th*5.0);   // (the fibres wave a little as they run out)
  vec2 dw = vec2(cos(tw), sin(tw));
  float fine = gvN3(vec3(dw*70.0, rho*0.7)), coarse = gvN3(vec3(dw*22.0, rho*0.5 + 3.0));
  float fib = smoothstep(0.3, 0.8, 0.55*fine + 0.45*coarse);   // radial fibres: 1 on a fibre, 0 between
  float rc = 0.26 + 0.05*(gvN3(vec3(dir*5.0, 9.1)) - 0.5) + 0.025*sin(th*26.0);   // the collarette, a zigzag ring a little way out
  float collar = exp(-pow((rho - rc)/0.03, 2.0));
  float ciliary = smoothstep(rc + 0.02, rc + 0.12, rho);
  float crypt = smoothstep(0.62, 0.76, gvN3(vec3(dw*14.0, rho*2.2 + 1.3)))*ciliary*(1.0 - smoothstep(0.7, 1.1, rho));
  float furrow = 0.0;   // contraction furrows: broken rings in the outer iris
  for (int k = 0; k < 3; k++) {
    float fr = rc + 0.32 + 0.17*float(k);
    furrow = max(furrow, exp(-pow((rho - fr)/0.016, 2.0))*smoothstep(0.45, 0.75, gvN3(vec3(dir*4.0, float(k)*7.3))));
  }
  float ruff = 1.0 - smoothstep(0.0, 0.012 + 0.012*R, gX);   // the dark ruffled rim at the opening
  vec3 deep = vec3(0.075, 0.004, 0.006), flesh = vec3(0.36, 0.045, 0.04);
  vec3 col = mix(deep, flesh, fib*(0.7 + 0.3*(1.0 - ciliary)));
  col += vec3(0.24, 0.05, 0.04)*collar*(0.6 + 0.4*fib);
  col = mix(col, deep*0.5, max(crypt*0.85, furrow*0.75));
  col = mix(col, vec3(0.02, 0.0, 0.002), ruff);
  col *= mix(1.0, 0.3, smoothstep(0.35, 1.3, rho));                 // darker towards its outer edge, black at the limbus
  col *= 1.0 + 0.25*beat*gl;                                       // (the blood in it, with each beat)
  col += vec3(0.08, 0.02, 0.02)*exp(-pow((rho - 0.12)/0.06, 2.0))*(0.5 + 0.5*dir.y);   // a wet sheen near the opening
  // what it hides: a veil near the opening (the view faint between its fibres), solid further out and at the rim
  float a = mix(0.62, 0.985, smoothstep(0.0, 0.45, rho))*(0.86 + 0.14*fib);
  a = max(a, ruff*0.95);
  return vec4(col, a);
}
// how much of what lies under the lens at gX the iris hides (the flight HUD fades out under it as the view does)
float gLensCover(float gl, float gX){ return smoothstep(0.0, 0.08, gX)*smoothstep(0.0, 0.12, gl)*0.97; }
// the lens over a display colour c (after tone mapping): the view through the opening reddening, darkening and losing its
// colour as the load builds, a shadow deepening towards the opening's edge; the iris over the rest; the vessels over the
// iris and just inside the opening, round and swelling with each beat; and a dark flush over everything on each beat
vec3 gLensColour(vec3 c, float gl, float gX, float rr, float th, float beat, float px){
  float R = gLensRadius(gl, beat);
  float lum = dot(c, vec3(0.299, 0.587, 0.114));
  c = mix(c, c*vec3(0.88, 0.2, 0.17) + lum*vec3(0.16, 0.012, 0.01), 0.85*smoothstep(0.0, 0.85, gl));
  c *= 1.0 - 0.42*pow(gl, 1.2);
  c *= 1.0 - 0.6*smoothstep(-0.1 - 0.15*R, 0.0, gX)*smoothstep(0.05, 0.5, gl);   // (the shadow the closing iris casts in)
  float edge = smoothstep(-px*1.5, px*1.5, gX);
  if (edge > 0.0) {
    vec4 iris = gIris(gX, rr, th, R, beat, gl);
    c = mix(c, iris.rgb, iris.a*edge*smoothstep(0.0, 0.12, gl));
  }
  if (gl > 0.05 && gX > -0.12) {
    vec2 v = gVeins(th, rr, px, 1.0 + 0.35*beat*gl, R);
    float cover = v.x*smoothstep(-0.12, 0.02, gX)*smoothstep(0.05, 0.3, gl);
    vec3 vessel = mix(vec3(0.035, 0.0, 0.006), vec3(0.24, 0.01, 0.018)*(1.0 + 0.45*beat), pow(v.y, 0.6));
    vessel += vec3(0.22, 0.05, 0.05)*smoothstep(0.7, 1.0, v.y)*0.5;   // (light along its round top)
    c = mix(c, vessel, clamp(cover, 0.0, 1.0)*0.92);
  }
  c = mix(c, c*vec3(0.62, 0.1, 0.08), 0.3*beat*gl);   // the flush with each beat
  c *= 1.0 - 0.5*smoothstep(0.55, 1.25, rr)*smoothstep(0.2, 1.0, gl);
  return c;
}
