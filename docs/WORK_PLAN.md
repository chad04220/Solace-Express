# Solace Express — Work Plan

The owner's work plan from v3.9.4 (`f8b8537`), kept here with its status. The renderer rebuild (docs/RENDERER_REBUILD.md,
R0–R3) replaced the GPU track's A-items with a deferred rasterizer; the career, input and audio tracks stand as written.
Line numbers in §2 are from `f8b8537` and no longer match: re-grep the identifier. The owner-side tools named below (`analyze.bat`, `benchmark.bat`, `render_shots.bat`, `report.bat`) are now the one `diagnostics.bat` in the release zip (docs/COLLABORATION.md).

## Status (updated 2026-10-05, v3.18.0)

| Item | Status | Where |
|---|---|---|
| B4 Linux CI with sanitizers | done | 2025fe1 |
| B2 shaders as files | done | 605140e |
| B5 determinism | done | 2025fe1 |
| D2 dead state | done | 2025fe1 |
| C4 parking-brake stiction | done | b5424e6 |
| C1 autopilot comfort law | done | 0139bde |
| E1 honest quotes and hygiene | done (all seven steps) | 4ac0ee5 |
| A7 frame-rate unlock, fixed-size targets | done | 8e95b02 |
| A3 instrument atlas static/dynamic split | open: the owner's v3.18.0 run shows the raster `cockpit` scene at 160 ms, 97.9% in the first pass, which until now lumped the world, the displays, the objects and the airframe shadow proxy; that pass is split in four for the next run, which decides A3 |
| A4 point-light shadow budget | superseded on the raster path: the shadow proxy keeps the three brightest shadow-casting lights (`gbShadowSlot`) | R1b |
| A1 cheaper terrain normals | superseded: the terrain is a mesh with vertex normals (R1a) | — |
| C2 learned performance drives quotes | done | 2205011 |
| E5.1–3 input contexts, release-to-rearm, controller loss | done | e052d72 |
| B1 WASAPI audio | done | 555e49f |
| A5 temporal upsampling (TAAU) | done: 16-point Halton jitter, Gaussian reconstruction with confidence, widened clip; harness `air` at 0.67 vs 1.0 measures 33.8 dB converged (the same as before: the neighbourhood clamp bounds what can accumulate), indistinguishable by eye | A5 commit |
| A6 UBO for scene parameters | open; cheap once the uniform set settles after R3 | — |
| E2 transactions, save v3, attempt marker | done | aefdbe5 |
| A8 steps 1–2 lighting / terrain pass | done as R1a | 1be93c2 |
| E3 resumable jobs | done (all ten steps; the hospital flag on CAP/NPT/PVI is data for a later alternate policy) | fe220e8 |
| A8 steps 3–5 aircraft pass, traffic, cloak, feeds | done as R1b/R1c | 0fe987d, ce5ca42, fe31039 |
| A9 shadow maps for the aircraft | partly: the proxy pass (R1b); real maps are R2's remaining item | — |
| E4 earning-path guarantee | done (640-state sweep in the progression test) | ac7aa73 |
| A12 go/no-go | decided: meshes done as R2a–c (bake by state sweep, not per part) | 9546382 |
| C9 scoring lines from recorded data | done | 224fa0e |
| C10 ATC compliance | done; landing against a go-around is charged (-50%, -rep) rather than failing the leg, so a resumable job is not stranded at its destination | 224fa0e |
| A10 water reflection probe | → R3 (the sky probe) | — |
| A11 cloud march | → R3 (cloud reprojection) | — |
| A2 baked partial fbm | superseded by the terrain mesh | — |
| C3 fuel and payload planning | done: maxMass, fuel price per airport, FUEL_PURCHASED for owned aircraft, the card's fuel choice, hangar refuel; the progression test checks every story contract's fuel with full tanks (at least 5% over the flown quote) and weight | C3 commit |
| C5 financing and pacing | done: one loan at a time (a quarter down, 8–12% by reputation, 24 payments taken at settlements, three missed → repossession, a sale settles the balance), used aircraft at 65% with condition 0.65 and half tanks, freelance pay × (1 + 0.35·chapter); the progression money model finances when it can't pay cash (grind ≤ 5 jobs); save v3 keeps the loan and condition | C5 commit |
| C7 failures and maintenance | done: Failures on the Plane (engine partial/total per engine with asymmetric yaw on twins, alternator and a 7-minute battery, blocked pitot, gear stuck up/down, flap asymmetry, icing in cloud below freezing); one failure rolled per flight from the condition (rentals 2%, lessons none); a belly landing on a paved runway at approach speed is survivable; HUD annunciators, glide ring on the GPS, autopilot drops out; condition wears with hours and hard landings, hangar service and hull insurance, repairs and the emergency bonus at the settlement; flight_test glide / belly / twin / pitot / icing / alternator cases, gameplay_test engine-out landing insured and uninsured | C7 commit |
| A12 bake the aircraft to meshes | done differently (R2) | — |
| A13 terrain clipmap | done as R1a's CDLOD terrain | — |
| B3 drop the helper-process shader compile | open, remeasure: the v3.18.0 run's 150.8 s first start was not the shader compile (its log says 28 of 30 programs came from the cache) but the aircraft mesh bakes filling an empty mesh cache; the v3.19.0 run started in 10.2 s with the caches warm. A true cold compile number needs both shadercache and the mesh cache set aside; the next diagnostics.bat does that |
| C6 freelance job types | done: medevac (patient meter, hospital destinations, hard clock), VIP charter (comfort meter, tip), night freight (lit fields, landing light), low-vis run (cloud base and visibility at minimums, alignment check when breaking out, go-around clears it), survey (six-ring pattern at one altitude, pay by time in band); each posts by licence, aircraft and fields, names its challenge and deductions on the card, and scores its own lines; gameplay_test generator sweep, scoring lines, medevac flights gentle and steep | C6 commit |
| C8 dynamic weather | done: Contract.wxEnd / wxShift (about 30% of freelance jobs carry a front: wind backs or veers 70-180 deg, strength, cloud, visibility, rain or snow), the flight drifts from wx to wxEnd over the estimated time with a slow wander, the tower and the HUD read the current wind, the brief shows the forecast and names a wind shift as the challenge, the autopilot re-plans the approach for the other runway end while there is room (the planner now weighs the wind against the distance flown round, not its square); save v3 keeps the forecast (wx2); gameplay_test wind-shift case, save_test round trip | C8 commit |
| C11 airline layer | done: Career::Airline (pilots rated 1-3 at a wage, routes of an owned aircraft between two fields); every settlement of the player's is a tick (fares by the type's load and the pilot's load factor, less fuel and wage; the aircraft wears, swaps ends; incidents by rating and condition, insured or not); an aircraft on a route can't be flown or sold; the AIRLINE hub tab (routes, log, route set-up, hire / let go); scheduled flights appear as traffic in the type's livery along the route when the player is near; save keeps pilots, routes and totals; gameplay_test and save_test cases | C11 commit |
| C12 challenge modes | done: a TRIALS list in the contracts tab (spot landing scored on the metres from the mark and the touchdown rate, the STOL contest at Summit Pass on the landing roll, a gate run of eight low rings out of the home field and a daily course seeded by the date on the time from the first gate to the last, and the formation run: the Spectre pair joins above 300 m and flies its show for three minutes while the seconds out of a steady platform count against you); flown off the books (no pay, fees or logbook), the five best per trial kept in settings.cfg and shown on the card; gameplay_test courses / scoring / board | C12 commit |
| C13 surface NIGHTGLASS | done: a main-menu entry (open once the campaign is flown, before that behind a "nothing there counts" confirmation); the research test cards are the objective chain per craft; the U + I combo stays | C13 commit |
| C14 options (FOV, head-look, colour-blind HUD, UI scale, HUD per camera) | done | ca6cb95 |
| E5.4 menu focus navigation | done: every enabled button registers itself; arrow keys (menus, hub, results, pause) and the D-pad move the focus to the nearest button, Enter / Space / A press it, the mouse or the stick takes over again; the research terminal keeps left / right for the site, the shoulders step through the hub's tabs; gameplay_test navigation case | E5.4 commit |
| D1 + E6 FlightSession split, clocks, ATC validity | E6 done: a tower call carries the flight state it was made for (Game::atcKey: phase and runway ends) and is dropped if that state has moved on before it is said (queued or cutting in); the recall line is labelled "(before pause)" after a pause; flight and job clocks already stop with the pause; F3 counts the particles, debris, wreck pieces, traffic and comms history. D1 (the FlightSession / CrashFx / Comms extraction) not done: a pure refactor of Game with regression risk and no player-visible gain; startFlight remains the reset checklist | E6 commit |
| R2 aircraft meshes from the field | done on the harness (docs/RENDERER_REBUILD.md); the aircraft shadow maps stay with R3 | — |
| R3 sky probe, cloud reprojection, entity draw caching, delete the ray tracer | in progress: the airframe shadow maps are done (rasterShadowMaps: the baked mesh from the sun and the three brightest lights into a depth array, the moving hull into a mask the proxy marches; SHMAPOFF=1 restores the march) and wait for the owner's next diagnostics run; the owner's v3.18.0 numbers are in docs/RENDERER_REBUILD.md 1b; A10 / A11 (sky probe, cloud reprojection), entity draw caching and deleting the ray tracer follow |
| QA v3.18.0 (Codex, 2026-10-05) | eight of nine findings fixed (C1-C6 career persistence, D1 diagnostics renderer, U1 labels); F1 open: six gusty autoland cases (Starling at CDR overruns with a tailwind the planner accepts, Nightjar / Mantis hard touchdowns, Specter gear collapse). A first attempt (a stopping-distance penalty on the runway end and a go-around when the float eats the runway) sent the Starling round into Cedar Ridge's terrain and was backed out; the fix needs the planner to weigh the other end's terrain against the tailwind, or to decline the field, and a retune of the heavy jet's flare in gusts. Codex's probes (tests of record: autoland_starling_probe / autoland_research_probe in its QA package) reproduce all six |

