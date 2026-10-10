# Solace Express v3.44.0 — combined gameplay, code and quality review (Claude + Codex)

**Release under review:** `v3.44.0`, tag → `b22c2fca66fff88b62801a79f9f0c6edf06dcb9e` (2026-10-10 16:29 UTC). This branch sits exactly on that commit, so every `file:line` below is valid here.
**Inputs merged:**
1. Claude's analysis of v3.43.0, re-verified on v3.44.0 (`claude/ANALYSIS.md`, probe `claude/probe.cpp`).
2. Codex's audit of v3.43.0 with its v3.44.0 addendum (`codex/`, the report text only).

Codex's full evidence bundle (logs, previews, repro harnesses, malformed-WAV fixtures; `Solace-Express-v3.44.0-Gameplay-Code-Quality-Audit.zip`, 7 MB) was handed to the owner separately. It is **not** in this branch, so paths like `logs/…` and `repros/…` in the Codex reports refer to that bundle.
**Production source is unchanged on this branch:** the only additions are under `docs/reviews/v3.44.0/`.

---

## How to read this

**Severity** (game-development triage, not safety certification):
- **P1:** the player loses money, an aircraft, progress or control through ordinary play, or the economy breaks.
- **P2:** a player-visible correctness or design defect, or a quality gate that's broken.
- **P3:** hardening, documentation, polish or tech debt.

**Origin:** **C** = Claude, **X** = Codex, **C+X** = both found it independently.

**Evidence:**
- **R44:** reproduced at runtime on v3.44.0.
- **R43:** reproduced at runtime on v3.43.0, with the relevant source byte-identical in v3.44.0 (Codex hashed the functions; Claude diffed the files).
- **S:** confirmed by reading the v3.44.0 source; *S✓C / S✓X* means the *other* reviewer confirmed it.
- **N:** a numerical or analytic reproduction.
- **P:** plausible from code, not reproduced.

Neither reviewer had a GPU or native Windows. Rendering, frame-rate and device claims need the owner's hardware (`codex/HARDWARE_CHECKLIST.md`).

---

## 1. Verdict

| | Result |
|---|---|
| Build, all Linux targets, `-Wall -Wextra` | Clean (C) |
| Local `ctest`, full flight model | C: **51/51** (no glslang validator here). X: **54/54** (with validators) |
| CI on the release commit (run 38066530909) | Windows 49/49, Linux ASan+UBSan 50/50 (`FLIGHT_QUICK=1`) |
| Findings open in v3.44.0 | **6 × P1, 22 × P2, 20 × P3**; one v3.43.0 finding fixed by v3.44.0 (WLD-x0) |

The engineering foundation is strong:
- physics from geometry, and an autopilot that learns each airframe;
- an atomic, validated save system with backups;
- typed flight outcomes;
- an end-to-end game-loop test and sanitizer CI;
- v3.44.0's runway-preservation golden and other environment guards.

The open defects cluster at **boundaries between systems that each pass their own tests**:
- UI overlay ↔ the controls underneath it;
- tower ↔ autopilot;
- job legs ↔ settlement and the airline;
- weapons damage ↔ physics and persistence;
- the quote simulator ↔ the job card.

**Both reviewers agree** that the v3.43.0 headline changes are sound: mesh-backed cloak, faster mesh extraction, threaded simplifier. Codex showed the old/new outputs match on 48 sanitizer fixtures and that the simplifier is byte-identical. Claude found it race-free and deterministic by inspection. The open cloak items (RND-1, RND-2) are about resources and temporal reprojection, not correctness of the mesh.

---

## 2. P1 findings

