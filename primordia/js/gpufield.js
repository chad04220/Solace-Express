'use strict';
// Environment chemistry on the GPU: advection by currents, diffusion, decay, upwelling and walls,
// ping-ponged between two RGBA32F render targets, with an asynchronous PBO readback for the CPU sim.
// FieldCPU runs the same maths in JavaScript when float render targets are unavailable.

P.glu = {
  shader(gl, type, src) {
    const s = gl.createShader(type);
    gl.shaderSource(s, src); gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s) + '\n' + src.split('\n').map((l, i) => i + 1 + ': ' + l).join('\n'));
    return s;
  },
  program(gl, vs, fs) {
    const p = gl.createProgram();
    gl.attachShader(p, P.glu.shader(gl, gl.VERTEX_SHADER, vs));
    gl.attachShader(p, P.glu.shader(gl, gl.FRAGMENT_SHADER, fs));
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
    const u = {}, n = gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS);
    for (let i = 0; i < n; i++) { const info = gl.getActiveUniform(p, i); const name = info.name.replace(/\[0\]$/, ''); u[name] = gl.getUniformLocation(p, info.name); }
    return { p, u };
  },
  floatTex(gl, w, h, data) {
    const t = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, t);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, w, h, 0, gl.RGBA, gl.FLOAT, data || null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
    return t;
  },
};

// Shared GLSL: world constants, wrapped texel fetch with manual bilinear filtering, and the current field.
P.GLSL_COMMON = `
const float WORLD = ${P.WORLD.toFixed(1)};
const int FW = ${P.FW};
const float FT = ${P.FT.toFixed(1)};
uniform vec4 uModes[4];
uniform float uPh2[4];
uniform vec4 uVort[8];
uniform int uNVort;
vec4 fetchW(highp sampler2D s, ivec2 p) { return texelFetch(s, p & (FW - 1), 0); }
vec4 bilerp(highp sampler2D s, vec2 t) { // t in texel units
  t -= 0.5; vec2 fl = floor(t); ivec2 i = ivec2(fl); vec2 f = t - fl;
  return mix(mix(fetchW(s, i), fetchW(s, i + ivec2(1, 0)), f.x), mix(fetchW(s, i + ivec2(0, 1)), fetchW(s, i + ivec2(1, 1)), f.x), f.y);
}
vec2 wrapD(vec2 d) { return d - WORLD * floor(d / WORLD + 0.5); }
vec2 flowAt(vec2 p) {
  const float K = 6.28318530718 / WORLD;
  vec2 v = vec2(0.0);
  for (int k = 0; k < 4; k++) {
    float a = K * uModes[k].x * p.x + uModes[k].w, b = K * uModes[k].y * p.y + uPh2[k];
    v.x -= uModes[k].z * sin(a) * sin(b) * uModes[k].y;
    v.y -= uModes[k].z * cos(a) * cos(b) * uModes[k].x;
  }
  for (int k = 0; k < 8; k++) {
    if (k >= uNVort) break;
    vec2 d = wrapD(p - uVort[k].xy);
    float R = uVort[k].w, f = uVort[k].z * exp(-dot(d, d) / (R * R));
    v += vec2(-d.y, d.x) / R * f;
  }
  return v;
}
`;

const FIELD_VS = `#version 300 es
in vec2 aPos; void main() { gl_Position = vec4(aPos, 0.0, 1.0); }`;
const FIELD_FS = `#version 300 es
precision highp float; precision highp int; precision highp sampler2D;
${P.GLSL_COMMON}
uniform sampler2D uField; uniform sampler2D uDelta; uniform sampler2D uEnv;
uniform float uAddDelta; uniform float uInflow; uniform vec4 uDiff; uniform vec4 uDecay;
out vec4 o;
void main() {
  ivec2 ip = ivec2(gl_FragCoord.xy);
  vec2 v = flowAt((vec2(ip) + 0.5) * FT);
  vec4 c = bilerp(uField, vec2(ip) + 0.5 - v / FT);
  vec4 lap = fetchW(uField, ip + ivec2(1, 0)) + fetchW(uField, ip - ivec2(1, 0)) + fetchW(uField, ip + ivec2(0, 1)) + fetchW(uField, ip - ivec2(0, 1)) - 4.0 * fetchW(uField, ip);
  c += uDiff * lap;
  vec4 env = texelFetch(uEnv, ip, 0);
  c.r += uInflow * 0.0018 * pow(min(env.a, 1.0), 1.5);
  c *= uDecay;
  c += uAddDelta * texelFetch(uDelta, ip, 0);
  c *= 1.0 - smoothstep(0.4, 0.8, env.b);
  o = max(c, vec4(0.0));
}`;