Status: every item that can be finished without the owner's GPU is done. The owner's v3.18.0 diagnostics (2026-10-05, RTX 3070 Laptop) closed B3 (compile stays on the helper process: 150 s cold) and pointed R3 at the airframe shadow maps first; A3 waits for the next run's split cockpit timings; A6 (UBO) follows R3. D1 (the FlightSession extraction) is deliberately not done: a pure refactor with regression risk and no player-visible gain.

---

## 1. Rules

1. **One item = one branch = one PR.** Branch `claude/<id>-<slug>`. Post in issue #2 with the heading lines from `docs/COLLABORATION.md`; claim the item's files before editing.
2. **Order:** work the phases in §3 top to bottom. Inside a phase, items are independent unless "Depends" says otherwise. The GPU track and the career track touch disjoint files and may run in parallel branches.
3. **Gates** (every item lists which apply):
   - **test** — extend the named test; `ctest` passes on Windows CI and on the Linux ASan/UBSan job (B4). Add B4 first.
   - **measure** — owner runs `analyze.bat` + `benchmark.bat` at the parent commit and at the branch head, same scenes (`menu,air,storm,night,cockpit,rjetc,wr_8_0_0_0_1`), pastes both `analysis.txt` per-pass tables in the PR. Merge only if the targeted pass or counter improves and nothing else regresses > 0.2 ms.
   - **image** — `render_shots.bat` before/after on the owner's GPU, plus `tests/render_harness.cpp` `multi:air,mountain,storm,cockpit` on llvmpipe (`GALLIVM_PERF=nopt`). Items marked "invisible" must diff to noise (PSNR > 40 dB on llvmpipe); others are judged by eye and attached.
4. **Never** start A12 or A13 before A8 and A9 have measured results. **Never** remove the legacy ray-trace path during A8 until the final step diffs clean.
5. **Never** play a voice recording that names a control the player has rebound (E1.7).
6. Keep `RELEASE_NOTES.md` current per item; update `README.md` for user-visible changes (A7, C1, C3, C5, C6, C7, C8, E3, E4).
7. Preserve existing behaviour the owner values: unrestricted manual flying (the AP comfort law affects the autopilot only), rentals launching without a cash check, courtesy positioning under $1,500, the hidden research content.

---

## 2. Baseline reference

### 2.1 Frame pipeline (`Renderer::renderScene`, renderer.cpp:883)
envelope raster (march starts) → entity G-buffer + 0–2 shadow cascades → 64 rows of the 2048² terrain-shadow bake → cockpit display atlases (2048×776 + 2048×1024 RGBA16F + mipmaps, every frame in cockpit view) → research-jet feeds → aircraft/traffic hull raster → **uber ray-trace fragment shader** (terrain ≤360 steps, water, boxes, player SDF 120–200 steps, ≤12 traffic SDFs, UFO, debris, ≤12 point lights each able to run a 40-step SDF shadow march) → ¼-res clouds + composite → TAA (also the temporal upscale) → sprites → 6-mip bloom → ¼-res light shafts → post.

G-buffer at ray-trace res: `texGB[0]` RGBA32F (distance, oct-normal xy, class) · `texGB[1]` RGBA8 (albedo, rough) · `texGB[2]` RGBA8 (emit, metal).

### 2.2 Verified facts the items rely on
- CPU is not a bottleneck: `World::height` 0.18 µs; `Plane::step` 4–14 µs/frame; chunk gen 0.5 ms; `Scenery::collide` 0.04 µs.
- `flight_test`: Caravan cruise 73 m/s (spec 85), Q400 127 (140), Starling 232 (200); Starling lift-off 420 m (spec runway 1250). AP HOLD banks 54–73° on career aircraft, up to 5 g.
- `progression_test`: campaign completes; freelance grind needed: $29,480 before O6, $20,000 before A4.
- `gameplay_test`: braked aircraft at full power creep 0.24–0.30 m/s.
- Every flight starts with full tanks (game.cpp:286); `OwnedPlane::fuel/condition` are never read.
- A safe diversion settles a failed contract: reputation −1, job gone (career.cpp:356–362). A4's displayed estimate 10.17 min = (68+6) km / 200 m/s + 4 min; measured flight 12.75 min; the gap is climb/descent/pattern, not cruise.
- `AtcVoice::history` grows unbounded (atc.cpp:246). `simDt` is computed before the time-accel auto-cancel (game.cpp:557–562). ATC timers use frame `dt`, the world uses `simDt` (game.cpp:2303). LB+RB hide-UI chord is unconditional (game.cpp:2371). `canFly` samples terrain along the direct line, ignoring waypoints (career.cpp:239–244). Checkpoint test is end-point only, 110 m sphere (game.cpp:764).