### CAR-1 · Medevac and VIP meters drain from ambient turbulence; story mission P4 can't be scored · C · R44
- **Where:** `src/game.cpp:618`, `rate += max(0, wx.turbulence - 0.25) * 0.01` per second airborne, independent of how the aircraft is flown. Settlement penalties at `src/career.cpp:759` ff.
- **Repro:** `claude/probe.cpp cruise`. P4's mission, Bushmaster, autopilot gentle hold, straight and level for 5 simulated minutes, g 0.58–1.30, bank ≤ 22°.
  - Patient meter: 100 % at turbulence 0.05 and 0.25, 70 % at 0.35, 40 % at 0.45, **10 % at 0.55 (P4 as shipped)**. Identical on v3.43.0 and v3.44.0.
- **Impact:**
  - P4 always gets *"Patient in distress"*: −40 % pay, −2 stars, −1 reputation.
  - Freelance medevac and VIP jobs with turbulence up to 0.35 lose about 6 % of the meter per minute, so the *good shape* and *delighted* bonuses are out of reach on longer jobs.
  - Briefs mention only g and bank.
- **Gap:** `tests/gameplay_test.cpp:678` tests the meter at turbulence 0.05.
- **Fix:** drop the term and let real gust loads act through `gLoad`, or scale it by turbulence above the contract's expectation. Cap P4. State the rule in briefs. Add a turbulent-air test.

### CAR-2 · The airline is farmable with 90-second circuits back to the departure field · C · R44
- **Where:**
  - `src/game.cpp:1632` turns a stop on any non-destination runway into `OUT_DIVERTED`, the *departure* included.
  - `Career::closeLeg` → `airlineTick` (`src/career.cpp:633`) whenever `routeFlightQualifies` (`:925`).
  - Fees are charged only on the first leg.
- **Repro:** `claude/probe.cpp airline`. Two Q400 routes, a rented Kestrel job out of CAP, 10 × "take off, land back at CAP": **+$12,394 every circuit**, +$123,820 in total, the rental charged once. With six routes that's about $37k per circuit.
- **Fix:** tick on simulated time or distance, or only on settlement or a diversion to a *different* field. At minimum reject `divertedTo == attemptFrom`. Add a test.
- *Related:* CAR-6, another diversion-accounting gap.

### UI-1 · Radio overlay clicks reach the controls underneath: buy a hidden aircraft, fire XR-40 weapons · X · R43, S✓C
- **Where:** `src/game_ui.cpp:125-161`. `button()` returns true on `in.mPressed[0]` without consuming it, and `hovered()` (`:68`) has no overlay exclusion.
- **Why it happens:** the hub tab is processed **before** `drawRadioPanel` (`:517-524`).
- **In flight:** `src/game_wraith.cpp:62-65` suppresses fire for `showMap`, not `showRadio`.
- **Repro (Codex):** one click on a station at (1607, 432), 1080p, over Hangar → Wren. Money $1,000,000 → **$970,000**, the Wren is owned, and the purchase is saved. In an XR-40 flight, radio clicks arm the weapons, then fire.
- **Fix:** the topmost overlay must own pointer hit-testing *before* the widgets underneath. Consuming the press later in the radio pass is too late. Suppress flight actions under interactive overlays. Add regressions with an overlay open on every economic tab.

### FLT-1 · Split-S admitted without enough height; XR-20/30/40 fly into the sea · X · R44 (XR-40), R43 (XR-20/30)
- **Where:** `src/aircraft_stunt.cpp:47` (structural-g `apStuntN`), `:75-80` (the pull is stall-limited), `:95-104` (admission sized on the structural radius), `:111-120` / `:181-196` (abort and recovery).
- **Repro (Codex):**
  - The real aerobatics key, XR-40, 700 m AGL: Split-S accepted, ground abort at 482 m, sea impact at t = 139.72 s.
  - The same input from 1,500 m passes.
  - XR-30 and XR-20 crash the same way at 700 m.
- **Fix:** admit on the attainable, stall-limited pull and the energy trajectory, including roll and pitch lag; add low-altitude tests for each research type.