P.FIELD_DIFF = [0.06, 0.1, 0.16, 0.03];
P.FIELD_DECAY = [0.9996, 0.99, 0.975, 0.998];

P.FieldGPU = class {
  constructor(gl, env) {
    this.gl = gl; this.env = env; this.mode = 'GPU';
    if (!gl.getExtension('EXT_color_buffer_float')) throw new Error('no float render targets');
    const N = P.FW;
    this.texA = P.glu.floatTex(gl, N, N, env.field);
    this.texB = P.glu.floatTex(gl, N, N, null);
    this.deltaTex = P.glu.floatTex(gl, N, N, null);
    this.envTex = P.glu.floatTex(gl, N, N, env.envTex);
    this.fbA = this.fb(this.texA); this.fbB = this.fb(this.texB);
    if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) throw new Error('float framebuffer incomplete');
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    this.prog = P.glu.program(gl, FIELD_VS, FIELD_FS);
    this.vao = gl.createVertexArray(); gl.bindVertexArray(this.vao);
    const b = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, b);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
    const loc = gl.getAttribLocation(this.prog.p, 'aPos');
    gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);
    this.pbo = gl.createBuffer(); gl.bindBuffer(gl.PIXEL_PACK_BUFFER, this.pbo);
    gl.bufferData(gl.PIXEL_PACK_BUFFER, N * N * 16, gl.STREAM_READ); gl.bindBuffer(gl.PIXEL_PACK_BUFFER, null);
    this.fence = null; this.readbacks = 0; this.lastReadMs = 0;
    this.tex = this.texA; this.fbCur = this.fbA;
  }
  fb(t) {
    const gl = this.gl, f = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, f);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, t, 0);
    return f;
  }
  uploadEnv() {
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.envTex);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, P.FW, P.FW, gl.RGBA, gl.FLOAT, this.env.envTex);
    this.env.envDirty = false;
  }
  // Pull the latest finished readback into env.field without stalling the pipeline.
  poll() {
    if (!this.fence) return;
    const gl = this.gl;
    if (gl.getSyncParameter(this.fence, gl.SYNC_STATUS) !== gl.SIGNALED) return;
    gl.deleteSync(this.fence); this.fence = null;
    gl.bindBuffer(gl.PIXEL_PACK_BUFFER, this.pbo);
    gl.getBufferSubData(gl.PIXEL_PACK_BUFFER, 0, this.env.field);
    gl.bindBuffer(gl.PIXEL_PACK_BUFFER, null);
    this.readbacks++;
  }
  // Run n field ticks. The first applies all deltas accumulated by the sim since the previous call.
  step(n) {
    const gl = this.gl, env = this.env;
    if (env.envDirty) this.uploadEnv();
    gl.bindTexture(gl.TEXTURE_2D, this.deltaTex);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, P.FW, P.FW, gl.RGBA, gl.FLOAT, env.delta);
    env.delta.fill(0);
    if (n <= 0) n = 1; // still apply the deltas while paused-but-painting
    const pr = this.prog, u = pr.u;
    gl.useProgram(pr.p); gl.bindVertexArray(this.vao);
    gl.viewport(0, 0, P.FW, P.FW); gl.disable(gl.BLEND);
    gl.uniform4fv(u.uModes, env.modeU); gl.uniform1fv(u.uPh2, env.modeP2); gl.uniform4fv(u.uVort, env.vortU); gl.uniform1i(u.uNVort, env.nVort);
    gl.uniform1f(u.uInflow, this.paused ? 0 : env.inflow);
    gl.uniform4fv(u.uDiff, this.paused ? [0, 0, 0, 0] : P.FIELD_DIFF);
    gl.uniform4fv(u.uDecay, this.paused ? [1, 1, 1, 1] : P.FIELD_DECAY);
    gl.uniform1i(u.uField, 0); gl.uniform1i(u.uDelta, 1); gl.uniform1i(u.uEnv, 2);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, this.deltaTex);
    gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, this.envTex);
    for (let k = 0; k < n; k++) {
      const src = this.tex, dstFb = this.tex === this.texA ? this.fbB : this.fbA;
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, src);
      gl.uniform1f(u.uAddDelta, k === 0 ? 1 : 0);
      if (this.paused) { // currents frozen too: zero the mode amplitudes
        gl.uniform4fv(u.uModes, new Float32Array(16)); gl.uniform1i(u.uNVort, 0);
      }
      gl.bindFramebuffer(gl.FRAMEBUFFER, dstFb);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      this.tex = src === this.texA ? this.texB : this.texA;
      this.fbCur = dstFb;
    }
    if (!this.fence) {
      gl.bindBuffer(gl.PIXEL_PACK_BUFFER, this.pbo);
      gl.readPixels(0, 0, P.FW, P.FW, gl.RGBA, gl.FLOAT, 0);
      gl.bindBuffer(gl.PIXEL_PACK_BUFFER, null);
      this.fence = gl.fenceSync(gl.SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.bindVertexArray(null);
    gl.activeTexture(gl.TEXTURE0);
  }
};