### 2.3 Hot identifiers
| What | Where |
|---|---|
| Uniform upload for the ray tracer | `setRT` lambda renderer.cpp:923–1110 |
| Render targets / scale change | `createRenderTargets` 776, `setRenderScale` 837 |
| Display atlases | `Renderer::renderDisplays` renderer.cpp:588 |
| Entity streaming / culling / cascades | `Renderer::drawEntities` entity_render.cpp:74 |
| Hull bake (0.25 m voxels, outside views only) | aircraft_hull.cpp:42, `hullKey` 278, `hullWanted` 292 |
| Terrain march / shadow / normal | shaders.h:1250 / 1294 / 1315 |
| `terrainFbm`, `noised`, `baseAt` | shaders.h:52, 45, 38 (CPU twins in world.cpp) |
| Player SDF march, hull start | `tracePieceOnce` 1112, `tracePlaneHull` 1159 |
| Traffic march / shadow blob | `traceTraffic` 1173, `trafficShadow` 1199 |
| Point-light loop + SDF shadows | `shadeSurface` 1929–1963, `pieceShadow` 1214, `planeShadow` 1230 |
| Clouds | `traceClouds` 1377, `cloudDensity` 1357 |
| Ray tracer entry | `main()` 3228; water reflection cloud march 3382; `applyTS` 3356 |
| Instruments | `drawInstruments` 2304, `panelTex` 2175 |
| TAA | `kTaaFS` 4727 |
| Frame limiter / vsync | platform_win32.cpp:1004–1017, `setupPacing` 41 |
| Audio out (waveOut 4×20 ms) | platform_win32.cpp:124–175 |
| Resolution scaler | `Game::update` game.cpp:2347–2363 |
| Lights | `Game::buildLights` 1079 |
| Flight start / end / settle | `startFlight` 267, `endFlight` 317, `Career::settle` career.cpp:338 |
| Flight loop, checkpoints, diversion | `updateFlight` 548; 746–800 |
| Flight model forces / contacts / friction | `Plane::substep` aircraft.cpp:111; contacts ≈300–365; friction 354–355 |
| AP limits | `apControl` aircraft.cpp:965–1030 (bank 979–981) |
| Economy | `canFly` 226, `estimate` 263, `refreshBoard` 298 |
| Story | `buildStory` career.cpp:48 |
| FOV | game.cpp:1627–1629 |

### 2.4 Before Phase 1 — owner collects (10 min)
`analyze.bat` (terrain samples/px, SDF samples/px, cloud steps, light steps per scene; CPU vs GPU ms) · `benchmark.bat` 1080p + native · F3 in a night cockpit approach with landing lights, and in the Wraith cockpit · first-run shader compile time (delete `shadercache`). These numbers order Phase 1–4 GPU items.

---

## 3. Work items by phase

Template: **Goal** · **Files** · **Steps** · **Accept** (tests/gates) · **Don't** (pitfalls).

### Phase 0 — foundations

#### B4 · Linux CI with sanitizers  *(small · gate: itself)*
**Goal:** the environment both agents use gates every push.
**Files:** `.github/workflows/build.yml`, `CMakeLists.txt`.
**Steps:** add an `ubuntu-latest` job: build `flight_test progression_test save_test airport_layout_test envelope_test hull_test entity_raycast_test gameplay_test` (Linux target uses `radio_stub.cpp`) with `-fsanitize=address,undefined -fno-omit-frame-pointer -O1`; run ctest. Optional second job: Mesa + `glslangValidator` for `shader_glsl` and `terrain_shadow_bake`.
**Accept:** green on the baseline commit.

#### B2 · Shaders as files, embedded at build time  *(small)*
**Goal:** editable GLSL, no 64 KB string splits, parallel edits without conflicts.
**Files:** new `src/shaders/*.glsl` (`common`, `rt`, `rt_wraith`, `rt_ufo`, `rt_text`, `rt_displays`, `tsh_bake`, `hull_bake`, `cloud`, `cloud_comp`, `map`, `disp`, `sprite_vs/fs`, `down`, `up`, `ck_mask`, `ray_mask`, `ray`, `feed_rays`, `taa`, `post`, `ui_vs/fs`, `ent_vs/fs`), `CMakeLists.txt`, generated `shaders_gen.h`, `renderer.cpp` (`rtSource`), `tools/shader_check.cpp`.
**Steps:** CMake `file(READ)` each `.glsl` → `configure_file` into `shaders_gen.h` as `static const char* kXxx` (chunk at 60 KB for MSVC); `rtSource(defines)` keeps prefix injection; `shader_check` reads the same files. Keep `shader_keyword_test` working.
**Accept:** byte-identical program sources (hash the concatenation before/after); image-gate invisible.

#### B5 · Determinism  *(tiny)*
**Files:** game.cpp:291 (`rand()` → `sparkRng`), `CMakeLists.txt`.
**Steps:** replace the unseeded `rand()`; in the Windows CI job build `envelope_test` with and without `/fp:fast` and run both (the envelope must bound the terrain either way); if it fails without, drop `/fp:fast` from `CORE_SOURCES`.

#### D2 · Dead/stale state  *(tiny)*
**Files:** `game.h` (`Settings::renderScale`), career.cpp (`mult`), aircraft.cpp (`pRate`), `game.h` `K_*` enum → shared `keys.h`.
**Steps:** delete `Settings::renderScale` (use `Renderer::renderScale`), `(void)mult`, `(void)pRate`. Keep `OwnedPlane::fuel/condition` — they become live in C3/C7.

#### C4 · Parking-brake stiction  *(tiny · gate: test)*
**Goal:** a braked aircraft holds at full power.
**Files:** aircraft.cpp:354–355 (`Plane::substep` wheel friction).
**Steps:** when `ctl.brake > 0.5 && |vlong| < 0.3`: force needed to hold = −(thrust + gravity) projected along the wheel direction, divided among braked wheels; cap at `nF·μs` (μs 0.9 paved, 0.7 rough); apply the cap instead of `clamp(vlong/0.3)`; the wheel slips only when the cap is exceeded.
**Accept:** `gameplay_test` parking case asserts speed after 8 s at full power < 0.02 m/s for every career aircraft; takeoff roll times unchanged (±1%).

#### C1 · Autopilot comfort law  *(small · gate: test)*
**Goal:** the AP never earns a passenger/fragile penalty; the stick stays unrestricted.
**Files:** aircraft.h/.cpp (`apControl` 965–1030), game.cpp (`startFlight`), game_ui.cpp (Settings), `tests/flight_test.cpp`.
**Steps:** `Plane::apComfort` (bool). When set: `bankMax = min(bankMax, 25°)`, `nzMax = min(nzMax, 1.3)`, `nzMin = max(nzMin, 0.8)`, `vsUp = min(vsUp, 0.6·P.roc)`, `rollCap = min(rollCap, 15°/s)`. `startFlight` sets it for every career flight; research craft keep false; Settings toggle "Autopilot: comfort / performance" for free flight. Keep APPR's 20°/35°.
**Accept:** `flight_test`: HOLD with comfort on, 150° heading change → bank ≤ 26°, 0.8 ≤ g ≤ 1.3; NAV case same; existing cases unchanged with comfort off.

