'use strict';
// WebGL2 renderer: a full-screen environment pass sampling the GPU field textures directly, then instanced
// pellets, colony bonds, procedurally shaded cells and effect rings. One buffer upload per layer per frame.

const BG_FS = `#version 300 es
precision highp float; precision highp int; precision highp sampler2D;
${P.GLSL_COMMON}
uniform sampler2D uField; uniform sampler2D uEnv;
uniform vec2 uCam; uniform float uZoom; uniform vec2 uRes; uniform float uTime; uniform int uOverlay;
out vec4 o;
float h21(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float pnoise(vec2 p, float per) { // periodic value noise so detail tiles seamlessly across the torus
  vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
  float a = h21(mod(i, per)), b = h21(mod(i + vec2(1, 0), per)), c = h21(mod(i + vec2(0, 1), per)), d = h21(mod(i + vec2(1, 1), per));
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
vec3 ramp(float t) { // perceptual-ish sequential ramp, deep indigo to pale gold
  t = clamp(t, 0.0, 1.0);
  return mix(mix(vec3(0.05, 0.03, 0.18), vec3(0.1, 0.45, 0.55), smoothstep(0.0, 0.45, t)), vec3(0.98, 0.9, 0.55), smoothstep(0.45, 1.0, t));
}
vec3 diverge(float t) { // cold blue, neutral, hot red
  t = clamp(t, -1.0, 1.0);
  return t < 0.0 ? mix(vec3(0.13, 0.13, 0.16), vec3(0.2, 0.5, 1.0), -t) : mix(vec3(0.13, 0.13, 0.16), vec3(1.0, 0.35, 0.15), t);
}
void main() {
  vec2 px = gl_FragCoord.xy - uRes * 0.5;
  vec2 wp = uCam + vec2(px.x, -px.y) / uZoom;
  wp = mod(wp, WORLD);
  vec2 t = wp / FT;
  vec4 f = bilerp(uField, t);
  vec4 e = bilerp(uEnv, t);
  float light = e.r, temp = e.g * 60.0 - 10.0, fert = e.a;
  float rockN = pnoise(wp / 24.0, WORLD / 24.0) * 0.6 + pnoise(wp / 9.0, WORLD / 9.0) * 0.4;
  float wall = e.b + (rockN - 0.5) * 0.35;
  // water: depth and sunlight
  vec3 col = mix(vec3(0.008, 0.02, 0.045), vec3(0.025, 0.11, 0.14), clamp(light, 0.0, 1.2));
  // animated caustics, periodic in world space
  vec2 q = wp * (6.28318530718 / WORLD);
  float c1 = sin(q.x * 37.0 + uTime * 0.7 + sin(q.y * 23.0 + uTime * 0.5) * 2.0);
  float c2 = sin(q.y * 41.0 - uTime * 0.6 + sin(q.x * 29.0 - uTime * 0.4) * 2.0);
  float caus = pow(max(0.0, 1.0 - abs(c1 + c2) * 0.6), 6.0);
  col += vec3(0.14, 0.24, 0.22) * caus * light * light;
  // chemistry
  col += vec3(0.22, 0.30, 0.05) * smoothstep(0.1, 2.6, f.r) * (0.7 + 0.3 * pnoise(wp / 40.0 + uTime * 0.05, WORLD / 40.0));
  col += vec3(0.42, 0.04, 0.36) * smoothstep(0.02, 0.9, f.g);
  col += vec3(0.02, 0.18, 0.2) * smoothstep(0.05, 1.5, f.b);
  col += vec3(0.5, 0.1, 0.6) * smoothstep(0.05, 0.8, f.a) * (0.6 + 0.4 * sin(uTime * 4.0 + wp.x * 0.05));
  // heat shimmer
  col += vec3(0.12, 0.03, 0.0) * smoothstep(30.0, 50.0, temp) + vec3(0.0, 0.04, 0.1) * smoothstep(6.0, -6.0, temp);
  // vents
  if (fert > 1.0) {
    float v = fert - 1.0;
    col += vec3(1.0, 0.45, 0.12) * v * v * (0.55 + 0.25 * sin(uTime * 3.0 + rockN * 9.0));
  }
  if (uOverlay == 1) col = ramp(f.r / 2.5);
  else if (uOverlay == 2) col = ramp(f.g / 1.0);
  else if (uOverlay == 3) col = diverge((temp - 20.0) / 22.0);
  else if (uOverlay == 4) col = ramp(light / 1.2);
  else if (uOverlay == 5) col = ramp(f.b / 1.5);
  else if (uOverlay == 6) col = ramp(f.a / 0.8);
  else if (uOverlay == 7) {
    vec2 v = flowAt(wp); float s = length(v);
    vec2 dir = v / max(s, 1e-4);
    float streak = smoothstep(0.75, 1.0, sin(dot(wp, dir) * 0.08 - uTime * 6.0 * s) * 0.5 + 0.5) * smoothstep(0.6, 1.0, pnoise(vec2(dot(wp, vec2(-dir.y, dir.x)) / 14.0, 0.0), 1e4));
    col = ramp(s / 1.6) * 0.6 + streak * 0.5;
  }
  if (uOverlay > 0) col *= 0.85;
  // rock
  float rk = smoothstep(0.48, 0.52, wall);
  if (rk > 0.0) {
    float sh = pnoise(wp / 6.0, WORLD / 6.0);
    vec3 rc = mix(vec3(0.13, 0.11, 0.1), vec3(0.27, 0.23, 0.19), rockN) * (0.8 + 0.3 * sh);
    float edge = smoothstep(0.52, 0.5, wall) + smoothstep(0.62, 0.5, wall) * 0.5;
    rc += vec3(0.25, 0.32, 0.3) * edge * 0.35;
    col = mix(col, rc, rk);
  }
  // microscope graticule: faint 256-unit grid when zoomed in
  vec2 gr = abs(fract(wp / 256.0 + 0.5) - 0.5) * 256.0;
  float gl = smoothstep(1.2 / uZoom, 0.0, min(gr.x, gr.y)) * smoothstep(0.15, 0.6, uZoom);
  col += vec3(0.05, 0.08, 0.09) * gl;
  // vignette
  vec2 uv = gl_FragCoord.xy / uRes - 0.5;
  col *= 1.0 - dot(uv, uv) * 0.55;
  o = vec4(col, 1.0);
}`;

