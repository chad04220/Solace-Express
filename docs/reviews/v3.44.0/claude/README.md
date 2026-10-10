# Claude's evidence: probe and full analysis

- `ANALYSIS.md`: Claude's complete analysis. It was written against v3.43.0 and then re-verified on v3.44.0; line numbers are v3.44.0's.
- `probe.cpp`: the headless probe behind every runtime claim in `../REVIEW.md` marked *(Claude, runtime)*. It is analysis code, not a test, and it is not built by CMake.
  - It links against the game's own `game_objs` objects and drives `Career`, `simulateFlightMinutes` and the real `Game::update`.
  - It uses the existing `friend struct GameTest` hook, the same way `tests/gameplay_test.cpp` does.

## Build (Linux, from the repository root)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target gameplay_test          # builds game_objs and the radio stub the probe links with
g++ -std=gnu++17 -O2 -DNDEBUG -Isrc -Ibuild/gen docs/reviews/v3.44.0/claude/probe.cpp \
    build/CMakeFiles/game_objs.dir/src/*.o build/CMakeFiles/gameplay_test.dir/src/radio_stub.cpp.o \
    -ldl -lpthread -o build/review_probe
```

## Run (from the repository root, so the world and voice assets resolve)

| Mode | What it checks | Review finding | v3.44.0 result (identical on v3.43.0) | Time |
|---|---|---|---|---|
| `cruise` | Patient meter in straight, level autopilot cruise at turbulence 0.05–0.55 (P4's mission) | CAR-1 | 100 / 100 / 70 / 40 / **10 %** after 5 min | ~1 min |
| `meter` | P4 flown on a scripted take-off plus autopilot, as shipped and in calm air | CAR-1 context | *Inconclusive:* the probe's own crude take-off fails at Summit Pass. Use `cruise`. | ~30 s |
| `airline` | 10 take-off / land-back-at-departure legs with two Q400 routes | CAR-2 | **+$12,394 per circuit**, +$123,820 total; rental charged once | <1 s |
| `story` | Every story mission × every aircraft eligible at that stage, through `simulateFlightMinutes` | CAR-3, CAR-8 | **13 of 62** non-lesson pairs never arrive; P4 and A4 late | ~4 min |
| `why` | A reporting copy of `simulateFlightMinutes` for the failing pairs | CAR-3 | All fail in phase 0 (take-off roll) within 20–40 s | ~10 s |
| `timed` | 40 timed freelance jobs, best eligible type on the autopilot | CAR-8 | 29 makeable, **7 late in every type**, 4 never arrive | ~12 min |
| `vip` | VIP title vs brief over 400 seeds × 16 airports | CAR-9 | **3,453 / 4,151** disagree | ~20 s |
| `names` | Job-type and medevac-name distribution over 48,000 boards | (RNG check, not a finding) | uniform | ~3 min |
| `fleet` | Price / rent / capacity table | context | — | <1 s |

Example: `./build/review_probe cruise`.