### FLT-2 · Re-engaging autoland after a single-engine failure abandons a reachable runway for an impossible climb or orbit · X · R44
- **Where:** `src/game.cpp:1141-1147` allows re-engagement (only a dark battery refuses). `src/aircraft.cpp:823-848` always runs NAV and descent-orbit planning. `:1166` clamps a non-positive climb to +0.5 m/s; `:1184-1190` orbits while asking for height.
- **Repro (Codex):** Kestrel, 2.5 km / 350 m out on CAP 32, engine failure, real AP key → ditches at 146.87 s. The matched control (same state, final stage) lands on the centreline.
- *Note:* the disconnect *on* failure (`:883-885`) is correct; the defect is the re-engagement.
- **Fix:** an energy- and range-aware branch for zero or negative climb: capture the aligned approach, pick a reachable alternate, or refuse with a reason.

### FLT-3 · Autoland ignores the tower's go-around, and the player is fined for it · X · R44, S✓C
- **Where:** `src/game.cpp:3484-3487` sets `F.goAround`; `:1490` scores the touchdown. Nothing under `src/aircraft.*` reads tower state (Claude grep).
- **Repro (Codex):** Wren on autoland, runway blocked. *"Go around. Aircraft on the runway"* at 66.95 s; it lands anyway and the $1,000 contract is charged **−$500 "Landed against a go-around instruction"**.
- **Fix:** feed tower clearance and occupancy into the guidance (a go-around within climb capability, or a clear hand-back). Don't penalise the player for the autopilot's choice. Test through settlement.

---

## 3. P2 findings

### Career, jobs and quotes

**CAR-3 · The background quote flight never steers its take-off roll; ~1 in 5 story jobs never get a flown quote, and the card says "flying it on the autopilot…" forever** · C · R44
- **Where:**
  - `src/career.cpp:417` (`simulateFlightMinutes` phase 0 sets throttle, flaps and pitch, never `ctl.yaw`);
  - the `-1` result is cached at `src/game.cpp:152`;
  - `useFlownTime` ignores it, so `e.flown` stays false;
  - `src/game_ui.cpp:714` keeps the suffix.
- **Repro:** `claude/probe.cpp story` / `why`. **13 of 62** non-lesson story mission × aircraft pairs crash 20–40 s into the roll: "landed off-airport", "wingtip struck", "prop strike", "belly". Affected: C1, C5, P1, P5, P6, O6, O7, O8, A2 and A5 in various types; in O6, O7, A2 and A5 it was the only type tried. Byte-identical on v3.43.0 and v3.44.0.
- When the flight *does* arrive, quotes are good (±15 %).
- *Related:* Codex looked at quote vs the overweight gate and rejected a mismatch (`codex/reports/career.md`), without hitting the failing flights.
- **Fix:** steer the roll (a rudder/nosewheel loop on runway heading); show a failed flight as "estimated"; add a story-wide background-quote test.

**CAR-4 · A continuation leg is planned over checkpoints already flown** · X · R43
- **Where:** `src/career.h:195` (`continuation()` keeps every waypoint); `src/career.cpp:206-213, 226-245, 283-333`.
- **Repro (Codex):** survey F1_3, all 6 rings done, diverted to CAP. Continuing in the Islander is refused, *"Range 90 km too short (need 96 km)"*, although the remaining 45 km route qualifies. **1,582 / 8,192** sampled states falsely rejected. Quotes are inflated too (18.1 min / 78.7 kg against 15.1 / 64.1).
- **Fix:** a remaining-route view, keeping global checkpoint indices.

**CAR-5 · A safe diversion can't close a checkpoint job until every checkpoint is flown** · X · R43, S✓C
- **Where:** `src/game.cpp:1629`. The `wpIndex < wps.size()` branch runs before the divert and off-airport branches.
- **Repro (Codex):** stopped safely at CAP with 5 rings left: still `SCR_FLIGHT` after 9.9 s, no recovery possible. The control with all rings done diverts in 1.2 s.
- **Fix:** separate the success gate from safe leg closure.