#### E1 · Honest quotes and hygiene  *(small–medium · gate: test)*
**Goal:** one launch plan used by card, launch and settlement; small long-session fixes.
**Files:** career.h/.cpp, game.cpp, game_ui.cpp (`drawHubContracts`), atc.h/.cpp, `tests/gameplay_test.cpp`.
**Steps:**
1. Add `LaunchPlan` (Appendix A.1) from `Career::plan(contract, spec, src)`; `estimate()` becomes a wrapper; job card shows **payment / est. operating costs / est. net** and the current lateness, passenger, fragile rules; "may miss the deadline" when `minutesEst + minutesSigma > timeLimitMin`. Fuel policy: `FUEL_INCLUDED` rentals, `FUEL_BILL_CONSUMED` owned (C3 adds `FUEL_PURCHASED`).
2. Estimator: cruise = `Plane::perf(s).cruiseV` (C2 adds it — until then use `s.cruise`); route highest ground sampled **along waypoints** (also fix `canFly` 239–244); headwind per leg from `wx`; descent allowance = altitude to lose / type descent rate + pattern time, replacing the flat `+4 min / +6 km`; `minutesSigma = 0.15·minutesEst`.
3. `gameplay_test`: fly A4 on the autopilot; assert `|flownMin − minutesEst| ≤ minutesSigma`. Keep A4's deadline unless the briefing can't be made honest.
4. `AtcVoice::historyLimit = 64` in play (tests set 0 = unbounded).
5. Move the time-accel auto-cancel block above `float simDt = dt * timeAccel;` (game.cpp:557).
6. LB+RB chord: `if (in.pad && !wraith.armed && …)`; add "Hide flight UI" to the pause menu.
7. Hints: replace literal key names in `buildStory` with `{ACT_PARK}`-style tokens expanded by `keyName/padName` at display time. Voice: `AtcVoice::resolve(message, mission, pad, tx)` gets a `bindingsDefault` flag for the actions the line names; if false, use fragment composition (`fragment.aster.*`) when available, else subtitle only.
**Accept:** `gameplay_test` A4 case; a rebound key makes the lesson hint show the new key and plays no recording naming the old one; `history.size() ≤ 64` after 3 flights.

### Phase 1

#### A7 · Frame-rate unlock + fixed-size render targets  *(small–medium · gate: test (5 fps case), owner check at 144 Hz)*
**Files:** platform_win32.cpp (`setupPacing` 41, limiter 1004–1017), game.h/.cpp (`Settings`, `update` 2347–2363), renderer.cpp (`createRenderTargets` 776, `setRenderScale` 837), shaders.h (`kTaaFS`, `kCloudCompFS`, sprite, `kRayMaskFS`), game_ui.cpp (Settings).
**Steps:**
1. `Settings::fpsTarget` (0 = monitor refresh via vsync; 30/60/90/120/144/240). `setupPacing`: target 0 → swap interval 1 (adaptive if `s_tear`); else interval 0 + limiter with `period = freq / fpsTarget`.
2. Scaler: `budgetMs = 1000 / effectiveHz · 0.92` replaces 15.5/12; keep the 0.4 s hysteresis.
3. Grep `* 60` / `/ 60.f` outside tests and the menu-video player; make TAA blend `a = 1 − pow(1 − a, dt·60)` (uniform `uDt`); `fpsAvg` lerp by dt.
4. Allocate `texRaw/texDepth/texCloudMask/texGB*/texGBDepth/texEnv*/texCloud*` at `W×H` once; render with `glViewport(0,0,rw,rh)`; scale normalized UVs by `uRawRes/uRes` in `kTaaFS` (`texture(uRaw, ruv)`), `kCloudCompFS`, the sprite FS (`uRes`), `kRayMaskFS` (`uUVS`); `texelFetch` users only need the clamp. `setRenderScale` = set `rw/rh` only.
5. Settings UI: frame-rate target; show measured fps / GPU ms; add max-frame-time to F3.
**Accept:** no reallocation in `setRenderScale` (assert); llvmpipe image invisible; owner: >60 fps at 144 Hz in `air`, no hitch on scale steps.
**Don't:** change `Plane::step` substepping or the 50 ms catch-up cap.

#### A3 · Instrument atlas static/dynamic split  *(small–medium · gate: measure (new display stamp), image)*
**Files:** renderer.cpp (`renderDisplays` 588, `kPasses`, `stamp`), shaders.h (`drawInstruments` 2304, `mfdPage`, `panelTex` 2175, page sampler), game_ui.cpp F3.
**Steps:**
1. `uniform int uDispLayer`: 0 static (`dialFace`, `dTicks`, `dNums`, bezels, LCD backgrounds, PFD frame, page chrome), 1 dynamic (`dNeedle`, attitude ball, compass card, drums, tapes, radio digits, PFD symbology, page data). Dynamic layer writes premultiplied colour + coverage alpha.
2. Textures: `texPanelS/texPagesS` rendered once per cockpit type (`fp.dispCk` change, first use) with mipmaps; `texPanelD/texPagesD` per frame, **no** `glGenerateMipmap` — sample with a 2×2 supersample in the ray tracer.
3. Composite `D over S` in `panelTex` and the page sampler.
4. Add `stamp()` around `renderDisplays` (bump `kPasses`, `passWall` static_assert, F3 labels).
5. When over budget: dynamic layer at 30 Hz alternating panel/pages (hook into the scaler).
**Accept:** display stamp < 25% of before in `cockpit`, `rjetc`; cockpit shots identical by eye.

#### A4 · Point-light shadow budget  *(small · gate: measure "light steps" in `night`, night `cockpit`, `wr_*`)*
**Files:** game.cpp (`buildLights` 1079), shaders.h (`shadeSurface` 1942–1960, `pieceShadow` 1214).
**Steps:** at most 2 lights with `shadow > 0` per frame — landing lights when on, else the brightest flame pair; nav/strobe/beacon get `shadow = 0`. Threshold: shadow ray only if `lum > 0.02·(0.05 + luminance(col_so_far))`. `pieceShadow` loop 40 → 24.
**Accept:** light-step counter down in the three scenes; no visible loss on `render_shots` night set.

#### A1 · Cheaper terrain normals  *(small · gate: measure terrain samples/px, image)*
**Files:** shaders.h (`terrainNormal` 1315, `terrainFbm` 52, `baseAt` 38, `applyTS` use at 3356).
**Steps:**
1. Forward differences using the hit height: `h0 = p.y`, evaluate `h(p+e·x)`, `h(p+e·z)` only (2 calls, not 4).
2. Octaves for the normal = octaves of the geometry (7/6/5 by distance). Fine detail: raise near-band `applyTS` strength 0.6 → 0.8 and add a 2-octave micro normal from `noised()` at 8 m / 2 m, computed once.
3. A/B (optional): `terrainFbmD(p, oct, out grad)`: per octave `grad += b·(Jᵀ·n.yz)/(1+dot(d,d)); J = M·J` with `M = [[1.6,−1.2],[1.2,1.6]]`, start `J = I`, then `grad /= 2200`; base gradient from `baseAt`'s corner differences; `n = normalize(vec3(−(amp·grad.x + gbase.x + fbm·gamp.x), 1, −(…z)))`. Keep only if `air`/`mountain` lighting is not visibly different.
**Accept:** steps 1–2 invisible (PSNR > 38 dB); counter down.

