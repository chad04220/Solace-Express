#!/usr/bin/env python3
"""Guard v3.39 aircraft specialization while integrating terrain materials.
Source contracts run without dependencies. With --shader-dir and --validator,
preprocess and link shader_check's real assemblies (not a copied shader list).
This establishes compilation/isolation, not driver register usage or GPU time.
"""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--shader-dir', type=Path, help='Output directory from tools/shader_check')
p.add_argument('--validator', help='glslangValidator executable')
a = p.parse_args()
if bool(a.shader_dir) != bool(a.validator):
    p.error('--shader-dir and --validator must be supplied together')

def read(name):
    return (ROOT / name).read_text()

common = read('src/shaders/plane_common.glsl')
expected = {
    'AF_WRAITH': (True, False, False, True),
    'AF_JET': (True, False, True, False),
    'AF_LIGHT': (False, True, False, False),
}
for family, values in expected.items():
    block = re.search(r'#(?:if|elif) defined\(' + family + r'\)(.*?)(?=#el)', common, re.S).group(1)
    for gate, value in zip(('RESEARCH_ON', 'FLEET_ON', 'JET_ON', 'WRAITH_ON'), values):
        assert f'#define {gate} {str(value).lower()}' in block, (family, gate)

material = read('src/shaders/plane_material.glsl')
for gate in ('FLEET_ON && mid == 1', 'FLEET_ON && mid == 2', 'FLEET_ON && mid == 10',
             'FLEET_ON && mid == 21', 'FLEET_ON && mid >= 60', 'FLEET_ON && gModelId == 8',
             'FLEET_ON && isMantis()', 'FLEET_ON && fleetCabin()',
             'WRAITH_ON && mid >= 80', 'WRAITH_ON && mid >= 61', 'JET_ON && mid >= 30'):
    assert gate in material, 'Missing material family guard: ' + gate
mesh = read('src/aircraft_mesh.cpp')
assert 'e == 6 ? 3 : e == 5 ? 2 : e < 5 ? 1 : 0' in mesh, 'Lost per-aircraft mesh dispatch'
assert '"#define AF_LIGHT\\n", "#define AF_JET\\n", "#define AF_WRAITH\\n"' in mesh + read('src/renderer.h'), 'Lost the mesh builds'
assembly = read('src/shaders.h')
assert 'defines + "#define AF_MESH\\n"' in assembly
terrain_helpers = ('groundRotatedNormal', 'groundSample', 'hblend', 'terrainTriNormal',
                   'terrainTriSample', 'terrainBumpStrength')
mat = read('src/shaders/material_common.glsl')
mesh_mat = re.sub(r'#ifndef AF_MESH\n.*?#endif // !AF_MESH', '', mat, flags=re.S)
for helper in terrain_helpers:
    assert not re.search(r'\b' + helper + r'\s*\(', mesh_mat), 'Terrain helper in aircraft mesh: ' + helper
for helper in ('matSample', 'triSample', 'applyTS'):
    assert re.search(r'\b' + helper + r'\s*\(', mesh_mat), 'Missing shared aircraft helper: ' + helper
assert 'bw = max(bw - 0.02, 0.0)' in mesh_mat
for axis in 'xyz':
    assert f'if (bw.{axis} > 0.0)' in mesh_mat
print('PASS: v3.39 family guards, per-aircraft dispatch, terrain-helper exclusion, projection skip')

if a.shader_dir:
    mesh_names = ('plane_mesh.frag', 'plane_mesh_light.frag', 'plane_mesh_jet.frag',
                  'plane_mesh_wraith.frag', 'plane_mesh_safegear.frag')
    for name in mesh_names + ('terrain.frag', 'map.frag'):
        path = a.shader_dir / name
        source = subprocess.run([a.validator, '-E', str(path)], check=True, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
        for helper in terrain_helpers:
            present = bool(re.search(r'\b' + helper + r'\s*\(', source))
            assert present == (name not in mesh_names), (name, helper, present)
        subprocess.run([a.validator, '-l', str(path)], check=True, stdout=subprocess.PIPE)
    print('PASS: five real aircraft mesh builds exclude terrain helpers; terrain/map retain them; all seven link')
