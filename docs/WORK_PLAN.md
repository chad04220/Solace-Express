# Solace Express — Work Plan

The owner's work plan from v3.9.4 (`f8b8537`), cut down to what is still open (2026-10-07, v3.32.0, with Codex's
review of v3.31.0 folded in). Every finished item, with its spec, is in this file's history (`git log -p
docs/WORK_PLAN.md`). The renderer rebuild is docs/RENDERER_REBUILD.md; the owner-side tool is the one `diagnostics.bat` in the release zip (docs/COLLABORATION.md).

## Open items

| Item | Status |
|---|---|
| A3 instrument atlas static/dynamic split | deferred: the display pass is 0.2-0.8 ms on the owner's GPU (v3.27.1); worth it only if it grows |
| A6 UBO for scene parameters | deferred: every scene is GPU-bound on the owner's machine (CPU submit 7-8 ms under a longer GPU frame) |
| Per-family aircraft shaders | done (v3.29.0-v3.29.2): light-aircraft builds (AF_LIGHT), a maps-only shadow proxy (PROXY_MAPS_ONLY; at night the airframe's beacon mapped looking down) and a UFO/debris-only objects build (OBJ_NO_AF). Owner's diagnostic v3.29.2 vs v3.28.0: every scene faster - cockpit 14.4 -> 7.4 ms, Mantis cockpit 18.5 -> 10.1, night 10.1 -> 7.7, UFO 6.9 -> 5.3 (objects 4.6 -> 0.9), proxy 0.1-0.2 ms everywhere. A research-only build is the remaining step if the jet cockpits need it |
| Per-aircraft and per-class shaders | done (after v3.41.0): each aircraft's own mesh-pass, bake, march and shadow programs (AF_MODEL: its code and its packed model as constants, nothing of another aircraft's; aircraft_specialization_test checks it), the scenery's per class (ENT_CLASS), every program cut to the conditionals its build settles and to a canonical layout (shader_prune.h), and the launch compiling no program with an aircraft's code. Goal (the owner's): an edit compiles only what it touches. Needs the owner's diagnostic: the first launch's compile time and the aircraft mesh pass's GPU time per aircraft against v3.41.0's |
| Launch front-loading (v3.31.0) | done: every aircraft's meshes (research jets too) at launch; the islands and the aircraft performance cached per build; the loading bar showing the share of the launch's steps done (each shader program, the islands, the career, the renderer, the menu, each aircraft's mesh: load_pacer.h) with each step named. Still streamed: the scenery round a flight's start (the world is 313 x 313 chunks) |
| Tiled light culling | deferred: low value at the owner's v3.29.2 diagnostics (every benchmark scene at 60 fps or better then). Not current evidence: recheck against the current release's diagnostics after the v3.30.1 hotfix |
| A10 / A11 water sky probe, cloud reprojection | deferred: the lighting and cloud passes are 1-3 ms; worth it only if the owner's diagnostics show them growing |
| Review visuals (cockpit framing, materials, vegetation, coast) | open, cheap changes only (the owner prefers performance and gameplay); the XR-20's cockpit framing is done |
| Cockpit edges (jagged perimeters, the wing roots' pale wedges) | v3.32.0: fixed in the shapes (no added shader work in flight; the meshes rebake once) - the window openings have a rounded 3 cm trim lip (the knife edges where flat cuts met the curved roof came off the lattice serrated), the visors follow the headliner, the cabin sun shadow is hardware-filtered. Open: faint lumps along the rounded sill rail's highlight (the 1.56 cm lattice on a 3 cm radius) |
| R1 aircraft reachability (reopened by the review of v3.31.0) | v3.32.0: the aircraft chooser lists owned aircraft first and scrolls (wheel, right stick), so rentals can't push one out of reach; a retry or restart needs an aircraft the job can use. Accept: the owner checks every career aircraft at 720p / 1080p and 80 / 100 / 140% |
| R7 autoland in the real career and from the GPS (reopened) | v3.32.0: the gentle (passenger) law and the full envelope both swept (`autoland_sweep --comfort`, `--all` for every field the GPS offers); runway length, the turn-in's ground and the glidepath checked before committing, with the reason said when refused; the stick takes over a refused autoland's hold; a stop past the runway's end is reported as an overrun. Accept: both sweeps land or refuse every case |
| R9 layouts at every supported scale (reopened) | v3.32.0: Work list, Settings and the debrief scroll; tabs fit or number themselves; the wind readout keeps clear of its components. Accept: the owner's screenshots at 140% |
| W1 audio follows a default output device change | implemented; validation on real devices (headset plugged and unplugged) pending |

Done since v3.24.0 and kept out of this list: R3's moving parts (every aircraft, the research jets' nozzles, gear and
actuators included: no moving hull is left), the cloaked XR-40 on its mesh, the review's career findings (R2-R6, R8,
R10).

Decided, not planned: D1 (the FlightSession / CrashFx / Comms extraction from `Game`: a pure refactor with regression
risk and no player-visible gain) and B3 (the first-run compile stays on the helper process).

---

## Rules

1. Post each item in issue #2 with the heading lines from `docs/COLLABORATION.md`; claim the item's files before editing.
2. **Gates** (each item lists which apply):
   - **test** — extend the named test; `ctest` passes on Windows CI and on the Linux ASan/UBSan job.
   - **measure** — the owner's `diagnostics.bat` at the parent commit and at the branch head, same scenes. Merge only
     if the targeted pass improves and nothing else regresses by more than 0.2 ms.
   - **image** — screenshots before and after on the owner's GPU, plus `tests/render_harness.cpp`
     `multi:air,mountain,storm,cockpit` on llvmpipe (`GALLIVM_PERF=nopt`). Items marked "invisible" must diff to noise
     (PSNR > 40 dB on llvmpipe); others are judged by eye.
3. **Never** play a voice recording that names a control the player has rebound.
4. Keep `RELEASE_NOTES.md` current per item; update `README.md` for user-visible changes.
5. Preserve existing behaviour the owner values: unrestricted manual flying (the autopilot's comfort law affects the
   autopilot only), rentals launching without a cash check, courtesy positioning under $1,500, the hidden research
   content.

---

## Item specs

Template: **Goal** · **Files** · **Steps** · **Accept**.

#### A3 · Instrument atlas static/dynamic split  *(small–medium · gate: measure (the display pass), image)*
**Files:** renderer.cpp (`renderDisplays`), the display shaders (`drawInstruments`, `mfdPage`, `panelTex`, the page
sampler), game_ui.cpp F3.
**Steps:**
1. `uniform int uDispLayer`: 0 static (dial faces, ticks, numerals, bezels, LCD backgrounds, PFD frame, page chrome),
   1 dynamic (needles, attitude ball, compass card, drums, tapes, radio digits, PFD symbology, page data). The dynamic
   layer writes premultiplied colour + coverage alpha.
2. Textures: `texPanelS/texPagesS` rendered once per cockpit type (on a cockpit change, first use) with mipmaps;
   `texPanelD/texPagesD` per frame, **no** `glGenerateMipmap`: sampled with a 2×2 supersample.
3. Composite `D over S` in `panelTex` and the page sampler.
4. When over budget: the dynamic layer at 30 Hz, alternating panel and pages (hook into the resolution scaler).
**Accept:** the display pass under 25% of before in `cockpit` and `rjetc`; cockpit shots identical by eye.

#### A6 · UBO for scene parameters  *(small · gate: CPU ms of `renderScene` on F3)*
**Files:** renderer.h/.cpp (`setRT`), the shared uniform declarations (`scene_uniforms.glsl`), gl.h
(`glBindBufferBase`, `glUniformBlockBinding`, `glGetUniformBlockIndex`).
**Steps:** one std140 block for every non-sampler in `setRT` (camera, sun, weather, the aircraft's `M/PS/Ctl/Pr/I*`,
lights, traffic count, effects, feeds); one `glBufferSubData` per view; samplers set once at link.
**Accept:** invisible image; fewer GL calls per frame (a debug counter).

#### A10 · Water reflection probe (R3's sky probe)  *(small · gate: image `air` over the sea at low sun)*
A 256×128 lat-long (or 6×64² cube) of the sky and clouds at 16 steps, every 4th frame; the water samples it along the
reflected direction.

#### A11 · Cloud reprojection (R3)  *(small · gate: measure the cloud pass)*
Skip the light march when transmittance is under 0.1; halve the light taps beyond 20 km; reproject last frame's cloud
texture through the previous camera rotation (blend 0.7) and drop the step count to 24/32.