const CELL_VS = `#version 300 es
precision highp float; precision highp int;
in vec2 aCorner; in vec4 a0; in vec4 a1; in vec4 a2; in vec4 a3;
uniform vec2 uCam; uniform float uZoom; uniform vec2 uRes;
out vec2 vL; out vec4 v0; out vec4 v1; out vec4 v2; out vec4 v3; out float vS;
const float WORLD = ${P.WORLD.toFixed(1)};
void main() {
  float S = 1.35 + a2.w * 0.5 + a3.x * 0.4 + a3.y * 1.6 + a3.w * 1.2;
  vec2 d = a0.xy - uCam; d -= WORLD * floor(d / WORLD + 0.5);
  vec2 wpos = d + aCorner * S * a0.z;
  gl_Position = vec4(wpos.x * uZoom / (uRes.x * 0.5), -wpos.y * uZoom / (uRes.y * 0.5), 0.0, 1.0);
  vL = aCorner * S; v0 = a0; v1 = a1; v2 = a2; v3 = a3; vS = S;
}`;

const CELL_FS = `#version 300 es
precision highp float; precision highp int;
in vec2 vL; in vec4 v0; in vec4 v1; in vec4 v2; in vec4 v3; in float vS;
uniform float uTime; uniform float uZoom;
out vec4 o;
vec3 hsl(float h, float s, float l) {
  vec3 rgb = clamp(abs(mod(h * 6.0 + vec3(0, 4, 2), 6.0) - 3.0) - 1.0, 0.0, 1.0);
  return l + s * (rgb - 0.5) * (1.0 - abs(2.0 * l - 1.0));
}
float h11(float n) { return fract(sin(n) * 43758.5453); }
void main() {
  float r = v0.z, ang = v0.w, hue = v1.x, en = v1.y, el = v1.z, seed = v1.w;
  float photo = v2.x, pred = v2.y, armor = v2.z, spikes = v2.w;
  float cilia = v3.x, glow = v3.y, flags = v3.z, motor = v3.w;
  float pxr = r * uZoom; // cell radius in pixels
  float aa = 1.4 / max(pxr, 0.5);
  bool spore = mod(flags, 2.0) >= 1.0, infected = mod(flags, 4.0) >= 2.0, selected = mod(flags, 8.0) >= 4.0;
  bool dim = mod(flags, 32.0) >= 16.0, inner = mod(flags, 128.0) >= 64.0, hunting = mod(flags, 256.0) >= 128.0;
  float ca = cos(ang), sa = sin(ang);
  vec2 p = vec2(ca * vL.x + sa * vL.y, -sa * vL.x + ca * vL.y); // body space, +x forward
  float t = uTime + seed * 17.0;
  vec3 base = hsl(hue, 0.25 + 0.5 * en, 0.42 + 0.12 * en);
  vec4 acc = vec4(0.0);
  // far away: a soft dot is all you can see
  if (pxr < 2.2) {
    float d = length(vL);
    float a = smoothstep(1.4, 0.4, d) * (dim ? 0.18 : 0.95);
    vec3 c = hsl(hue, 0.6, 0.6) + glow * vec3(0.3, 0.6, 0.7) * (0.6 + 0.4 * sin(t * 3.0));
    o = vec4(c * a, a);
    return;
  }
  vec2 b = p; b.x /= 1.0 + el * 0.8; b.y /= 1.0 - el * 0.3;
  float d = length(b), a = atan(b.y, b.x);
  if (spore) {
    // dormant cyst: thick faceted capsule
    float hex = cos(floor((a + 3.14159) / 1.0472 + 0.5) * 1.0472 - a - 3.14159);
    float R = 0.82 / max(hex, 0.6);
    float body = smoothstep(R + aa, R - aa, d);
    float shell = smoothstep(R - 0.28 - aa, R - 0.22, d) * body;
    vec3 c = mix(base * 0.5, hsl(hue, 0.3, 0.7), shell);
    c += vec3(0.25) * smoothstep(0.5, 0.0, length(b - vec2(-0.25, -0.3))) * 0.5;
    acc = vec4(c, 1.0) * body * 0.95;
  } else {
    float wob = (0.05 * sin(a * 5.0 + t * 2.1) + 0.035 * sin(a * 3.0 - t * 1.3) + 0.02 * sin(a * 9.0 + t * 3.7)) * (1.0 - armor * 0.75);
    float nsp = 6.0 + floor(h11(seed * 3.1) * 7.0);
    float spk = spikes * 0.5 * pow(max(0.0, cos(a * nsp)), 10.0);
    float R = 1.0 + wob + spk;
    float body = smoothstep(R + aa, R - aa, d);
    float memW = 0.1 + armor * 0.16;
    float ring = smoothstep(R - memW - aa, R - memW * 0.4, d) * body;
    // cytoplasm
    vec3 cyto = base * (0.55 + 0.25 * smoothstep(1.0, 0.0, d));
    float alpha = 0.62;
    // chloroplasts drifting with cytoplasmic streaming
    if (photo > 0.05) {
      float rot = t * 0.25;
      vec2 cp = vec2(cos(rot) * b.x - sin(rot) * b.y, sin(rot) * b.x + cos(rot) * b.y) * 3.4 + seed * 9.0;
      vec2 fc = fract(cp) - 0.5, id = floor(cp);
      float on = step(1.0 - photo * 0.9, h11(dot(id, vec2(12.9, 78.2))));
      float dot1 = smoothstep(0.26, 0.16, length(fc + (vec2(h11(id.x + id.y * 7.0), h11(id.y - id.x)) - 0.5) * 0.3)) * on;
      cyto = mix(cyto, vec3(0.2, 0.75, 0.25), dot1 * 0.85 * smoothstep(R - 0.1, R - 0.3, d));
    }
    // storage vacuoles
    float vac = smoothstep(0.2, 0.14, length(b - vec2(-0.45, 0.35) * (0.8 + 0.1 * sin(t)))) + smoothstep(0.14, 0.09, length(b - vec2(0.15, -0.5)));
    cyto = mix(cyto, cyto * 0.6 + vec3(0.25, 0.3, 0.35), vac * 0.6);
    // nucleus
    vec2 np = b - vec2(-0.18 + 0.04 * sin(t * 0.7), 0.06);
    float nuc = smoothstep(0.36, 0.3, length(np));
    float nucleolus = smoothstep(0.12, 0.07, length(np - vec2(0.06, 0.05)));
    cyto = mix(cyto, hsl(hue + 0.05, 0.4, 0.22), nuc * 0.85);
    cyto = mix(cyto, hsl(hue + 0.05, 0.5, 0.55), nucleolus * 0.7);
    // predator maw at the front
    if (pred > 0.1) {
      float open = 0.25 + 0.15 * sin(t * (hunting ? 9.0 : 3.0));
      float maw = smoothstep(open * pred, 0.0, abs(b.y)) * smoothstep(0.35, 0.95, b.x);
      cyto = mix(cyto, vec3(0.25, 0.02, 0.04), maw * 0.9);
      cyto = mix(cyto, vec3(0.6, 0.1, 0.08), pred * 0.25 * smoothstep(0.2, 1.0, b.x));
    }
    if (infected) {
      vec2 ip = b * 6.0 + vec2(t * 0.5, 0.0);
      float sp2 = step(0.82, h11(dot(floor(ip), vec2(3.1, 17.7)))) * smoothstep(0.35, 0.1, length(fract(ip) - 0.5));
      cyto = mix(cyto, vec3(0.75, 0.2, 1.0), sp2 * (0.6 + 0.4 * sin(t * 8.0)));
    }
    vec3 mem = hsl(hue, 0.55, 0.62 + 0.1 * en);
    if (armor > 0.25) { // plated wall
      float plates = smoothstep(0.08, 0.0, abs(fract(a / 6.2831 * 14.0) - 0.5) - 0.42);
      mem = mix(mem, mem * 0.55, plates * armor);
    }
    vec3 c = mix(cyto, mem, ring);
    float aBody = mix(alpha, 0.97, ring) * body;
    if (inner) c *= 0.85;
    acc = vec4(c * aBody, aBody);
    // cilia beating in metachronal waves
    if (cilia > 0.05) {
      float n = floor(14.0 + cilia * 26.0);
      float beat = 0.5 * sin(t * 9.0 - a * 4.0);
      float sd = abs(fract(a / 6.2831 * n + beat * 0.08 * (d - R) * 4.0) - 0.5);
      float len = 0.18 + cilia * 0.22;
      float hair = smoothstep(0.12, 0.04, sd) * step(R, d) * smoothstep(R + len, R + len * 0.6, d);
      float ah = hair * 0.75 * smoothstep(3.5, 7.0, pxr);
      acc += vec4(mem * ah, ah) * (1.0 - acc.a);
    }
    // flagellum trailing behind
    if (motor > 0.15) {
      float tx = -b.x - R * 0.9;
      float L = 0.6 + motor * 1.3;
      if (tx > 0.0 && tx < L) {
        float wave = 0.22 * sin(tx * 7.0 - t * (10.0 + motor * 10.0)) * (tx / L);
        float w = 0.07 * (1.0 - tx / L * 0.7);
        float fl = smoothstep(w + aa, w * 0.3, abs(b.y - wave));
        float af = fl * 0.85;
        acc += vec4(mem * af, af) * (1.0 - acc.a);
      }
    }
    // spines get a bright tip
    if (spikes > 0.1) {
      float tip = smoothstep(0.2, 0.0, abs(d - R + 0.04)) * step(1.0 + spikes * 0.25, d) * body;
      acc.rgb += vec3(0.9, 0.85, 0.7) * tip * 0.4;
    }
  }
  // bioluminescent halo (additive: adds light without occluding)
  if (glow > 0.05) {
    float pulse = 0.65 + 0.35 * sin(t * 2.3);
    float hdist = max(0.0, length(vL) - 0.8);
    float halo = exp(-hdist * 2.4) * glow * pulse * 0.55;
    acc.rgb += hsl(hue + 0.45, 0.9, 0.6) * halo;
  }
  if (selected) {
    float sr = length(vL), R2 = vS * 0.92;
    float sel = smoothstep(0.06, 0.0, abs(sr - R2 + 0.04 * sin(uTime * 5.0))) ;
    acc += vec4(vec3(1.0, 0.95, 0.8) * sel, sel) * 0.9;
  }
  if (dim) acc *= 0.16;
  o = acc;
}`;