P.FieldCPU = class {
  constructor(gl, env) {
    this.gl = gl; this.env = env; this.mode = 'CPU';
    this.tmp = new Float32Array(env.field.length);
    this.tex = P.glu.floatTex(gl, P.FW, P.FW, env.field);
    this.envTex = P.glu.floatTex(gl, P.FW, P.FW, env.envTex);
    this.readbacks = 0;
  }
  poll() {}
  uploadEnv() {
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.envTex);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, P.FW, P.FW, gl.RGBA, gl.FLOAT, this.env.envTex);
    this.env.envDirty = false;
  }
  step(n) {
    const env = this.env, W = P.FW, F = env.field, T = this.tmp, et = env.envTex;
    env.delta.fill(0); // deltas were already applied to env.field as they happened
    if (!this.paused) for (let k = 0; k < Math.min(n, 2); k++) { // capped: the CPU path trades accuracy for frame rate
      for (let y = 0; y < W; y++) for (let x = 0; x < W; x++) {
        const i = y * W + x;
        const sx = x - env.flowU[i] / P.FT, sy = y - env.flowV[i] / P.FT;
        const fx = Math.floor(sx), fy = Math.floor(sy), tx = sx - fx, ty = sy - fy;
        const x0 = ((fx % W) + W) % W, y0 = ((fy % W) + W) % W, x1 = (x0 + 1) % W, y1 = (y0 + 1) % W;
        const a = (y0 * W + x0) * 4, b = (y0 * W + x1) * 4, c = (y1 * W + x0) * 4, d = (y1 * W + x1) * 4;
        const l = (y * W + ((x + W - 1) % W)) * 4, r = (y * W + ((x + 1) % W)) * 4, u = (((y + W - 1) % W) * W + x) * 4, dn = (((y + 1) % W) * W + x) * 4;
        const wallK = 1 - P.smooth(P.clamp((et[i * 4 + 2] - 0.4) / 0.4, 0, 1));
        for (let ch = 0; ch < 4; ch++) {
          let v = (F[a + ch] * (1 - tx) + F[b + ch] * tx) * (1 - ty) + (F[c + ch] * (1 - tx) + F[d + ch] * tx) * ty;
          v += P.FIELD_DIFF[ch] * (F[l + ch] + F[r + ch] + F[u + ch] + F[dn + ch] - 4 * F[i * 4 + ch]);
          if (ch === 0) v += env.inflow * 0.0018 * Math.pow(Math.min(et[i * 4 + 3], 1), 1.5);
          v *= P.FIELD_DECAY[ch] * wallK;
          T[i * 4 + ch] = v > 0 ? v : 0;
        }
      }
      F.set(T);
    }
    const gl = this.gl;
    if (env.envDirty) this.uploadEnv();
    gl.bindTexture(gl.TEXTURE_2D, this.tex);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, W, W, gl.RGBA, gl.FLOAT, F);
  }
};
