#!/usr/bin/env python3
"""Extract current production methods for CPU mock tests; write only into the requested output directory."""
from pathlib import Path
import hashlib, json, sys

HERE = Path(sys.argv[1]).resolve()
SRC = Path(__file__).resolve().parents[2]
HERE.mkdir(parents=True, exist_ok=True)

def function(text, name):
    start = text.index(name)
    brace = text.index('{', start)
    depth = 0
    for end in range(brace, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if not depth:
            return text[start:end + 1]
    raise ValueError(name)

hull = (SRC / 'src/aircraft_hull.cpp').read_text()
renderer = (SRC / 'src/renderer.cpp').read_text()
feeds = (SRC / 'src/camera_feeds.cpp').read_text()
shader = (SRC / 'src/shaders/hull_bake_main.glsl').read_text()
mesh = (SRC / 'src/aircraft_mesh.cpp').read_text()
upstream = Path(__file__).with_name('reference_hull_bake_main.glsl').read_text()
out = [hull[hull.index('namespace {'):hull.index('bool Renderer::compileHull')]]
out += [function(hull, name) for name in [
    'bool Renderer::compileHull(', 'void Renderer::beginHullBake(',
    'GLuint Renderer::bindHullBake(', 'void Renderer::hullEval(',
    'void Renderer::hullEval4(', 'void Renderer::hullEvalBatch(',
    'float Renderer::hullNear(']]
out += [function(renderer, 'void Renderer::setRT('), function(feeds, 'void Renderer::measureFeedMounts(')]
(HERE / 'actual_dispatch.inc').write_text('\n\n'.join(out) + '\n')
(HERE / 'actual_link_cache.inc').write_text(function(renderer, 'GLuint linkProgramCached(') + '\n')
normalized = shader.replace('#ifdef HULL_BAKE_NORMALS\nconst int uHMode = 3;\n#else\nuniform int uHMode;\nuniform sampler2D uHNormals;   // exact mode-3 output at these same points/state, reused for cabin AO\n#endif\nuniform int uHState;', 'uniform int uHMode; uniform int uHState;')
normalized = normalized.replace('#ifdef HULL_BAKE_NORMALS\n    vec3 n = planeNormal(p);\n#else\n    vec3 n = texelFetch(uHNormals, ivec2(gl_FragCoord.xy), 0).xyz;\n#endif', '    vec3 n = planeNormal(p);')
checks = {
    'upstream bake shader identical except reviewed selector and exact-normal input substitution': normalized == upstream,
    'body mode 2 gets immediately preceding exact normals': 'mode(3, 0); hullEval4(vpos, vn);\n      mode(2, 0); hullEval4(vpos, d4, &vn);' in mesh,
    'part mode 2 gets immediately preceding exact normals': 'mode(3, 0); hullEval4(vp, pn);\n        mode(2, 0); hullEval4(vp, p4, &pn);' in mesh,
    'mesh callers have no stale direct baker uniform uploads': 'U(progHullBake,' not in mesh,
    'feed starts its own canonical distance query': 'beginHullBake(fp, 1, ps, ctl);' in function(feeds, 'void Renderer::measureFeedMounts('),
    'shader identity includes shared bake main': 'kHullBakeMain' in function(renderer, 'std::string shaderCacheStamp('),
    'mesh identity includes shared bake main': 'kHullBakeMain' in function(renderer, 'std::string meshCacheStamp('),
    'point batches obey device limit and 8192-row cap': 'size_t(512) * std::min(maxTex, 8192)' in function(hull, 'void Renderer::hullEval4('),
}
paths = ['src/renderer.cpp', 'src/renderer.h', 'src/aircraft_hull.cpp', 'src/aircraft_mesh.cpp', 'src/camera_feeds.cpp', 'src/shaders/hull_bake_main.glsl', 'src/shaders/plane_trace.glsl']
report = {'source_root': str(SRC), 'source_sha256': {f: hashlib.sha256((SRC / f).read_bytes()).hexdigest() for f in paths}, 'source_checks': checks}
(HERE / 'source-contract.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(checks, indent=2))
assert all(checks.values())

# Current compiler/cache/lifetime code, without the full renderer dependencies.
start=renderer.index("static GLuint compile(GLenum")
end=renderer.index("static GLuint program(",start)
(HERE / "actual_shader_functions.inc").write_text(renderer[start:end])
