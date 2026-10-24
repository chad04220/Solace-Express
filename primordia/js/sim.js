'use strict';
// The cell simulation. Structure-of-arrays storage in a fixed pool, a counting-sort spatial hash rebuilt
// every tick, staggered sensing, cached phenotypes and allocation-free hot loops.

const GI_ = P.GI;
const F32 = (n) => new Float32Array(n);

P.CAUSES = ['Starvation', 'Predators', 'Old age', 'Heat or cold', 'Toxins', 'Viruses', 'Your hand', 'Catastrophes'];

P.Sim = class {
  constructor(env) {
    const C = P.MAXC, M = P.MAXP;
    this.env = env;
    // dynamic state
    this.x = F32(C); this.y = F32(C); this.vx = F32(C); this.vy = F32(C); this.ang = F32(C);
    this.energy = F32(C); this.hp = F32(C); this.age = F32(C);
    this.alive = new Uint8Array(C); this.state = new Uint8Array(C); this.touch = new Uint8Array(C); this.cool = new Uint8Array(C);
    this.sp = new Int32Array(C); this.uid = new Int32Array(C); this.gen = new Uint16Array(C); this.kids = new Uint16Array(C);
    this.sx = F32(C); this.sy = F32(C); this.thrust = F32(C); this.mood = new Uint8Array(C); // 0 roam 1 chase 2 flee 3 graze 4 school
    this.host = new Int32Array(C).fill(-1); this.hostUid = new Int32Array(C); this.pang = F32(C);
    this.infect = new Uint8Array(C); this.infT = new Uint16Array(C); this.immune = new Uint8Array(C);
    this.bonds = new Int32Array(C * 4).fill(-1); this.nb = new Uint8Array(C);
    this.genome = F32(C * P.G);
    // phenotype cache
    this.r = F32(C); this.maxSpd = F32(C); this.turn = F32(C); this.senseR = F32(C); this.maxE = F32(C); this.maxHP = F32(C);
    this.metab = F32(C); this.ePhoto = F32(C); this.eChemo = F32(C); this.ePred = F32(C); this.eScav = F32(C); this.ePara = F32(C);
    this.tOpt = F32(C); this.tTol = F32(C); this.maxAge = F32(C); this.reproF = F32(C);
    this.free = []; this.hi = 0; this.count = 0; this.nextUid = 1; this.tick = 0;
    // spatial hash
    const GN = P.GW * P.GW;
    this.gStart = new Int32Array(GN + 1); this.gList = new Int32Array(C); this.gOf = new Int32Array(C); this.gFill = new Int32Array(GN);
    // detritus pellets
    this.px = F32(M); this.py = F32(M); this.pe = F32(M); this.pa = F32(M); this.palive = new Uint8Array(M);
    this.pfree = []; this.phi = 0; this.pcount = 0;
    this.dens = new Uint16Array(32 * 32); this.pdens = new Uint16Array(32 * 32); // 128-unit blocks: the "scent" of crowds and carrion
    this.pStart = new Int32Array(GN + 1); this.pList = new Int32Array(M); this.pOf = new Int32Array(M); this.pFill = new Int32Array(GN);
    // settings & stats
    this.popCap = 7000;
    this.strains = []; // viruses
    this.deathsBy = new Array(P.CAUSES.length).fill(0); this.births = 0; this.kills = 0;
    this.history = []; this.lastCensus = null; this.roleCount = {};
    this.fx = []; // visual effects for the renderer: {x,y,r,t,life,color}
    this.hooks = { log() {} };
  }

  // ---------------- pool ----------------
  alloc() {
    let i;
    if (this.free.length) i = this.free.pop();
    else if (this.hi < P.MAXC) i = this.hi++;
    else return -1;
    this.alive[i] = 1; this.count++;
    return i;
  }

  express(i) {
    const f = P.phen(this.genome, i * P.G, this._ph || (this._ph = {}));
    this.r[i] = f.r; this.ePhoto[i] = f.ePhoto; this.eChemo[i] = f.eChemo; this.ePred[i] = f.ePred; this.eScav[i] = f.eScav; this.ePara[i] = f.ePara;
    this.maxSpd[i] = f.maxSpd; this.turn[i] = f.turn; this.senseR[i] = f.senseR; this.maxE[i] = f.maxE; this.maxHP[i] = f.maxHP;
    this.metab[i] = f.metab; this.tOpt[i] = f.tOpt; this.tTol[i] = f.tTol; this.maxAge[i] = f.maxAge; this.reproF[i] = f.reproF;
  }

  spawn(genome, x, y, spId, eFrac = 0.5, gOff = 0) {
    const i = this.alloc();
    if (i < 0) return -1;
    this.genome.set(genome.subarray(gOff, gOff + P.G), i * P.G);
    this.express(i);
    this.x[i] = P.wrapP(x); this.y[i] = P.wrapP(y); this.vx[i] = 0; this.vy[i] = 0; this.ang[i] = P.rand() * P.TAU;
    this.energy[i] = this.maxE[i] * eFrac; this.hp[i] = this.maxHP[i]; this.age[i] = 0;
    this.state[i] = 0; this.cool[i] = 0; this.sp[i] = spId; this.uid[i] = this.nextUid++; this.gen[i] = 0; this.kids[i] = 0;
    this.sx[i] = Math.cos(this.ang[i]); this.sy[i] = Math.sin(this.ang[i]); this.thrust[i] = 0.3; this.mood[i] = 0;
    this.host[i] = -1; this.infect[i] = 0; this.infT[i] = 0; this.immune[i] = 0; this.nb[i] = 0;
    this.bonds.fill(-1, i * 4, i * 4 + 4);
    return i;
  }

  addPellet(x, y, e) {
    let k;
    if (this.pfree.length) k = this.pfree.pop();
    else if (this.phi < P.MAXP) k = this.phi++;
    else return;
    this.px[k] = P.wrapP(x); this.py[k] = P.wrapP(y); this.pe[k] = e; this.pa[k] = 0; this.palive[k] = 1; this.pcount++;
  }
  removePellet(k) { this.palive[k] = 0; this.pfree.push(k); this.pcount--; }

  bond(i, j) {
    if (this.nb[i] >= 4 || this.nb[j] >= 4 || i === j) return false;
    const B = this.bonds;
    for (let s = 0; s < 4; s++) if (B[i * 4 + s] === j) return false;
    for (let s = 0; s < 4; s++) if (B[i * 4 + s] < 0) { B[i * 4 + s] = j; break; }
    for (let s = 0; s < 4; s++) if (B[j * 4 + s] < 0) { B[j * 4 + s] = i; break; }
    this.nb[i]++; this.nb[j]++;
    return true;
  }
  unbond(i, j) {
    const B = this.bonds;
    for (let s = 0; s < 4; s++) if (B[i * 4 + s] === j) { B[i * 4 + s] = -1; this.nb[i]--; }
    for (let s = 0; s < 4; s++) if (B[j * 4 + s] === i) { B[j * 4 + s] = -1; this.nb[j]--; }
  }

  kill(i, cause) {
    if (!this.alive[i]) return;
    const r = this.r[i], x = this.x[i], y = this.y[i];
    const total = Math.max(0, this.energy[i]) * 0.5 + r * r * 0.25;
    const n = Math.min(4, 1 + ((r / 4) | 0));
    for (let k = 0; k < n; k++) this.addPellet(x + (P.rand() - 0.5) * r, y + (P.rand() - 0.5) * r, (total * 0.7) / n);
    this.env.add(x, y, 0, total * 0.3 * 0.004);
    for (let s = 0; s < 4; s++) { const j = this.bonds[i * 4 + s]; if (j >= 0) this.unbond(i, j); }
    if (this.infect[i]) this.burst(i);
    this.alive[i] = 0; this.count--; this.free.push(i);
    this.deathsBy[cause]++;
    if (cause === 1) this.kills++;
  }

  // ---------------- spatial hash ----------------
  buildGrid() {
    const GN = P.GW * P.GW, st = this.gStart, fill = this.gFill, of = this.gOf, list = this.gList;
    st.fill(0);
    const x = this.x, y = this.y, al = this.alive, inv = 1 / P.GRID, GW = P.GW, dens = this.dens, pdens = this.pdens;
    dens.fill(0); pdens.fill(0);
    for (let i = 0; i < this.hi; i++) {
      if (!al[i]) continue;
      const gx = (x[i] * inv) & P.GM, gy = (y[i] * inv) & P.GM, g = gy * GW + gx;
      of[i] = g; st[g + 1]++;
      dens[(gy >> 2) * 32 + (gx >> 2)]++;
    }
    for (let g = 0; g < GN; g++) st[g + 1] += st[g];
    fill.set(st.subarray(0, GN));
    for (let i = 0; i < this.hi; i++) if (al[i]) list[fill[of[i]]++] = i;
    // pellets
    const ps = this.pStart, pf = this.pFill, po = this.pOf, pl = this.pList, pa = this.palive, pxa = this.px, pya = this.py;
    ps.fill(0);
    for (let k = 0; k < this.phi; k++) {
      if (!pa[k]) continue;
      const gx = (pxa[k] * inv) & P.GM, gy = (pya[k] * inv) & P.GM, g = gy * GW + gx;
      po[k] = g; ps[g + 1]++;
      pdens[(gy >> 2) * 32 + (gx >> 2)]++;
    }
    for (let g = 0; g < GN; g++) ps[g + 1] += ps[g];
    pf.set(ps.subarray(0, GN));
    for (let k = 0; k < this.phi; k++) if (pa[k]) pl[pf[po[k]]++] = k;
  }

  // ---------------- main step ----------------
  step() {
    this.tick++;
    const env = this.env;
    env.step(this.hooks);
    this.buildGrid();
    this.sense();
    this.physics();
    this.live();
    this.pellets();
    // shrink the high-water marks; drop free-list entries above them so they are never handed out twice
    const hi0 = this.hi, phi0 = this.phi;
    while (this.hi > 0 && !this.alive[this.hi - 1]) this.hi--;
    if (this.hi < hi0) { const hi = this.hi; this.free = this.free.filter((i) => i < hi); }
    while (this.phi > 0 && !this.palive[this.phi - 1]) this.phi--;
    if (this.phi < phi0) { const phi = this.phi; this.pfree = this.pfree.filter((k) => k < phi); }
    if (this.tick % 30 === 0) this.census();
    for (let k = this.fx.length - 1; k >= 0; k--) if (++this.fx[k].t > this.fx[k].life) this.fx.splice(k, 1);
  }

  // Perception and decision making, run for a third of the cells each tick.
  sense() {
    const env = this.env, F = env.field, W = P.FW, tick = this.tick;
    const x = this.x, y = this.y, r = this.r, al = this.alive, st = this.state, sp = this.sp, g = this.genome, G = P.G;
    const ePred = this.ePred, ePara = this.ePara, eScav = this.eScav, eChemo = this.eChemo, ePhoto = this.ePhoto;
    const gStart = this.gStart, gList = this.gList, GW = P.GW, GM = GW - 1;
    const temp = env.temp, light = env.light;
    for (let i = 0; i < this.hi; i++) {
      if (!al[i] || st[i] || ((i + tick) % 3) !== 0 || this.host[i] >= 0) continue;
      const o = i * G, xi = x[i], yi = y[i], ri = r[i];
      // ---- field gradients (central differences on the texel grid) ----
      let tx = (xi / P.FT) & P.FM, ty = (yi / P.FT) & P.FM;
      const ti = ty * W + tx, il = ty * W + ((tx + W - 1) & (W - 1)), ir = ty * W + ((tx + 1) & (W - 1));
      const iu = ((ty + W - 1) & (W - 1)) * W + tx, id = ((ty + 1) & (W - 1)) * W + tx;
      let ax = 0, ay = 0;
      const chemo = eChemo[i] + g[o + GI_.TRACK] * 0.3;
      if (chemo > 0.05) { ax += (F[ir * 4] - F[il * 4]) * chemo * 3; ay += (F[id * 4] - F[iu * 4]) * chemo * 3; }
      const ls = g[o + GI_.LIGHTSEEK] * (0.3 + ePhoto[i]);
      if (ls > 0.05) { ax += (light[ir] - light[il]) * ls * 4; ay += (light[id] - light[iu]) * ls * 4; }
      const tr = g[o + GI_.TRACK];
      if (tr > 0.1) { ax += (F[ir * 4 + 2] - F[il * 4 + 2]) * tr * 6; ay += (F[id * 4 + 2] - F[iu * 4 + 2]) * tr * 6; }
      const tox = 1 - g[o + GI_.RESIST];
      if (tox > 0.2) { ax -= (F[ir * 4 + 1] - F[il * 4 + 1]) * tox * 8; ay -= (F[id * 4 + 1] - F[iu * 4 + 1]) * tox * 8; }
      const dT = this.tOpt[i] - temp[ti], need = dT > 0 ? Math.min(1, dT / this.tTol[i]) : Math.max(-1, dT / this.tTol[i]);
      if (need * need > 0.09) { ax += (temp[ir] - temp[il]) * need * 0.15; ay += (temp[id] - temp[iu]) * need * 0.15; }
      // walls ahead: steer away
      if (env.wall[ir] + env.wall[il] + env.wall[iu] + env.wall[id] > 0.2) { ax -= (env.wall[ir] - env.wall[il]) * 2; ay -= (env.wall[id] - env.wall[iu]) * 2; }

      // ---- neighbours ----
      const sr = this.senseR[i], sr2 = sr * sr, R = Math.min(4, Math.ceil(sr / P.GRID));
      const gx = (xi / P.GRID) & P.GM, gy = (yi / P.GRID) & P.GM, mySp = sp[i];
      const isPred = ePred[i] > 0.12, isPara = ePara[i] > 0.15, reach = ri * (0.6 + ePred[i]);
      const lureSeek = g[o + GI_.LIGHTSEEK], fear = g[o + GI_.FEAR], social = g[o + GI_.SOCIAL];
      let bestPrey = -1, bestPreyD = 1e9, fx = 0, fy = 0, cx = 0, cy = 0, cn = 0, avx = 0, avy = 0, sepx = 0, sepy = 0, threat = 0;
      let seen = 0;
      outer: for (let dy = -R; dy <= R; dy++) {
        const row = ((gy + dy) & GM) * GW;
        for (let dx = -R; dx <= R; dx++) {
          const cell = row + ((gx + dx) & GM);
          for (let q = gStart[cell], qe = gStart[cell + 1]; q < qe; q++) {
            const j = gList[q];
            if (j === i) continue;
            let ddx = x[j] - xi, ddy = y[j] - yi;
            if (ddx > 2048) ddx -= 4096; else if (ddx < -2048) ddx += 4096;
            if (ddy > 2048) ddy -= 4096; else if (ddy < -2048) ddy += 4096;
            const d2 = ddx * ddx + ddy * ddy;
            if (d2 > sr2 || d2 < 1e-6) continue;
            if (++seen > 70) break outer;
            const d = Math.sqrt(d2), inv = 1 / d, rj = r[j];
            if (sp[j] === mySp) {
              cx += ddx; cy += ddy; avx += this.vx[j]; avy += this.vy[j]; cn++;
              const sep = ri + rj + 3;
              if (d < sep) { sepx -= ddx * inv * (sep - d); sepy -= ddy * inv * (sep - d); }
              continue;
            }
            // threats: anything that could eat me
            if (ePred[j] > 0.12 && rj * (0.6 + ePred[j]) > ri * 0.5 && st[j] === 0) {
              const w = (1 - d / sr) * (0.5 + ePred[j]);
              fx -= ddx * inv * w; fy -= ddy * inv * w; threat += w;
            }
            // prey or hosts
            if (isPred && rj < reach * 1.2 && !st[j]) {
              const score = d / (1 + (rj / ri)) ;
              if (score < bestPreyD) { bestPreyD = score; bestPrey = j; }
            } else if (isPara && rj > ri * 1.2) {
              if (d < bestPreyD) { bestPreyD = d; bestPrey = j; }
            }
            // glowing lures attract light seekers
            const glow = g[j * G + GI_.GLOW];
            if (glow > 0.3 && lureSeek > 0.25) { ax += ddx * inv * glow * lureSeek * 0.8; ay += ddy * inv * glow * lureSeek * 0.8; }
          }
        }
      }
      let mood = 0, thrust = 0.25 + g[o + GI_.MOTOR] * 0.2;
      const hunger = 1 - this.energy[i] / this.maxE[i];
      // nothing to eat in sight: follow the scent of crowds (hunters, parasites) or carrion (scavengers)
      if (bestPrey < 0 && (isPred || isPara || eScav[i] > 0.25)) {
        const bx = gx >> 2, by = gy >> 2, D = eScav[i] > ePred[i] + ePara[i] ? this.pdens : this.dens;
        const l = D[by * 32 + ((bx + 31) & 31)], rr = D[by * 32 + ((bx + 1) & 31)], u = D[((by + 31) & 31) * 32 + bx], dn = D[((by + 1) & 31) * 32 + bx];
        const w = (0.6 + hunger) * (0.4 + g[o + GI_.AGGRO] * 0.6 + eScav[i]) * 1.6;
        ax += ((rr - l) / (rr + l + 2)) * w; ay += ((dn - u) / (dn + u + 2)) * w;
      }
      if (threat > 0.05 && fear > 0.05) {
        const n = Math.hypot(fx, fy) + 1e-6;
        ax += (fx / n) * fear * 4; ay += (fy / n) * fear * 4; thrust = 0.6 + fear * 0.4; mood = 2;
      }
      if (bestPrey >= 0) {
        const aggro = g[o + GI_.AGGRO] * (0.4 + hunger);
        let ddx = P.wrapD(x[bestPrey] - xi), ddy = P.wrapD(y[bestPrey] - yi);
        // lead the target a little
        ddx += this.vx[bestPrey] * 6; ddy += this.vy[bestPrey] * 6;
        const n = Math.hypot(ddx, ddy) + 1e-6;
        ax += (ddx / n) * aggro * 3.5; ay += (ddy / n) * aggro * 3.5;
        if (mood !== 2 || aggro > fear) { mood = 1; thrust = Math.max(thrust, 0.5 + aggro * 0.5); }
      }
      if (cn > 0 && social > 0.05) {
        const n1 = Math.hypot(cx, cy) + 1e-6, n2 = Math.hypot(avx, avy) + 1e-6;
        ax += (cx / n1) * social * 0.8 + (avx / n2) * social * 1.2 + sepx * 0.15;
        ay += (cy / n1) * social * 0.8 + (avy / n2) * social * 1.2 + sepy * 0.15;
        if (mood === 0) mood = 4;
      } else if (cn > 0) { ax += sepx * 0.08; ay += sepy * 0.08; }
      // detritus for scavengers
      if (eScav[i] > 0.1) {
        let best = -1, bd = 1e9;
        const pR = Math.min(2, R), pS = this.pStart, pL = this.pList;
        for (let dy = -pR; dy <= pR; dy++) for (let dx = -pR; dx <= pR; dx++) {
          const cell = ((gy + dy) & GM) * GW + ((gx + dx) & GM);
          for (let q = pS[cell], qe = pS[cell + 1]; q < qe; q++) {
            const k = pL[q];
            const ddx = P.wrapD(this.px[k] - xi), ddy = P.wrapD(this.py[k] - yi), d2 = ddx * ddx + ddy * ddy;
            if (d2 < bd && d2 < sr2) { bd = d2; best = k; }
          }
        }
        if (best >= 0) {
          const ddx = P.wrapD(this.px[best] - xi), ddy = P.wrapD(this.py[best] - yi), n = Math.sqrt(bd) + 1e-6;
          ax += (ddx / n) * eScav[i] * (1 + hunger) * 2.5; ay += (ddy / n) * eScav[i] * (1 + hunger) * 2.5;
          if (mood === 0) { mood = 3; thrust = Math.max(thrust, 0.5); }
        }
      }
      // wander
      const wa = this.ang[i] + (P.rand() - 0.5) * 1.6;
      ax += Math.cos(wa) * 0.35; ay += Math.sin(wa) * 0.35;
      // well-fed phototrophs in good light settle down
      if (ePhoto[i] > 0.4 && mood === 0) thrust *= 0.3 + Math.max(0, 0.9 - light[ti]);
      if (hunger > 0.6 && mood === 0) thrust = Math.min(1, thrust + 0.25);
      // colony differentiation: inner cells hold still
      if (this.nb[i] >= 3) thrust *= 1 - g[o + GI_.DIFFER] * 0.9;
      const n = Math.hypot(ax, ay) + 1e-6;
      this.sx[i] = ax / n; this.sy[i] = ay / n; this.thrust[i] = thrust > 1 ? 1 : thrust; this.mood[i] = mood;
    }
  }

  // Contacts, collisions, predation, parasitism, infection, gene transfer, bonds and movement.
  physics() {
    const env = this.env, x = this.x, y = this.y, vx = this.vx, vy = this.vy, r = this.r, al = this.alive, st = this.state, sp = this.sp;
    const gStart = this.gStart, gList = this.gList, GW = P.GW, GM = GW - 1, g = this.genome, G = P.G;
    const touch = this.touch, B = this.bonds;
    touch.fill(0, 0, this.hi);
    for (let i = 0; i < this.hi; i++) {
      if (!al[i]) continue;
      if (this.cool[i]) this.cool[i]--;
      const xi = x[i], yi = y[i], ri = r[i], gx = (xi / P.GRID) & P.GM, gy = (yi / P.GRID) & P.GM;
      for (let dy = -1; dy <= 1; dy++) {
        const row = ((gy + dy) & GM) * GW;
        for (let dx = -1; dx <= 1; dx++) {
          const cell = row + ((gx + dx) & GM);
          for (let q = gStart[cell], qe = gStart[cell + 1]; q < qe; q++) {
            const j = gList[q];
            if (j <= i || !al[j]) continue;
            let ddx = x[j] - x[i], ddy = y[j] - y[i];
            if (ddx > 2048) ddx -= 4096; else if (ddx < -2048) ddx += 4096;
            if (ddy > 2048) ddy -= 4096; else if (ddy < -2048) ddy += 4096;
            const rr = ri + r[j], d2 = ddx * ddx + ddy * ddy;
            if (d2 >= rr * rr) continue;
            if (this.host[i] === j || this.host[j] === i) continue;
            const d = Math.sqrt(d2) + 1e-4, nx = ddx / d, ny = ddy / d;
            if (touch[i] < 255) touch[i]++;
            if (touch[j] < 255) touch[j]++;
            // soft collision, mass-weighted
            const mi = ri * ri, mj = r[j] * r[j], ov = (rr - d) * 0.5, wi = mj / (mi + mj), wj = mi / (mi + mj);
            x[i] -= nx * ov * wi; y[i] -= ny * ov * wi; x[j] += nx * ov * wj; y[j] += ny * ov * wj;
            const rv = (vx[j] - vx[i]) * nx + (vy[j] - vy[i]) * ny;
            if (rv < 0) { vx[i] += nx * rv * wi; vy[i] += ny * rv * wi; vx[j] -= nx * rv * wj; vy[j] -= ny * rv * wj; }
            if (sp[i] !== sp[j]) {
              this.bite(i, j, nx, ny); if (!al[i]) break;
              if (al[j]) this.bite(j, i, -nx, -ny);
              if (!al[i]) break;
              if (!al[j]) continue;
              if (this.ePara[i] > 0.15 && this.host[i] < 0 && r[j] > ri * 1.2) this.attach(i, j);
              else if (this.ePara[j] > 0.15 && this.host[j] < 0 && ri > r[j] * 1.2) this.attach(j, i);
            } else {
              // colony fusion and gene sharing among kin
              if (this.nb[i] < 4 && this.nb[j] < 4 && g[i * G + GI_.ADHESION] > 0.6 && g[j * G + GI_.ADHESION] > 0.6 && P.rand() < 0.01) this.bond(i, j);
              if (g[i * G + GI_.CONJ] > 0.3 && g[j * G + GI_.CONJ] > 0.3 && P.rand() < 0.003) this.conjugate(i, j);
            }
            if (this.infect[i] && !this.infect[j]) this.transmit(i, j);
            else if (this.infect[j] && !this.infect[i]) this.transmit(j, i);
          }
          if (!al[i]) break;
        }
        if (!al[i]) break;
      }
    }
    // bonds: springs, damping and energy sharing
    const E = this.energy, mE = this.maxE;
    for (let i = 0; i < this.hi; i++) {
      if (!al[i] || !this.nb[i]) continue;
      for (let s = 0; s < 4; s++) {
        const j = B[i * 4 + s];
        if (j <= i) continue;
        let ddx = P.wrapD(x[j] - x[i]), ddy = P.wrapD(y[j] - y[i]);
        const d = Math.sqrt(ddx * ddx + ddy * ddy) + 1e-4, rest = (r[i] + r[j]) * 0.92;
        if (d > (r[i] + r[j]) * 3.2) { this.unbond(i, j); continue; }
        const f = (d - rest) * 0.2, nx = ddx / d, ny = ddy / d, mi = r[i] * r[i], mj = r[j] * r[j];
        x[i] += nx * f * (mj / (mi + mj)); y[i] += ny * f * (mj / (mi + mj));
        x[j] -= nx * f * (mi / (mi + mj)); y[j] -= ny * f * (mi / (mi + mj));
        const avx = (vx[i] + vx[j]) * 0.5, avy = (vy[i] + vy[j]) * 0.5;
        vx[i] += (avx - vx[i]) * 0.15; vy[i] += (avy - vy[i]) * 0.15; vx[j] += (avx - vx[j]) * 0.15; vy[j] += (avy - vy[j]) * 0.15;
        const flow = (E[i] / mE[i] - E[j] / mE[j]) * 0.03 * Math.min(mE[i], mE[j]);
        E[i] -= flow; E[j] += flow;
      }
    }
    // integrate
    const fU = env.flowU, fV = env.flowV, W = P.FW;
    for (let i = 0; i < this.hi; i++) {
      if (!al[i]) continue;
      const ti = ((y[i] / P.FT) & P.FM) * W + ((x[i] / P.FT) & P.FM);
      if (this.host[i] >= 0) { // riding a host
        const h = this.host[i];
        if (!al[h] || this.uid[h] !== this.hostUid[i]) { this.host[i] = -1; }
        else {
          this.pang[i] += 0.004;
          const a = this.pang[i], d = r[h] + r[i] * 0.6;
          x[i] = P.wrapP(x[h] + Math.cos(a) * d); y[i] = P.wrapP(y[h] + Math.sin(a) * d);
          vx[i] = vx[h]; vy[i] = vy[h]; this.ang[i] = a + Math.PI;
          continue;
        }
      }
      if (!st[i]) {
        // steer: rotate heading toward the desired direction, limited by agility
        const ca = Math.cos(this.ang[i]), sa = Math.sin(this.ang[i]);
        const cross = ca * this.sy[i] - sa * this.sx[i], dot = ca * this.sx[i] + sa * this.sy[i];
        const tr = this.turn[i];
        let da = dot < 0 ? (cross >= 0 ? tr : -tr) : cross * 2;
        if (da > tr) da = tr; else if (da < -tr) da = -tr;
        this.ang[i] += da;
        let th = this.thrust[i];
        if (this.nb[i] >= 3) th *= 1 - g[i * G + GI_.DIFFER] * 0.9;
        const acc = this.maxSpd[i] * th * 0.12 * (dot > 0 ? 1 : 0.4);
        vx[i] += ca * acc; vy[i] += sa * acc;
      }
      vx[i] *= 0.88; vy[i] *= 0.88;
      const drift = 0.8 / (1 + r[i] * 0.04);
      let nx = x[i] + vx[i] + fU[ti] * drift, ny = y[i] + vy[i] + fV[ti] * drift;
      // rock: push back out along the wall gradient
      if (env.wall[ti] > 0.05) {
        const w = env.wallAt(nx, ny);
        if (w > 0.45) {
          const gxw = env.wallAt(nx + 6, ny) - env.wallAt(nx - 6, ny), gyw = env.wallAt(nx, ny + 6) - env.wallAt(nx, ny - 6);
          const gn = Math.hypot(gxw, gyw);
          if (gn > 1e-4) { nx -= (gxw / gn) * (w - 0.4) * 10; ny -= (gyw / gn) * (w - 0.4) * 10; }
          else { nx = x[i]; ny = y[i]; }
          vx[i] *= -0.3; vy[i] *= -0.3;
        }
      }
      x[i] = nx < 0 ? nx + 4096 : nx >= 4096 ? nx - 4096 : nx;
      y[i] = ny < 0 ? ny + 4096 : ny >= 4096 ? ny - 4096 : ny;
    }
  }

  bite(a, t, nx, ny) {
    const pa = this.ePred[a];
    if (pa < 0.12 || this.cool[a] || this.state[a]) return;
    const ra = this.r[a], rt = this.r[t];
    if (rt > ra * (0.6 + pa) * 1.2) return;
    // must bite with the front of the cell
    if (Math.cos(this.ang[a]) * nx + Math.sin(this.ang[a]) * ny < -0.2) return;
    const g = this.genome, ga = a * P.G, gt = t * P.G;
    const armor = g[gt + GI_.ARMOR] * (this.state[t] ? 1.6 : 1);
    const dmg = pa * ra * 0.45 * (1 + g[ga + GI_.SPIKES] * 0.4) * Math.max(0.08, 1 - armor * 0.7);
    const stolen = Math.min(Math.max(0, this.energy[t]), dmg * 3);
    this.energy[a] += stolen * 0.8; this.energy[t] -= stolen; this.hp[t] -= dmg;
    this.cool[a] = 8;
    const sp = g[gt + GI_.SPIKES];
    if (sp > 0.15) this.hp[a] -= sp * rt * 0.3;
    if (this.hp[t] <= 0 || this.energy[t] <= 0) {
      this.energy[a] += (Math.max(0, this.energy[t]) * 0.5 + rt * rt * 0.5) * 0.8;
      if (this.energy[a] > this.maxE[a] * 1.2) this.energy[a] = this.maxE[a] * 1.2;
      this.kill(t, 1);
    }
    if (this.hp[a] <= 0 && this.alive[a]) this.kill(a, 1);
  }

  attach(p, h) {
    this.host[p] = h; this.hostUid[p] = this.uid[h];
    this.pang[p] = Math.atan2(P.wrapD(this.y[p] - this.y[h]), P.wrapD(this.x[p] - this.x[h]));
    for (let s = 0; s < 4; s++) { const j = this.bonds[p * 4 + s]; if (j >= 0) this.unbond(p, j); }
  }

  conjugate(i, j) { // horizontal gene transfer: j donates a short run of genes to i
    const G = P.G, start = P.randi(G), len = 2 + P.randi(4), g = this.genome;
    for (let k = 0; k < len; k++) { const q = (start + k) % G; g[i * G + q] = g[j * G + q]; }
    this.express(i);
    if (this.energy[i] > this.maxE[i]) this.energy[i] = this.maxE[i];
    if (this.hp[i] > this.maxHP[i]) this.hp[i] = this.maxHP[i];
  }

  // ---------------- viruses ----------------
  makeStrain(i, name) {
    if (this.strains.length >= 250) return -1;
    const s = { id: this.strains.length, genome: Float32Array.from(this.genome.subarray(i * P.G, i * P.G + P.G)), sp: this.sp[i], name, infected: 0, killed: 0, born: this.tick };
    this.strains.push(s);
    return s.id;
  }
  infectable(j, sid) {
    if (!this.alive[j] || this.infect[j] || this.immune[j] === sid + 1) return false;
    return P.gdist(this.genome, j * P.G, this.strains[sid].genome, 0) < 0.11;
  }
  transmit(i, j) {
    const sid = this.infect[i] - 1;
    if (!this.infectable(j, sid)) return;
    if (P.rand() < 0.08 * (1 - this.genome[j * P.G + GI_.RESIST] * 0.8)) { this.infect[j] = sid + 1; this.infT[j] = 0; this.strains[sid].infected++; }
  }
  burst(i) { // lysis spreads virions to nearby compatible cells
    const sid = this.infect[i] - 1, xi = this.x[i], yi = this.y[i];
    this.forNear(xi, yi, 48, (j) => { if (this.infectable(j, sid) && P.rand() < 0.5) { this.infect[j] = sid + 1; this.infT[j] = 0; this.strains[sid].infected++; } });
    this.strains[sid].killed++;
  }

  forNear(x, y, R, fn) {
    const gx = (P.wrapP(x) / P.GRID) | 0, gy = (P.wrapP(y) / P.GRID) | 0, n = Math.ceil(R / P.GRID), GM = P.GW - 1, R2 = R * R;
    for (let dy = -n; dy <= n; dy++) for (let dx = -n; dx <= n; dx++) {
      const cell = ((gy + dy) & GM) * P.GW + ((gx + dx) & GM);
      for (let q = this.gStart[cell], qe = this.gStart[cell + 1]; q < qe; q++) {
        const j = this.gList[q];
        if (!this.alive[j]) continue;
        const ddx = P.wrapD(this.x[j] - x), ddy = P.wrapD(this.y[j] - y);
        if (ddx * ddx + ddy * ddy <= R2) fn(j, ddx, ddy);
      }
    }
  }

  // ---------------- metabolism, life and death, reproduction ----------------
  live() {
    const env = this.env, F = env.field, W = P.FW, light = env.light, temp = env.temp, g = this.genome, G = P.G;
    const E = this.energy, HP = this.hp, mE = this.maxE, mHP = this.maxHP, al = this.alive, st = this.state, r = this.r;
    const hiNow = this.hi, cap = this.popCap, mutMult = env.mutationMult;
    for (let i = 0; i < hiNow; i++) {
      if (!al[i]) continue;
      const o = i * G, ri = r[i], area = ri * ri;
      const ti = ((this.y[i] / P.FT) & P.FM) * W + ((this.x[i] / P.FT) & P.FM);
      this.age[i]++;
      const inner = this.nb[i] >= 3, differ = g[o + GI_.DIFFER];
      const feedBoost = inner ? 1 + differ * 0.7 : 1;
      const dormant = st[i] === 1;
      // income
      let gain = 0;
      if (this.ePhoto[i] > 0.01) {
        // photosynthesis also needs a little dissolved nutrient (nitrogen, phosphorus)
        const crowd = 1 / (1 + this.touch[i] * 0.15), N = F[ti * 4];
        const ph = this.ePhoto[i] * light[ti] * area * 0.03 * crowd * feedBoost * (1 - g[o + GI_.ARMOR] * 0.25) * (dormant ? 0.3 : 1) * (0.3 + (0.7 * N) / (N + 0.25));
        gain += ph; env.addAt(ti, 0, -ph * 0.0012);
      }
      if (this.eChemo[i] > 0.01 && !dormant) {
        const N = F[ti * 4], up = this.eChemo[i] * area * 0.009 * (N / (N + 0.6)) * feedBoost;
        gain += up; env.addAt(ti, 0, -up * 0.004);
      }
      // parasites drain their host
      const h = this.host[i];
      if (h >= 0 && al[h] && this.uid[h] === this.hostUid[i]) {
        const d = Math.min(Math.max(0, E[h]), this.ePara[i] * area * 0.02 * (1 - g[h * G + GI_.ARMOR] * 0.5));
        E[h] -= d; gain += d * 0.9;
        if (E[h] <= 0) this.kill(h, 1);
        if (P.rand() < 0.0008) this.host[i] = -1;
      }
      // scavenging pellets in reach
      if (this.eScav[i] > 0.05 && !dormant) {
        const gx = (this.x[i] / P.GRID) & P.GM, gy = (this.y[i] / P.GRID) & P.GM, GM = P.GW - 1;
        for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
          const cell = ((gy + dy) & GM) * P.GW + ((gx + dx) & GM);
          for (let q = this.pStart[cell], qe = this.pStart[cell + 1]; q < qe; q++) {
            const k = this.pList[q];
            if (!this.palive[k]) continue;
            const ddx = P.wrapD(this.px[k] - this.x[i]), ddy = P.wrapD(this.py[k] - this.y[i]);
            if (ddx * ddx + ddy * ddy < (ri + 3) * (ri + 3)) {
              const take = Math.min(this.pe[k], this.eScav[i] * area * 0.05 + 0.2);
              this.pe[k] -= take; gain += take * (0.3 + 0.7 * this.eScav[i]);
              if (this.pe[k] < 0.05) this.removePellet(k);
            }
          }
        }
      }
      // costs
      let cost = this.metab[i];
      const tDev = Math.abs(temp[ti] - this.tOpt[i]) - this.tTol[i];
      if (dormant) cost *= 0.04;
      else {
        const motor = g[o + GI_.MOTOR];
        cost += this.thrust[i] * motor * (area * 0.0006 + ri * 0.002);
        if (tDev > 0) cost *= 1 + tDev * 0.05;
      }
      // toxins: make and suffer
      const tg = g[o + GI_.TOXIN];
      if (tg > 0.05 && !dormant && F[ti * 4 + 1] < 0.8) { const amt = tg * ri * 0.0004 * (1 - F[ti * 4 + 1] / 0.8); env.addAt(ti, 1, amt); cost += amt * 15; }
      const sig = g[o + GI_.SIGNAL];
      if (sig > 0.1 && !dormant && (this.tick + i) % 4 === 0) env.addAt(ti, 2, sig * 0.01);
      const T = F[ti * 4 + 1];
      const dTox = T > 0.04 ? (T - 0.04) * (1 - g[o + GI_.RESIST]) * ri * 0.03 : 0;
      const dTemp = tDev > 0 && (!dormant || tDev > 25) ? mHP[i] * tDev * 0.0006 : 0;
      const dVir = this.infect[i] ? mHP[i] * 0.0012 : 0;
      const dmg = dTox + dTemp + dVir;
      const dmgCause = dVir >= dTox && dVir >= dTemp ? 5 : dTemp >= dTox ? 3 : 4;
      // virus
      if (this.infect[i]) {
        this.infT[i]++;
        cost += mE[i] * 0.002;
        if (this.infT[i] > 400 && P.rand() < 0.002 + g[o + GI_.RESIST] * 0.01) { this.immune[i] = this.infect[i]; this.infect[i] = 0; }
        else if (this.infT[i] > 1600) { this.kill(i, 5); continue; }
      }
      E[i] += gain - cost;
      HP[i] -= dmg;
      if (HP[i] < mHP[i] && E[i] > mE[i] * 0.2) { const heal = mHP[i] * 0.002; HP[i] += heal; E[i] -= heal * 0.4; }
      if (E[i] > mE[i] * 1.1) E[i] = mE[i] * 1.1;
      // death
      if (E[i] <= 0) { this.kill(i, 0); continue; }
      if (HP[i] <= 0) { this.kill(i, dmg > 0 ? dmgCause : 1); continue; }
      if (this.age[i] > this.maxAge[i] * (dormant ? 4 : 1) && P.rand() < 0.004) { this.kill(i, 2); continue; }
      // sporulation and germination
      const spore = g[o + GI_.SPORE];
      if (!dormant && spore > 0.15 && (E[i] < mE[i] * 0.18 || tDev > 4) && P.rand() < spore * 0.02 && h < 0) {
        st[i] = 1; this.vx[i] *= 0.2; this.vy[i] *= 0.2;
        for (let s = 0; s < 4; s++) { const j = this.bonds[i * 4 + s]; if (j >= 0) this.unbond(i, j); }
      } else if (dormant && tDev <= 0 && P.rand() < 0.004) st[i] = 0;
      // division
      if (!dormant && E[i] > mE[i] * this.reproF[i] && this.age[i] > 100 + ri * 8 && this.count < cap && (this.count < cap * 0.85 || P.rand() < 0.3)) this.divide(i, mutMult * (1 + F[ti * 4 + 3] * 10));
    }
  }

  divide(i, mutMult) {
    const c = this.alloc();
    if (c < 0) return;
    const G = P.G, g = this.genome;
    g.copyWithin(c * G, i * G, i * G + G);
    const cg = g.subarray(c * G, c * G + G);
    P.mutate(cg, mutMult);
    this.express(c);
    const a = P.rand() * P.TAU, ri = this.r[i];
    this.x[c] = P.wrapP(this.x[i] + Math.cos(a) * ri * 0.8); this.y[c] = P.wrapP(this.y[i] + Math.sin(a) * ri * 0.8);
    this.x[i] = P.wrapP(this.x[i] - Math.cos(a) * ri * 0.4); this.y[i] = P.wrapP(this.y[i] - Math.sin(a) * ri * 0.4);
    this.vx[c] = Math.cos(a) * 0.5; this.vy[c] = Math.sin(a) * 0.5;
    this.ang[c] = this.ang[i] + (P.rand() - 0.5);
    const e = this.energy[i] * 0.45;
    this.energy[i] = e; this.energy[c] = Math.min(e, this.maxE[c]);
    this.hp[c] = this.maxHP[c]; this.age[c] = 0; this.state[c] = 0; this.cool[c] = 10;
    this.uid[c] = this.nextUid++; this.gen[c] = this.gen[i] + 1; this.kids[c] = 0; this.kids[i]++;
    this.sx[c] = this.sx[i]; this.sy[c] = this.sy[i]; this.thrust[c] = this.thrust[i]; this.mood[c] = 0;
    this.host[c] = -1; this.infect[c] = 0; this.infT[c] = 0; this.immune[c] = this.immune[i]; this.nb[c] = 0;
    this.bonds.fill(-1, c * 4, c * 4 + 4);
    if (this.infect[i] && P.rand() < 0.5) { this.infect[c] = this.infect[i]; } // vertical transmission
    // speciation: compare with the species founder
    const sp = P.species[this.sp[i]];
    if (P.gdist(g, c * G, sp.genome, 0) > P.SPECIATION) {
      const ns = P.newSpecies(cg, sp.id, this.tick);
      this.sp[c] = ns.id;
    } else this.sp[c] = sp.id;
    // multicellularity: daughters of adhesive cells stay attached
    const adh = g[i * G + GI_.ADHESION];
    if (this.sp[c] === this.sp[i] && adh > 0.45 && P.rand() < (adh - 0.45) * 2.4) this.bond(i, c);
    this.births++;
  }

  pellets() {
    const env = this.env, fU = env.flowU, fV = env.flowV, W = P.FW;
    for (let k = 0; k < this.phi; k++) {
      if (!this.palive[k]) continue;
      const ti = ((this.py[k] / P.FT) & P.FM) * W + ((this.px[k] / P.FT) & P.FM);
      this.px[k] = P.wrapP(this.px[k] + fU[ti] * 0.9); this.py[k] = P.wrapP(this.py[k] + fV[ti] * 0.9);
      this.pa[k]++;
      const rot = this.pe[k] * 0.0025;
      this.pe[k] -= rot; env.addAt(ti, 0, rot * 0.004);
      if (this.pe[k] < 0.1 || this.pa[k] > 7000 || env.wall[ti] > 0.7) { env.addAt(ti, 0, this.pe[k] * 0.004); this.removePellet(k); }
    }
  }

  // ---------------- census & statistics ----------------
  census() {
    const S = P.species, n = S.length;
    if (!this.popArr || this.popArr.length < n) { this.popArr = new Int32Array(n * 2 + 64); }
    const pop = this.popArr; pop.fill(0);
    const roles = { photo: 0, chemo: 0, pred: 0, scav: 0, para: 0, omni: 0 };
    let biomass = 0, mSize = 0, mMotor = 0, mArmor = 0, mAdh = 0, mTox = 0, mGlow = 0, colonial = 0, spores = 0, infected = 0;
    const g = this.genome, G = P.G;
    for (let i = 0; i < this.hi; i++) {
      if (!this.alive[i]) continue;
      pop[this.sp[i]]++;
      biomass += this.r[i] * this.r[i];
      const o = i * G;
      mSize += this.r[i]; mMotor += g[o + GI_.MOTOR]; mArmor += g[o + GI_.ARMOR]; mAdh += g[o + GI_.ADHESION]; mTox += g[o + GI_.TOXIN]; mGlow += g[o + GI_.GLOW];
      if (this.nb[i]) colonial++;
      if (this.state[i]) spores++;
      if (this.infect[i]) infected++;
    }
    let living = 0, H = 0;
    const c = Math.max(1, this.count);
    for (let s = 0; s < n; s++) {
      const sp = S[s], p = pop[s];
      sp.pop = p;
      if (p > 0) {
        living++; roles[sp.role] += p;
        const q = p / c; H -= q * Math.log(q);
        if (p > sp.peak) sp.peak = p;
        if (sp.extinct >= 0) sp.extinct = -1;
        if (!sp.announced && p >= 25 && !sp.designed && sp.parent >= 0) {
          sp.announced = true;
          this.hooks.log(`New species <b style="color:${sp.color}">${P.escape(sp.name)}</b> (${P.ROLES[sp.role].label.toLowerCase()}) is spreading, descended from ${P.escape(S[sp.parent].name)}.`, 'species', sp.id);
        }
        if (this.tick % 150 === 0 || !sp.hist.length) { sp.hist.push(p); if (sp.hist.length > 160) sp.hist.shift(); }
      } else if (sp.extinct < 0 && sp.peak > 0) {
        sp.extinct = this.tick;
        if (sp.peak >= 30) this.hooks.log(`<b style="color:${sp.color}">${P.escape(sp.name)}</b> went extinct after ${((this.tick - sp.born) / this.env.dayLen).toFixed(1)} days (peak ${sp.peak}).`, 'extinct', sp.id);
      }
    }
    this.living = living; this.diversity = H; this.roleCount = roles; this.biomass = biomass;
    this.infected = infected; this.spores = spores; this.colonial = colonial;
    if (this.tick % 150 === 0 || this.history.length === 0) {
      this.history.push({
        t: this.tick, pop: this.count, species: living, H, biomass, roles: { ...roles },
        size: mSize / c, motor: mMotor / c, armor: mArmor / c, adh: mAdh / c, tox: mTox / c, glow: mGlow / c,
        births: this.births, deaths: this.deathsBy.reduce((a, b) => a + b, 0), pellets: this.pcount,
      });
      if (this.history.length > 600) this.history.shift();
    }
  }

  // ---------------- player & event actions ----------------
  killRadius(x, y, R, cause) { let n = 0; this.forNear(x, y, R, (j) => { this.kill(j, cause); n++; }); return n; }
  feed(x, y, R, amt) {
    for (let k = 0; k < amt; k++) { const a = P.rand() * P.TAU, d = Math.sqrt(P.rand()) * R; this.addPellet(x + Math.cos(a) * d, y + Math.sin(a) * d, 6 + P.rand() * 8); }
  }
  releaseVirus(x, y) {
    let best = -1, bd = 1e9;
    this.forNear(x, y, 80, (j, dx, dy) => { const d = dx * dx + dy * dy; if (d < bd) { bd = d; best = j; } });
    if (best < 0) return null;
    const sp = P.species[this.sp[best]];
    const sid = this.makeStrain(best, 'Phage ' + sp.genus.slice(0, 4) + '-' + (this.strains.length + 1));
    if (sid < 0) return null;
    let n = 0;
    this.forNear(x, y, 90, (j) => { if (this.infectable(j, sid)) { this.infect[j] = sid + 1; this.infT[j] = 0; n++; } });
    this.strains[sid].infected = n;
    return { strain: this.strains[sid], sp, n };
  }
  addFx(x, y, r, life, color) { if (this.fx.length < 64) this.fx.push({ x, y, r, t: 0, life, color }); }

  // Build a primordial soup of varied founders.
  seed(nFounders) {
    const env = this.env;
    const findSpot = (pred) => {
      for (let t = 0; t < 400; t++) {
        const x = P.rand() * P.WORLD, y = P.rand() * P.WORLD, i = env.idx(x, y);
        if (env.wall[i] < 0.1 && pred(i)) return [x, y];
      }
      return [P.rand() * P.WORLD, P.rand() * P.WORLD];
    };
    const place = (genome, n, name, designed) => {
      const sp = P.newSpecies(genome, -1, this.tick, name, designed);
      const role = sp.role;
      const ok = (i) => {
        const tOk = Math.abs(env.temp[i] - (-5 + 50 * genome[GI_.TEMP_OPT])) < 4 + 16 * genome[GI_.TEMP_TOL];
        if (!tOk) return false;
        if (role === 'photo') return env.depth[i] > 0.55;
        if (role === 'chemo') return env.fert[i] > 0.3;
        return true;
      };
      const [cx, cy] = findSpot(ok);
      for (let k = 0; k < n; k++) {
        const a = P.rand() * P.TAU, d = Math.sqrt(P.rand()) * (40 + n * 2.2);
        const g = Float32Array.from(genome);
        P.mutate(g, 0.5);
        this.spawn(g, cx + Math.cos(a) * d, cy + Math.sin(a) * d, sp.id, 0.5 + P.rand() * 0.3);
      }
      return sp;
    };
    const names = Object.keys(P.PRESETS);
    for (const nm of names) {
      const g = P.genomeFrom(P.PRESETS[nm]);
      g[GI_.HUE] = (g[GI_.HUE] + (P.rand() - 0.5) * 0.06 + 1) % 1;
      const role = P.roleOf(g);
      const n = role === 'photo' ? 90 : role === 'chemo' ? 60 : role === 'pred' ? 24 : role === 'para' ? 30 : role === 'scav' ? 70 : 40;
      place(g, nm === 'Leviathan' ? 8 : n);
      if (role === 'photo') place(g, n); // a second patch of producers elsewhere
    }
    for (let k = 0; k < nFounders; k++) {
      const g = new Float32Array(P.G);
      for (let q = 0; q < P.G; q++) g[q] = P.rand() * 0.7;
      g[GI_.HUE] = P.rand();
      const diet = [GI_.PHOTO, GI_.CHEMO, GI_.PRED, GI_.SCAV, GI_.PARASITE];
      for (const d of diet) g[d] *= 0.3;
      const main = P.rand() < 0.55 ? P.pick([GI_.PHOTO, GI_.CHEMO]) : P.pick(diet);
      g[main] = 0.75 + P.rand() * 0.25;
      g[GI_.TEMP_OPT] = 0.3 + P.rand() * 0.4; g[GI_.TEMP_TOL] = 0.25 + P.rand() * 0.4;
      g[GI_.TOXIN] *= P.rand() < 0.2 ? 1 : 0.1; g[GI_.ADHESION] *= P.rand() < 0.3 ? 1.4 : 0.6; g[GI_.GLOW] *= P.rand() < 0.25 ? 1.3 : 0.2;
      const role = P.roleOf(g);
      place(g, role === 'photo' || role === 'chemo' ? 50 : 22);
    }
    this.buildGrid();
    this.census();
  }
};
