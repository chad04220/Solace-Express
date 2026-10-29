# Code analysis and rework plan (v4.4.2, 2026-10-29)

A pass over the whole tree, measured rather than read end to end: file and function sizes, the state kept outside
objects, duplicated blocks, compile times, the test suite's shape. The question was what should be reworked. The
answer is a short list of structural problems that make every change slower and riskier than it needs to be, and a
longer list of things that are fine and should be left alone.

## 1. The shape of the code

| | |
|---|---|
| Source | 75 `.cpp`, 105 `.h`, 69,000 lines (8,100 of them the embedded font, 1,100 the star catalogue) |
| Shaders | 97 GLSL files, 14,300 lines; `plane_sdf.glsl` alone 1,800 |
| Tests | 132 C++ tests and harnesses (25,300 lines) plus 28 Python tests; 174 CTest entries |
| Build | one `CMakeLists.txt` of 1,018 lines, 166 hand-written executables |

The biggest translation units and what they cost to compile on one core here:

| File | Lines | Compile alone | Note |
|---|---|---|---|
| `src/game.cpp` | 7,959 | **80 s** | the `Game` class: flight, menus' logic, cameras, effects, ATC, audio params, loading, the 72 debug scenes |
| `src/game_ui.cpp` | 4,334 | 19 s | every menu's drawing and hit-testing |
| `src/renderer.cpp` | 2,661 | 14 s | plus `raster_renderer.cpp`, `entity_render.cpp`, `aircraft_mesh.cpp` and others: all methods of one `Renderer` |
| `src/aircraft.cpp` | 2,567 | 6 s | the flight model and the autopilot |

`game.h` declares `class Game` in 928 lines: 764 members and 317 member functions. Nine translation units include
it, so a change to any member rebuilds about two and a half minutes of single-core work. `Renderer` has 530 members
and 180 methods across 854 header lines.

The longest functions:

| Function | Lines |
|---|---|
| `WinMain` (`platform_win32.cpp`) | 1,235: the window, Steam, the launch steps, then `--analyze`, `--profile`, `--soak`, `--bench`, `--loadshots`, `--shots` in one body |
| `Game::debugScene` | 918: 72 named scenes for the harnesses and `diagnostics.bat`, a chain of `if (name == ...)` |
| `Renderer::bakePlaneMesh` | 914 |
| `Game::drawResearch`, `Game::drawHud`, `Renderer::drawEntities`, `Plane::substep`, `Game::updateFlight`, `Plane::apGuidance`, `Game::buildSprites`, `Game::drawHubContracts` | 400 to 575 each |

Other measurements:

- **101 distinct environment variables** are read in production code (`getenv`), 67 of them in `game.cpp`: debug
  A/B switches (`WAKEOFF`, `SHMAPOFF`, `AF_ALL`, `HULLDBG` 21 times) and scene parameters (`TOD`, `WX`, `PHOTO`,
  `FEEDS`, `SIGHT`, `STRIKE`, `REGION_*`). The shipped executable honours all of them, and several are ordinary words
  a player's shell could carry.
- **105 function-local `static` variables** hold state in `game.cpp`, `game_ui.cpp`, `renderer.cpp` and
  `raster_renderer.cpp`: scroll positions, "which item did I last lay out", cached lists, preview `Plane` objects, a
  `static Contract jobCont`. They are invisible to the class, to the save, to `initHeadless()` and to the tests.
- The loading screen's state is **15 fields reset by copy-pasted lines in four places** (`game.cpp` 2385, 7480,
  7582, 7644); 13 copies of the "consume this press" line (`in.pressed[K_ENTER] = in.pressed[' '] = false; ...`).
- **Globals constructed at static initialisation**: `g_world`, `g_scenery`, `g_ren`, `g_audio`, `g_music`,
  `g_roadTraffic`, `g_sceneryProfiles`, `g_story` and about ten more. Their destruction order is the compiler's; the
  v4.4.2 fix for a worker reading a destroyed cache at exit was a symptom of it.