#### C2 · Learned performance drives quotes and gating  *(small–medium · gate: test)*
**Files:** aircraft.h (`PerfModel`), aircraft_perf.cpp, career.cpp (250, 268, 326, `runwayNeeded` via aircraft.h), `tests/flight_test.cpp` (`--table`), README table.
**Steps:** add `cruiseV` (level speed, 75% power, 1500 m, mid weight), `toRoll` (lift-off at MTOW, sea level, no wind), `ldgRoll` (1.3·Vs0 to stop, max braking) to `PerfModel`, learned in `Plane::perf()`. `estimate/canFly/refreshBoard` use `cruiseV`; `runwayNeeded = max(toRoll, ldgRoll)·1.15·(1 + elev/3000)`. `flight_test --table` prints the README aircraft table; D4's CI check compares it.
**Accept:** `progression_test` still finishes with ≤ the same grind; E1's A4 assertion holds.

#### E5.1–3 · Contextual input dispatch, release-to-rearm, controller loss  *(small–medium · gate: test)*
**Files:** game.h (`Input`, `actPressed/actDown`), game.cpp (`update`, `flightControls`, GPS handling), platform_win32.cpp (`pollPad`).
**Steps:**
1. `enum InputCtx { CTX_BIND, CTX_DIALOG, CTX_PAUSE, CTX_SCREEN, CTX_OVERLAY, CTX_FLIGHT }`; `Game::update` computes `ctx` once per frame in that priority. `static const unsigned kActionCtx[ACT_COUNT]` bitmask; `actPressed(a)`/`actDown(a)` return false unless `ctx ∈ kActionCtx[a]`. GPS overlay consumes selection/confirm edges, passes flight axes.
2. `Input::armed[256]`, `padArmed`: on a context change, every held key/button becomes unarmed; it cannot produce `pressed` until released (loading A → brake; pause A → fire; reconnect).
3. `pollPad` disconnect → pause + device prompt; reconnect does not resume.
**Accept:** `gameplay_test`: A held through loading never brakes; Esc→pause→A never fires; chord disabled when armed.
**Don't:** change AP hold vs route/stunt disconnect semantics; keep the virtual mouse cursor.

#### B1 · WASAPI audio  *(small · owner test)*
**Files:** platform_win32.cpp:124–175, audio.cpp:395 (block logic).
**Steps:** `IMMDeviceEnumerator → GetDefaultAudioEndpoint → IAudioClient::Initialize(SHARED, EVENTCALLBACK|AUTOCONVERTPCM|SRC_DEFAULT_QUALITY, 10 ms)`, float32 stereo, event-driven thread calling `g_audio.render(buf, framesAvailable)`; handle `AUDCLNT_E_DEVICE_INVALIDATED` by reopen; fall back to waveOut on failure. Make `Audio::render` accept any frame count (internal 64-frame ring if the `& 63` block logic needs it).
**Accept:** latency ≈ 10–20 ms (owner); no dropouts over a 20-minute flight.

### Phase 2

#### A5 · Temporal upsampling (TAAU)  *(medium · gate: image at 0.67 and 0.5 scale)*
**Files:** shaders.h (`kTaaFS` 4727), renderer.cpp (jitter, uniforms), game.cpp (scaler default 0.75).
**Steps:**
1. 16-point Halton(2,3) jitter in render-pixel units.
2. Reconstruction: gather 3×3 render samples around the output pixel; sample position `(ip+0.5)/rawRes + uJit`; `w = exp(−d²/(2σ²))`, `d` in output px, `σ = 0.45·(W/rw)`; `cur = Σw·c/Σw`; `conf = clamp(Σw, 0, 1)`.
3. Blend: `a = mix(aMin, aMax, motion)`, `a *= conf`, then `a = 1 − pow(1 − a, dt·60)`.
4. Variance clip from the 3×3 render-res neighbourhood, box widened by `(1 − conf)·0.5·sd`.
5. Keep class-flag disocclusion. Scaler: default 0.75, step up only with > 25% headroom.
**Accept:** add `--scale` to `render_shots.bat`; 0.67 vs 1.0 indistinguishable on static shots; no new ghosting on `storm` with the Spectre pair.

#### A6 · UBO for scene parameters  *(small · gate: CPU ms of `renderScene` on F3)*
**Files:** renderer.h/.cpp (`setRT`), shaders.h (`layout(std140) uniform SceneParams` in common), gl.h (`glBindBufferBase`, `glUniformBlockBinding`, `glGetUniformBlockIndex`).
**Steps:** one std140 block for every non-sampler in `setRT` (camera, sun, weather, plane `M/PS/Ctl/Pr/I*`, lights, traffic count, fx, feeds); one `glBufferSubData` per view; samplers set once at link.
**Accept:** invisible image; fewer GL calls per frame (count in a debug counter).

#### E2 · Transactions, save v3, attempt marker  *(medium · gate: test `save_test`, `gameplay_test`)*
**Files:** career.h/.cpp (`save` 428, `load` 451), game.cpp (`endFlight` 317, `saveGame` 220, buy/sell callers), game_ui.cpp (debrief retry button), `tests/save_test.cpp`.
**Steps:**
1. Settlement = candidate snapshot (Appendix A.3). `Game::commitSettlement()` is the only writer; `buy/sell` use `Game::commit(fn)`. While `pending` is set: Accept/Buy/Sell disabled with the reason; retry on the debrief button and on hub entry.
2. Schema v3 (Appendix A.4). `load` accepts 1–3; v1/v2 → `job none`, `attempt_open 0`.
3. `startFlight` saves with `attempt_open 1` before the flight; on load with `attempt_open 1`: message "your last flight was interrupted; you are back at <airport> with the job still accepted". No midair restore.
**Accept:** `save_test`: round-trip with a job in each state; v2 loads; truncated v3 falls back to `.bak`; a failed write leaves `career` untouched and `pending` set; a second commit cannot double-pay (money asserted).

### Phase 3

#### A8 (steps 1–2) · Lighting pass, terrain pass  *(large · gate: measure whole frame + per pass; image per step, invisible)*
**Files:** shaders.h (→ `kTerrainFS`, `kPlaneFS`, `kLightFS`, shared common), renderer.cpp/.h (`compilePrograms`, `renderScene`, `rtSource` per pass), `tools/shader_check.cpp`, `tests/shader_keyword_test.cpp`, `tests/render_harness.cpp`.
**Design:** passes E (exists) · T terrain/water/boxes → same G-buffer with depth test (write `gl_FragDepth` in the entity log-depth encoding `uLogC`; classes 1 terrain, 2 water, 3 box, 5 entity) · P player (+wreck, debris, UFO) · R traffic · L lighting (full screen → `texRaw`, `texDepth`, cloud mask exactly as today) · C clouds (exists). Add `texGB[3]` RGBA8 (P extras: cabin-light/AO pre-term, up to 3 point-light visibilities, cloak mask). Water stores its precomputed colour in `texGB[1]`. Keep the uber path behind `--rt-legacy` until the last step.
**Steps:**
1. Extract `shadeSurface`, `applyFog`, water shading, sky, point-light loop into `kLightFS`; make the uber shader write the G-buffer instead of colour. Gate: PSNR > 45 dB vs legacy.
2. Move terrain/water/boxes into `kTerrainFS` (depth-tested into the G-buffer); the remaining uber program is `kPlaneFS`. Gate: measure — terrain pass must drop; image invisible.
**Don't:** drop `COST()` counters; `buildCostProgram` must build the cost variant of each pass.

