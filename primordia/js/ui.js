'use strict';
// Interface: tools and pointer input, panels, charts, phylogeny, Lab designer, inspector, minimap and the chronicle.

const $ = (id) => document.getElementById(id);
const h = (html) => { const t = document.createElement('template'); t.innerHTML = html.trim(); return t.content.firstElementChild; };
const MOODS = ['Roaming', 'Hunting', 'Fleeing', 'Foraging', 'Schooling'];
const ROLE_ORDER = ['photo', 'chemo', 'omni', 'scav', 'para', 'pred'];
const TRAIT_LINES = [['size', 'Size', '#ffb547', (v) => (v - 3) / 11], ['motor', 'Flagella', '#6fb8ff', (v) => v], ['armor', 'Cell wall', '#b9a58c', (v) => v], ['adh', 'Adhesion', '#7fd38a', (v) => v], ['tox', 'Toxins', '#d36bd0', (v) => v], ['glow', 'Glow', '#7ff0e6', (v) => v]];

const TOOLS = [
  { key: 'inspect', label: 'Inspect', hot: 'i', icon: '<circle cx="10.5" cy="10.5" r="6"/><path d="M15 15l5 5"/>', hint: '<b>Click</b> a cell to inspect it. <b>Drag</b> to pan.' },
  null,
  { key: 'feed', label: 'Feed', hot: 'f', icon: '<circle cx="7" cy="8" r="2"/><circle cx="15.5" cy="6" r="1.5"/><circle cx="12" cy="13" r="2.5"/><circle cx="6.5" cy="17" r="1.5"/><circle cx="17" cy="16.5" r="2"/>', hint: '<b>Paint</b> food: detritus for scavengers and dissolved nutrients for chemotrophs.' },
  { key: 'toxin', label: 'Toxin', hot: 't', icon: '<path d="M9 3h6M10 3v6l-5 9a2 2 0 0 0 2 3h10a2 2 0 0 0 2-3l-5-9V3"/><path d="M7.5 15h9"/>', hint: '<b>Paint</b> poison. Cells without detox genes sicken and die.' },
  { key: 'heat', label: 'Heat', hot: 'h', icon: '<path d="M12 3c1 4 5 5 5 10a5 5 0 0 1-10 0c0-3 2-4 2-7 1.5 1 2 2 3 4 1-2 0-5 0-7z"/>', hint: '<b>Paint</b> warmth. Fades slowly back to the climate.' },
  { key: 'cool', label: 'Cool', hot: 'c', icon: '<path d="M12 2v20M3.5 7l17 10M20.5 7l-17 10M9 4l3 2 3-2M9 20l3-2 3 2"/>', hint: '<b>Paint</b> cold water. Fades slowly back to the climate.' },
  { key: 'light', label: 'Sunlight', hot: 'l', icon: '<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M2 12h2M20 12h2M5 5l1.5 1.5M17.5 17.5L19 19M5 19l1.5-1.5M17.5 6.5L19 5"/>', hint: '<b>Paint</b> brighter light. Photosynthesisers thrive.' },
  { key: 'shade', label: 'Shade', hot: 's', icon: '<path d="M7 18h10a4 4 0 0 0 0-8 6 6 0 0 0-11.5 1.5A3.3 3.3 0 0 0 7 18z"/>', hint: '<b>Paint</b> shade. Starves photosynthesisers.' },
  { key: 'current', label: 'Whirlpool', hot: 'v', icon: '<path d="M21 12a9 9 0 1 1-9-9M17 12a5 5 0 1 1-5-5M13 12a1 1 0 1 1-1-1"/>', hint: '<b>Click</b> for a whirlpool. <b>Drag in a circle</b> to set its spin.' },
  { key: 'rock', label: 'Rock', hot: 'r', icon: '<path d="M3 19l4-8 4 3 4-8 6 13z"/>', hint: '<b>Paint</b> rock: walls, reefs and isolated basins.' },
  { key: 'erode', label: 'Erode', hot: 'e', icon: '<path d="M4 16c3-1 5 1 8 0s5-1 8 0M4 12c3-1 5 1 8 0s5-1 8 0M7 8c2-1 3 1 5 0s3-1 5 0"/>', hint: '<b>Paint</b> to wear rock away.' },
  { key: 'mutagen', label: 'Mutagen', hot: 'm', icon: '<path d="M7 3c0 6 10 6 10 12s-10 6-10 6M17 3c0 6-10 6-10 12s10 6 10 6M8.5 7h7M8.5 17h7M10 12h4"/>', hint: '<b>Paint</b> mutagen. Offspring born here mutate much faster.' },
  null,
  { key: 'zap', label: 'Zap', hot: 'k', icon: '<path d="M13 2L4 14h7l-1 8 9-12h-7z"/>', hint: '<b>Paint</b> to destroy every cell under the brush.' },
  { key: 'virus', label: 'Virus', hot: 'x', icon: '<circle cx="12" cy="12" r="5"/><path d="M12 2v5M12 17v5M2 12h5M17 12h5M5 5l3.5 3.5M15.5 15.5L19 19M5 19l3.5-3.5M15.5 8.5L19 5"/>', hint: '<b>Click</b> a cell to engineer a phage that infects its close relatives.' },
  { key: 'grab', label: 'Tweezers', hot: 'g', icon: '<path d="M6 3l5 12v6M18 3l-5 12v6"/>', hint: '<b>Drag</b> to pick up cells and move them.' },
  { key: 'spawn', label: 'Spawn design', hot: 'p', icon: '<circle cx="12" cy="12" r="8"/><path d="M12 8v8M8 12h8"/>', hint: '<b>Click</b> to release your Lab design here.' },
];