- Dense style: 286 lines over 160 characters in `game.cpp`, 281 in `game_ui.cpp`, 94 in `career.cpp`; many
  statements per line. There is no formatter configuration.
- Tests: 103 of 132 define their own `check` lambda and `main`; 35 time themselves against the wall clock (a class
  that failed CI once this week, on the sanitizer shard). The GPU harness tests share twelve duplicated 8-line blocks
  (`earth_gpu_test`, `planet_gpu_test`, `road_grade_gpu_test`); the render harnesses share nine. The sanitizer shard
  runs 15 to 27 minutes; the five longest tests take 5 to 6.5 minutes each there.
- Exceptions: `earth_data.cpp` throws 17 ways; `game.cpp` catches with `catch (...) {}` in five places around
  futures, so a failed background job can vanish without a line in any log.

## 2. What should be reworked, in order

Each item says what, why it matters, and roughly how much work. The first three are where nearly all the friction
is.

### 2.1 Split `Game` (and `game.cpp`) by responsibility - large, the biggest payoff

`Game` is the flight session, the career hub, the research terminal, every camera, the effects, ATC, the loading
screens and the debug scenes at once. `game.cpp` takes 80 s to compile and every member change rebuilds nine files.
Proposed cut, each a class owned by `Game` with its own header, so a change rebuilds one file:

- `FlightSession` (the flight loop, controls, ATC, effects: `updateFlight`, `flightControls`, `jetEffects`,
  `updateAtc`, `buildSprites`)
- `Cameras` (`updateCamera`, the menu, hub, hangar, stage and research cameras: nine `*Camera` methods)
- `LoadingScreen` (`updateLoading`, the readiness model, the loading log, `queueLaunch`: see 2.3)
- `MenuState` (the 28 statics in `game_ui.cpp`: see 2.2)
- `DebugScenes` (`debugScene`'s 918 lines, in a file of its own, as a table from name to lambda instead of 72
  `if (name == ...)`)

`game.h` would shrink from 1,038 lines to a few hundred, and most edits would recompile under 20 s. This is a
mechanical refactor: moving members and methods, no behaviour change, with the 174 tests as the net.

### 2.2 Take the hidden state out of function-local statics - medium, fixes a bug class

The 28 statics in `game_ui.cpp` and 25 in `game.cpp` are per-menu state (scroll offsets, cached lists, preview
planes). Three consequences:

- they survive a new career or a loaded one. The destinations list at `game_ui.cpp` 1304 is keyed on region,
  licence and world, not on the career, so a new career in the same region can show the previous one's list until
  one of those keys changes;
- `initHeadless()` cannot reset them, so a test's second scenario inherits the first's menu state;
- nothing can save or inspect them.

Move them into a `MenuState` struct on `Game` (reset on `newGame`/`load`), and the preview `Plane` demos into the
camera class. Half a day; the tests cover the menus.

### 2.3 One `LoadingState` struct - small, removes copy-paste

Fifteen `load*` fields, reset in four places by hand, read by the loading screen, the research warm-up, the prewarm
stage and the loading log. Make them one struct with `reset()`, and `sceneWork()`/`sceneProgress()` its methods. The
v4.4.0 "one readiness model" did the logic; the data is still scattered. Two hours.

### 2.4 Debug switches behind one gate - small, a correctness fix

The 101 `getenv` hooks should be read once, at launch, into a `DevSwitches` struct, and only when a `--dev` flag (or
a tool mode such as `--shots`) is present. A player's `TOD` or `WX` environment variable must not change the game.
Also removes 67 scattered `getenv` calls from `game.cpp`. Half a day, mostly mechanical.

### 2.5 `WinMain` into tool modes - small

The six command-line tool modes (`--analyze`, `--profile`, `--soak`, `--bench`, `--loadshots`, `--shots`) are 700 of
`WinMain`'s 1,235 lines. Move each to a function in a `tool_modes.cpp` and dispatch from a table. No behaviour
change; `diagnostics.bat` keeps working.