const SIMPLE_VS = `#version 300 es
precision highp float; precision highp int;
in vec2 aCorner; in vec4 a0; in vec4 a1;
uniform vec2 uCam; uniform float uZoom; uniform vec2 uRes; uniform int uKind;
out vec2 vL; out vec4 v1; out float vLen;
const float WORLD = ${P.WORLD.toFixed(1)};
void main() {
  vec2 wpos;
  if (uKind == 1) { // segment from a0.xy to a0.zw, width a1.y
    vec2 p1 = a0.xy - uCam; p1 -= WORLD * floor(p1 / WORLD + 0.5);
    vec2 dd = a0.zw - a0.xy; dd -= WORLD * floor(dd / WORLD + 0.5);
    float L = length(dd); vec2 dir = dd / max(L, 1e-3), nrm = vec2(-dir.y, dir.x);
    wpos = p1 + dir * (aCorner.x * 0.5 + 0.5) * L + nrm * aCorner.y * a1.y;
    vLen = L;
  } else { // quad at a0.xy with radius a0.z
    vec2 d = a0.xy - uCam; d -= WORLD * floor(d / WORLD + 0.5);
    wpos = d + aCorner * a0.z;
    vLen = a0.z;
  }
  gl_Position = vec4(wpos.x * uZoom / (uRes.x * 0.5), -wpos.y * uZoom / (uRes.y * 0.5), 0.0, 1.0);
  vL = aCorner; v1 = a1;
}`;
const SIMPLE_FS = `#version 300 es
precision highp float; precision highp int;
in vec2 vL; in vec4 v1; in float vLen;
uniform int uKind; uniform float uTime; uniform float uZoom;
out vec4 o;
vec3 hsl(float h, float s, float l) {
  vec3 rgb = clamp(abs(mod(h * 6.0 + vec3(0, 4, 2), 6.0) - 3.0) - 1.0, 0.0, 1.0);
  return l + s * (rgb - 0.5) * (1.0 - abs(2.0 * l - 1.0));
}
void main() {
  if (uKind == 0) { // detritus
    float d = length(vL);
    float a = smoothstep(1.0, 0.45, d) * 0.85;
    vec3 c = mix(vec3(0.55, 0.42, 0.22), vec3(0.85, 0.7, 0.4), smoothstep(0.7, 0.0, d));
    o = vec4(c * a, a);
  } else if (uKind == 1) { // cytoplasmic bridge
    float a = smoothstep(1.0, 0.3, abs(vL.y)) * 0.8 * (v1.w > 0.5 ? 0.16 : 1.0);
    vec3 c = hsl(v1.x, 0.5, 0.55);
    o = vec4(c * a, a);
  } else { // effect ring: v1 = (hue, progress, kind, alpha)
    float d = length(vL), p = v1.y;
    float w = 0.04 + 0.05 * (1.0 - p);
    float a = smoothstep(w, 0.0, abs(d - (0.3 + 0.7 * p))) * (1.0 - p) * v1.w;
    a += smoothstep(1.0, 0.0, d) * (1.0 - p) * (1.0 - p) * 0.15 * v1.w;
    vec3 c = hsl(v1.x, 0.85, 0.65);
    o = vec4(c * a, a * 0.6);
  }
}`;
const BG_VS = `#version 300 es
in vec2 aPos; void main() { gl_Position = vec4(aPos, 0.0, 1.0); }`;