#### E3 · Resumable jobs  *(medium–large · gate: test · depends E2)*
**Files:** career.h/.cpp, game.h/.cpp (`startFlight`, `endFlight`, `updateFlight` 746–800), game_ui.cpp (contracts tab, debrief), world.h (`Airport::hospital`), `tests/gameplay_test.cpp`, `tests/progression_test.cpp`.
**Steps:**
1. `JobState` (Appendix A.2); `Career::job` (one at a time).
2. Accept: `career.accept(contract, spec, src)` → `READY` with `plan`. Positioning/ferry charged and `at = c.from` only when the flight **begins** (cancel in the hub is free).
3. `startFlight(JobState&)`: departure = `job.at`; `wpIndex = job.wpDone`; `flightClock = 0`; deadline uses `job.jobClockMin + flightClock/60`; same aircraft + `hirePaid` → no hire; different aircraft → new quoted hire shown before confirm.
4. `Career::closeLeg(job, result)` on OUT_DIVERTED: bill owned fuel for this leg (`FUEL_BILL_CONSUMED`); `jobClockMin += flightMin`; aggregate `maxG/minG/maxBank/fragileHit`; `legs++`; `wpDone = wpIndex`; `at = divertedTo`; `location = at`; owned aircraft location = `at`; **no reputation change**; state `RECOVERY`; flight time/landings counted here. Debrief title "Diverted to <airport> — job continues".
5. `Career::settleJob(job, result)` on success at `c.to` with all objectives: pay once; landing bonus from the final touchdown; lateness from `jobClockMin`; passenger/fragile deductions from aggregates; story/licence; `boardSeed++`; `DONE`; `job.reset()`.
6. Crash / airborne fuel exhaustion → `FAILED` (today's costs, load lost). Release → `CANCELLED`, load to local custody, no fee. Off-airport landing intact → load road-recovered to `g_world.nearestAirport` for the existing recovery fee, job continues there. Airborne abandonment → load back to the attempt's departure, recovery fee, `RECOVERY`.
7. Recovery screen: job card pinned on top of the contracts tab; **Continue** (aircraft picker → revised quote showing only new costs), **Release the job**, **Practise the approach** (isolated free flight to `c.to`, tagged like `researchFlight`); disabled buttons show why.
8. Policy: resumable by default for cargo/pax/tour/survey; lessons and checkrides (`forceAircraft ≥ 0` or `grantLicense ≥ 0`) retake whole; timed/VIP resume with the clock; medevac keeps the destination requirement. Add `Airport::hospital` for CAP, NPT, PVI now (data for a later alternate policy).
9. Checkpoints: closest approach of segment `prevPos → plane.pos` (game.cpp:564) to the ring centre ≤ 110 m; active checkpoint only; nav card shows its distance, MSL and AGL.
10. Touch-and-go must not close a leg (`stillTimer` 1.2 s stop already; keep).
**Accept:** `gameplay_test`: accept → divert → quit → reload → continue → deliver: hire charged once, fuel per leg, payment once, `legs == 2`, locations, aggregates; go-around keeps `wpDone`; release leaves the load at `at`; crash fails with repairs. `progression_test`: every `g_story` entry has a continuation policy (assert `policyOf(c) != UNSET`): L1 airborne completion, L2 same-field pattern, the four checkrides, `ownedOnly` O-chapter, A4 deadline, A7 finale.

### Phase 4

#### A8 (steps 3–5) · Hull-restricted aircraft pass, traffic pass, cloak  *(large · gates as A8)*
**Steps:**
3. Draw the hull mesh with the SDF-marching `kPlaneFS` (raster depth = march start), `glDepthFunc(GL_LESS)` against the merged depth; cabin view stays full-screen until A12. The hull draw must apply `uJit` like the entity VS.
4. `kTrafficFS` via `drawTrafficHulls`.
5. Cloak → post step in L: P writes mask + projected offset into `texGB[3]`; L samples the lit scene at the offset.
6. Feeds run the same passes (`setRT`/`trace` lambdas → `drawPasses(view)`).
7. Delete the legacy path once `multi:` diffs are clean.
**Accept:** owner numbers show the aircraft pass cost scales with its screen area; first-run compile time recorded (feeds B3).

#### A9 · Shadow maps for the aircraft  *(medium · depends A8.1 · gate: measure light steps in `night`, `cockpit`)*
**Files:** renderer.cpp (new FBOs, `kShadowMarchFS` modelled on `kHullBakeMain`), shaders.h (`planeShadow` → sampler), game.cpp (`buildLights` publishes mapped lights).
**Steps:** SDF-rendered depth maps (fragment shader marches the SDF from the light's view): sun = 512² ortho over the aircraft's bounding box per frame, 3×3 PCF + existing penumbra → `planeShadow` on terrain/entities/water becomes a lookup; self-shadow in close-up stays in P (or mapped with normal-offset PCF if acne is acceptable). Point lights: landing lights 256² spot maps; flames 256² dual-paraboloid; ≤ 3 shadowed lights (A4's policy). `trafficShadow` blob unchanged.

#### E4 · Earning-path guarantee  *(small–medium · gate: test · depends E3)*
**Files:** career.cpp (`refreshBoard`), `tests/progression_test.cpp`.
**Steps:** `Career::earningPath()` after `refreshBoard()` and every commit: true if a board job has `canFly != SRC_NONE` with positive net under its plan, or `nextStory()` is a free lesson. If false → push `recoveryContract(location, license)`: light non-fragile mail, no deadline, daylight, mild weather, a rentable licensed aircraft, fuel included, net floor ≥ $150 after fixed fees and the hard-landing deduction; if no licensed aircraft can use the local strip, offer from the nearest compatible hub with courtesy ground travel.
**Accept:** sweep airport × licence × money ∈ {−8000, −1500, 0, 500, 3000} × fleet ∈ {none, one owned remote}: `earningPath()` true; the board's 200-try limit never yields an empty board.
**Don't:** add any cash gate to rentals or courtesy positioning.

#### E5.4 · Menu focus navigation  *(medium · one screen per PR)*
**Files:** game_ui.cpp (`button/card/tab`, `anim(id)`), game.cpp (`gamepadMenus`).
**Steps:** per-frame widget registry `{id, rect, enabled}` (ids via the existing `anim` hashing: screen + label + index); D-pad/stick → nearest rect in the pressed direction (60° cone); A sets `uiActivate = id`, consumed by the matching widget's next `button()` call (same path as a mouse click: one activation per frame, never from draw code); B backs out; shoulders change tabs; remember focus per screen; scroll focused row into view; disabled items focusable to show the reason. Order: pause → hub contracts → hangar → settings → controls → research. Virtual cursor stays until all screens register ids. Controls screen checks binding overlaps across contexts (fixed shortcuts, chords, GPS D-pad).

### Phase 5

#### Decision point · A12 go/no-go
From the owner's A8/A9 numbers: if the cabin view is still the slowest scene by a wide margin, do A12; else continue with A10/A11/A2 as the profiler directs.

#### D1 + E6 · `FlightSession` split, clocks, ATC validity  *(medium · gate: test)*
**Files:** game.h/.cpp, atc.h/.cpp.
**Steps:**
1. Extract from `Game`: `FlightSession` (plane, contract, wx, timers, phase, arrival recording, controls, `timeAccel`, warnings, `commsPending`, `hintsVoiced`, trails, toasts of the flight), `CrashFx` (particles, wreck, debris, pops, craters), `Comms` (ATC state `atcF`, `updateAtc/updateComms`), `WraithSystems`, `MenuTour/Loading`, `UiState`. `startFlight` does `session = FlightSession{}` — that assignment is the reset checklist. Practice/research flights set `session.isolated = true`; `endFlight` returns before any career call when set.
2. Clocks: `double realTime, gameTime`; `flightClock` (attempt, simulated); `job.jobClockMin` (cumulative, simulated); UI on real time. Pause/loading/lost focus advance neither flight nor job time (verify `updateLoading`).
3. ATC: world-state checks read traffic at simulated time; call spacing uses real playback time; `AtcVoice::Tx` gets a validity key `{phase, airport, runwayReverse}` checked in `update()` when the transmission starts — stale → dropped, phase re-evaluated. After a pause show the last instruction labelled "(before pause)".
4. F3 lines: particles / chunks / hulls / history counts.
**Accept:** `gameplay_test` 5 fps timing and tower cases unchanged; a 4× cruise with a go-around never plays a stale clearance (new case).

#### C9 · Scoring lines from recorded data  *(small · gate: test)*
**Files:** career.cpp (`settleJob`), game_ui.cpp (debrief).
**Lines:** touchdown zone (first third) +5% / floated past half −5% (surface `longLdg`); centreline < 2 m +2%; stable approach (threshold within Vref −5/+15 kt, 30–80 ft) +3%; needed go-around free; fuel reserve ≥ 15% +2%, < 5% −10% and −rep; taxi to a stand and shut down +2%.

#### C10 · ATC compliance  *(small · gate: test)*
**Files:** game.cpp (`updateAtc`), career.cpp.
**Rules:** takeoff without clearance −10% and −rep; landing after a go-around instruction = leg fails; ignoring "hold position" −rep. HUD shows the current clearance state (recall line exists).

### Phase 6

#### A10 · Water reflection probe  *(small · gate: image `air` over the sea at low sun)*
256×128 lat-long (or 6×64² cube) of `skyColor + traceClouds` at 16 steps, every 4th frame; water samples it with the reflected direction; per-pixel march only at `uQuality == 2` within 2 km.

#### A11 · Cloud march  *(small · gate: measure cloud steps)*
Skip the light march when `T < 0.1`; halve light taps beyond 20 km; reproject last frame's `texCloud` through `uPrevCamRot` (blend 0.7) and drop `N` to 24/32.

#### A2 · Baked partial fbm  *(medium · strictly gated on the terrain-samples/px heat map: skip if the median near the ground is < 6)*
Bake octaves 0–3 state `(a₃, d₃.x, d₃.y)` at 4096² (RGBA16F, 134 MB) or 2048² RGB32F; GPU continues from octave 4 with `p` pre-transformed by `M⁴`, `b = 1/16`. **Consistency rule:** `World::height`, `buildEnvelope`, `buildHMax` must sample the same array with the same manual bilinear; `envelope_test` is the guard; update `kTShBakeMain`/`kMapMain` includes.

#### C3 · Fuel and payload planning  *(medium · gate: test · depends E1)*
**Files:** career.h/.cpp (`LaunchPlan::fuel = FUEL_PURCHASED`), aircraft.h (`maxMass`), world.h (`Airport::fuelPrice`), game.cpp (`startFlight` fuel from plan), game_ui.cpp (pre-flight card, hangar refuel).
**Steps:** pre-flight card: fuel slider from `plan.fuelKgEst·1.25` to `maxFuel`; live take-off weight vs MTOW, take-off roll at departure elevation (`toRoll·(mass/MTOW)²/σ`), range ring, fuel cost here. `Airport::fuelPrice`: hubs ×1, strips ×1.4, glacier/volcano ×1.8; owned aircraft pay at uplift and keep `fuel` between flights (already saved); hangar refuel; ferry moves the aircraft with its fuel. Overweight = cannot dispatch (message). Rentals stay `FUEL_INCLUDED`.
**Accept:** `progression_test`: every story contract feasible at minimum fuel + reserve; money model includes fuel.
**Don't:** bill consumption and purchase in the same mode (they are alternatives).

#### C5 · Financing and pacing  *(medium · gate: test)*
**Files:** career.h/.cpp, game_ui.cpp (hangar).
**Steps:** `Career::loan {principal, rate, paymentPerFlight, missed}`: 20–30% down, 8–12% rate, payment on each completed flight; N missed → repossession. Used aircraft at 60–70% with lower `condition`. Freelance payout × `(1 + 0.35·chapter)`.
**Accept:** `progression_test` money model with the loan option: grind ≤ 5 jobs at any point.

#### C7 · Failures and maintenance  *(medium · gate: test · depends C3)*
**Files:** aircraft.h/.cpp (`Plane::Failure` bitset: engine partial/total per engine, alternator, pitot, gear leg, flap asymmetry, icing), career.cpp (condition decay, maintenance, insurance), audio.cpp (cylinder dropouts, n1 roll-back), atc voices ("engine failure", "alternator"), game.cpp (AP disengages on engine failure; HUD glide range from the GPS ring).
**Rules:** `p = base·(1.5 − condition)` per flight for owned aircraft; rentals fixed low; lessons zero. Condition decays per hour and per hard landing; maintenance restores; insurance premium per flight covers repairs.
**Accept:** `flight_test` cases per failure (engine-out glide to a field; stuck gear belly landing survivable at Vref); `gameplay_test` engine-out landing.

### Phase 7

#### A12 · Bake the aircraft to meshes; rasterize; SDF offline  *(very large · only after the Phase 5 decision)*
**Steps:** (1) `uPartMask` so `mapPlane/mapWraith` evaluate one part at a time; dual-contouring bake per part at neutral pose (5 cm outside, 2 cm cabin), hermite data from the SDF gradient; per vertex: position, normal, material id (`.y`), local position; disk cache keyed like `hullKey`. (2) Fragment shader calls the existing `planeMaterial(lp, id, n)` / `shadeWraith` with interpolated `lp`/id; moving parts transformed in the VS from `Ctl/PS/wr` exactly as the SDF does. (3) Shadow maps from A9 drawn with the meshes. (4) Cabin: baked per-vertex AO (cone taps at bake) + SSAO; fixtures sample shadow maps. (5) Per-aircraft visual test: render mesh vs SDF to PNG via the harness and diff; keep the SDF march as a flagged fallback until each type is signed off. (6) Traffic: 5 cm set with two 4:1 decimated LODs.

#### A13 · Terrain clipmap  *(very large · only if terrain is the last bottleneck after A12)*
CDLOD from the 2048² base + fbm displacement by level; `terrainMaterial` moves onto the mesh; water as a plane; keep the terrain-shadow bake; remove envelope/hmax/march. A Vulkan/DX12 port becomes mechanical after this.

#### B3 · Drop the helper-process shader compile  *(after A8)*
If first-run compile < ~5 s on the owner's GPU, remove `childReader` (platform_win32.cpp:385–414) and compile on the worker thread with the shared context; keep the binary cache.

#### C6 · Freelance job types  *(medium–large · gate: one `gameplay_test` flight per type · depends C1, E3)*
Add `ContractType` entries and generator cases; each with its `settleJob` scoring and HUD element:
Medevac (hard clock; patient tolerance meter: g > 1.5, bank > 30°, touchdown > 300 fpm; `Airport::hospital` destinations) · Night freight (21–05, `Airport::lit` both ends, landing-light use) · IFR/low-vis (vis 1.5–3 km, base 300–600 ft; break-out check below minimums → go-around required) · VIP (live comfort meter) · Survey (ring pattern at fixed altitude ±50 m, coverage %) · Organ run (medevac + chained leg). Weight by licence/location; each names its challenge in the plan.

#### C8 · Dynamic weather  *(medium · gate: test wind-shift case)*
`Contract.wxEnd`; `updateFlight` interpolates by `flightClock / plan.minutesEst` (clamped) + smooth noise; ATC reads current wind; brief shows a forecast; `apPlan` re-picks the runway at APS_NAV → APS_FINAL if the wind flipped.

#### C11 · Airline layer  *(large · depends C5, C7, E3)*
`Career::airline {routes, pilots, dailyTick}`; Airline tab after A7; owned aircraft on routes; incidents from C7 by pilot rating; scheduled flights appear as traffic in the player's livery; save bump.

#### C12 · Challenge modes  *(medium)*
`CT_TRIAL`: canyon gate runs (Spine), spot landing (distance to mark, fpm, stop), STOL contest at Summit Pass, formation run with the Spectre pair; local leaderboard per trial; daily seeded gate course. Reuse `ringGeom`/`bursts`.

#### C13 · Surface NIGHTGLASS  *(small)*
Main-menu entry (after the campaign, or from the start with a "does not count" confirmation) and a short objective chain for the XR-30/XR-40.

#### C14 · Options  *(small)*
FOV slider (game.cpp:1627–1629), frame-rate target (A7), head-look toggle, colour-blind HUD palette, UI scale, HUD-off default per camera.

---

## Appendix A — Data structures and schemas

### A.1 `LaunchPlan` (career.h)
```cpp
struct LaunchPlan {
  int spec; Career::Source src; int startAirport;
  int positioning, ferry, hire;                    // fixed, quoted exactly at acceptance
  enum FuelPolicy { FUEL_INCLUDED, FUEL_BILL_CONSUMED, FUEL_PURCHASED } fuel;
  float fuelKgEst, minutesEst, minutesSigma;       // estimates with visible uncertainty
  int fuelCostEst, net;                            // net = payout - fixed fees - fuelCostEst
  std::string challenge;
};
LaunchPlan Career::plan(const Contract&, int spec, Source src) const;
```

### A.2 `JobState` (career.h)
```cpp
struct JobState {
  Contract c;                 // terms frozen at acceptance
  LaunchPlan plan;
  int spec; Career::Source src;
  enum State { READY, ACTIVE, RECOVERY, DONE, FAILED, CANCELLED } state;
  int at;                     // airport where the load / party currently is
  int legs = 0, wpDone = 0;
  float jobClockMin = 0;      // cumulative simulated minutes against the deadline
  float maxG = 1, minG = 1, maxBank = 0; bool fragileHit = false;
  float fuelBilledKg = 0;
  bool hirePaid = false, positioningPaid = false;
  uint32_t id, attempt;
};
std::optional<JobState> Career::job;   // one at a time
```

### A.3 Transactional settlement (game.cpp)
```cpp
void Game::commitSettlement() {
  Career cand = career;                       // Career is a plain value type
  payout = cand.settleJob(*cand.job, result, &stars);   // or closeLeg(...)
  if (cand.save(savePath)) { career = cand; pending.reset(); }
  else pending = cand;                        // debrief shows "not saved - retry"; Accept/Buy/Sell disabled
}
```

### A.4 Save schema v3 (`Career::save/load`)
```
solace_save 3
money … license … rep … location … story … flights … landings … crashes … hours … best … seed … finished …
attempt <uint32>            # monotonic per career
attempt_open 0|1            # 1 = a flight was in progress when this was written
fleet N
plane <id> <location> <fuel> <condition>
job none | job <state> <spec> <src> <at> <legs> <wpDone> <jobClockMin> <maxG> <minG> <maxBank> <fragileHit> <fuelBilledKg> <hirePaid> <positioningPaid> <id>
contract <id> <type> <from> <to> <cargoKg> <pax> <payout> <timeLimitMin> <minLicense> <ownedOnly> <fragile> <story> <startAirborne> <repBonusPct>
wx <windFrom> <windSpeed> <gust> <turbulence> <cloudCover> <cloudBase> <visibility> <precip> <storm> <timeOfDay>
wps N  /  wp <x> <z> <alt> ...
title <quoted> / brief <quoted>
plan <positioning> <ferry> <hire> <fuelPolicy> <fuelKgEst> <minutesEst>
end
```
Serialize the accepted contract in full (freelance boards are seeded by `boardSeed` **and** `location`, so regenerating from an id is fragile); story contracts may store only `contract <id>` and resolve the rest from `g_story`. Quote strings with the existing escaping in `save/load`. v1/v2 files load with `job none`.

### A.5 Input contexts
`CTX_BIND > CTX_DIALOG > CTX_PAUSE > CTX_SCREEN > CTX_OVERLAY > CTX_FLIGHT`. Flight axes pass through `CTX_OVERLAY`; everything is owned by `CTX_PAUSE`/`CTX_DIALOG`; `CTX_BIND` captures one input and nothing fires.

## Appendix B — Test matrix

| Test | Extended by |
|---|---|
| `flight_test` | C1 (comfort cases), C2 (`--table`, cruiseV/toRoll/ldgRoll), C7 (failures) |
| `progression_test` | C2, C3 (min-fuel feasibility), C5 (loan model), E3 (policy per story contract), E4 (earning-path sweep) |
| `gameplay_test` | C4 (stiction), E1 (A4 estimate, history bound, hint rebinding), E5 (rearm/context), E3 (divert→resume→deliver, go-around, release, crash), E6 (stale clearance at 4×), C6 (one per type), C7 (engine-out), C8 (wind shift), C9/C10 |
| `save_test` | E2 (v3 round-trip, v2 load, failed write, no double pay) |
| `envelope_test` | A2 (consistency), B5 (`/fp:fast`) |
| `render_harness` (llvmpipe) | A1, A3, A5, A6, A7, A8 each step, A9, A12 per aircraft |
| CI | B4 (Linux ASan/UBSan), D4 (render regression with reference PNGs), C2 (README table check) |

## Appendix C — Dependencies
A9 ← A8.1 · A12 ← A8, A9 · A13 ← A12 · B3 ← A8 · E2 ← E1 · E3 ← E2 · E4 ← E3 · E6 ← D1 · C3 ← E1 · C7 ← C3 · C9/C10 ← E3 · C6 ← C1, E3 · C11 ← C5, C7, E3.