P.UI = class {
  constructor(app) {
    this.app = app;
    this.tool = 'inspect'; this.brush = 110; this.power = 0.5;
    this.tab = 'eco'; this.selSp = -1; this.logs = [];
    this.pointers = new Map(); this.lastUpd = {};
    this.labGenome = P.genomeFrom(P.PRESETS['Hunter']); this.labGenome[P.GI.HUE] = 0.95; this.labSpecies = null;
    this.fpsS = 60;
    this.buildTools(); this.buildPanels(); this.buildLab(); this.bindInput(); this.bindKeys();
    P.log = (msg, kind, spId) => this.log(msg, kind, spId);
    let seen = false;
    try { seen = localStorage.getItem('primordia.seen') === '1'; } catch (e) { /* storage unavailable */ }
    if (!seen) $('help').hidden = false;
  }

  // ---------------- chronicle ----------------
  log(msg, kind = 'event', spId = -1) {
    const env = this.app.env;
    const stamp = `Y${env.year} D${env.day}`;
    this.logs.unshift({ msg, kind, spId, stamp });
    if (this.logs.length > 300) this.logs.pop();
    const feed = $('feed');
    const t = h(`<div class="toast glass ${kind}">${msg}</div>`);
    feed.prepend(t);
    while (feed.children.length > 4) feed.lastChild.remove();
    setTimeout(() => { t.classList.add('fade'); setTimeout(() => t.remove(), 700); }, 7000);
    this.logDirty = true;
  }

  // ---------------- tools ----------------
  buildTools() {
    const nav = $('tools');
    for (const t of TOOLS) {
      if (!t) { nav.appendChild(h('<div class="tsep"></div>')); continue; }
      const b = h(`<button class="tool" data-tool="${t.key}" aria-label="${t.label}"><svg viewBox="0 0 24 24">${t.icon}</svg><span class="tip">${t.label}<kbd>${t.hot.toUpperCase()}</kbd></span></button>`);
      b.onclick = () => this.setTool(t.key);
      nav.appendChild(b);
    }
    const bs = $('brushSize'), bp = $('brushPow');
    bs.oninput = () => { this.brush = +bs.value; $('brushSizeOut').textContent = bs.value; };
    bp.oninput = () => { this.power = +bp.value / 100; $('brushPowOut').textContent = bp.value; };
    this.setTool('inspect');
  }
  setTool(k) {
    this.tool = k;
    document.querySelectorAll('.tool').forEach((b) => b.classList.toggle('on', b.dataset.tool === k));
    const t = TOOLS.find((x) => x && x.key === k);
    $('toolHint').innerHTML = `<b>${t.label}.</b> ${t.hint.replace(/^<b>[^<]*<\/b>/, (m) => m)}`;
    $('view').style.cursor = k === 'inspect' ? 'default' : k === 'grab' ? 'grab' : 'crosshair';
  }
  brushed() { return !['inspect', 'virus', 'current'].includes(this.tool); }

  bindInput() {
    const cv = $('view'), ren = this.app.ren;
    cv.addEventListener('contextmenu', (e) => e.preventDefault());
    cv.addEventListener('wheel', (e) => {
      e.preventDefault();
      const f = Math.exp(-e.deltaY * (e.deltaMode === 1 ? 0.05 : 0.0015));
      this.zoomAt(e.offsetX, e.offsetY, f);
    }, { passive: false });
    cv.addEventListener('pointerdown', (e) => {
      cv.setPointerCapture(e.pointerId);
      this.pointers.set(e.pointerId, { x: e.offsetX, y: e.offsetY });
      if (this.pointers.size === 2) { this.drag = null; this.pinch = this.pinchState(); return; }
      const pan = e.button === 1 || e.button === 2;
      this.drag = { id: e.pointerId, x0: e.offsetX, y0: e.offsetY, x: e.offsetX, y: e.offsetY, pan, moved: false, frames: 0, curl: 0, last: null, shift: e.shiftKey };
      if (!pan) this.toolDown(e.offsetX, e.offsetY);
    });
    cv.addEventListener('pointermove', (e) => {
      this.mouse = { x: e.offsetX, y: e.offsetY };
      const p = this.pointers.get(e.pointerId);
      if (p) { p.x = e.offsetX; p.y = e.offsetY; }
      if (this.pinch && this.pointers.size === 2) { this.pinchMove(); return; }
      const d = this.drag;
      if (!d || d.id !== e.pointerId) return;
      const dx = e.offsetX - d.x, dy = e.offsetY - d.y;
      if (Math.hypot(e.offsetX - d.x0, e.offsetY - d.y0) > 4) d.moved = true;
      if (d.pan || (this.tool === 'inspect' && d.moved)) {
        const z = ren.cam.zoom / ren.dpr;
        ren.cam.x = P.wrapP(ren.cam.x - dx / z); ren.cam.y = P.wrapP(ren.cam.y - dy / z);
        if (this.tool === 'inspect' && !d.pan) this.app.follow = 0;
      }
      if (this.tool === 'current' && d.last) d.curl += (d.last[0]) * dy - (d.last[1]) * dx;
      d.last = [dx, dy];
      d.x = e.offsetX; d.y = e.offsetY;
    });
    const up = (e) => {
      this.pointers.delete(e.pointerId);
      if (this.pointers.size < 2) this.pinch = null;
      const d = this.drag;
      if (!d || d.id !== e.pointerId) return;
      if (!d.pan) this.toolUp(d);
      this.drag = null;
    };
    cv.addEventListener('pointerup', up); cv.addEventListener('pointercancel', up);
    cv.addEventListener('pointerleave', () => { this.mouse = null; });
    $('minimap').addEventListener('pointerdown', (e) => {
      const r = e.target.getBoundingClientRect();
      ren.cam.x = ((e.clientX - r.left) / r.width) * P.WORLD; ren.cam.y = ((e.clientY - r.top) / r.height) * P.WORLD; this.app.follow = 0;
    });
  }
  pinchState() {
    const [a, b] = [...this.pointers.values()];
    return { d: Math.hypot(a.x - b.x, a.y - b.y), cx: (a.x + b.x) / 2, cy: (a.y + b.y) / 2 };
  }
  pinchMove() {
    const ren = this.app.ren, s = this.pinchState(), p = this.pinch;
    const z = ren.cam.zoom / ren.dpr;
    ren.cam.x = P.wrapP(ren.cam.x - (s.cx - p.cx) / z); ren.cam.y = P.wrapP(ren.cam.y - (s.cy - p.cy) / z);
    if (p.d > 10) this.zoomAt(s.cx, s.cy, s.d / p.d);
    this.pinch = s;
  }
  zoomAt(sx, sy, f) {
    const ren = this.app.ren, [wx, wy] = ren.toWorld(sx, sy);
    ren.cam.zoom *= f; ren.clampZoom();
    const [wx2, wy2] = ren.toWorld(sx, sy);
    ren.cam.x = P.wrapP(ren.cam.x + P.wrapD(wx - wx2)); ren.cam.y = P.wrapP(ren.cam.y + P.wrapD(wy - wy2));
  }
  pickCell(sx, sy, slop = 6) {
    const ren = this.app.ren, sim = this.app.sim, [wx, wy] = ren.toWorld(sx, sy);
    const z = ren.cam.zoom / ren.dpr;
    let best = -1, bd = 1e9;
    sim.forNear(wx, wy, 14 + slop / z, (j, dx, dy) => { const d = Math.hypot(dx, dy) - sim.r[j]; if (d < bd) { bd = d; best = j; } });
    return bd < slop / z + 2 ? best : -1;
  }

  toolDown(sx, sy) {
    const app = this.app, sim = app.sim, [wx, wy] = app.ren.toWorld(sx, sy);
    if (this.tool === 'grab') {
      this.grabbed = [];
      sim.forNear(wx, wy, this.brush * 0.5, (j, dx, dy) => this.grabbed.push([j, sim.uid[j], dx, dy]));
      $('view').style.cursor = 'grabbing';
    }
    if (this.tool === 'virus') {
      const res = sim.releaseVirus(wx, wy);
      if (res) {
        sim.addFx(wx, wy, 90, 70, 0.8);
        this.log(`You released <b>${P.escape(res.strain.name)}</b> against <b style="color:${res.sp.color}">${P.escape(res.sp.name)}</b>. ${res.n} cells infected. Mutants may escape it.`, 'event', res.sp.id);
      } else this.log('No cell there to build a virus against. Click right on a cell.', 'event');
    }
  }
  toolUp(d) {
    const app = this.app;
    if (this.tool === 'inspect' && !d.moved) {
      const i = this.pickCell(d.x, d.y, 10);
      this.select(i);
    }
    if (this.tool === 'current') {
      const [wx, wy] = app.ren.toWorld(d.x0, d.y0);
      const sgn = d.curl < 0 ? -1 : 1;
      app.env.addVortex(wx, wy, sgn * (0.6 + this.power * 2.2), 140 + this.brush * 0.9);
      app.env.updateUniforms();
      app.sim.addFx(wx, wy, 140 + this.brush * 0.9, 60, 0.55);
    }
    if (this.tool === 'grab') { this.grabbed = null; $('view').style.cursor = 'grab'; }
  }
  select(i) {
    const app = this.app, sim = app.sim;
    if (i >= 0) {
      app.ren.selected = i; app.ren.selUid = sim.uid[i];
      this.showTab('cell');
    } else { app.ren.selected = -1; app.follow = 0; }
    this.renderCell(true);
  }
  // continuous brush effects, once per frame while the pointer is held
  applyTool() {
    const d = this.drag;
    if (!d || d.pan || this.pinch) return;
    d.frames++;
    const app = this.app, env = app.env, sim = app.sim, [wx, wy] = app.ren.toWorld(d.x, d.y), R = this.brush * 0.5, pw = this.power;
    switch (this.tool) {
      case 'feed': sim.feed(wx, wy, R, Math.ceil(pw * 3)); env.paint(wx, wy, R, (i, f) => env.addAt(i, 0, 0.06 * pw * f)); break;
      case 'toxin': env.paint(wx, wy, R, (i, f) => env.addAt(i, 1, 0.06 * pw * f)); break;
      case 'mutagen': env.paint(wx, wy, R, (i, f) => env.addAt(i, 3, 0.05 * pw * f)); break;
      case 'heat': case 'cool': {
        const s = this.tool === 'heat' ? 1 : -1;
        env.paint(wx, wy, R, (i, f) => { env.heat[i] = P.clamp(env.heat[i] + s * 0.7 * pw * f, -40, 40); });
        if (d.frames % 4 === 1) env.recomputeTemp();
        break;
      }
      case 'light': case 'shade': {
        const s = this.tool === 'light' ? 1 : -1;
        env.paint(wx, wy, R, (i, f) => { env.shade[i] = P.clamp(env.shade[i] + s * 0.05 * pw * f, -0.95, 1.5); });
        if (d.frames % 3 === 1) env.recomputeLight();
        break;
      }
      case 'rock': case 'erode': {
        const s = this.tool === 'rock' ? 1 : -1;
        env.paint(wx, wy, R, (i, f) => { env.wall[i] = P.clamp(env.wall[i] + s * 0.09 * pw * f, 0, 1); env.envTex[i * 4 + 2] = env.wall[i]; });
        env.envDirty = true;
        break;
      }
      case 'zap': {
        const n = sim.killRadius(wx, wy, R, 6);
        if (n || d.frames % 6 === 1) sim.addFx(wx, wy, R, 18, 0.15);
        break;
      }
      case 'grab':
        if (this.grabbed) for (const [j, uid, ox, oy] of this.grabbed) {
          if (!sim.alive[j] || sim.uid[j] !== uid) continue;
          sim.x[j] = P.wrapP(wx + ox); sim.y[j] = P.wrapP(wy + oy); sim.vx[j] = 0; sim.vy[j] = 0;
        }
        break;
      case 'spawn':
        if (d.frames === 1 || (d.moved && d.frames % 8 === 0)) this.spawnLab(wx, wy);
        break;
    }
  }
  spawnLab(wx, wy) {
    const app = this.app, sim = app.sim, name = $('labName').value.trim() || 'Synthetica';
    if (!this.labSpecies || this.labSpecies.name !== name) {
      this.labSpecies = P.newSpecies(this.labGenome, -1, sim.tick, name, true);
      this.log(`You synthesised <b style="color:${this.labSpecies.color}">${P.escape(name)}</b> and released it into the sea.`, 'species', this.labSpecies.id);
    }
    const n = +$('labCount').value, R = 10 + Math.sqrt(n) * 12;
    let placed = 0;
    for (let k = 0; k < n; k++) {
      const a = P.rand() * P.TAU, d = Math.sqrt(P.rand()) * R;
      const g = Float32Array.from(this.labGenome);
      if (sim.spawn(g, wx + Math.cos(a) * d, wy + Math.sin(a) * d, this.labSpecies.id, 0.7) >= 0) placed++;
    }
    sim.addFx(wx, wy, R + 20, 40, this.labGenome[P.GI.HUE]);
    if (!placed) this.log('The sea is full: no room for new cells. Raise the population cap in Climate.', 'event');
  }

  bindKeys() {
    const app = this.app;
    this.keys = {};
    window.addEventListener('keydown', (e) => {
      if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA') return;
      const k = e.key.toLowerCase();
      this.keys[k] = true;
      if (k === ' ') { e.preventDefault(); app.togglePause(); return; }
      if (k >= '1' && k <= '5') { app.setSpeed([1, 2, 4, 8, 16][+k - 1]); return; }
      if (k === '?' || (k === '/' && e.shiftKey)) { $('help').hidden = !$('help').hidden; return; }
      if (k === 'escape') { $('help').hidden = true; this.select(-1); return; }
      if (k === '[') { $('brushSize').value = Math.max(16, this.brush - 20); $('brushSize').oninput(); return; }
      if (k === ']') { $('brushSize').value = Math.min(480, this.brush + 20); $('brushSize').oninput(); return; }
      if (k === 'tab') { e.preventDefault(); const tabs = ['eco', 'species', 'tree', 'climate', 'lab', 'cell']; this.showTab(tabs[(tabs.indexOf(this.tab) + (e.shiftKey ? 5 : 1)) % 6]); return; }
      if (k === 'o') { app.ren.overlay = (app.ren.overlay + 1) % P.OVERLAYS.length; $('selOverlay').value = app.ren.overlay; return; }
      if (k === 'j') { if (app.ren.selected >= 0) app.follow = app.ren.selUid; return; }
      if (k === '+' || k === '=') { this.zoomAt(innerWidth / 2, innerHeight / 2, 1.25); return; }
      if (k === '-') { this.zoomAt(innerWidth / 2, innerHeight / 2, 0.8); return; }
      if ('wasd'.includes(k) && !e.ctrlKey && !e.metaKey && k !== 's') return; // movement keys handled per frame (S is also Shade)
      const t = TOOLS.find((x) => x && x.hot === k);
      if (t && !e.ctrlKey && !e.metaKey) this.setTool(t.key);
    });
    window.addEventListener('keyup', (e) => { this.keys[e.key.toLowerCase()] = false; });
    window.addEventListener('blur', () => (this.keys = {}));
  }
  panKeys(dt) {
    const k = this.keys, ren = this.app.ren, sp = (600 * dt) / (ren.cam.zoom / ren.dpr);
    let dx = 0, dy = 0;
    if (k.a || k.arrowleft) dx -= sp; if (k.d || k.arrowright) dx += sp;
    if (k.w || k.arrowup) dy -= sp; if (k.arrowdown) dy += sp;
    if (dx || dy) { ren.cam.x = P.wrapP(ren.cam.x + dx); ren.cam.y = P.wrapP(ren.cam.y + dy); this.app.follow = 0; }
  }

  // ---------------- panels ----------------
  buildPanels() {
    const app = this.app;
    document.querySelectorAll('.tabs button[data-tab]').forEach((b) => (b.onclick = () => this.showTab(b.dataset.tab)));
    $('btnCollapse').onclick = () => { const p = $('panel'); p.classList.toggle('collapsed'); $('btnCollapse').textContent = p.classList.contains('collapsed') ? '▴' : '▾'; };
    $('btnHelp').onclick = () => ($('help').hidden = false);
    $('btnHelpClose').onclick = () => { $('help').hidden = true; try { localStorage.setItem('primordia.seen', '1'); } catch (e) { /* storage unavailable */ } };
    $('btnPause').onclick = () => app.togglePause();
    document.querySelectorAll('.speed button[data-speed]').forEach((b) => (b.onclick = () => app.setSpeed(+b.dataset.speed)));
    const so = $('selOverlay'), sc = $('selColor');
    P.OVERLAYS.forEach((n, i) => so.appendChild(h(`<option value="${i}">${n}</option>`)));
    P.COLOR_MODES.forEach((n, i) => sc.appendChild(h(`<option value="${i}">${n}</option>`)));
    so.onchange = () => (app.ren.overlay = +so.value);
    sc.onchange = () => (app.ren.colorMode = +sc.value);
    // species list controls
    $('spSearch').oninput = () => this.renderSpecies(true);
    $('spSort').onchange = () => this.renderSpecies(true);
    $('spExtinct').onchange = () => this.renderSpecies(true);
    $('spList').onclick = (e) => { const r = e.target.closest('[data-sp]'); if (r) this.selectSpecies(+r.dataset.sp); };
    $('spDetail').onclick = (e) => this.speciesAction(e);
    $('treeMinor').onchange = () => this.renderTree(true);
    $('treeCanvas').onclick = (e) => this.treeClick(e);
    $('treeCanvas').onmousemove = (e) => this.treeHover(e);
    $('treeCanvas').onmouseleave = () => ($('tooltip').style.display = 'none');
    $('cellInfo').onclick = (e) => this.cellAction(e);
    // climate sliders
    const env = app.env, sim = app.sim;
    const sliders = [
      ['Temperature shift', -15, 15, 0.5, () => env.tempOffset, (v) => { env.tempOffset = v; env.recomputeTemp(); }, (v) => (v > 0 ? '+' : '') + v.toFixed(1) + ' °C'],
      ['Sunlight', 0, 200, 5, () => env.sunlight * 100, (v) => { env.sunlight = v / 100; env.recomputeLight(); }, (v) => v + '%'],
      ['Nutrient upwelling', 0, 300, 5, () => env.inflow * 100, (v) => (env.inflow = v / 100), (v) => v + '%'],
      ['Ocean currents', 0, 300, 5, () => env.currents * 100, (v) => { env.currents = v / 100; env.updateUniforms(); }, (v) => v + '%'],
      ['Mutation rate', 0, 500, 5, () => env.mutationMult * 100, (v) => (env.mutationMult = v / 100), (v) => v + '%'],
      ['Population cap', 1000, P.MAXC, 500, () => sim.popCap, (v) => (sim.popCap = v), (v) => P.fmt(v)],
      ['Day length', 10, 120, 5, () => env.dayLen / 60, (v) => { const ph = env.tick / env.dayLen; env.dayLen = v * 60; env.tick = Math.round(ph * env.dayLen); }, (v) => v + ' s'],
    ];
    const box = $('climSliders');
    sliders.forEach(([label, min, max, step, get, set, fmt], k) => {
      const id = 'cs' + k, el = h(`<div class="sl"><label for="${id}">${label}</label><output id="${id}o"></output><input type="range" id="${id}" min="${min}" max="${max}" step="${step}"></div>`);
      box.appendChild(el);
      const inp = el.querySelector('input'), out = el.querySelector('output');
      inp.value = get(); out.textContent = fmt(+inp.value);
      inp.oninput = () => { set(+inp.value); out.textContent = fmt(+inp.value); };
    });
    $('tgDay').onchange = (e) => { env.dayNightOn = e.target.checked; env.recomputeLight(); };
    $('tgSeason').onchange = (e) => (env.seasonsOn = e.target.checked);
    $('tgEvents').onchange = (e) => (env.eventsOn = e.target.checked);
    const cat = $('catas');
    for (const ev of P.EVENTS) {
      const b = h(`<button class="btn"><span>${ev.label}</span><small>${ev.desc}</small></button>`);
      b.onclick = () => { ev.run(app.ctx, app.ren.cam.x, app.ren.cam.y); };
      cat.appendChild(b);
    }
    $('btnReset').onclick = () => ($('btnResetYes').hidden = false);
    $('btnResetYes').onclick = () => app.reset();
    this.buildLegends();
  }
  buildLegends() {
    $('lgTraits').innerHTML = TRAIT_LINES.map((t) => `<span><i style="background:${t[2]}"></i>${t[1]}</span>`).join('');
  }
  showTab(t) {
    this.tab = t;
    document.querySelectorAll('.tabs button[data-tab]').forEach((b) => b.classList.toggle('on', b.dataset.tab === t));
    document.querySelectorAll('.pane').forEach((p) => p.classList.toggle('on', p.dataset.pane === t));
    $('panel').classList.remove('collapsed'); $('btnCollapse').textContent = '▾';
    this.lastUpd.pane = 0;
    if (t === 'tree') this.renderTree(true);
    if (t === 'species') this.renderSpecies(true);
    if (t === 'cell') this.renderCell(true);
    if (t === 'climate') this.logDirty = true;
  }

  // ---------------- per-frame update (throttled per element) ----------------
  update(now, dt) {
    const app = this.app;
    this.panKeys(dt);
    this.fpsS = this.fpsS * 0.95 + (1 / Math.max(dt, 1e-3)) * 0.05;
    const due = (k, ms) => { if (now - (this.lastUpd[k] || 0) >= ms) { this.lastUpd[k] = now; return true; } return false; };
    // cursor ring
    const cur = $('cursor');
    if (this.mouse && this.brushed()) {
      const d = (this.brush * app.ren.cam.zoom) / app.ren.dpr;
      cur.style.display = 'block'; cur.style.width = cur.style.height = d + 'px'; cur.style.left = this.mouse.x + 'px'; cur.style.top = this.mouse.y + 'px';
    } else cur.style.display = 'none';
    if (due('top', 250)) this.renderTop();
    if (due('hover', 120)) this.renderHover();
    if (due('map', 500)) this.renderMinimap();
    if (this.tab === 'lab' && app.preview) app.preview.draw(this.labGenome, now / 1000);
    const paneMs = { eco: 900, species: 1000, tree: 3000, climate: 1000, lab: 1000, cell: 200 }[this.tab];
    if (due('pane', paneMs)) {
      if (this.tab === 'eco') this.renderEco();
      else if (this.tab === 'species') this.renderSpecies();
      else if (this.tab === 'tree') this.renderTree();
      else if (this.tab === 'climate') this.renderClimate();
      else if (this.tab === 'cell') this.renderCell();
    }
  }

  renderTop() {
    const app = this.app, sim = app.sim, env = app.env;
    const ph = env.dayPhase, part = ph < 0.05 || ph > 0.97 ? 'dawn' : ph < 0.2 ? 'morning' : ph < 0.32 ? 'midday' : ph < 0.47 ? 'afternoon' : ph < 0.55 ? 'dusk' : 'night';
    $('clkMain').textContent = `Day ${env.day} · ${env.seasonName}`;
    $('clkSub').textContent = `Year ${env.year} · ${env.dayNightOn ? part : 'constant light'} · water ${(env.meanTemp ?? 20).toFixed(1)} °C`;
    $('stPop').textContent = P.fmt(sim.count);
    $('stSp').textContent = sim.living || 0;
    $('stH').textContent = (sim.diversity || 0).toFixed(2);
    $('stBio').textContent = P.fmt((sim.biomass || 0) / 100);
    $('stFps').textContent = Math.round(this.fpsS);
    $('stMode').textContent = 'FPS · ' + app.field.mode;
    $('btnPause').classList.toggle('on', app.paused);
    $('btnPause').textContent = app.paused ? '▶' : '❚❚';
    document.querySelectorAll('.speed button[data-speed]').forEach((b) => b.classList.toggle('on', !app.paused && +b.dataset.speed === app.speed));
    this.drawSunDial();
    // scale bar
    const z = app.ren.cam.zoom / app.ren.dpr, target = 60 / z, pow = Math.pow(10, Math.floor(Math.log10(target)));
    const nice = [1, 2, 5, 10].map((m) => m * pow).find((v) => v >= target * 0.6) || pow * 10;
    $('scaleBar').style.width = Math.round(nice * z) + 'px';
    $('scaleTxt').textContent = nice >= 1000 ? nice / 1000 + ' mm' : nice + ' µm';
  }
  drawSunDial() {
    const cv = $('sunDial'), c = cv.getContext('2d'), env = this.app.env, W = cv.width;
    c.clearRect(0, 0, W, W);
    const cx = W / 2, cy = W / 2;
    // season ring
    const seasonCol = ['#7fd38a', '#ffb547', '#d9824b', '#8fc4ff'];
    for (let s = 0; s < 4; s++) {
      c.beginPath(); c.arc(cx, cy, 23, -Math.PI / 2 + s * Math.PI / 2 + 0.06, -Math.PI / 2 + (s + 1) * Math.PI / 2 - 0.06);
      c.strokeStyle = seasonCol[s]; c.globalAlpha = Math.floor(env.yearPhase * 4) === s ? 1 : 0.25; c.lineWidth = 3; c.stroke();
    }
    c.globalAlpha = 1;
    const ya = -Math.PI / 2 + env.yearPhase * P.TAU;
    c.beginPath(); c.arc(cx + Math.cos(ya) * 23, cy + Math.sin(ya) * 23, 3, 0, P.TAU); c.fillStyle = '#fff'; c.fill();
    // sun or moon
    const sun = env.sun() / env.seasonLight();
    const g = c.createRadialGradient(cx, cy, 2, cx, cy, 16);
    g.addColorStop(0, sun > 0.3 ? '#fff2c4' : '#c9d6ff'); g.addColorStop(1, sun > 0.3 ? 'rgba(255,181,71,0)' : 'rgba(120,140,220,0)');
    c.fillStyle = g; c.beginPath(); c.arc(cx, cy, 16, 0, P.TAU); c.fill();
    c.fillStyle = sun > 0.3 ? '#ffd27a' : '#dfe6ff'; c.beginPath(); c.arc(cx, cy, 5 + sun * 4, 0, P.TAU); c.fill();
  }
  renderHover() {
    const tip = $('tooltip');
    if (!this.mouse || this.drag || this.tab === 'tree' && this.treeHovering) { if (!this.treeHovering) tip.style.display = 'none'; return; }
    const i = this.pickCell(this.mouse.x, this.mouse.y, 4);
    if (i < 0) { tip.style.display = 'none'; return; }
    const sim = this.app.sim, sp = P.species[sim.sp[i]];
    const st = sim.state[i] ? 'Dormant spore' : sim.host[i] >= 0 ? 'Riding a host' : MOODS[sim.mood[i]];
    tip.innerHTML = `<b style="color:${sp.color}">${P.escape(P.speciesLabel(sp))}</b><br><span class="muted">${P.ROLES[sp.role].label} · ${st}${sim.infect[i] ? ' · infected' : ''}<br>Energy ${Math.round((100 * sim.energy[i]) / sim.maxE[i])}% · gen ${sim.gen[i]}</span>`;
    tip.style.display = 'block';
    const x = Math.min(this.mouse.x + 16, innerWidth - tip.offsetWidth - 8), y = Math.min(this.mouse.y + 16, innerHeight - tip.offsetHeight - 8);
    tip.style.left = x + 'px'; tip.style.top = y + 'px';
  }
  renderMinimap() {
    const cv = $('minimap');
    if (!cv.offsetParent) return;
    const c = cv.getContext('2d'), env = this.app.env, sim = this.app.sim, W = P.FW;
    if (!this.mapImg) this.mapImg = c.createImageData(W, W);
    const d = this.mapImg.data;
    for (let i = 0; i < W * W; i++) {
      const l = env.depth[i], w = env.wall[i];
      let r = 4 + l * 14, g = 14 + l * 34, b = 24 + l * 40;
      const n = env.field[i * 4];
      g += Math.min(30, n * 14); r += Math.min(14, n * 6);
      if (env.envTex[i * 4 + 3] > 1) { r += 90; g += 30; }
      if (w > 0.5) { r = 70; g = 62; b = 54; }
      d[i * 4] = r; d[i * 4 + 1] = g; d[i * 4 + 2] = b; d[i * 4 + 3] = 255;
    }
    for (let i = 0; i < sim.hi; i++) {
      if (!sim.alive[i]) continue;
      const sp = P.species[sim.sp[i]];
      if (!sp.rgb) sp.rgb = P.hsl(sp.hue, 0.8, 0.62).map((v) => (v * 255) | 0);
      const q = (((sim.y[i] / P.FT) & P.FM) * W + ((sim.x[i] / P.FT) & P.FM)) * 4;
      d[q] = sp.rgb[0]; d[q + 1] = sp.rgb[1]; d[q + 2] = sp.rgb[2];
    }
    c.putImageData(this.mapImg, 0, 0);
    // view rectangle (wrapping)
    const ren = this.app.ren, z = ren.cam.zoom, vw = ren.canvas.width / z / P.FT, vh = ren.canvas.height / z / P.FT;
    const x0 = ren.cam.x / P.FT - vw / 2, y0 = ren.cam.y / P.FT - vh / 2;
    c.strokeStyle = 'rgba(255,181,71,0.9)'; c.lineWidth = 1;
    for (const ox of [-W, 0, W]) for (const oy of [-W, 0, W]) c.strokeRect(x0 + ox + 0.5, y0 + oy + 0.5, vw, vh);
  }

  // ---------------- charts ----------------
  ctx2d(cv) {
    const dpr = Math.min(window.devicePixelRatio || 1, 2), w = cv.clientWidth, hgt = cv.clientHeight;
    if (!w) return null;
    if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(hgt * dpr)) { cv.width = Math.round(w * dpr); cv.height = Math.round(hgt * dpr); }
    const c = cv.getContext('2d');
    c.setTransform(dpr, 0, 0, dpr, 0, 0); c.clearRect(0, 0, w, hgt);
    c.font = '10px "JetBrains Mono", monospace'; c.textBaseline = 'middle';
    return { c, w, h: hgt };
  }
  grid(c, w, hgt, max, fmt, padL = 30, padB = 4) {
    c.strokeStyle = 'rgba(134,161,158,0.14)'; c.fillStyle = '#6f8986'; c.lineWidth = 1;
    for (let k = 0; k <= 2; k++) {
      const y = 4 + (hgt - 4 - padB) * (1 - k / 2);
      c.beginPath(); c.moveTo(padL, Math.round(y) + 0.5); c.lineTo(w, Math.round(y) + 0.5); c.stroke();
      c.textAlign = 'right'; c.fillText(fmt((max * k) / 2), padL - 5, y);
    }
  }
  renderEco() {
    const sim = this.app.sim, env = this.app.env, H = sim.history;
    const r = sim.roleCount || {}, total = Math.max(1, sim.count);
    // headline
    let head = 'A young ocean';
    if (H.length > 8) {
      const a = H[H.length - 8], b = H[H.length - 1];
      const dPop = (b.pop - a.pop) / Math.max(1, a.pop), dSp = b.species - a.species;
      const top = ROLE_ORDER.reduce((m, k) => ((r[k] || 0) > (r[m] || 0) ? k : m), 'photo');
      if (dPop < -0.3) head = 'A die-off is under way';
      else if (dSp >= 8) head = 'Life is diversifying fast';
      else if (dPop > 0.35) head = 'Populations are booming';
      else if ((r.pred || 0) / total > 0.25) head = 'Predators are on the rise';
      else if (sim.infected > total * 0.05) head = 'Disease is spreading';
      else head = `${P.ROLES[top].label}s dominate the sea`;
    }
    $('ecoHeadline').textContent = head;
    const extinct = P.species.reduce((n, s) => n + (s.extinct >= 0 ? 1 : 0), 0);
    $('ecoSub').textContent = `${P.fmt(sim.count)} cells in ${sim.living} living species · ${extinct} species extinct · ${env.vents.length} active vent${env.vents.length === 1 ? '' : 's'}`;
    $('ecoPopNow').textContent = 'last ' + Math.max(1, Math.round((H.length * 150) / env.dayLen)) + ' days';
    $('lgRoles').innerHTML = ROLE_ORDER.map((k) => `<span><i style="background:${P.ROLES[k].color}"></i>${P.ROLES[k].label}<b>${P.fmt(r[k] || 0)}</b></span>`).join('');
    // population stacked area
    let g = this.ctx2d($('chPop'));
    if (g && H.length > 1) {
      const { c, w, h: hh } = g, padL = 34, n = H.length;
      let max = 1; for (const s of H) max = Math.max(max, s.pop);
      max = niceMax(max);
      this.grid(c, w, hh, max, P.fmt, padL);
      const X = (k) => padL + ((w - padL) * k) / (n - 1), Y = (v) => 4 + (hh - 8) * (1 - v / max);
      const base = new Float32Array(n);
      for (const role of ROLE_ORDER) {
        c.beginPath();
        for (let k = 0; k < n; k++) c.lineTo(X(k), Y(base[k] + (H[k].roles[role] || 0)));
        for (let k = n - 1; k >= 0; k--) c.lineTo(X(k), Y(base[k]));
        c.closePath(); c.fillStyle = P.ROLES[role].color; c.globalAlpha = 0.78; c.fill(); c.globalAlpha = 1;
        for (let k = 0; k < n; k++) base[k] += H[k].roles[role] || 0;
      }
      c.beginPath(); for (let k = 0; k < n; k++) c.lineTo(X(k), Y(base[k])); c.strokeStyle = '#dcebe6'; c.lineWidth = 1.2; c.stroke();
      c.fillStyle = '#dcebe6'; c.beginPath(); c.arc(X(n - 1), Y(base[n - 1]), 2.5, 0, P.TAU); c.fill();
    }
    // diversity
    g = this.ctx2d($('chDiv'));
    if (g && H.length > 1) {
      const { c, w, h: hh } = g, padL = 34, n = H.length;
      let max = 1, maxH = 0.5; for (const s of H) { max = Math.max(max, s.species); maxH = Math.max(maxH, s.H); }
      max = niceMax(max);
      this.grid(c, w, hh, max, (v) => Math.round(v), padL);
      const X = (k) => padL + ((w - padL) * k) / (n - 1);
      line(c, H, X, (s) => 4 + (hh - 8) * (1 - s.species / max), '#ffb547', 1.6);
      line(c, H, X, (s) => 4 + (hh - 8) * (1 - s.H / (maxH * 1.1)), '#6fb8ff', 1.3);
    }
    // trait drift
    g = this.ctx2d($('chTraits'));
    if (g && H.length > 1) {
      const { c, w, h: hh } = g, padL = 34, n = H.length;
      this.grid(c, w, hh, 1, (v) => Math.round(v * 100) + '%', padL);
      const X = (k) => padL + ((w - padL) * k) / (n - 1);
      for (const [key, , col, norm] of TRAIT_LINES) line(c, H, X, (s) => 4 + (hh - 8) * (1 - P.clamp(norm(s[key]), 0, 1)), col, 1.4);
    }
    // deaths
    const tot = Math.max(1, sim.deathsBy.reduce((a, b) => a + b, 0));
    const deathCol = ['#d2a865', '#ff6a55', '#86a19e', '#ff8a5b', '#d36bd0', '#c58bff', '#ffb547', '#6fb8ff'];
    $('deathBars').innerHTML = P.CAUSES.map((n, k) => `<div class="bar"><span>${n}</span><div class="track"><div class="fill" style="width:${(100 * sim.deathsBy[k]) / tot}%;background:${deathCol[k]}"></div></div><b>${P.fmt(sim.deathsBy[k])}</b></div>`).join('');
    const kv = [['Births', P.fmt(sim.births)], ['Kills', P.fmt(sim.kills)], ['Colony cells', P.fmt(sim.colonial || 0)], ['Spores', P.fmt(sim.spores || 0)], ['Infected', P.fmt(sim.infected || 0)], ['Virus strains', sim.strains.length], ['Detritus', P.fmt(sim.pcount)], ['Species ever', P.species.length]];
    $('ecoKv').innerHTML = kv.map(([a, b]) => `<div><span>${a}</span><b>${b}</b></div>`).join('');
  }

  // ---------------- species ----------------
  renderSpecies(force) {
    const q = $('spSearch').value.trim().toLowerCase(), sort = $('spSort').value, ext = $('spExtinct').checked;
    let list = P.species.filter((s) => (ext ? s.peak > 0 : s.pop > 0) && (!q || s.name.toLowerCase().includes(q) || P.ROLES[s.role].label.toLowerCase().includes(q)));
    const key = { pop: (s) => -s.pop - (s.extinct >= 0 ? 0 : 1e6), new: (s) => -s.born, old: (s) => s.born, peak: (s) => -s.peak }[sort];
    list.sort((a, b) => key(a) - key(b));
    const total = list.length;
    list = list.slice(0, 80);
    const env = this.app.env;
    const sig = list.map((s) => s.id + ':' + s.pop).join(',') + this.selSp;
    if (!force && sig === this.spSig) return;
    this.spSig = sig;
    const el = $('spList');
    el.innerHTML = list.map((s) => `<div class="sprow${s.id === this.selSp ? ' on' : ''}${s.pop ? '' : ' extinct'}" data-sp="${s.id}">
      <span class="swatch" style="color:${s.color}"></span>
      <div class="nm"><b>${P.escape(P.speciesLabel(s))}</b><small>${P.ROLES[s.role].label}${s.colonial ? ' · colonial' : ''} · ${s.pop ? 'since day ' + (1 + Math.floor(s.born / env.dayLen)) : 'extinct'}</small></div>
      <canvas width="128" height="40" data-spark="${s.id}"></canvas>
      <span class="pop">${s.pop ? P.fmt(s.pop) : '–'}</span></div>`).join('') + (total > 80 ? `<div class="empty">${total - 80} more not shown. Search to find them.</div>` : '') + (total ? '' : '<div class="empty">No species match.</div>');
    el.querySelectorAll('canvas[data-spark]').forEach((cv) => spark(cv, P.species[+cv.dataset.spark]));
    this.renderSpeciesDetail();
  }
  selectSpecies(id) {
    this.selSp = id;
    if (this.tab !== 'species') this.showTab('species');
    this.renderSpecies(true);
    $('panel').querySelector('.pbody').scrollTop = 0;
  }
  renderSpeciesDetail() {
    const box = $('spDetail'), sp = P.species[this.selSp];
    if (!sp) { box.innerHTML = ''; return; }
    const env = this.app.env, ren = this.app.ren;
    const anc = [];
    for (let p = sp.parent, k = 0; p >= 0 && k < 6; p = P.species[p].parent, k++) anc.push(P.species[p]);
    let desc = 0; const stack = [...sp.children];
    while (stack.length) { const c = P.species[stack.pop()]; desc++; stack.push(...c.children); }
    const day = (t) => 'day ' + (1 + Math.floor(t / env.dayLen));
    const keyGenes = ['SIZE', 'PHOTO', 'CHEMO', 'PRED', 'SCAV', 'PARASITE', 'MOTOR', 'ARMOR', 'TOXIN', 'ADHESION', 'GLOW', 'SPORE'];
    box.innerHTML = `<div class="card" style="display:grid;gap:10px">
      <div style="display:flex;gap:9px;align-items:center"><span class="swatch" style="color:${sp.color};width:14px;height:14px"></span><h2 style="min-width:0;overflow-wrap:anywhere">${P.escape(P.speciesLabel(sp))}</h2></div>
      <div class="btnrow"><span class="chip" style="color:${P.ROLES[sp.role].color}">${P.ROLES[sp.role].label}</span>${sp.colonial ? '<span class="chip" style="color:#7fd38a">Colonial</span>' : ''}${sp.designed ? '<span class="chip" style="color:var(--amber)">Your design</span>' : ''}${sp.pop ? '' : '<span class="chip" style="color:var(--bad)">Extinct</span>'}</div>
      <div class="kv"><div><span>Cells now</span><b>${P.fmt(sp.pop)}</b></div><div><span>Peak</span><b>${P.fmt(sp.peak)}</b></div>
        <div><span>Appeared</span><b>${day(sp.born)}</b></div><div><span>${sp.extinct >= 0 ? 'Died out' : 'Age'}</span><b>${sp.extinct >= 0 ? day(sp.extinct) : ((env.tick - sp.born) / env.dayLen).toFixed(1) + ' d'}</b></div>
        <div><span>Daughter species</span><b>${desc}</b></div><div><span>Lineage depth</span><b>${sp.depth}</b></div></div>
      <div class="lineage">${anc.length ? 'Descends from ' + anc.map((a) => `<a data-goto="${a.id}" style="color:${a.color}">${P.escape(a.name)}</a>`).join(' ← ') : 'A founding species of this sea.'}</div>
      <canvas class="chart" style="height:48px" id="spBig"></canvas>
      <div class="genes">${keyGenes.map((k) => geneBar(k, sp.genome[P.GI[k]])).join('')}</div>
      <div class="btnrow">
        <button class="btn" data-act="hl">${ren.highlightSp === sp.id ? 'Stop highlighting' : 'Highlight'}</button>
        ${sp.pop ? '<button class="btn" data-act="follow">Follow one</button>' : ''}
        <button class="btn" data-act="lab">Edit in Lab</button>
        ${sp.pop ? '<button class="btn" data-act="virus">Release virus</button><button class="btn danger" data-act="cull">Cull half</button>' : ''}
        <button class="btn" data-act="close">Close</button>
      </div></div>`;
    spark($('spBig'), sp, true);
  }
  speciesAction(e) {
    const app = this.app, sim = app.sim, sp = P.species[this.selSp];
    const go = e.target.closest('[data-goto]');
    if (go) { this.selectSpecies(+go.dataset.goto); return; }
    const b = e.target.closest('[data-act]');
    if (!b || !sp) return;
    const members = () => { const m = []; for (let i = 0; i < sim.hi; i++) if (sim.alive[i] && sim.sp[i] === sp.id) m.push(i); return m; };
    switch (b.dataset.act) {
      case 'hl': app.ren.highlightSp = app.ren.highlightSp === sp.id ? -1 : sp.id; break;
      case 'follow': { const m = members(); if (m.length) { const i = P.pick(m); app.ren.selected = i; app.ren.selUid = sim.uid[i]; app.follow = sim.uid[i]; } break; }
      case 'lab': this.loadLab(sp.genome, sp.name.split(' ')[0] + ' novus'); this.showTab('lab'); return;
      case 'virus': { const m = members(); if (m.length) { const i = P.pick(m); const r = sim.releaseVirus(sim.x[i], sim.y[i]); if (r) this.log(`You released <b>${P.escape(r.strain.name)}</b> into a colony of <b style="color:${sp.color}">${P.escape(sp.name)}</b>. ${r.n} cells infected.`, 'event', sp.id); } break; }
      case 'cull': { let n = 0; for (const i of members()) if (P.rand() < 0.5) { sim.kill(i, 6); n++; } this.log(`You culled ${n} cells of <b style="color:${sp.color}">${P.escape(sp.name)}</b>.`, 'event', sp.id); break; }
      case 'close': this.selSp = -1; break;
    }
    this.renderSpecies(true);
  }

  // ---------------- phylogeny ----------------
  renderTree(force) {
    const cv = $('treeCanvas');
    if (!cv.clientWidth) return;
    const env = this.app.env, now = Math.max(1, env.tick), minor = $('treeMinor').checked;
    const S = P.species, keep = new Uint8Array(S.length);
    for (const s of S) if (s.pop > 0 && (minor || s.peak >= 12 || s.depth === 0) || (s.peak >= (minor ? 3 : 40))) {
      for (let p = s.id; p >= 0 && !keep[p]; p = S[p].parent) keep[p] = 1;
    }
    const rows = [];
    const visit = (id) => { rows.push(id); const kids = S[id].children.filter((c) => keep[c]).sort((a, b) => S[a].born - S[b].born); for (const c of kids) visit(c); };
    for (const s of S) if (s.parent < 0 && keep[s.id]) visit(s.id);
    const sig = rows.length + ':' + Math.floor(now / 600) + ':' + this.selSp;
    if (!force && sig === this.treeSig) return;
    this.treeSig = sig;
    const rowH = P.clamp(560 / Math.max(1, rows.length), 4, 16), padT = 18, padL = 8, padR = 8;
    const hgt = Math.max(220, Math.min(5000, rows.length * rowH + padT + 10));
    cv.style.height = hgt + 'px';
    const g = this.ctx2d(cv);
    if (!g) return;
    const { c, w } = g, X = (t) => padL + ((w - padL - padR) * t) / now, Y = new Map();
    rows.forEach((id, k) => Y.set(id, padT + k * rowH + rowH / 2));
    // day ticks
    c.fillStyle = '#6f8986'; c.strokeStyle = 'rgba(134,161,158,0.12)'; c.textAlign = 'center';
    const days = now / env.dayLen, step = Math.max(1, Math.ceil(days / 6));
    for (let d = 0; d <= days; d += step) { const x = X(d * env.dayLen); c.beginPath(); c.moveTo(x, padT - 4); c.lineTo(x, hgt); c.stroke(); c.fillText('d' + (d + 1), x, 8); }
    for (const id of rows) {
      const s = S[id], y = Y.get(id), x0 = X(s.born), x1 = X(s.extinct >= 0 ? s.extinct : now);
      const alive = s.pop > 0, col = alive ? s.color : 'rgba(134,161,158,0.4)';
      if (s.parent >= 0 && Y.has(s.parent)) {
        c.strokeStyle = alive ? s.color : 'rgba(134,161,158,0.25)'; c.globalAlpha = 0.5; c.lineWidth = 1;
        c.beginPath(); c.moveTo(x0, Y.get(s.parent)); c.lineTo(x0, y); c.stroke(); c.globalAlpha = 1;
      }
      c.strokeStyle = col; c.lineWidth = Math.max(1, Math.min(rowH * 0.8, 1 + Math.log10(1 + s.peak) * 1.4));
      c.beginPath(); c.moveTo(x0, y); c.lineTo(Math.max(x1, x0 + 1.5), y); c.stroke();
      if (id === this.selSp) { c.strokeStyle = '#fff'; c.lineWidth = 1; c.strokeRect(x0 - 2, y - rowH / 2, Math.max(4, x1 - x0) + 4, rowH); }
      if (alive && rowH >= 9 && s.pop >= 5) {
        c.fillStyle = s.color; c.textAlign = 'right'; c.font = '600 10px "Instrument Sans", sans-serif';
        c.fillText(s.name, Math.max(x1 - 4, 60), y - rowH * 0.45 + 1);
        c.font = '10px "JetBrains Mono", monospace';
      }
    }
    this.treeRows = rows; this.treeRowH = rowH; this.treePadT = padT;
  }
  treeRowAt(e) {
    if (!this.treeRows) return -1;
    const r = e.target.getBoundingClientRect(), k = Math.floor((e.clientY - r.top - this.treePadT) / this.treeRowH);
    return k >= 0 && k < this.treeRows.length ? this.treeRows[k] : -1;
  }
  treeClick(e) { const id = this.treeRowAt(e); if (id >= 0) this.selectSpecies(id); }
  treeHover(e) {
    const id = this.treeRowAt(e), tip = $('tooltip');
    this.treeHovering = id >= 0;
    if (id < 0) { tip.style.display = 'none'; return; }
    const s = P.species[id];
    tip.innerHTML = `<b style="color:${s.color}">${P.escape(P.speciesLabel(s))}</b><br><span class="muted">${P.ROLES[s.role].label} · ${s.pop ? P.fmt(s.pop) + ' cells' : 'extinct'} · peak ${P.fmt(s.peak)}</span>`;
    tip.style.display = 'block';
    tip.style.left = Math.min(e.clientX + 14, innerWidth - tip.offsetWidth - 8) + 'px'; tip.style.top = e.clientY + 14 + 'px';
  }

  // ---------------- climate ----------------
  renderClimate() {
    const env = this.app.env, H = env.history;
    let tsum = 0, lsum = 0, n = 0, nut = 0, tox = 0;
    for (let i = 0; i < env.temp.length; i += 5) { if (env.wall[i] > 0.5) continue; tsum += env.temp[i]; lsum += env.light[i]; nut += env.field[i * 4]; tox += env.field[i * 4 + 1]; n++; }
    const kv = [['Season', env.seasonName], ['Water temp.', (tsum / n).toFixed(1) + ' °C'], ['Sunlight', Math.round((lsum / n) * 100) + '%'], ['Nutrients', (nut / n).toFixed(2)],
      ['Toxins', (tox / n).toFixed(3)], ['Vents', env.vents.length], ['Whirlpools', env.vortices.length], ['Climate trend', (env.climateTarget >= env.climate ? '↑ ' : '↓ ') + env.climateTarget.toFixed(1) + ' °C']];
    $('climKv').innerHTML = kv.map(([a, b]) => `<div><span>${a}</span><b>${b}</b></div>`).join('');
    const g = this.ctx2d($('chClim'));
    if (g && H.length > 1) {
      const { c, w, h: hh } = g, padL = 34, nn = H.length;
      let tmin = 1e9, tmax = -1e9, nmax = 0.1, xmax = 0.02;
      for (const s of H) { tmin = Math.min(tmin, s.temp); tmax = Math.max(tmax, s.temp); nmax = Math.max(nmax, s.nut); xmax = Math.max(xmax, s.tox); }
      tmin = Math.floor(tmin - 1); tmax = Math.ceil(tmax + 1);
      c.strokeStyle = 'rgba(134,161,158,0.14)'; c.fillStyle = '#6f8986'; c.textAlign = 'right';
      for (let k = 0; k <= 2; k++) { const y = 4 + (hh - 8) * (1 - k / 2); c.beginPath(); c.moveTo(padL, Math.round(y) + 0.5); c.lineTo(w, Math.round(y) + 0.5); c.stroke(); c.fillText(Math.round(tmin + ((tmax - tmin) * k) / 2) + '°', padL - 5, y); }
      const X = (k) => padL + ((w - padL) * k) / (nn - 1);
      line(c, H, X, (s) => 4 + (hh - 8) * (1 - (s.temp - tmin) / (tmax - tmin)), '#ff8a5b', 1.6);
      line(c, H, X, (s) => 4 + (hh - 8) * (1 - s.nut / (nmax * 1.1)), '#a9d05a', 1.3);
      line(c, H, X, (s) => 4 + (hh - 8) * (1 - s.tox / (xmax * 1.1)), '#c46bd0', 1.2);
    }
    if (this.logDirty) {
      this.logDirty = false;
      $('fullLog').innerHTML = this.logs.length ? this.logs.map((l) => `<div><time>${l.stamp}</time><span>${l.msg}</span></div>`).join('') : '<div class="empty">Nothing has happened yet. Give it a minute.</div>';
    }
  }

  // ---------------- cell inspector ----------------
  renderCell(force) {
    const app = this.app, sim = app.sim, env = app.env, i = app.ren.selected, box = $('cellInfo');
    if (i < 0 || !sim.alive[i] || sim.uid[i] !== app.ren.selUid) {
      if (this.cellShown !== -1 || force) {
        this.cellShown = -1;
        box.innerHTML = i >= 0 ? '<div class="empty">That cell has died. Pick another with the Inspect tool.</div>' : '<div class="empty">Pick the Inspect tool (<b>I</b>) and click any cell to see what it is, what it is doing and what its genes say.</div>';
      }
      return;
    }
    const sp = P.species[sim.sp[i]], o = i * P.G, g = sim.genome;
    const T = env.tempAt(sim.x[i], sim.y[i]), ti = env.idx(sim.x[i], sim.y[i]);
    const state = sim.state[i] ? 'Dormant spore' : sim.host[i] >= 0 ? 'Feeding on a host' : MOODS[sim.mood[i]];
    let colony = 0;
    if (sim.nb[i]) { const seen = new Set([i]), st = [i]; while (st.length && seen.size < 500) { const a = st.pop(); for (let s = 0; s < 4; s++) { const b = sim.bonds[a * 4 + s]; if (b >= 0 && !seen.has(b)) { seen.add(b); st.push(b); } } } colony = seen.size; }
    const pct = (a, b) => P.clamp(a / b, 0, 1) * 100;
    const tFit = Math.abs(T - sim.tOpt[i]) <= sim.tTol[i];
    const sh = P.dietShares(g, o);
    const html = `<div style="display:grid;gap:12px">
      <div><div class="label">Cell #${sim.uid[i]} · generation ${sim.gen[i]}</div>
        <h2 style="margin-top:3px"><a data-sp="${sp.id}" style="color:${sp.color};cursor:pointer;text-decoration:none">${P.escape(P.speciesLabel(sp))}</a></h2>
        <div class="btnrow" style="margin-top:6px"><span class="chip" style="color:${P.ROLES[sp.role].color}">${P.ROLES[sp.role].label}</span><span class="chip" style="color:var(--ink)">${state}</span>${sim.infect[i] ? '<span class="chip" style="color:#c58bff">Infected</span>' : ''}${colony ? `<span class="chip" style="color:#7fd38a">Colony of ${colony}</span>` : ''}</div></div>
      <div class="bars">
        <div class="bar"><span>Energy</span><div class="track"><div class="fill" style="width:${pct(sim.energy[i], sim.maxE[i])}%;background:var(--good)"></div></div><b>${Math.round(sim.energy[i])}</b></div>
        <div class="bar"><span>Health</span><div class="track"><div class="fill" style="width:${pct(sim.hp[i], sim.maxHP[i])}%;background:var(--bad)"></div></div><b>${Math.round(sim.hp[i])}</b></div>
        <div class="bar"><span>Age</span><div class="track"><div class="fill" style="width:${pct(sim.age[i], sim.maxAge[i])}%;background:#6fb8ff"></div></div><b>${(sim.age[i] / env.dayLen).toFixed(1)}d</b></div>
        <div class="bar"><span>Ready to divide</span><div class="track"><div class="fill" style="width:${pct(sim.energy[i], sim.maxE[i] * sim.reproF[i])}%"></div></div><b>${Math.round(pct(sim.energy[i], sim.maxE[i] * sim.reproF[i]))}%</b></div>
      </div>
      <div class="kv">
        <div><span>Water</span><b style="color:${tFit ? 'var(--good)' : 'var(--bad)'}">${T.toFixed(1)} °C</b></div>
        <div><span>Comfort</span><b>${(sim.tOpt[i] - sim.tTol[i]).toFixed(0)}…${(sim.tOpt[i] + sim.tTol[i]).toFixed(0)} °C</b></div>
        <div><span>Light</span><b>${Math.round(env.light[ti] * 100)}%</b></div>
        <div><span>Nutrients</span><b>${env.field[ti * 4].toFixed(2)}</b></div>
        <div><span>Toxins</span><b>${env.field[ti * 4 + 1].toFixed(3)}</b></div>
        <div><span>Offspring</span><b>${sim.kids[i]}</b></div>
        <div><span>Radius</span><b>${sim.r[i].toFixed(1)} µm</b></div>
        <div><span>Top speed</span><b>${(sim.maxSpd[i] * 60).toFixed(0)} µm/s</b></div>
        <div><span>Senses</span><b>${sim.senseR[i].toFixed(0)} µm</b></div>
        <div><span>Upkeep</span><b>${(sim.metab[i] * 60).toFixed(1)}/s</b></div>
      </div>
      <div><h3>Diet</h3>${dietBar(sh)}</div>
      <div><h3>Genome <em>32 genes</em></h3><div class="genes">${P.GENES.map((d, k) => geneBar(d[0], g[o + k])).join('')}</div></div>
      <div class="btnrow">
        <button class="btn" data-act="follow">${app.follow === sim.uid[i] ? 'Stop following' : 'Follow'}</button>
        <button class="btn" data-act="feed">Feed</button>
        <button class="btn" data-act="split">Force division</button>
        <button class="btn" data-act="lab">Copy to Lab</button>
        <button class="btn danger" data-act="kill">Kill</button>
      </div></div>`;
    box.innerHTML = html;
    this.cellShown = i;
  }
  cellAction(e) {
    const app = this.app, sim = app.sim, i = app.ren.selected;
    const a = e.target.closest('[data-sp]');
    if (a) { this.selectSpecies(+a.dataset.sp); return; }
    const b = e.target.closest('[data-act]');
    if (!b || i < 0 || !sim.alive[i] || sim.uid[i] !== app.ren.selUid) return;
    switch (b.dataset.act) {
      case 'follow': app.follow = app.follow === sim.uid[i] ? 0 : sim.uid[i]; break;
      case 'feed': sim.energy[i] = Math.min(sim.maxE[i], sim.energy[i] + sim.maxE[i] * 0.5); sim.hp[i] = sim.maxHP[i]; break;
      case 'split': sim.energy[i] = Math.max(sim.energy[i], sim.maxE[i] * 0.9); sim.divide(i, app.env.mutationMult); break;
      case 'lab': this.loadLab(sim.genome.subarray(i * P.G, i * P.G + P.G), P.species[sim.sp[i]].genus + ' novus'); this.showTab('lab'); return;
      case 'kill': sim.kill(i, 6); break;
    }
    this.renderCell(true);
  }

  // ---------------- Lab ----------------
  buildLab() {
    const pre = $('labPresets');
    for (const name of Object.keys(P.PRESETS)) {
      const b = h(`<button>${name}</button>`);
      b.onclick = () => { const g = P.genomeFrom(P.PRESETS[name]); g[P.GI.HUE] = P.PRESETS[name].HUE ?? P.rand(); this.loadLab(g, P.genusName() + ' ' + P.epithet()); };
      pre.appendChild(b);
    }
    const groups = {};
    P.GENES.forEach((d, k) => (groups[d[2]] = groups[d[2]] || []).push(k));
    const wrap = $('labGenes');
    for (const [grp, ks] of Object.entries(groups)) {
      const det = h(`<details class="gsec"${grp === 'Diet' || grp === 'Body' ? ' open' : ''}><summary>${grp}</summary><div class="sliders"></div></details>`);
      const sl = det.querySelector('.sliders');
      for (const k of ks) {
        const [key, label, , hint] = P.GENES[k];
        const el = h(`<div class="sl" title="${P.escape(hint)}"><label for="lg${key}">${label}</label><output id="lg${key}o"></output><input type="range" id="lg${key}" min="0" max="100" step="1"></div>`);
        el.querySelector('input').oninput = (e) => { this.labGenome[k] = +e.target.value / 100; this.labSpecies = null; this.labRefresh(false); };
        sl.appendChild(el);
      }
      wrap.appendChild(det);
    }
    $('labName').oninput = () => (this.labSpecies = null);
    $('labCount').oninput = () => ($('labCountOut').textContent = $('labCount').value);
    $('labPlace').onclick = () => { this.setTool('spawn'); if (innerWidth <= 760) $('panel').classList.add('collapsed'); };
    $('labRandom').onclick = () => {
      const g = new Float32Array(P.G);
      for (let k = 0; k < P.G; k++) g[k] = P.rand() * 0.8;
      g[P.GI.HUE] = P.rand(); g[P.GI.TEMP_OPT] = 0.35 + P.rand() * 0.3; g[P.GI.TEMP_TOL] = 0.3 + P.rand() * 0.3;
      this.loadLab(g, P.genusName() + ' ' + P.epithet());
    };
    $('labMutate').onclick = () => { P.mutate(this.labGenome, 6); this.labSpecies = null; this.labRefresh(true); };
    $('labSave').onclick = () => {
      const list = this.savedDesigns();
      list.unshift({ name: $('labName').value.trim() || 'Untitled', genes: Array.from(this.labGenome, (v) => +v.toFixed(3)) });
      try { localStorage.setItem('primordia.designs', JSON.stringify(list.slice(0, 30))); } catch (e) { this.log('Your browser blocked saving. The design is still in the Lab.', 'event'); }
      this.renderSaved();
    };
    $('labSaved').onclick = (e) => {
      const b = e.target.closest('[data-k]');
      if (!b) return;
      const list = this.savedDesigns(), d = list[+b.dataset.k];
      if (!d) return;
      if (b.dataset.do === 'load') this.loadLab(Float32Array.from(d.genes), d.name);
      else { list.splice(+b.dataset.k, 1); try { localStorage.setItem('primordia.designs', JSON.stringify(list)); } catch (err) { /* storage unavailable */ } this.renderSaved(); }
    };
    this.renderSaved();
    this.labRefresh(true);
  }
  savedDesigns() { try { return JSON.parse(localStorage.getItem('primordia.designs') || '[]'); } catch (e) { return []; } }
  renderSaved() {
    const list = this.savedDesigns();
    $('labSaved').innerHTML = list.length ? list.map((d, k) => `<div><span class="swatch" style="color:${P.hueCss(d.genes[P.GI.HUE] || 0)}"></span><span>${P.escape(d.name)}</span><button data-k="${k}" data-do="load">Load</button><button data-k="${k}" data-do="del">Delete</button></div>`).join('') : '<div class="empty">Designs you save appear here, in this browser only.</div>';
  }
  loadLab(g, name) {
    this.labGenome = Float32Array.from(g);
    if (name) $('labName').value = name;
    this.labSpecies = null;
    this.labRefresh(true);
  }
  labRefresh(sliders) {
    const g = this.labGenome;
    P.GENES.forEach(([key], k) => {
      const inp = $('lg' + key), out = $('lg' + key + 'o');
      if (sliders) inp.value = Math.round(g[k] * 100);
      out.textContent = key === 'TEMP_OPT' ? (-5 + 50 * g[k]).toFixed(0) + ' °C' : key === 'TEMP_TOL' ? '±' + (3 + 17 * g[k]).toFixed(0) + ' °C' : key === 'SIZE' ? (3 + 11 * g[k]).toFixed(1) + ' µm' : Math.round(g[k] * 100);
    });
    const f = P.phen(g, 0, {}), env = this.app.env, sh = P.dietShares(g);
    const stats = [['Radius', f.r.toFixed(1) + ' µm'], ['Top speed', (f.maxSpd * 60).toFixed(0) + ' µm/s'], ['Energy store', f.maxE.toFixed(0)], ['Upkeep', (f.metab * 60).toFixed(1) + ' /s'], ['Lifespan', (f.maxAge / env.dayLen).toFixed(1) + ' days']];
    $('labStats').innerHTML = stats.map(([a, b]) => `<div><span>${a}</span><b>${b}</b></div>`).join('');
    // viability hints
    const area = f.r * f.r, notes = [];
    const income = f.ePhoto * 0.45 * area * 0.0065 + f.eChemo * area * 0.006 * 0.5;
    if (sh[0] > 0.35) notes.push('Lives on sunlight. Release it in bright shallows (set Water shows: Light).');
    if (sh[1] > 0.35) notes.push('Absorbs dissolved nutrients. Vents and upwelling zones suit it.');
    if (sh[2] > 0.3) notes.push(`Hunts cells up to about ${(f.r * (0.6 + f.ePred) * 1.2).toFixed(1)} µm across.`);
    if (sh[3] > 0.3) notes.push('Eats detritus. It does well near predators and after die-offs.');
    if (sh[4] > 0.3) notes.push(`Needs hosts larger than ${(f.r * 1.2).toFixed(1)} µm to latch onto.`);
    if (sh[2] + sh[3] + sh[4] < 0.3 && income < f.metab) notes.push('<span style="color:var(--warn)">Its upkeep is higher than it can make from light and nutrients. It may starve.</span>');
    const meanT = env.history.length ? env.history[env.history.length - 1].temp : 20;
    if (Math.abs(meanT - f.tOpt) > f.tTol) notes.push(`<span style="color:var(--warn)">Prefers ${(f.tOpt - f.tTol).toFixed(0)} to ${(f.tOpt + f.tTol).toFixed(0)} °C, but the sea averages ${meanT.toFixed(0)} °C. Look for a warmer or colder patch.</span>`);
    else notes.push(`Comfortable from ${(f.tOpt - f.tTol).toFixed(0)} to ${(f.tOpt + f.tTol).toFixed(0)} °C.`);
    if (g[P.GI.ADHESION] > 0.45) notes.push('Daughter cells stay attached and grow into a multicellular colony.');
    if (g[P.GI.GLOW] > 0.3 && sh[2] > 0.3) notes.push('Its glow lures light-seeking prey toward it.');
    $('labViab').innerHTML = notes.map((n) => `<div>${n}</div>`).join('');
  }
};