**CAR-6 · Diverting and continuing erases a recorded tower violation** · X · R43, S✓C
- **Where:** `JobState` has no compliance flags (`src/career.h:180-195`). `closeLeg` / `settleJob` (`src/career.cpp:598-662`) don't carry `holdViolated` or `landedAgainstGoAround`.
- **Repro (Codex):** direct delivery −$70 and 2 stars; divert-continue-deliver $0 and 3 stars.
- **Fix:** carry the flags job-wide, or settle them at leg close. Covers the same class of leak as CAR-2.

**CAR-7 · A research runway start inherits the career's fuel choice** · X · R43; the source path is consistent (`chosenFuel`, `src/game.cpp:355`)
- **Repro (Codex):** a 7 kg Kestrel selection → XR-10 and XR-20 runway starts with **7 kg** (default 337 / 201 kg); Restart changes it back.
- **Fix:** an isolated research launch plan, as Free Flight already has.

**CAR-8 · Deadlines the autopilot can't meet; A4's aircraft requirement not enforced** · C · R44 (story and timed sweeps; output byte-identical to v3.43.0)
- **Generator:** deadlines are straight-line cruise × 1.5–1.6 + 3–4 min (`src/career.cpp:542, 552`). The autopilot's arrival adds a fixed 5–8 min.
- **Timed jobs:** 40 sampled, each flown in every licensed type → 29 makeable, **7 late in every type**, 4 with no flight arriving (CAR-3).
- **Story:** P4 takes 17.2 min against 14 (Bushmaster, the only eligible type). A4 takes 16.9 and 17.3 min against 12. A4's *"Only a jet will do"* (`:186`) isn't enforced: an owned Q400 qualifies (open since issue #2).
- **Fix:** derive limits from `plan().minutesEst` × ~1.25; add a required-type field.

### Flight and ATC

**FLT-4 · Runway occupancy isn't re-checked after clearance; an occupied departure runway is cleared after 90 s** · X · R44, S✓C
- **Where:** `src/game.cpp:3471-3501` (arrival phase 5 never calls `rwyTraffic`); `:3398-3400` (`F.trafficT < 90.f` escape).
- **Fix:** re-check until touchdown or take-off; resolve stuck AI traffic independently. Tests: a late incursion, and a hold longer than 90 s.
- *Related:* FLT-6.

**FLT-5 · A GPS alternate doesn't retarget the tower** · X · R44, S✓C
- **Where:** `atcF.arr` is written only at `src/game.cpp:1007` (from the contract); `engageAutopilot` (`:1141-1154`) never updates it.
- **Fix:** keep an operational destination separate from the commercial one.

### UI, input and audio

| ID | Finding | Origin · evidence | Where |
|---|---|---|---|
| UI-2 | At 1280×720 / 140 %, hidden research test cards catch the visible **Abort** click | X · R43 | `src/game_research_ui.cpp:157-166, 437, 464-511, 628-642` |
| UI-3 | Settings toggles lose keyboard / D-pad focus once their label changes (the ID includes the label); the next Enter opens Radio | X · R43 | `src/game_ui.cpp:127, 1370-1373`; `src/game.cpp:1858` |
| UI-4 | At 800×600 / 140 %, right-side settings are clipped or unreachable (the panel doesn't reflow, no minimum window width) | X · R43 | `src/game_ui.cpp:70, 522, 1356-1394` |
| AUD-1 | Master volume doesn't affect the internet radio | X · R43, S✓C | `src/game.cpp:3585` (`rv = radioVol × duck`, no master); `src/game_ui.cpp:1563-1578` |
| AUD-2 | Pausing mid-speech leaves the radio ducked (~35 %) in menus; changing station bypasses the duck | X · R43 | `src/game.cpp:3522-3523, 3571, 3585` |
| AUD-3 | A hazard call the player has already recovered from still plays later ("PULL UP" while climbing at ~1,958 m AGL) | X · R43 | `src/game.cpp:3544-3548`; `src/atc.cpp:303-330` |
| AUD-4 | A truncated or empty WAV gives an ASan heap overflow / UB in `readWav` (only with damaged or replaced assets; all 1,068 shipped clips decode) | X · R43 | `src/atc.cpp:212-238` |

### World, collision and persistence

| ID | Finding | Origin · evidence | Where |
|---|---|---|---|
| WLD-1 | A plasma crater is drawn 8 m deep, but aircraft, weapons and wrecks rest on the old ground (an invisible floor) | X · R44, S✓C (contacts sample only `g_world.height`) | `src/game_wraith.cpp:252-258`; `src/aircraft.cpp:494-499`; `src/game.cpp:2134-2140` |
| WLD-2 | The eighth bomb restores the scenery, *and its collision*, destroyed by the first (blast damage lives only in the 7-crater render list) | X · R44 | `src/game_wraith.cpp:258` (7-crater cap); `src/entities.cpp:139-140` (`destroyed()` reads only the live crater list) |
| WLD-3 | A destroyed entity owned by a neighbouring chunk stays visible and casts shadows but has no collision. v3.44.0 cuts the cases from **130 to 13** in 1.21 M entities; the `chunkOf` float-boundary cases remain | X · R44 | `src/entities.h:88` (`chunkOf`); `src/entities.cpp:149` (`chunkAffected`); `src/entity_render.cpp:252` |

### Renderer, startup and quality gates

**RND-1 · Cloak target: resolution changes reallocate it, and its allocation is never checked** · C+X · S
- **Where:** `src/raster_renderer.cpp:652`. RGBA32F + D24 at render size, about 38–41 MiB at 1080p.
  - **C:** it's reallocated whenever *Auto 60* moves `rw/rh` (`src/renderer.cpp:936`), while every other target is allocated once (`:1018`, and the README's "never reallocates" promise). So a cloaked flight near the GPU budget can hitch on every 5 % scale step.
  - **X:** there's no completeness or allocation-failure fallback, and the target is never freed.
- **Fix:** allocate once at `allocW × allocH`, use RGBA16F, check FBO completeness.

**RND-2 · Cloak pixels are reprojected with aircraft motion but background depth** · X · N
- **Where:** `src/shaders/effects_fs.glsl` (`taaFlag 0.5`); `src/shaders/taa_fs.glsl:52-65`.
- Numerically up to 112 px misplacement at 1080p (5° yaw); visible severity unmeasured.
- **Fix:** its own temporal class, or reduced history.

**RND-3 · An unused world declaration invalidates all 26 aircraft body caches** · X · S/N, S✓C
- **Where:** `src/shaders/terrain_material.glsl:48`, `const int COMMUNITY_INFO…`. It reaches every builder via `hullBakeFSAssembly → worldLibAssembly` (`src/shaders.h:33-41`), and the pruner keeps top-level constants.
- **Impact:** all 52 builder programs differ by that line alone, which forces an avoidable full rebuild after updating.
- **Fix:** move it into `communityGrid`; add a cache-stability test.

**RND-4 · The CPU-renderer regression runner fails before running anything** · X · R44 (Claude ran it too)
- **Where:** `python3 tools/validation/run_cpu.py` → `AssertionError` at `tools/validation/extract.py:51`. The reference checks are stale and the mocks use the old API.
- **Fix:** update the contracts and mocks, and wire the runner into CI so it fails loudly.

---

## 4. P3 findings

| ID | Finding | Origin · evidence | Where / note |
|---|---|---|---|
| CAR-9 | A VIP job's title and brief name different VIPs (83 % of 4,151 generated) | C · R44 | `src/career.cpp:558`: the name is drawn twice |
| CAR-10 | A research card is announced "signed off" even when the settings save fails | X · R43 | `src/game.cpp:242-259, 670-675` (`saveSettings` returns void) |
| CAR-11 | The save parser accepts malformed states: a zero-payment open loan, duplicate types or routes, `finished 999`, negative reputation | X · R43 | `src/career.cpp:1131-1161, 1234-1240`. Defensive only. |
| CAR-12 | Design notes: "Hull insurance" doesn't cover crashes (`:718`, though the hangar note is accurate); one aircraft per type (`:805`) caps the airline at 9 and leaves no late-game money sink (the story ends at ~$482k); trial records keyed by a static ID; the first-run Settings path creates a career save | C+X · S | design decisions for the owner |
| FLT-6 | The ATC hold allows 40 m of creep, which can trip while the hold call is still being spoken | C · S | `src/game.cpp:3338` |
| FLT-7 | Engine-out fuel burn ignores how many engines are still running | X · R43 | `src/aircraft.cpp:332-334`. A model simplification. |
| UI-5 | Keyboard / D-pad can't reach the key-binding cells or the hub tabs | X · R43 | `src/game_ui.cpp:499-509, 1325-1334, 1490-1523` |
| UI-6 | The FOV value is clipped even at 1080p / 100 % | X · R43 | `src/game_ui.cpp:1367, 1412` |
| UI-7 | `Renderer::image(0, …)` samples whatever texture is bound (the committed `docs/ui-review/contracts-layout.png` shows the font atlas as the route map) | C · S | `src/renderer.cpp:1762`. Latent in game. |
| WLD-4 | A world cache with a NaN height but a valid structure loads | X · R43 | `src/world.cpp:300` (`loadCache`): no finite checks or checksum |
| WLD-5 | The release ZIP's README says "See LICENSE", but LICENSE isn't packaged | X · R43, S✓C | `.github/workflows/build.yml:78` |
| RND-5 | Volcano smoke is appended after the depth-sorted particles, so a distant plume draws over nearer smoke and trails | C · P | `src/game.cpp:3771` vs `:3246` |
| RND-6 | The environment-material gate checks *combined* texture units (≥ 32), not the fragment-stage limit, though those shaders gained two samplers | C · S | `src/renderer.cpp:433` |
| RND-7 | v3.44.0 adds 317.3 MiB of texture storage; the notes say ~180 MB, measured from an intermediate 3-layer set. Low preloads the 2K set by design (`docs/living-islands/PHOTOGRAPHIC_MATERIALS.md`) | C+X · N | `src/renderer.cpp:479, 905`. Fix the notes; consider loading lazily. |
| RND-8 | Data race: a `static float dbgT` in the autopilot is written by the main thread and the quote worker | C · S | `src/aircraft.cpp:1859`; `src/game.cpp:157` |
| ENG-1 | Shared sources are compiled once per test target: 609 compile steps, `world/scenery/entities/airport_scenery.cpp` **×39 each**; CI picks up new tests through `add_dependencies` | C · S | `CMakeLists.txt`. Use OBJECT libraries. |
| ENG-2 | The sanitizer job runs `FLIGHT_QUICK` only, so the full flight suite never runs under ASan/UBSan | C+X · S | `.github/workflows/build.yml`. Add a nightly. |
| ENG-3 | The README is stale: line 29 says crashes break into 5 pieces; line 94 describes the old red g-tint, contradicting line 29's g-lens; "never reallocates" vs RND-1 | C · S | `README.md:29, 94` |
| ENG-4 | Tech debt: `Game` god class (`src/game.h:71-531`, ~146 methods); a 760-line `debugScene` and 124 `getenv` hooks in the release binary; ~20 function-static UI state variables (`src/game_ui.cpp`); two code styles; raw QA logs committed under `docs/living-islands/` | C · S | Refactor D1 was declined by the owner; isolate the debug code at least |
| ENG-5 | Windows risks (source only, not run): the Media Foundation callback hands off a `volatile` without synchronisation; `game.quit` is a plain bool; A-APIs and MAX_PATH paths; XInput slot 0 only | X · S | `src/radio_win.cpp:14-26`; `src/platform_win32.cpp` |

---

## 5. Fixed or closed

- **WLD-x0 (Codex WR-4), airport terminal frustum culling:** **fixed in v3.44.0** (authored all-LOD bounds; all four original repros pass). Keep the regression test.
- **Issue #2 (Codex review, 2026-10-03):** most of it is fixed; per-item table in `claude/ANALYSIS.md` §7.
  - **Fixed:** corrupt or truncated saves, atomic writes, diversions counted as crashes, the dawn/dusk NaN, checkpoint + zero-fuel stall, sim time below 20 fps, traffic and raycast tunnelling, audio thread join, L4 PPL on a hard landing, `gameplay_test` added to CTest.
  - **Still open:** A4's type requirement and P4's lateness (now CAR-8).
- **Rejected after checking:**
  - **X:** replaying a successful story mission (the UI only offers Retry after a failure).
  - **X:** quote vs overweight-gate mismatch (0 of 1,063 pairs).
  - **X:** routine Islander fragile jobs failing (3 real board jobs flown take-off to debrief, all clean).
  - **C:** RNG bias in the job generator (uniform over 48,000 boards).
  - **C+X:** races in the threaded simplifier (none; deterministic).
  - **X:** an unjoined helper thread when the shared GL context is missing (guarded).

---

## 6. Where the reviews differ

| Topic | Claude | Codex | Resolution |
|---|---|---|---|
| 2K textures on Low (RND-7) | First rated Medium | P3: documented intent | **P3.** `PHOTOGRAPHIC_MATERIALS.md` documents the preload. Keep "load lazily" as an optimisation. |
| Cloak target (RND-1) | Reallocation on scale steps | Size noted, no completeness check, "resize reallocation present" | Merged. Claude's hitch claim is from code only; measure on the owner's GPU. |
| Test count | 51/51 | 54/54 | Both correct. Codex had the GLSL and Python validators that register 3 more tests. |
| Scope | Economy, job rules, quoting, build, docs | UI/input/audio, flight/ATC integration, world collision, renderer caches, Windows | Complementary. Only RND-1 and RND-7 overlap. |

---

## 7. Checklist for the reviewing agent

Verify in this order; each item is one reproduction or code check.

1. **CAR-1:** build `claude/probe.cpp` (`claude/README.md`), run `cruise`, expect 10 % at turbulence 0.55. Then read `src/game.cpp:618`.
2. **CAR-2:** run `airline`, expect +$12,394 per circuit. Read `src/game.cpp:1632` and `src/career.cpp:633, 925`.
3. **UI-1:** read `src/game_ui.cpp:68-71, 125-161, 512-524`. Confirm the hub tab handles the click before the radio panel draws and nothing consumes `mPressed`.
4. **FLT-3:** `grep -n goAround src/aircraft.*` gives no hits. `src/game.cpp:1490` scores it.
5. **FLT-1 / FLT-2:** these rest on Codex's runtime evidence (its `repros/flight/game_flight_probe.cpp` in the external bundle). Re-run it if possible; otherwise review `src/aircraft_stunt.cpp:47-120` and `src/aircraft.cpp:1166, 1184-1190` for the stated mechanism.
6. **CAR-3:** run `story` and `why`; confirm phase 0 in `src/career.cpp:410-425` never sets `ctl.yaw`.
7. **RND-3:** confirm `src/shaders/terrain_material.glsl:48` reaches `hullBakeFSAssembly` (`src/shaders.h:41`) and that the cache key hashes the assembled source.
8. **RND-4:** run `python3 tools/validation/run_cpu.py --output /tmp/x`; expect an `AssertionError` at `extract.py:51`.
9. Anything you can't confirm, mark **disputed** with the reason. Don't drop it silently.

**Suggested fix order:**
- P1s, cheapest first: CAR-2 → CAR-1 → UI-1 → FLT-3 → FLT-1 → FLT-2.
- Then CAR-3, CAR-5/CAR-6 (diversion accounting together with CAR-2), RND-3 (it saves every player a rebuild on the next update) and RND-1.
- Then the rest of P2, then P3.

Every fix should come with the regression test named in its finding. Most of these escaped because no test crosses the system boundary involved.
