# Solace Express — Gameplay, Code & Quality Analysis (v3.43.0, re-verified on v3.44.0)

> Claude's standalone analysis, kept as evidence. **`../REVIEW.md` supersedes it.** That combined review merges these findings with Codex's audit, renumbers them, and records Codex's correction to N1 (the Low-quality preload is documented intent, so it is P3).

**Releases:**
- `v3.43.0` (tag → `91212bf`, 2026-10-10 12:34 UTC): the full analysis.
- `v3.44.0` (tag → `b22c2fc`, 16:29 UTC, "Living Islands, first step"): every finding re-checked, plus a review of what v3.44.0 adds (§2).

**Repo:** `chad04220/Solace-Express` (formerly Air-Xpress). **Not covered:** `main` past v3.44.0 (`1836295`, "Eastern islands settled; road network", unreleased).
**Environment:** Linux container, GCC 13, clang-tidy 18. No GPU and no EGL, so nothing was rendered live.
**Line numbers below are v3.44.0's** (v3.43.0 differs only in `game.cpp`, +1, and `renderer.cpp`, about +99).

---

## 0. Summary

| | v3.43.0 | v3.44.0 |
|---|---|---|
| Build (`-Wall -Wextra`, all Linux targets) | ✅ clean | ✅ clean (same warning set) |
| Tests (`ctest`, full flight model) | ✅ 39 / 39 | ✅ 51 / 51 (1 skipped: needs a GPU) |
| CI on the release commit (MSVC + ASan/UBSan + release) | ✅ | ✅ (run 857) |
| Confirmed gameplay / economy bugs (§3) | 7 | **all 7 still present** |
| New findings in v3.44.0 (§2) | — | 2 Medium, 4 Low |

**Bottom line:**
- The engineering is unusually strong for a solo game: physics from first principles, an autopilot that learns each airframe, an end-to-end game-loop test, a careful save system, and a sanitizer CI.
- The weak points are in **game rules and economy edge cases**. None of them is tested, and v3.44.0 (an environment release) touched none of them.
  - Medevac and VIP meters drain from turbulence the player can't avoid, which makes story mission P4 unwinnable on score.
  - The airline pays out on 90-second circuits back to the departure field: about $12k per circuit, repeatable.
  - The job board's background flight crashes on takeoff for about 1 in 5 story jobs, and the card then says "flying it on the autopilot…" forever.
- v3.44.0 adds about 317 MiB of textures that are uploaded **even on Low quality**; the release notes say about 180 MB. It may also composite the volcano's smoke over nearer smoke (§2).

---

## 1. Method