// ---------- small drawing helpers ----------
function niceMax(v) { const p = Math.pow(10, Math.floor(Math.log10(v))); return [1, 1.5, 2, 3, 5, 7.5, 10].map((m) => m * p).find((m) => m >= v) || v; }
function line(c, H, X, Y, col, w) {
  c.beginPath(); H.forEach((s, k) => c.lineTo(X(k), Y(s))); c.strokeStyle = col; c.lineWidth = w; c.stroke();
  const k = H.length - 1; c.fillStyle = col; c.beginPath(); c.arc(X(k), Y(H[k]), 2.4, 0, P.TAU); c.fill();
}
function spark(cv, sp, big) {
  const dpr = big ? Math.min(window.devicePixelRatio || 1, 2) : 1;
  if (big) { cv.width = cv.clientWidth * dpr; cv.height = cv.clientHeight * dpr; }
  const c = cv.getContext('2d'), w = cv.width, hh = cv.height, d = sp.hist;
  c.clearRect(0, 0, w, hh);
  if (d.length < 2) return;
  let max = 1; for (const v of d) max = Math.max(max, v);
  c.beginPath();
  d.forEach((v, k) => c.lineTo((k / (d.length - 1)) * w, hh - 2 - (v / max) * (hh - 4)));
  c.lineTo(w, hh); c.lineTo(0, hh); c.closePath();
  c.fillStyle = sp.color; c.globalAlpha = 0.18; c.fill(); c.globalAlpha = 1;
  c.beginPath(); d.forEach((v, k) => c.lineTo((k / (d.length - 1)) * w, hh - 2 - (v / max) * (hh - 4)));
  c.strokeStyle = sp.color; c.lineWidth = 1.5 * dpr; c.stroke();
  if (big) { c.fillStyle = '#86a19e'; c.font = `${10 * dpr}px "JetBrains Mono", monospace`; c.textBaseline = 'top'; c.fillText('peak ' + max, 3 * dpr, 2 * dpr); }
}
function geneBar(key, v) {
  const d = P.GENES[P.GI[key]];
  return `<div class="gene" title="${P.escape(d[3])}"><span>${d[1]}</span><div class="track"><div class="fill" style="width:${Math.round(v * 100)}%"></div></div></div>`;
}
function dietBar(sh) {
  const cols = [P.ROLES.photo.color, P.ROLES.chemo.color, P.ROLES.pred.color, P.ROLES.scav.color, P.ROLES.para.color], names = ['Light', 'Nutrients', 'Prey', 'Detritus', 'Hosts'];
  return `<div style="display:flex;height:8px;border-radius:4px;overflow:hidden">${sh.map((v, k) => `<div style="width:${v * 100}%;background:${cols[k]}"></div>`).join('')}</div>
    <div class="legend">${sh.map((v, k) => (v > 0.04 ? `<span><i style="background:${cols[k]}"></i>${names[k]}<b>${Math.round(v * 100)}%</b></span>` : '')).join('')}</div>`;
}
