'use strict';
// Shared namespace, constants, fast RNG, periodic noise, colour and naming helpers.
const P = (window.P = {});

P.WORLD = 4096;          // world is a torus WORLD x WORLD units
P.FW = 128;              // environment field resolution (texels per side)
P.FT = P.WORLD / P.FW;   // world units per field texel (32)
P.GRID = 32;             // spatial hash cell size (>= 2 * max cell radius)
P.GW = P.WORLD / P.GRID; // spatial hash cells per side
P.FM = P.FW - 1;         // masks: coordinates that round up to WORLD in float32 wrap to 0
P.GM = P.GW - 1;
P.MAXC = 16384;          // cell pool capacity
P.MAXP = 12000;          // detritus pellet capacity
P.TAU = Math.PI * 2;

// ---------- xorshift32 RNG (deterministic per seed, no allocation) ----------
let _s = 0x9e3779b9 >>> 0;
P.seed = (s) => { _s = (s >>> 0) || 1; };
P.rand = () => { _s ^= _s << 13; _s ^= _s >>> 17; _s ^= _s << 5; return (_s >>> 0) / 4294967296; };
P.randn = () => (P.rand() + P.rand() + P.rand() + P.rand() - 2) * 1.7320508;
P.randi = (n) => (P.rand() * n) | 0;
P.pick = (a) => a[(P.rand() * a.length) | 0];
P.clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
P.lerp = (a, b, t) => a + (b - a) * t;
P.smooth = (t) => t * t * (3 - 2 * t);

// wrapped delta on the torus
const HW = P.WORLD / 2;
P.wrapD = (d) => (d > HW ? d - P.WORLD : d < -HW ? d + P.WORLD : d);
P.wrapP = (v) => (v < 0 ? v + P.WORLD : v >= P.WORLD ? v - P.WORLD : v);

// ---------- periodic value noise / fbm, used to build static maps ----------
P.makeNoise = (period, seed) => {
  const save = _s; P.seed(seed);
  const lat = new Float32Array(period * period);
  for (let i = 0; i < lat.length; i++) lat[i] = P.rand();
  _s = save;
  return (u, v) => { // u,v in [0,1) torus coordinates
    const x = u * period, y = v * period;
    let xi = Math.floor(x), yi = Math.floor(y);
    const fx = P.smooth(x - xi), fy = P.smooth(y - yi);
    xi = ((xi % period) + period) % period; yi = ((yi % period) + period) % period;
    const x1 = (xi + 1) % period, y1 = (yi + 1) % period;
    const a = lat[yi * period + xi], b = lat[yi * period + x1];
    const c = lat[y1 * period + xi], d = lat[y1 * period + x1];
    return P.lerp(P.lerp(a, b, fx), P.lerp(c, d, fx), fy);
  };
};
P.makeFbm = (base, octaves, seed) => {
  const ns = [];
  for (let o = 0; o < octaves; o++) ns.push(P.makeNoise(base << o, seed + o * 977));
  return (u, v) => {
    let s = 0, a = 0.5, n = 0;
    for (let o = 0; o < octaves; o++) { s += ns[o](u, v) * a; n += a; a *= 0.5; }
    return s / n;
  };
};

// ---------- colour ----------
P.hsl = (h, s, l) => { // h 0..1 -> [r,g,b] 0..1
  h = ((h % 1) + 1) % 1;
  const k = (n) => (n + h * 12) % 12;
  const a = s * Math.min(l, 1 - l);
  const f = (n) => l - a * Math.max(-1, Math.min(k(n) - 3, Math.min(9 - k(n), 1)));
  return [f(0), f(8), f(4)];
};
P.css = (rgb, a = 1) => `rgba(${(rgb[0] * 255) | 0},${(rgb[1] * 255) | 0},${(rgb[2] * 255) | 0},${a})`;
P.hueCss = (h, s = 0.7, l = 0.6, a = 1) => P.css(P.hsl(h, s, l), a);

// ---------- binomial-style names for species ----------
const GEN_A = ['Pro', 'Cyan', 'Chlor', 'Rhod', 'Vort', 'Stent', 'Amoeb', 'Volv', 'Parame', 'Dinof', 'Acanth', 'Physa', 'Myxo', 'Spiro', 'Noct', 'Hydr', 'Peri', 'Gymn', 'Thal', 'Bdell', 'Lepto', 'Coel', 'Eugl', 'Trich', 'Halo', 'Pyro', 'Synur', 'Oxy', 'Arcel', 'Diffl', 'Coleps', 'Lacry'];
const GEN_B = ['ella', 'ium', 'ococcus', 'omonas', 'icella', 'ophora', 'ox', 'ina', 'ula', 'aster', 'odinium', 'obacter', 'ispora', 'onema', 'yxa', 'idium', 'ozoa', 'ocystis', 'aria', 'ion'];
const EPI_A = ['aur', 'rub', 'vir', 'luc', 'nig', 'vor', 'ten', 'mag', 'parv', 'cel', 'fer', 'noct', 'gel', 'ign', 'tox', 'mir', 'pall', 'ferv', 'leth', 'amb', 'sol', 'umbr', 'cry', 'therm', 'pel', 'graci', 'rapt', 'spin', 'glob', 'fil'];
const EPI_B = ['ea', 'ens', 'ida', 'ax', 'ata', 'ica', 'ifera', 'osa', 'alis', 'ina', 'ix', 'ophila', 'ivora', 'escens', 'issima'];
P.genusName = () => P.pick(GEN_A) + P.pick(GEN_B);
P.epithet = () => P.pick(EPI_A) + P.pick(EPI_B);

P.fmt = (n) => (n >= 1e6 ? (n / 1e6).toFixed(1) + 'M' : n >= 1e4 ? (n / 1e3).toFixed(1) + 'k' : n >= 1000 ? (n / 1e3).toFixed(2) + 'k' : String(Math.round(n)));
P.escape = (s) => String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
