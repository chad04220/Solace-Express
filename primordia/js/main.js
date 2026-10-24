'use strict';
// Boot and frame loop. Simulation ticks run inside a per-frame time budget so the view stays smooth at any speed.

(() => {
  const canvas = document.getElementById('view');
  const app = (P.app = { speed: 1, paused: false, follow: 0 });

  const build = (seed) => {
    P.seed(seed);
    P.species.length = 0;
    app.env = new P.Env(seed);
    app.sim = new P.Sim(app.env);
    app.ctx = { sim: app.sim, env: app.env, log: (m, k, s) => P.log && P.log(m, k, s) };
    app.sim.hooks = { log: app.ctx.log, randomEvent: () => P.randomEvent(app.ctx) };
    app.env.hooks = app.sim.hooks;
    app.sim.seed(16);
  };

  try {
    build((Date.now() & 0x7fffffff) || 1);
    app.ren = new P.Renderer(canvas, app.sim, app.env);
  } catch (err) {
    document.getElementById('app').innerHTML = `<div id="glfail"><div><h2 style="font:800 26px var(--display);color:var(--amber);margin:0 0 8px">Primordia needs WebGL2</h2><p class="muted">${P.escape(err.message)}</p><p class="muted">Try a current version of Chrome, Edge, Firefox or Safari with hardware acceleration turned on.</p></div></div>`;
    return;
  }
  const attachField = () => {
    try { app.field = new P.FieldGPU(app.ren.gl, app.env); } catch (e) { console.warn('GPU field unavailable, using CPU:', e.message); app.field = new P.FieldCPU(app.ren.gl, app.env); }
    app.ren.field = app.field;
  };
  attachField();
  app.preview = new P.CellPreview(document.getElementById('labPreview'));
  app.ren.resize();
  app.ren.cam.zoom = app.ren.minZoom() * 2.2;
  // start the camera over the busiest patch of producers
  {
    const s = app.sim; let bx = 0, by = 0, best = -1;
    for (let t = 0; t < 40; t++) { const i = P.randi(Math.max(1, s.hi)); if (!s.alive[i]) continue; let n = 0; s.forNear(s.x[i], s.y[i], 200, () => n++); if (n > best) { best = n; bx = s.x[i]; by = s.y[i]; } }
    if (best > 0) { app.ren.cam.x = bx; app.ren.cam.y = by; }
  }
  app.ui = new P.UI(app);
  app.ui.log('A new sea has formed. ' + P.species.length + ' founding species are competing for light, nutrients and each other.', 'event');

  app.setSpeed = (s) => { app.speed = s; app.paused = false; };
  app.togglePause = () => { app.paused = !app.paused; };
  app.reset = () => {
    build((Date.now() & 0x7fffffff) || 1);
    app.ren.sim = app.sim; app.ren.env = app.env; app.ren.selected = -1; app.ren.highlightSp = -1;
    attachField();
    app.ui.selSp = -1; app.ui.labSpecies = null; app.ui.logs = []; app.follow = 0;
    document.getElementById('btnResetYes').hidden = true;
    app.ui.log('You started a new world. ' + P.species.length + ' founding species.', 'event');
    app.ui.showTab('eco');
  };
  window.addEventListener('resize', () => { app.ren.resize(); app.ren.clampZoom(); });

  let last = performance.now(), simMs = 0;
  const frame = (now) => {
    const dt = Math.min(0.1, (now - last) / 1000);
    last = now;
    const ren = app.ren, sim = app.sim;
    ren.resize();
    app.field.poll();
    app.ui.applyTool();
    // run as many ticks as the speed asks for, within ~11 ms
    let done = 0;
    const t0 = performance.now();
    if (!app.paused) {
      for (let k = 0; k < app.speed; k++) {
        sim.step(); done++;
        if (performance.now() - t0 > 11) break;
      }
    }
    simMs = simMs * 0.9 + (performance.now() - t0) * 0.1;
    app.simMs = simMs; app.ticksDone = done;
    app.field.paused = app.paused;
    app.field.step(done);
    // camera follow
    if (app.follow) {
      const i = ren.selected;
      if (i >= 0 && sim.alive[i] && sim.uid[i] === app.follow) {
        ren.cam.x = P.wrapP(ren.cam.x + P.wrapD(sim.x[i] - ren.cam.x) * 0.15);
        ren.cam.y = P.wrapP(ren.cam.y + P.wrapD(sim.y[i] - ren.cam.y) * 0.15);
      } else app.follow = 0;
    }
    ren.draw(now / 1000);
    app.ui.update(now, dt);
    requestAnimationFrame(frame);
  };
  requestAnimationFrame(frame);
})();
