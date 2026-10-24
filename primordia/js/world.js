'use strict';
// The environment: static terrain maps, light, temperature, currents, vents, seasons, climate and events.

P.SEASONS = ['Spring', 'Summer', 'Autumn', 'Winter'];

P.Env = class {
  constructor(seed) {
    const N = P.FW * P.FW;
    this.field = new Float32Array(N * 4); // [nutrient, toxin, pheromone, mutagen] CPU copy (GPU is authoritative)
    this.delta = new Float32Array(N * 4); // pending additions since the last GPU upload
    this.wall = new Float32Array(N);      // rock 0..1
    this.shade = new Float32Array(N);     // player light modifier -1..+1
    this.heat = new Float32Array(N);      // player temperature modifier (°C), relaxes to 0
    this.depth = new Float32Array(N);     // 0 deep .. 1 shallow
    this.fert = new Float32Array(N);      // upwelling fertility 0..1
    this.tempBase = new Float32Array(N);
    this.cloud = new Float32Array(N);
    this.light = new Float32Array(N);
    this.temp = new Float32Array(N);
    this.envTex = new Float32Array(N * 4); // [light, temp(normalised), wall, fertility+vents] for the renderer
    this.envDirty = true;
    this.tick = 0;
    // player-tunable climate
    this.sunlight = 1; this.inflow = 1; this.currents = 1; this.tempOffset = 0; this.mutationMult = 1;
    this.dayLen = 1800; this.yearDays = 12;
    this.seasonsOn = true; this.dayNightOn = true; this.eventsOn = true;
    this.climate = 0; this.climateTarget = 0; this.transients = [];
    this.vents = []; this.vortices = [];
    this.cloudOff = [0, 0];
    this.nextEvent = 2400 + P.randi(2400);
    this.history = []; // climate samples for charts

    const depthN = P.makeFbm(4, 5, seed + 11), fertN = P.makeFbm(3, 4, seed + 23), tN = P.makeFbm(4, 3, seed + 37);
    const cloudN = P.makeFbm(4, 4, seed + 51), rockN = P.makeFbm(5, 4, seed + 71);
    for (let y = 0; y < P.FW; y++) for (let x = 0; x < P.FW; x++) {
      const i = y * P.FW + x, u = x / P.FW, v = y / P.FW;
      const d = P.clamp((depthN(u, v) - 0.3) * 2.2, 0, 1);
      this.depth[i] = d;
      this.fert[i] = P.clamp((fertN(u, v) - 0.35) * 2.4, 0, 1) * (0.4 + 0.6 * (1 - d));
      this.tempBase[i] = 21 + 10 * Math.cos(P.TAU * v) + (tN(u, v) - 0.5) * 14 - (1 - d) * 4;
      this.cloud[i] = cloudN(u, v);
      const r = rockN(u, v);
      this.wall[i] = r > 0.7 ? P.clamp((r - 0.7) * 12, 0, 1) : 0;
      this.field[i * 4] = this.fert[i] * 1.2 + 0.15;
    }
    // ocean currents: a few periodic stream-function modes that drift over time
    this.modes = [];
    for (let k = 0; k < 4; k++) this.modes.push({ m: 1 + P.randi(3), n: 1 + P.randi(3), A: 0.25 + P.rand() * 0.35, p1: P.rand() * P.TAU, p2: P.rand() * P.TAU, w1: (P.rand() - 0.5) * 4e-4, w2: (P.rand() - 0.5) * 4e-4 });
    this.modeU = new Float32Array(16); this.modeP2 = new Float32Array(4); this.vortU = new Float32Array(32);
    for (let k = 0; k < 3; k++) this.addVent(P.rand() * P.WORLD, P.rand() * P.WORLD, 0.6 + P.rand() * 0.6, -1);
    this.updateUniforms();
    this.recomputeLight(); this.recomputeTemp();
  }

  idx(x, y) { // texel index of a world position (wrapped)
    let fx = (x / P.FT) | 0, fy = (y / P.FT) | 0;
    if (fx < 0) fx += P.FW; else if (fx >= P.FW) fx -= P.FW;
    if (fy < 0) fy += P.FW; else if (fy >= P.FW) fy -= P.FW;
    return fy * P.FW + fx;
  }
  add(x, y, ch, amt) {
    const i = this.idx(x, y) * 4 + ch;
    this.delta[i] += amt;
    const v = this.field[i] + amt;
    this.field[i] = v < 0 ? 0 : v;
  }
  addAt(i, ch, amt) {
    this.delta[i * 4 + ch] += amt;
    const v = this.field[i * 4 + ch] + amt;
    this.field[i * 4 + ch] = v < 0 ? 0 : v;
  }
  // bilinear wall value (smooth rock edges for collisions)
  wallAt(x, y) {
    const fx = x / P.FT - 0.5, fy = y / P.FT - 0.5;
    let x0 = Math.floor(fx), y0 = Math.floor(fy);
    const tx = fx - x0, ty = fy - y0, W = P.FW;
    x0 = (x0 + W) % W; y0 = (y0 + W) % W;
    const x1 = (x0 + 1) % W, y1 = (y0 + 1) % W, w = this.wall;
    return (w[y0 * W + x0] * (1 - tx) + w[y0 * W + x1] * tx) * (1 - ty) + (w[y1 * W + x0] * (1 - tx) + w[y1 * W + x1] * tx) * ty;
  }

  // ----- currents: identical maths runs in the GPU field shader -----
  updateUniforms() {
    const s = this.currents;
    for (let k = 0; k < 4; k++) {
      const m = this.modes[k];
      this.modeU[k * 4] = m.m; this.modeU[k * 4 + 1] = m.n; this.modeU[k * 4 + 2] = (m.A * s * 1.6) / Math.hypot(m.m, m.n); this.modeU[k * 4 + 3] = m.p1;
      this.modeP2[k] = m.p2;
    }
    this.vortU.fill(0);
    for (let k = 0; k < Math.min(8, this.vortices.length); k++) {
      const v = this.vortices[k], f = Math.min(1, v.life / 300);
      this.vortU[k * 4] = v.x; this.vortU[k * 4 + 1] = v.y; this.vortU[k * 4 + 2] = v.s * f; this.vortU[k * 4 + 3] = v.R;
    }
    this.nVort = Math.min(8, this.vortices.length);
    // cache the flow at every texel centre so cells and pellets never evaluate trig per tick
    if (!this.flowU) { this.flowU = new Float32Array(P.FW * P.FW); this.flowV = new Float32Array(P.FW * P.FW); this.uver = 0; }
    for (let y = 0; y < P.FW; y++) for (let x = 0; x < P.FW; x++) {
      this.flow((x + 0.5) * P.FT, (y + 0.5) * P.FT);
      this.flowU[y * P.FW + x] = this.fu; this.flowV[y * P.FW + x] = this.fv;
    }
    this.uver++;
  }
  flow(x, y) { // sets this.fu, this.fv (world units per tick)
    const K = P.TAU / P.WORLD, mu = this.modeU;
    let u = 0, v = 0;
    for (let k = 0; k < 4; k++) {
      const m = mu[k * 4], n = mu[k * 4 + 1], A = mu[k * 4 + 2];
      const a = K * m * x + mu[k * 4 + 3], b = K * n * y + this.modeP2[k];
      u -= A * Math.sin(a) * Math.sin(b) * n;
      v -= A * Math.cos(a) * Math.cos(b) * m;
    }
    for (let k = 0; k < this.nVort; k++) {
      const vx = this.vortU[k * 4], vy = this.vortU[k * 4 + 1], s = this.vortU[k * 4 + 2], R = this.vortU[k * 4 + 3];
      const dx = P.wrapD(x - vx), dy = P.wrapD(y - vy);
      const f = s * Math.exp(-(dx * dx + dy * dy) / (R * R));
      u -= (dy / R) * f; v += (dx / R) * f;
    }
    this.fu = u; this.fv = v;
  }

  addVent(x, y, str, life) { this.vents.push({ x, y, str, life }); this.envDirty = true; }
  addVortex(x, y, s, R) {
    // reinforce a nearby vortex instead of stacking many
    for (const v of this.vortices) if (Math.hypot(P.wrapD(v.x - x), P.wrapD(v.y - y)) < R * 0.5) { v.s = P.clamp(v.s + s * 0.2, -3, 3); v.life = Math.max(v.life, 1200); return; }
    if (this.vortices.length >= 8) this.vortices.shift();
    this.vortices.push({ x, y, s, R, life: 1500 });
  }

  // ----- time -----
  get dayPhase() { return (this.tick % this.dayLen) / this.dayLen; }
  get yearPhase() { return (this.tick % (this.dayLen * this.yearDays)) / (this.dayLen * this.yearDays); }
  get day() { return Math.floor(this.tick / this.dayLen) % this.yearDays + 1; }
  get year() { return Math.floor(this.tick / (this.dayLen * this.yearDays)) + 1; }
  get seasonName() { return P.SEASONS[Math.floor(this.yearPhase * 4) % 4]; }
  sun() {
    if (!this.dayNightOn) return 0.72 * this.seasonLight();
    const s = Math.sin(this.dayPhase * P.TAU);
    return (0.15 + 0.85 * Math.pow(Math.max(0, s), 0.6)) * this.seasonLight();
  }
  seasonLight() { return this.seasonsOn ? 1 + 0.22 * Math.cos((this.yearPhase - 0.375) * P.TAU) : 1; }
  seasonTemp() { return this.seasonsOn ? 6 * Math.cos((this.yearPhase - 0.375) * P.TAU) : 0; }
  globalTemp() {
    let t = this.climate + this.tempOffset + this.seasonTemp();
    for (const e of this.transients) t += e.dT * Math.min(1, (this.tick - e.start) / 600, (e.until - this.tick) / 600);
    return t;
  }

  recomputeLight() {
    const sun = this.sun() * this.sunlight, W = P.FW, ox = this.cloudOff[0], oy = this.cloudOff[1];
    const ix = Math.floor(ox), iy = Math.floor(oy), tx = ox - ix, ty = oy - iy;
    for (let y = 0; y < W; y++) {
      const y0 = (y + iy) % W, y1 = (y0 + 1) % W;
      for (let x = 0; x < W; x++) {
        const i = y * W + x, x0 = (x + ix) % W, x1 = (x0 + 1) % W, c = this.cloud;
        const cl = (c[y0 * W + x0] * (1 - tx) + c[y0 * W + x1] * tx) * (1 - ty) + (c[y1 * W + x0] * (1 - tx) + c[y1 * W + x1] * tx) * ty;
        const l = sun * (0.25 + 0.75 * this.depth[i]) * (0.45 + 0.75 * cl) * (1 + this.shade[i]);
        this.light[i] = l < 0 ? 0 : l > 1.6 ? 1.6 : l;
        this.envTex[i * 4] = this.light[i];
      }
    }
    this.envDirty = true;
  }
  recomputeTemp() {
    const g = this.globalTemp(), W = P.FW, dayVar = this.dayNightOn ? (this.sun() - 0.5) * 3 : 0;
    for (let i = 0; i < W * W; i++) this.temp[i] = this.tempBase[i] + g + dayVar + this.heat[i];
    for (let i = 0; i < W * W; i++) this.envTex[i * 4 + 3] = this.fert[i];
    for (const v of this.vents) { // vents heat and fertilise their surroundings
      const cx = v.x / P.FT, cy = v.y / P.FT, R = 4 * v.str + 1;
      for (let dy = -6; dy <= 6; dy++) for (let dx = -6; dx <= 6; dx++) {
        const d2 = dx * dx + dy * dy, f = Math.exp(-d2 / (R * R));
        if (f < 0.02) continue;
        const i = (((Math.floor(cy) + dy) % W + W) % W) * W + (((Math.floor(cx) + dx) % W + W) % W);
        this.temp[i] += 22 * v.str * f;
        this.envTex[i * 4 + 3] = Math.max(this.envTex[i * 4 + 3], 1 + f * v.str);
      }
    }
    for (let i = 0; i < W * W; i++) {
      this.envTex[i * 4 + 1] = (this.temp[i] + 10) / 60;
      this.envTex[i * 4 + 2] = this.wall[i];
    }
    this.envDirty = true;
  }

  // world-unit sampling helpers used by the sim and the inspector
  lightAt(x, y) { return this.light[this.idx(x, y)]; }
  tempAt(x, y) { return this.temp[this.idx(x, y)]; }

  step(hooks) {
    this.tick++;
    const t = this.tick;
    for (const m of this.modes) { m.p1 += m.w1; m.p2 += m.w2; }
    for (let k = this.vortices.length - 1; k >= 0; k--) if (--this.vortices[k].life <= 0) this.vortices.splice(k, 1);
    for (let k = this.vents.length - 1; k >= 0; k--) {
      const v = this.vents[k];
      if (v.life > 0 && --v.life <= 0) { this.vents.splice(k, 1); this.envDirty = true; continue; }
      this.add(v.x, v.y, 0, 0.05 * v.str * this.inflow);
    }
    this.cloudOff[0] = (this.cloudOff[0] + 0.0045) % P.FW;
    this.cloudOff[1] = (this.cloudOff[1] + 0.0021) % P.FW;
    if (t % 8 === 0) this.recomputeLight();
    if (t % 30 === 0) {
      for (let i = 0; i < this.heat.length; i++) this.heat[i] *= 0.985;
      this.transients = this.transients.filter((e) => e.until > t);
      this.recomputeTemp();
    }
    // slow climate drift: a new target every year
    if (t % (this.dayLen * this.yearDays) === 0) {
      this.climateTarget = P.clamp(this.climateTarget + P.randn() * 3, -8, 8);
      const d = this.climateTarget - this.climate;
      if (Math.abs(d) > 2) hooks.log(`Climate is ${d > 0 ? 'warming' : 'cooling'}: the next year trends ${Math.abs(d).toFixed(1)} °C ${d > 0 ? 'hotter' : 'colder'}.`, 'climate');
    }
    this.climate += (this.climateTarget - this.climate) * 0.0004;
    if (t % 120 === 0) { this.updateUniforms(); }
    if (t % 300 === 0 || this.history.length === 0) {
      let tsum = 0, lsum = 0, n = 0, nut = 0, tox = 0;
      for (let i = 0; i < this.temp.length; i += 7) { if (this.wall[i] > 0.5) continue; tsum += this.temp[i]; lsum += this.light[i]; nut += this.field[i * 4]; tox += this.field[i * 4 + 1]; n++; }
      this.history.push({ t, temp: tsum / n, light: lsum / n, nut: nut / n, tox: tox / n });
      this.meanTemp = tsum / n;
      if (this.history.length > 400) this.history.shift();
    }
    if (this.eventsOn && t >= this.nextEvent) {
      this.nextEvent = t + this.dayLen * (1.5 + P.rand() * 3);
      hooks.randomEvent();
    }
  }

  // ----- painting (player tools) -----
  paint(x, y, radius, fn) {
    const r = radius / P.FT, cx = x / P.FT, cy = y / P.FT, W = P.FW, R = Math.ceil(r);
    for (let dy = -R; dy <= R; dy++) for (let dx = -R; dx <= R; dx++) {
      const tx = Math.floor(cx) + dx, ty = Math.floor(cy) + dy;
      const ddx = tx + 0.5 - cx, ddy = ty + 0.5 - cy, d = Math.sqrt(ddx * ddx + ddy * ddy) / Math.max(r, 0.5);
      if (d > 1) continue;
      fn(((ty % W) + W) % W * W + (((tx % W) + W) % W), 1 - d * d);
    }
  }
};