P.COLOR_MODES = ['Genes', 'Species', 'Diet', 'Energy', 'Age', 'Health'];
P.OVERLAYS = ['Natural', 'Nutrients', 'Toxins', 'Temperature', 'Light', 'Pheromones', 'Mutagen', 'Currents'];
const ROLE_HUE = { photo: 0.28, chemo: 0.48, pred: 0.02, scav: 0.1, para: 0.78, omni: 0.14 };

P.Renderer = class {
  constructor(canvas, sim, env) {
    this.canvas = canvas; this.sim = sim; this.env = env;
    const gl = canvas.getContext('webgl2', { antialias: false, alpha: false, premultipliedAlpha: false, powerPreference: 'high-performance', preserveDrawingBuffer: false });
    if (!gl) throw new Error('WebGL2 is not available in this browser.');
    this.gl = gl;
    this.cam = { x: P.WORLD / 2, y: P.WORLD / 2, zoom: 0.5 };
    this.overlay = 0; this.colorMode = 0; this.highlightSp = -1; this.selected = -1; this.selUid = 0;
    this.bg = P.glu.program(gl, BG_VS, BG_FS);
    this.cellP = P.glu.program(gl, CELL_VS, CELL_FS);
    this.simpleP = P.glu.program(gl, SIMPLE_VS, SIMPLE_FS);
    // full-screen triangle
    this.bgVao = gl.createVertexArray(); gl.bindVertexArray(this.bgVao);
    const tb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, tb);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
    let loc = gl.getAttribLocation(this.bg.p, 'aPos'); gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
    // instanced layers
    this.quad = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, this.quad);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);
    this.cellBuf = new Float32Array(P.MAXC * 16);
    this.cellLayer = this.layer(this.cellP, 4, P.MAXC);
    this.pelBuf = new Float32Array(P.MAXP * 8);
    this.pelLayer = this.layer(this.simpleP, 2, P.MAXP);
    this.bondBuf = new Float32Array(P.MAXC * 2 * 8);
    this.bondLayer = this.layer(this.simpleP, 2, P.MAXC * 2);
    this.fxBuf = new Float32Array(256 * 8);
    this.fxLayer = this.layer(this.simpleP, 2, 256);
    gl.bindVertexArray(null);
    this.drawn = 0;
  }
  layer(prog, nAttr, cap) {
    const gl = this.gl, vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.quad);
    const lc = gl.getAttribLocation(prog.p, 'aCorner'); gl.enableVertexAttribArray(lc); gl.vertexAttribPointer(lc, 2, gl.FLOAT, false, 0, 0);
    const buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buf);
    gl.bufferData(gl.ARRAY_BUFFER, cap * nAttr * 16, gl.DYNAMIC_DRAW);
    for (let k = 0; k < nAttr; k++) {
      const l = gl.getAttribLocation(prog.p, 'a' + k);
      gl.enableVertexAttribArray(l); gl.vertexAttribPointer(l, 4, gl.FLOAT, false, nAttr * 16, k * 16); gl.vertexAttribDivisor(l, 1);
    }
    return { vao, buf, nAttr };
  }
  resize() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.max(1, Math.round(this.canvas.clientWidth * dpr)), h = Math.max(1, Math.round(this.canvas.clientHeight * dpr));
    if (this.canvas.width !== w || this.canvas.height !== h) { this.canvas.width = w; this.canvas.height = h; }
    this.dpr = dpr;
  }
  // screen (CSS px) <-> world
  toWorld(sx, sy) {
    const c = this.cam, z = c.zoom / this.dpr;
    return [P.wrapP(c.x + (sx - this.canvas.clientWidth / 2) / z), P.wrapP(c.y + (sy - this.canvas.clientHeight / 2) / z)];
  }
  toScreen(wx, wy) {
    const c = this.cam, z = c.zoom / this.dpr;
    return [P.wrapD(wx - c.x) * z + this.canvas.clientWidth / 2, P.wrapD(wy - c.y) * z + this.canvas.clientHeight / 2];
  }
  minZoom() { return (Math.max(this.canvas.width, this.canvas.height) / P.WORLD) * 1.0; }
  clampZoom() { this.cam.zoom = P.clamp(this.cam.zoom, this.minZoom(), 12 * this.dpr); }

  cellHue(i) {
    const s = this.sim, m = this.colorMode;
    if (m === 0) return s.genome[i * P.G + P.GI.HUE];
    if (m === 1) return P.species[s.sp[i]].hue;
    if (m === 2) return ROLE_HUE[P.species[s.sp[i]].role];
    if (m === 3) return 0.33 * P.clamp(s.energy[i] / s.maxE[i], 0, 1);
    if (m === 4) return 0.66 - 0.66 * P.clamp(s.age[i] / s.maxAge[i], 0, 1);
    return 0.33 * P.clamp(s.hp[i] / s.maxHP[i], 0, 1);
  }

  draw(time) {
    const gl = this.gl, s = this.sim, c = this.cam, W = this.canvas.width, H = this.canvas.height, G = P.G, g = s.genome;
    gl.viewport(0, 0, W, H);
    gl.disable(gl.BLEND);
    // ---- environment ----
    const bu = this.bg.u, f = this.field, env = this.env;
    gl.useProgram(this.bg.p); gl.bindVertexArray(this.bgVao);
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, f.tex);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, f.envTex);
    gl.uniform1i(bu.uField, 0); gl.uniform1i(bu.uEnv, 1);
    gl.uniform2f(bu.uCam, c.x, c.y); gl.uniform1f(bu.uZoom, c.zoom); gl.uniform2f(bu.uRes, W, H);
    gl.uniform1f(bu.uTime, time); gl.uniform1i(bu.uOverlay, this.overlay);
    gl.uniform4fv(bu.uModes, env.modeU); gl.uniform1fv(bu.uPh2, env.modeP2); gl.uniform4fv(bu.uVort, env.vortU); gl.uniform1i(bu.uNVort, env.nVort);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.activeTexture(gl.TEXTURE0);

    gl.enable(gl.BLEND); gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
    const hw = W / 2 / c.zoom + 40, hh = H / 2 / c.zoom + 40;
    const hl = this.highlightSp, colorMode = this.colorMode;
    // ---- detritus ----
    let n = 0;
    const pb = this.pelBuf;
    if (c.zoom > 0.25) for (let k = 0; k < s.phi; k++) {
      if (!s.palive[k]) continue;
      const dx = P.wrapD(s.px[k] - c.x), dy = P.wrapD(s.py[k] - c.y);
      if (dx < -hw || dx > hw || dy < -hh || dy > hh) continue;
      const o = n * 8;
      pb[o] = s.px[k]; pb[o + 1] = s.py[k]; pb[o + 2] = 1.2 + Math.sqrt(s.pe[k]) * 0.35; pb[o + 3] = 0;
      n++;
    }
    const su = this.simpleP.u;
    gl.useProgram(this.simpleP.p);
    gl.uniform2f(su.uCam, c.x, c.y); gl.uniform1f(su.uZoom, c.zoom); gl.uniform2f(su.uRes, W, H); gl.uniform1f(su.uTime, time);
    if (n) this.drawLayer(this.pelLayer, pb, n, 0, su);
    // ---- colony bonds ----
    n = 0;
    const bb = this.bondBuf;
    for (let i = 0; i < s.hi; i++) {
      if (!s.alive[i] || !s.nb[i]) continue;
      const dx = P.wrapD(s.x[i] - c.x), dy = P.wrapD(s.y[i] - c.y);
      if (dx < -hw || dx > hw || dy < -hh || dy > hh) continue;
      for (let q = 0; q < 4; q++) {
        const j = s.bonds[i * 4 + q];
        if (j <= i) continue;
        const o = n * 8;
        bb[o] = s.x[i]; bb[o + 1] = s.y[i]; bb[o + 2] = s.x[j]; bb[o + 3] = s.y[j];
        bb[o + 4] = this.cellHue(i); bb[o + 5] = Math.min(s.r[i], s.r[j]) * 0.45; bb[o + 6] = 0; bb[o + 7] = hl >= 0 && s.sp[i] !== hl ? 1 : 0;
        n++;
      }
    }
    if (n) this.drawLayer(this.bondLayer, bb, n, 1, su);
    // ---- cells ----
    n = 0;
    const cb = this.cellBuf, sel = this.selected >= 0 && s.alive[this.selected] && s.uid[this.selected] === this.selUid ? this.selected : -1;
    for (let i = 0; i < s.hi; i++) {
      if (!s.alive[i]) continue;
      const dx = P.wrapD(s.x[i] - c.x), dy = P.wrapD(s.y[i] - c.y);
      if (dx < -hw || dx > hw || dy < -hh || dy > hh) continue;
      const o = n * 16, go = i * G;
      cb[o] = s.x[i]; cb[o + 1] = s.y[i]; cb[o + 2] = s.r[i]; cb[o + 3] = s.ang[i];
      cb[o + 4] = colorMode === 0 ? g[go + P.GI.HUE] : this.cellHue(i);
      cb[o + 5] = P.clamp(s.energy[i] / s.maxE[i], 0, 1); cb[o + 6] = g[go + P.GI.SHAPE]; cb[o + 7] = (i * 0.618) % 1;
      cb[o + 8] = s.ePhoto[i]; cb[o + 9] = s.ePred[i]; cb[o + 10] = g[go + P.GI.ARMOR]; cb[o + 11] = g[go + P.GI.SPIKES];
      let flags = s.state[i] ? 1 : 0;
      if (s.infect[i]) flags += 2;
      if (i === sel) flags += 4;
      if (hl >= 0 && s.sp[i] !== hl) flags += 16;
      if (s.nb[i] >= 3) flags += 64;
      if (s.mood[i] === 1) flags += 128;
      cb[o + 12] = g[go + P.GI.CILIA]; cb[o + 13] = g[go + P.GI.GLOW]; cb[o + 14] = flags; cb[o + 15] = s.state[i] ? 0 : g[go + P.GI.MOTOR];
      n++;
    }
    this.drawn = n;
    if (n) {
      const cu = this.cellP.u;
      gl.useProgram(this.cellP.p);
      gl.uniform2f(cu.uCam, c.x, c.y); gl.uniform1f(cu.uZoom, c.zoom); gl.uniform2f(cu.uRes, W, H); gl.uniform1f(cu.uTime, time);
      gl.bindVertexArray(this.cellLayer.vao);
      gl.bindBuffer(gl.ARRAY_BUFFER, this.cellLayer.buf);
      gl.bufferSubData(gl.ARRAY_BUFFER, 0, cb, 0, n * 16);
      gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, n);
    }
    // ---- effects ----
    n = 0;
    const fb = this.fxBuf;
    for (const e of s.fx) {
      const o = n * 8;
      fb[o] = e.x; fb[o + 1] = e.y; fb[o + 2] = e.r; fb[o + 3] = 0;
      fb[o + 4] = e.color; fb[o + 5] = e.t / e.life; fb[o + 6] = 0; fb[o + 7] = 1;
      n++;
    }
    if (n) {
      gl.useProgram(this.simpleP.p);
      this.drawLayer(this.fxLayer, fb, n, 2, su);
    }
    gl.bindVertexArray(null);
  }
  drawLayer(layer, data, n, kind, u) {
    const gl = this.gl;
    gl.uniform1i(u.uKind, kind);
    gl.bindVertexArray(layer.vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, layer.buf);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, data, 0, n * layer.nAttr * 4);
    gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, n);
  }
};

