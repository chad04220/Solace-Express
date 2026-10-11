#!/usr/bin/env python3
"""CI's test selection (tools/ci/select_tests.py) on this build: a change picks the tests it can affect and leaves
the rest. Documentation picks nothing; the build's own files pick everything; a test's own source picks it; a shared
source, header, shader or fixture picks what uses it, and not what doesn't.

Usage: ci_select_test.py <build dir> [config]. Exits 77 (skipped) where the build has no CMake file-API answer yet:
CMakeLists.txt asks for one, and the next configure gives it.
"""
import glob, ntpath, os, sys
from types import SimpleNamespace
from unittest.mock import patch

SRC = os.path.realpath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(SRC, "tools", "ci"))
import select_tests as st  # noqa: E402

build = sys.argv[1]
config = sys.argv[2] if len(sys.argv) > 2 else ""
if not glob.glob(os.path.join(build, ".cmake", "api", "v1", "reply", "index-*.json")):
    print("skipped: no CMake file-API answer in this build yet (configure once more)")
    sys.exit(77)
model = st.Model(SRC, build, config)
names = {t["name"] for t in model.tests}
fails = 0


# Exercise Windows path semantics on every host, including a system Python on C:
# with the Actions checkout on D:. Such tools are external, not source inputs.
win_model = st.Model.__new__(st.Model)
win_model.src = r"D:\a\Solace-Express\Solace-Express"
with patch.object(st, "os", SimpleNamespace(path=ntpath, sep="\\")):
    for path, expected in (
        (r"C:\hostedtoolcache\windows\Python\3.12\python.exe", None),
        (r"C:/Program Files/CMake/bin/cmake.exe", None),
        (r"D:\a\Solace-Express\Solace-Express\src\game.cpp", "src/game.cpp"),
        (r"d:\a\Solace-Express\Solace-Express\tests\flight_test.cpp", "tests/flight_test.cpp"),
        (r"src\game.cpp", "src/game.cpp"),
        (win_model.src, "."),
        (r"D:\a\external\tool.exe", None),
        (r"\\server\share\tool.exe", None),
    ):
        actual = win_model.rel(path)
        ok = actual == expected
        print(f"{'ok  ' if ok else 'FAIL'} Windows source path {path}: {actual!r}")
        fails += not ok


def check(changed, mode=None, picks=(), skips=()):
    global fails
    m, picked, targets, notes = st.decide(model, changed)
    picks = [p for p in picks if p in names]   # (a test this platform doesn't register can't be asked for)
    skips = [s for s in skips if s in names]
    ok = (mode is None or m == mode) and all(p in picked for p in picks) and not any(s in picked for s in skips)
    print(f"{'ok  ' if ok else 'FAIL'} {', '.join(changed) or '(nothing)'}: {m}, {len(picked)} tests"
          + ("" if ok else f" (wanted {mode or 'any'}, {picks} in, {skips} out; picked {picked})"))
    fails += not ok


check([], mode="none")
check(["README.md", "docs/LIVING_ISLANDS_PLAN.md", "RELEASE_NOTES.md"], mode="none")
check(["CMakeLists.txt"], mode="all")
check([".github/workflows/build.yml"], mode="all")
check(["tools/ci/select_tests.py"], mode="all")
check(["tests/flight_test.cpp"], mode="some", picks=["flight_model_1"], skips=["gameplay_loop_2", "world_cache", "campaign_progression_1"])
check(["src/game_ui.cpp"], mode="some", picks=["gameplay_loop_2"], skips=["flight_model_1", "terrain_envelope", "campaign_progression_1"])
check(["src/aircraft.cpp"], picks=["flight_model_1", "campaign_progression_1", "gameplay_loop_2"], skips=["mesh_simplify", "community_layout"])
check(["src/world.h"], picks=["world_cache", "flight_model_1", "community_layout", "runway_preservation"])
check(["src/shaders/water.glsl"], picks=["shader_prune", "gameplay_loop_2"], skips=["flight_model_1", "career_saves"])
check(["src/entity_mesh.cpp"], picks=["environment_buildings"], skips=["flight_model_1", "career_saves"])
check(["tests/fixtures/runways_52317bb.golden"], picks=["runway_preservation"], skips=["flight_model_1"])
# the run's islands (tests/test_world.cpp, a ctest fixture): picked with the tests that load them, and on their own
check(["src/world.cpp"], picks=["test_world", "flight_model_1", "runway_preservation", "world_cache"])
check(["tests/test_world.h"], mode="some", picks=["test_world", "flight_model_1", "ufo_encounter"], skips=["runway_preservation", "hull_mesh"])
check(["tests/test_world.cpp"], mode="some", picks=["test_world"], skips=["flight_model_1"])
check(["tests/encounter_timing_test.cpp"], mode="some", picks=["ufo_encounter", "test_world"], skips=["flight_model_1"])
# every compiled test is picked by a change to its own main source
for t in model.tests:
    cmd = t.get("command") or []
    exe = model.target_of(cmd[0]) if cmd else None
    own = [s["path"] for s in model.targets[exe]["sources"]] if exe else []
    own = [p for p in own if p.startswith("tests/") and p.endswith(".cpp")]
    if own:
        m, picked, _, _ = st.decide(model, own[:1])
        if t["name"] not in picked:
            print(f"FAIL {own[0]} does not pick {t['name']}"); fails += 1
# CI's sanitizer shards: every test dealt to exactly one, the fixture setups (the run's islands) to each shard with a
# test that needs them, and no shard more than the costliest test over another
setups = {t["name"] for t in model.tests if t["setup"]}
needs = {t["name"]: t["requires"] for t in model.tests}
for n in (2, 3):
    got = [st.shard(model, sorted(names), n, k) for k in range(1, n + 1)]
    load = got[0][2]
    dealt = sorted(x for mine, _, _ in got for x in mine if x not in setups)
    served = all(not any(needs[x] for x in mine) or setups & set(mine) for mine, _, _ in got)
    ok = dealt == sorted(names - setups) and served and max(load) - min(load) <= max(t["cost"] for t in model.tests if t["name"] not in setups)
    print(f"{'ok  ' if ok else 'FAIL'} {n} shards: {', '.join('%.0f s' % l for l in load)}, each with the islands it needs {served}")
    fails += not ok
print(f"{len(model.tests)} tests; {'all passed' if not fails else str(fails) + ' FAILED'}")
sys.exit(1 if fails else 0)
