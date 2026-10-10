#!/usr/bin/env python3
"""Guard the CPU/GLSL part ABI and separate aircraft-family mesh-build modules."""
from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return (ROOT / path).read_text()

cpu = read('src/aircraft_mesh_build_types.h')
glsl = read('src/shaders/plane_parts.glsl')
pattern = r'\b(PT_[A-Z_]+)\s*=\s*(\d+)'
assert dict(re.findall(pattern, cpu)) == dict(re.findall(pattern, glsl)), 'CPU/GLSL part identities differ'
registry = read('src/aircraft_mesh_build.h')
for family in ('fleet', 'specter', 'wraith'):
    module = read(f'src/aircraft_mesh_build_{family}.h')
    assert f'#include "aircraft_mesh_build_{family}.h"' in registry
    for entry in ('parts', 'partPlan', 'appendHullStates'):
        assert f'{family}::{entry}' in registry, (family, entry, 'not registered')
        assert re.search(r'\b' + entry + r'\s*\(', module), (family, entry, 'missing interface')
    assert 'kPlaneSDF' not in module and 'kWraithSDF' not in module, 'CPU builders must not combine shaders'
mesh = read('src/aircraft_mesh.cpp')
assert 'meshBuilderFor(M).partPlan(type, M)' in mesh, 'Mesh bake bypasses the family build plan'
assert 'using aircraftBuild::partList;' in mesh, 'Bake/pose instance lists must share one owner'
assert 'bool wraithPartBox' not in mesh and 'bool gearPartBox' not in mesh, 'Geometry ownership leaked into orchestrator'
assert 'meshBuilderFor(M).appendHullStates(st, inside, meshBake)' in read('src/hull_mesh.h')
assert 'aircraftBuild::shaderDefine(aircraftBuild::familyOf(packed))' in read('src/shaders.h')
print('PASS: CPU/GLSL part ABI, separate family builders, common bake/pose/hull dispatch')