// A small standalone renderer for the Lab preview, sharing the cell shader.
P.CellPreview = class {
  constructor(canvas) {
    this.canvas = canvas;
    const gl = canvas.getContext('webgl2', { premultipliedAlpha: true, alpha: true, antialias: true });
    if (!gl) return;
    this.gl = gl;
    this.prog = P.glu.program(gl, CELL_VS, CELL_FS);
    this.vao = gl.createVertexArray(); gl.bindVertexArray(this.vao);
    const q = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, q);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);
    const lc = gl.getAttribLocation(this.prog.p, 'aCorner'); gl.enableVertexAttribArray(lc); gl.vertexAttribPointer(lc, 2, gl.FLOAT, false, 0, 0);
    this.buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, this.buf);
    gl.bufferData(gl.ARRAY_BUFFER, 8 * 64, gl.DYNAMIC_DRAW);
    for (let k = 0; k < 4; k++) {
      const l = gl.getAttribLocation(this.prog.p, 'a' + k);
      gl.enableVertexAttribArray(l); gl.vertexAttribPointer(l, 4, gl.FLOAT, false, 64, k * 16); gl.vertexAttribDivisor(l, 1);
    }
    this.data = new Float32Array(8 * 16);
  }
  draw(genome, time) {
    const gl = this.gl;
    if (!gl) return;
    const cv = this.canvas, dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.round(cv.clientWidth * dpr), h = Math.round(cv.clientHeight * dpr);
    if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; }
    const GI = P.GI, r = 3 + genome[GI.SIZE] * 11, sh = P.dietShares(genome);
    const colony = genome[GI.ADHESION] > 0.45;
    const pos = colony ? [[0, 0], [1, 0], [-0.5, 0.87], [-0.5, -0.87], [1.5, 0.87], [0.5, -0.87]] : [[0, 0]];
    const d = r * 1.85, zoom = Math.min(w, h) / ((colony ? 5.2 : 3.6) * r * 1.6 + (genome[GI.GLOW] + genome[GI.MOTOR]) * r * 1.5);
    const D = this.data;
    pos.forEach((p, k) => {
      const o = k * 16;
      D[o] = 2048 + p[0] * d; D[o + 1] = 2048 + p[1] * d; D[o + 2] = r; D[o + 3] = colony ? k * 1.1 : Math.sin(time * 0.4) * 0.4;
      D[o + 4] = genome[GI.HUE]; D[o + 5] = 0.8; D[o + 6] = genome[GI.SHAPE]; D[o + 7] = k * 0.37;
      D[o + 8] = Math.pow(sh[0], 1.25); D[o + 9] = Math.pow(sh[2], 1.25); D[o + 10] = genome[GI.ARMOR]; D[o + 11] = genome[GI.SPIKES];
      D[o + 12] = genome[GI.CILIA]; D[o + 13] = genome[GI.GLOW]; D[o + 14] = colony && k === 0 ? 64 : 0; D[o + 15] = genome[GI.MOTOR];
    });
    gl.viewport(0, 0, w, h);
    gl.clearColor(0, 0, 0, 0); gl.clear(gl.COLOR_BUFFER_BIT);
    gl.enable(gl.BLEND); gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
    gl.useProgram(this.prog.p);
    const u = this.prog.u, cx = colony ? 2048 + d * 0.3 : 2048;
    gl.uniform2f(u.uCam, cx, 2048); gl.uniform1f(u.uZoom, zoom); gl.uniform2f(u.uRes, w, h); gl.uniform1f(u.uTime, time);
    gl.bindVertexArray(this.vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.buf);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, D, 0, pos.length * 16);
    gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, pos.length);
  }
};