### 2.6 The `Renderer` as subsystems - large, do it gradually

530 members and 180 methods in one class, implemented across eight files. Natural seams already exist in those files:
the scenery streamer and instancing (`entity_render.cpp`), the aircraft baker (`aircraft_mesh.cpp`), the shadow maps
and proxies, the post-processing chain, the shader cache. Promote each to its own class held by `Renderer`, one at
a time, starting with the scenery streamer (the most recently changed, best tested). Not urgent: the renderer is
stable and its cost shows in compile time, not in bugs.

### 2.7 A test header and a CMake function - small, pays back every week

- `tests/test_main.h`: the `check` lambda, the pass/fail summary line, the exit code, and a `TimeBudget` helper that
  judges wall-clock thresholds against a measured single-thread baseline (or skips under `FLIGHT_QUICK`), so the 35
  timing tests stop depending on the runner. The GPU and render harnesses get a shared `gpu_harness.h` for their
  duplicated set-up.
- A CMake function `sv_test(name sources... [WORLD] [COST n])` replacing the 166 `add_executable` /
  `target_include_directories` / `add_test` triplets and the fixture lists; `CMakeLists.txt` from 1,018 lines to
  about 400.
- Split the five tests over five minutes (`new_aircraft_go_around`, `hangar`, `procedural_region_runtime`,
  `gameplay_loop_2`, `soak`) or mark them nightly where the per-push value is low: the sanitizer shard would drop
  from 27 to about 15 minutes.

### 2.8 Style: a column limit - small, cumulative

Adopt a `.clang-format` with the project's brace style and a 140-column limit, applied file by file as files are
touched (not in one commit, which would bury the history). The one-statement-per-line change alone makes `git
blame`, review diffs and the sanitizer's line numbers usable.

### 2.9 Background jobs that fail silently - small

Replace the five `catch (...) {}` around futures in `game.cpp` with a catch that records the message (the existing
`g_error`/startup log), so a failed Earth prefetch or retire job is at least written down.

## 3. What is fine and should stay

- **The flight model** (`aircraft.cpp`, `aero_strips.cpp`): long functions, but one topic each, dense with physics,
  and covered by the flight-model, envelope, water and XR tests. Splitting `Plane::substep` would hurt more than help.
- **The world and scenery generation** (`world.cpp`, `scenery.cpp`, `settlements.cpp`, `road_network.cpp`,
  `entities.cpp`): well bounded, worker-thread discipline is clean (`thread_local` world per worker, drained
  replacement), and the streaming now has a test.
- **The real Earth stack** (`earth_*.cpp`, `world_earth.cpp`): recent, documented in `REAL_EARTH_PLAN.md`, verified
  against the published pack this week.
- **Content as C++ tables** (`kAircraft` with 43 fields a type, the research cards, the missions, the story): they
  recompile `aircraft.cpp` on a change (6 s) and the tests read them at compile time. Moving them to data files is
  not worth it at 17 aircraft.
- **The shader set**: one `plane_sdf.glsl` for every airframe is large, but the per-type pruning and the helper
  processes already keep each program's compile bounded, and `shader_prune_test` guards it.
- **Exceptions in the Earth reader**: a pack that fails to parse should throw; the catch sites are the problem
  (2.9), not the throws.
- **The platform layer** being Windows-only with Mesa for Linux tests: it is the product's shape.

## 4. Suggested order

1. 2.3 and 2.4 (a day together): small, each removes a bug class.
2. 2.2 (half a day), then 2.1 one class at a time, `DebugScenes` and `LoadingScreen` first since they are already
   self-contained; measure `game.cpp`'s compile time after each move.
3. 2.7 alongside, as tests are touched.
4. 2.5, 2.9 when convenient; 2.6 and 2.8 gradually.

None of it changes what the player sees. The tests, the Windows build and `diagnostics.bat`'s outputs are the
acceptance test for every step.