| What | How |
|---|---|
| Build | A worktree at each release tag, `RelWithDebInfo`, `-Wall -Wextra`, every Linux target |
| Tests | `ctest`, the **full** flight model (not the CI's `FLIGHT_QUICK`) |
| Static analysis | clang-tidy `bugprone-*`, `performance-*` over 33 translation units |
| Gameplay probes | A program of my own, linked against the game's own object files. It drives `Career`, `simulateFlightMinutes` and the real `Game::update` loop headless. Every probe was run on **both** releases. |
| Code reading | Career, economy, settlement, job legs, airline, failures, job meters, ATC holds, quotes, save/load, the v3.42.0 → v3.43.0 and v3.43.0 → v3.44.0 diffs |
| Earlier review | Issue #2 (Codex, 2026-10-03): each finding re-checked |

No game source was changed. The probes live only in the scratchpad.

---

## 2. What v3.44.0 changes, and what it adds

**Scope:** +41,069 / −1,152 lines across 215 files, of which:
- `src/` +4,132: settlements rebuilt around streets, building/vehicle/tree/rock meshes in four LODs, airport service yards, 2K photographic materials, shallow water, Mount Kaleo's crater and volcano effects, WLD3 world cache;
- `tests/` +22,088, mostly a 17.5k-line height golden;
- `docs/` +11,987;
- `tools/` +1,937.

**What it doesn't change:** none of the gameplay files (`career.*`, `game_ui.cpp`, `aircraft.cpp`, `raster_renderer.cpp`, `effects_fs.glsl`, `README.md`). `game.cpp` changes by 3 lines (the volcano hook).

### New findings

**N1. 2K environment textures are uploaded on every quality setting, and cost about 1.8× what the notes say — Medium (VRAM)**
- `Renderer::genMaterials` runs once at init (`renderer.cpp:905`). It always loads both the 512 px set and the 2K set (`renderer.cpp:479`, unconditional).
- The 2K set is only *bound* when `quality > 0` (`bindEnvironmentMaterials`).
- Cost, from the code's own comments: 2K albedo + normal arrays, 7 layers with mipmaps = **298.7 MiB**; the 512 px set = 18.7 MiB; about **317 MiB** in total. The release notes say "about 180 MB more GPU memory".
- The players most likely to pick **Low** (small-VRAM GPUs and iGPUs) pay the full 2K cost for textures they never sample.
- Decode staging also peaks at about 224 MiB of CPU memory at startup.
- **Fix:** upload the 2K set only when quality > 0, lazily when quality is raised, and free it when it's lowered. Correct the notes.

**N2. Volcano smoke is drawn after the depth-sorted particles — Medium–Low (visual, code-read, plausible)**
- `Game::render` appends `volcano::append(...)` after `buildSprites` (`game.cpp:3771`).
- `buildSprites` sorts the frame's alpha particles back-to-front (`game.cpp:3246`); the volcano's puffs are sorted only among themselves (`volcano_effects.h:39`) and drawn after all of them.
- Result: a plume kilometres away blends **over** nearer smoke, cloud wisps, exhaust or crash smoke wherever they overlap on screen. Most visible when flying near Mount Kaleo with your own smoke or trail, or after a crash nearby.
- **Fix:** add the puffs to `order` before the sort.

**N3. The environment-material gate checks the combined texture-unit limit, not the fragment-stage one — Low (portability)**
- `envMaterialUnits = maxUnits >= 32` reads `GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS` (`renderer.cpp:433`). That makes units 29 and 31 *addressable*.
- The terrain, water and scenery fragment shaders now carry two more samplers, and their count must fit `GL_MAX_TEXTURE_IMAGE_UNITS` (16 guaranteed on GL 3.3; issue #2 had counted 19 in one program).
- Fine on NVIDIA, AMD and Intel (32), but the gate doesn't prove it. Query the fragment limit, or check `glGetProgramiv(GL_LINK_STATUS)` and fall back.

**N4. Build duplication grew — Low (developer time)**
- 59 executables, **609 object compile steps**.
- `world.cpp`, `scenery.cpp`, `entities.cpp` and `airport_scenery.cpp` are each compiled **39 times**, once per test target (`CMakeLists.txt` `CORE_SOURCES` / `ENVIRONMENT_SOURCES`).
- New tests reach the CI's explicit target list by being made dependencies of old targets (`add_dependencies(airport_layout_test …)`). That works, but it's easy to miss.
- **Fix:** an `OBJECT` library for each shared set (as `game_objs` already is), and a ctest label or target list for CI.

**N5. Raw QA logs and preview manifests committed under `docs/` — Low (repo hygiene)**
`docs/living-islands/…/performance/` holds several 1,840-line raw `full.log` / `asan` logs and an 876-line preview manifest. They'd fit better as CI artifacts or a release attachment.

**N6. Two code styles — Low (maintainability)**
New modules use a compact style with no spaces round operators and one-line loops (`volcano_effects.h`, the `entity_mesh_*.inc` files, `tests/cockpit_focus_zoom_test.cpp`); the rest of the codebase is long-line prose. A `.clang-format` would settle it.

### What v3.44.0 does well
- **New guard tests:**
  - `runway_preservation` (a golden of every runway, so environment work can't move a runway);
  - `community_layout`, `airport_environment`, `environment_scale_audit`, `environment_close_detail` / `close_bounds` / `chunk_bounds`, `airport_construction_detail`;
  - `volcano_effects`, `environment_asset_mesh`, `environment_nature_detail`.
- **Graceful degradation for the textures:** GL errors checked on upload, `std::bad_alloc` caught, a 512 px fallback, and no out-of-range bindings when units are short.
- The release commit's CI is green (run 857).

---

## 3. Confirmed issues (all present in both v3.43.0 and v3.44.0), most severe first

### 3.1 Medevac and VIP meters drain from turbulence the player can't avoid — **High (gameplay)**
`game.cpp:618`: `rate += std::max(0.f, wx.turbulence - 0.25f) * 0.01f;` (per second, airborne)

This term doesn't depend on how the aircraft is flown. **Probe:** story mission P4 (Medevac: Injured Climber) in the Bushmaster on the autopilot's gentle hold, straight and level, 5 simulated minutes. g stayed in 0.58–1.30 and bank under 22°, inside the brief's limits. Results were identical on v3.43.0 and v3.44.0:

| turbulence | patient after 5 min |
|---|---|
| 0.05 / 0.25 | 100 % |
| 0.35 | 70 % |
| 0.45 | 40 % |
| **0.55 (P4 as shipped)** | **10 %** |

- **P4 can't be scored well.** The meter empties in about 5.5 minutes whatever you do. The autopilot needs about 17 minutes (§3.4). The settlement then always applies *"Patient in distress"*: −40 % pay, −2 stars, −1 reputation (`career.cpp:759`).
- **Freelance medevacs and VIPs** get turbulence up to 0.35, which costs 0.06 of the meter per minute. A 10-minute job lands at about 40 %: *"Rough ride"* −20 % on a medevac, *"VIP unimpressed"* on a VIP. The *"good shape"* and *"delighted"* bonuses can't be earned on longer jobs in that air.
- **The brief doesn't say this.** It says "under 1.5 g and 30° of bank, a soft touchdown". The low-g term (below 0.6 g for medevac, 0.75 g for VIP) isn't stated either.
- **Why the tests miss it:** `tests/gameplay_test.cpp:678` sets the medevac test's turbulence to 0.05.

**Fix:** drop the term and let real gust loads reach the meter through `gLoad`, or scale it by turbulence above what the contract expects. Cap P4. State the rule in the brief. Add a turbulent-air test.

### 3.2 The airline pays out on every touch-and-stop at the departure field — **High (economy exploit)**
- `game.cpp:1632` turns a full stop on any runway that isn't the destination into `OUT_DIVERTED`, the departure's own runway included.
- `Career::closeLeg` (`career.cpp:633`) then runs `airlineTick` whenever `routeFlightQualifies` (`landed && flightMin > 0 && (SUCCESS || DIVERTED)`).
- The job stays open at the departure, and the rental, positioning and ferry fees are only charged on the first leg.

**Probe** (identical on both releases): an ATP career with two Q400s on routes (rating-3 pilots), a rented Kestrel cargo job out of CAP, 10 × "take off, land back at CAP" (1.5 min each). Every circuit paid **+$12,394**; the rental was charged once. Total **+$123,820**. With 6 routes that's about $37k per circuit, against $55k for the A1 story mission. The v3.24.0 review fix (R3, zero-time aborts) closed one path; this reaches the same exploit through a 90-second circuit.

**Fix:** tick the airline on simulated time or distance flown, or only on a settled job / a diversion to a *different* field. At minimum, reject `divertedTo == attemptFrom`.

### 3.3 Job quotes: the background flight crashes on takeoff, and the card says "flying it on the autopilot…" forever — **Medium (UX / quoting)**
`simulateFlightMinutes` (`career.cpp:417`) scripts the takeoff roll with throttle, flaps and pitch, but **never steers**: no `ctl.yaw`, no nosewheel. In a crosswind the aircraft runs off the runway.

**Probe:** every story mission × every aircraft eligible at that stage. On v3.43.0, **13 of 62 non-lesson pairs fail inside 20–40 s**:
- failures: *"Landed off-airport on rough terrain"*, *"Wingtip struck the ground"*, *"Prop/nose strike"*, *"Belly landing"*;
- affected: C1 Kestrel and Swift, C5 Swift, P1 Bushmaster, Pelican and Swift, P5 Pelican, P6 Islander, O6/O7/O8 Pelican, A2 and A5 Q400;
- for O6, O7, A2 and A5, the only aircraft the sweep tried for the mission failed;
- on v3.44.0: byte-identical output, the same 13 failing pairs.

Where the background flight does arrive, the quote is good: the quick estimate is mostly within ±15 % of the flown time (appendix).

**Effect in game:**
- `Game::applyQuote` (`game.cpp:152`) caches the `-1` result.
- `useFlownTime` ignores minutes ≤ 0, so `e.flown` never becomes true.
- `game_ui.cpp:714` therefore keeps showing *"– flying it on the autopilot…"* for that job, permanently, and it's never retried.

**Fix:** steer the roll with a rudder/nosewheel loop on runway heading. Show a failed quote as "estimated". Add a test that runs every story mission's background quote.

### 3.4 Mission and deadline design mismatches — **Medium (design)**
- **A4 "The Minister's Jet"** says *"Only a jet will do: the Starling 500"* (`career.cpp:186`), but `canFly` has no type requirement, so an owned **Meridian Q400 qualifies** (open since issue #2). On the autopilot both types take about 17 min against the 12-minute limit (Starling 16.9, Q400 17.3). The brief does tell you to hand-fly a straight-in.
- **P4:** the only eligible aircraft (Bushmaster) takes 17.2 min against the 14-minute limit, on top of §3.1.
- **Timed freelance jobs:** 40 sampled, each flown on the autopilot in *every* licensed type. On v3.43.0, 29 can be made, **7 are late in every type** (e.g. a medevac with an 8-minute limit, best 9.7 min; a 10-minute medical-supplies job, best 15.1 min), and 4 had no background flight arrive (§3.3). On v3.44.0 the sweep output is byte-identical.
  - Cause: the generator sets deadlines from straight-line distance at cruise × 1.5–1.6 + 3–4 min (`career.cpp:542`, `:552`). The arrival (descent orbit, intercept, final) is a fixed 5–8 minutes, which dominates short legs.
  - **Fix:** base the limit on `plan().minutesEst` (which includes the arrival) × ~1.25.

### 3.5 v3.43.0 cloak: its render target is reallocated whenever the resolution scaler moves — **Medium (perf regression risk)**
- `raster_renderer.cpp:652`: `if (!texCloak || cloakW != rw || cloakH != rh)` re-runs `glTexImage2D` on a full-res **RGBA32F** colour + depth target, about 41 MB at 1080p.
- `rw/rh` follow the render scale (`renderer.cpp:936`), which *Auto 60* (the default) moves in 5 % steps.
- Every other target is allocated once ("the targets stay: only the part drawn changes", `renderer.cpp:1018`), and the README promises resolution changes "never reallocate anything".
- The cloak is exactly the scene that pushes the scaler (before v3.43.0 it dropped the owner's GPU to 59 fps).

**Fix:** allocate at `allocW/allocH` once and draw into the `rw×rh` viewport. Use RGBA16F.

### 3.6 VIP job title and brief name different VIPs — **Low (cosmetic)**
`career.cpp:558` draws the VIP name twice. **Probe** (both releases): 3,453 of 4,151 VIP jobs (83 %) disagree, e.g. *"VIP: A minister to Lighthouse Key"* / *"The island's governor and party, 3 aboard…"*. **Fix:** draw once.

### 3.7 Smaller defects — **Low**
- **Data race:** `static float dbgT` (`aircraft.cpp:1859`) inside the autopilot is written by the main thread and the quote worker (`std::async`, `game.cpp:157`) at once. It only feeds a debug print, but it is formally UB, and ThreadSanitizer would flag it.
- **`Renderer::image(0, …)`** (`renderer.cpp:1762`) treats texture 0 as "no image" and samples whatever is bound. The committed `docs/ui-review/contracts-layout.png` shows the **font atlas in the route-map panel** because of it (the harness has no minimap). In game, `minimapTex` always exists (`renderer.cpp:530`), so this is latent.
- **"Hull insurance" doesn't cover a crash:** the crash path charges `price/12` with no `insured` check (`career.cpp:718`). The hangar note does say "failure or belly landing"; only the name misleads.
- **One aircraft per type** (`career.cpp:805`): the airline is capped at 9 airframes, and the late game has no money sink (the story ends at about $482k).
- **ATC hold tolerance:** 40 m (`game.cpp:3338`). A player who releases the brakes as the hold is spoken can trip it before the call ends.

---

## 4. Gameplay analysis

### 4.1 Career loop — strong
- **Structure:** 4 lessons + 30 story contracts in 5 chapters, Student → ATP. The licence gates match the fleet ladder.
- **Variety:** 7 freelance job kinds. Over 48,000 boards: cargo 34 %, passengers 32 %, survey 12 %, VIP 11 %, low-vis 4 %, night 4 %, medevac 2 % (rare because few fields have a hospital). The RNG is uniform (checked).
- **Economy curve** (`progression_test`): steady growth, one loan for the first owned aircraft, **$0 of extra freelancing needed**, `earningPath` proven over 640 states with no softlock. The flip side is that the freelance board and the airline are optional.
- **Fleet pricing** is coherent: about $10k per seat for light types, $15k for the Q400, $37k for the Starling (buy-only, which fits A4/A6 being owned-only).
- **Scoring** rewards airmanship: touchdown zone, centreline, stable approach, reserve, shut-down at the stand, tower compliance. Checkrides now fail on a hard landing or an ignored instruction.
- **Job legs and recovery** are well designed: the clock, ride state and survey history carry over, fees are charged once, outcomes are typed.

### 4.2 Flight model and autopilot — exceptional, well tested
- Aerodynamics come from geometry. Each type's envelope is learned by flying it; one autopilot flies every type to its learned limits, with a gentle law for passengers.
- The full flight test passes on every type.
- **Weak spot:** the autopilot's 5–8 minute arrival (descent orbit, intercept) collides with the deadlines (§3.4).

### 4.3 ATC, traffic, weather, research content
- **Towers** react to real state: holds, sequencing, wake caution, go-arounds.
- **Weather** drifts on forecast jobs and triggers re-plans.
- **Research content:** XR-10/20/30/40, test cards, cloak, weapons, UFO. A rich, isolated spoiler layer.

---

## 5. v3.43.0 release diff (`v3.42.0..v3.43.0`)

| Change | Assessment |
|---|---|
| **Cloak from the mesh** | Right idea (one raster pass instead of a per-pixel march), with a march fallback. **Reallocation on scale change (§3.5).** The 41 MB target is never freed. |
| **Survey cut of the deep interior** | Correct: a point deep inside in *every* state can't hold surface. |
| **Surface nets on per-cell tables** | Corner ownership, neighbour lookups and the rim fallback are consistent. Vertex-order quads **remove a platform dependence** (hash-map order). `kAlgorithmVersion` is unchanged on purpose. |
| **Rigid parts sampled in 4³ blocks with flood-fill** | Terminates, and covers fields that overstate distances. |
| **Threaded simplifier** | Race-free by construction; the result is deterministic. Threads are created each round (≤ 63); a small pool would be cheaper. |
| **compile.log timings; stale `error.log` handling** | Good. |
| **`FLIGHT_QUICK` cuts more** | The full flight suite now runs only on Windows **without** sanitizers. Consider a nightly full sanitizer run. |

---

## 6. Code architecture and quality (as of v3.44.0)

### 6.1 Size and shape
- About **32.4k lines of C++** (excluding the font atlas and stb), **10.7k lines of GLSL**, **8.8k lines of tests** plus fixtures.
- No dependencies beyond stb_image.
- Lines are dense: median 59 chars, p99 209, 276 lines over 200 chars in `src/`.
- Comments are plentiful and explain *why*, but read as long prose.

### 6.2 Structure — the main debt
- **`Game` is a god object:** `game.h:71–531`, about 146 methods and hundreds of fields. `game.cpp` is 4,591 lines / 312 KB. The owner declined the extraction (WORK_PLAN D1). That's defensible for a solo project, but every feature lands in this one class.
- **Debug and benchmark scenes ship in the release binary:** `Game::debugScene` is 760 lines (`game.cpp:3831`), and `src/` has 124 `getenv` hooks (35 in `game.cpp`). Move them to `game_debug.cpp` behind `SOLACE_DEV`.
- **UI state in function statics:** about 20 in `game_ui.cpp` (e.g. `:585`, `:653`, `:701`, `:793`). It survives a new career and is shared across `Game` instances.
- **Globals:** `g_world`, `g_ren`, `g_audio`, `g_scenery`, `g_story`.

### 6.3 Correctness hygiene — very good
- **Compiler:** `-Wall -Wextra` is close to clean. In `src/`: `game.cpp:585` (`&&` within `||`, intent OK), misleading indentation at `game.cpp:4025` and `camera_feeds.cpp:103` (both benign), three missing initialisers, two unused locals.
- **clang-tidy (bugprone, performance):** 36 findings. Mostly `(int)(x + 0.5)` rounding on non-negative values (correct in practice). Also a `memcmp` of a padded `AircraftSpec` (`aero_strips.cpp:449`, worst case a cache miss), an unused `std::string hdr` (`renderer.cpp:591`), and three `push_back`-in-loop / int-to-ptr notes.
- **Save system** (`career.cpp:1057–1252`):
  - atomic temp file → `MoveFileEx`/`rename`;
  - a `.bak` kept only if the current save is itself valid;
  - full validation (indices, enums, finite floats, mandatory fields, end marker, route ↔ fleet ↔ pilot links);
  - versioned and backward-compatible; floats written under the C locale.
- **Threads** are joined properly, and shared caches are mutex-guarded. `aeroGeom` takes a recursive mutex on **every call**, which sits on the physics hot path. The one unsynchronised variable is §3.7's.

### 6.4 Tests and CI — strong, with gaps
- **v3.44.0 registers 55 tests** (51 on Linux): flight per type, aero, wakes, breakup, campaign progression with an earning-path proof, saves (corrupt and truncated), layouts, envelope, hulls, meshes, shader contracts, an end-to-end game-loop test, and now runway/environment guards.
- CI: MSVC plus ASan/UBSan, and a release only after both pass.
- **Gaps that let §3 through:**
  - The medevac test uses calm air.
  - The quote simulator is tested only on A4/Starling and 4 fuel legs.
  - Nothing tests the airline against short or same-field legs.
  - Nothing checks title/brief consistency.
- **Process:** five releases in about 18 hours (v3.40 → v3.44). The owner-GPU "measure" and "image" gates can't keep pace. Releases are cut by `workflow_dispatch` from a default branch named `claude/compassionate-davinci-4cfo20`; a `main` branch would be clearer.

---

## 7. Status of issue #2's findings (Codex, 2026-10-03)

| Finding | v3.43.0 / v3.44.0 |
|---|---|
| P1 corrupt saves crash or reset progress | ✅ Fixed |
| P1 save truncation, write errors ignored | ✅ Fixed (temp + atomic replace + `.bak`) |
| P1 autoland climb and fuel planning | ◑ Much improved (ground checks, refusals with reasons, `autoland_sweep`); not re-swept |
| P1 dawn/dusk infinite light colours | ✅ Fixed (full-day sweep in `gameplay_test`) |
| P2 checkpoints left + zero fuel stalls | ✅ Fixed |
| P2 diversions counted as crashes | ✅ Fixed (typed `FlightOutcome`) |
| P2 sim time lost below 20 fps | ✅ Fixed (up to 250 ms replayed a frame) |
| P2 traffic collision tunnelling | ✅ Fixed (closest approach) |
| P2 scenery raycast sampling | ✅ Fixed (analytic tests) |
| P2 19 samplers > 16 on GL 3.3 | ◑ The ray tracer is gone; v3.44.0 adds samplers (N3); needs a GPU recount |
| P2 audio thread not joined | ✅ Fixed |
| L4 hard landing still grants PPL | ✅ Fixed (`checkrideFault`) |
| A4 Meridian allowed despite "only a jet" | ❌ Open (§3.4) |
| P4 late on the Bushmaster autopilot | ❌ Open, 17.2 min against 14, plus §3.1 |
| `gameplay_test` not in CTest | ✅ Fixed |

---

## 8. Documentation accuracy (unchanged in v3.44.0)
- `README.md:29` still says crashes break into "nose, centre section, both wings and tail". v3.42.0 replaced that with per-component break-ups.
- `README.md:94` still describes the old red edge tint "from about 1.8 g" and the XR-30's "damped cell". Line 29 of the same file describes the v3.42.0 g-lens, so the two contradict each other.
- "Changing the resolution… never reallocates anything" is not true while the cloak is on (§3.5).
- The v3.44.0 notes understate the new textures' GPU memory (N1).
- The medevac and VIP briefs don't state the turbulence or low-g penalties (§3.1).

---

## 9. Recommended order of work
1. **§3.1** remove or scale the turbulence term; cap P4; state the rule; add a turbulent-air medevac test. *(small)*
2. **§3.2** tick the airline on flown time or settled jobs; reject same-field "diversions"; add a test. *(small)*
3. **N1** upload the 2K environment set only above Low; correct the notes. *(small)*
4. **§3.3** steer the quote simulator's takeoff roll; show failed quotes as estimates; add a story-wide quote test. *(small–medium)*
5. **§3.5** allocate the cloak target once (16-bit float). *(small)*
6. **N2** sort the volcano puffs with the other particles. *(trivial)*
7. **§3.4** derive deadlines from `plan()`; add a required-type field for A4. *(small)*
8. **§3.6 / §3.7 / §8** the VIP name, the `dbgT` race, the `image(0)` fallback, the README. *(trivial)*
9. **Structure:** OBJECT libraries for the shared sources (N4), move `debugScene` out of `game.cpp`, run the full flight suite under the sanitizers nightly, add a `.clang-format` (N6), move the raw QA logs out of `docs/` (N5).

---

## Appendix — probe output (abridged)

**Story missions on the background autopilot** (v3.43.0; quote = quick estimate, flown = `simulateFlightMinutes`):
```
L4  PPL checkride        Kestrel    quote 15.8  flown 14.8  fuel 43/70
C2  Parts for the Port   Kestrel    quote 18.5  flown 18.7  fuel 57/70
C6  Urgent Documents     Wren       quote 14.6  flown 12.4  ok (limit 16)
P4  Medevac              Bushmaster quote 16.1  flown 17.2  LATE (limit 14)
O1  Lodge Supplies       Bushmaster quote 24.9  flown 24.3  fuel 98/120
A4  Minister's Jet       Q400       quote 13.4  flown 17.3  LATE (limit 12)
A4  Minister's Jet       Starling   quote 10.5  flown 16.9  LATE (limit 12)
A6  Grand Tour           Starling   quote 20.9  flown 21.6
... 13 non-lesson pairs crashed on the take-off roll (C1, C5, P1, P5, P6, O6, O7, O8, A2, A5)
```
v3.44.0: the sweep output is byte-identical to v3.43.0

**Airline circuits** (both): `10 circuits at the departure: money 100000 -> 223820 (+123820), job still open at CAP`
**VIP names** (both): `3453 of 4151 VIP jobs name a different VIP in the title and the brief`
**Timed freelance jobs** (v3.43.0): `40 timed jobs: 29 makeable on the autopilot, 7 late in every type, 4 where no background flight arrived`; v3.44.0: byte-identical
