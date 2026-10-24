'use strict';
// Natural events and player-triggered catastrophes. Each takes the world and a focus point.

const ringRock = (env, x, y, R, w) => {
  env.paint(x, y, R + w, (i) => {
    const tx = i % P.FW, ty = (i / P.FW) | 0;
    const dx = P.wrapD((tx + 0.5) * P.FT - x), dy = P.wrapD((ty + 0.5) * P.FT - y), d = Math.hypot(dx, dy);
    if (Math.abs(d - R) < w) env.wall[i] = Math.max(env.wall[i], 0.75 * (1 - Math.abs(d - R) / w) + 0.2 * P.rand());
    else if (d < R - w) env.wall[i] *= 0.2;
  });
  env.recomputeTemp();
};

P.EVENTS = [
  {
    key: 'meteor', label: 'Meteor strike', desc: 'Crater, shockwave, iron-rich dust',
    run(c, x, y) {
      const n = c.sim.killRadius(x, y, 150, 7);
      ringRock(c.env, x, y, 170, 40);
      c.env.paint(x, y, 420, (i, f) => { c.env.addAt(i, 0, 2.2 * f); c.env.heat[i] += 18 * f; });
      c.sim.feed(x, y, 260, 160);
      c.sim.addFx(x, y, 520, 160, 0.07); c.sim.addFx(x, y, 260, 90, 0.12);
      c.env.recomputeTemp();
      c.log(`A meteor struck the sea, wiping out ${n} cells and scattering minerals.`, 'event');
    },
  },
  {
    key: 'vent', label: 'Volcanic vent', desc: 'New hot spring full of nutrients',
    run(c, x, y) {
      c.env.addVent(x, y, 0.8 + P.rand() * 0.6, c.env.dayLen * (6 + P.rand() * 10));
      ringRock(c.env, x, y, 70, 22);
      c.sim.addFx(x, y, 220, 120, 0.06);
      c.log('The seabed cracked open: a new hydrothermal vent is pouring heat and minerals into the water.', 'event');
    },
  },
  {
    key: 'bloom', label: 'Nutrient upwelling', desc: 'Fertilises the whole sea',
    run(c) {
      for (let i = 0; i < P.FW * P.FW; i++) c.env.addAt(i, 0, 0.6 + c.env.fert[i] * 2.5);
      c.log('Cold, nutrient-rich water is welling up from the deep. Expect a bloom.', 'event');
    },
  },
  {
    key: 'ice', label: 'Ice age', desc: '−10 °C for three days',
    run(c) {
      c.env.transients.push({ dT: -10, start: c.env.tick, until: c.env.tick + c.env.dayLen * 3 });
      c.log('An ice age has begun. The sea will cool by 10 °C over the next three days.', 'climate');
    },
  },
  {
    key: 'heat', label: 'Heat wave', desc: '+9 °C for two days',
    run(c) {
      c.env.transients.push({ dT: 9, start: c.env.tick, until: c.env.tick + c.env.dayLen * 2 });
      c.log('A heat wave is coming. The water will warm by 9 °C for two days.', 'climate');
    },
  },
  {
    key: 'spill', label: 'Toxic spill', desc: 'Poison cloud spreading on the current',
    run(c, x, y) {
      c.env.paint(x, y, 320, (i, f) => c.env.addAt(i, 1, 1.6 * f));
      c.sim.addFx(x, y, 320, 120, 0.85);
      c.log('Toxic brine is seeping into the water. Cells without detox genes are in trouble.', 'event');
    },
  },
  {
    key: 'plague', label: 'Plague', desc: 'A virus hits the most common species',
    run(c) {
      const S = P.species;
      let best = null;
      for (const s of S) if (s.pop > 0 && (!best || s.pop > best.pop)) best = s;
      if (!best) return;
      const sim = c.sim;
      let seedCell = -1, n = 0;
      for (let i = 0; i < sim.hi; i++) if (sim.alive[i] && sim.sp[i] === best.id) { seedCell = i; break; }
      if (seedCell < 0) return;
      const sid = sim.makeStrain(seedCell, 'Plague of ' + best.genus);
      if (sid < 0) return;
      for (let i = 0; i < sim.hi; i++) if (sim.alive[i] && sim.sp[i] === best.id && P.rand() < 0.12 && sim.infectable(i, sid)) { sim.infect[i] = sid + 1; sim.infT[i] = 0; n++; }
      sim.strains[sid].infected = n;
      c.log(`A virus is sweeping through <b style="color:${best.color}">${P.escape(best.name)}</b>, the most common species (${n} infected).`, 'event', best.id);
    },
  },
  {
    key: 'cambrian', label: 'Cambrian burst', desc: 'Mutagens everywhere for a while',
    run(c) {
      for (let k = 0; k < 40; k++) { const x = P.rand() * P.WORLD, y = P.rand() * P.WORLD; c.env.paint(x, y, 260, (i, f) => c.env.addAt(i, 3, 0.9 * f)); }
      c.log('Radiation and mutagens flood the sea. Offspring will vary wildly for a while.', 'event');
    },
  },
  {
    key: 'quake', label: 'Earthquake', desc: 'Moves vents, raises ridges, shifts currents',
    run(c) {
      const env = c.env;
      for (const v of env.vents) { v.x = P.wrapP(v.x + (P.rand() - 0.5) * 900); v.y = P.wrapP(v.y + (P.rand() - 0.5) * 900); }
      for (let k = 0; k < 3; k++) {
        let x = P.rand() * P.WORLD, y = P.rand() * P.WORLD, a = P.rand() * P.TAU;
        for (let s = 0; s < 14; s++) { env.paint(x, y, 50, (i, f) => (env.wall[i] = Math.max(env.wall[i], f))); x += Math.cos(a) * 40; y += Math.sin(a) * 40; a += (P.rand() - 0.5) * 0.6; }
      }
      for (const m of env.modes) { m.p1 += P.rand() * 3; m.p2 += P.rand() * 3; m.A = 0.25 + P.rand() * 0.4; }
      env.updateUniforms(); env.recomputeTemp();
      c.log('An earthquake reshaped the seabed. Vents moved, new ridges rose and the currents changed course.', 'event');
    },
  },
  {
    key: 'calm', label: 'Calm waters', desc: 'Clears whirlpools and toxins',
    run(c) {
      c.env.vortices.length = 0;
      for (let i = 0; i < P.FW * P.FW; i++) { const v = c.env.field[i * 4 + 1]; if (v > 0) c.env.addAt(i, 1, -v); }
      c.env.updateUniforms();
      c.log('The water settled. Whirlpools faded and toxins dispersed.', 'event');
    },
  },
];
P.EVENT_BY_KEY = {};
P.EVENTS.forEach((e) => (P.EVENT_BY_KEY[e.key] = e));

// Natural events, weighted, chosen when the environment's timer fires.
P.randomEvent = (c) => {
  let dom = 0;
  for (const s of P.species) if (s.pop > dom) dom = s.pop;
  const pool = [['vent', 3], ['bloom', 3], ['ice', 2], ['heat', 2], ['quake', 2], ['spill', 1], ['meteor', 1], ['cambrian', 1]];
  if (dom > c.sim.count * 0.22) pool.push(['plague', 5]);
  let tot = 0;
  for (const p of pool) tot += p[1];
  let r = P.rand() * tot, key = pool[0][0];
  for (const p of pool) { if ((r -= p[1]) < 0) { key = p[0]; break; } }
  P.EVENT_BY_KEY[key].run(c, P.rand() * P.WORLD, P.rand() * P.WORLD);
};
