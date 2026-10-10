#!/usr/bin/env python3
"""Guard the aircraft specialization: the family builds (v3.39) and each aircraft's own build (AF_MODEL), while
integrating terrain materials. Source contracts run without dependencies. With --shader-dir and --validator,
preprocess and link shader_check's real assemblies (not a copied shader list), and check that each aircraft's own
programs, as pruned for the driver (shader_prune.h), hold that aircraft's own code and no other aircraft's.
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
             'FLEET_ON && mid == 21', 'FLEET_ON && mid >= 60', 'FLEET_ON && MODEL_IS(8)',
             'FLEET_ON && isMantis()', 'FLEET_ON && fleetCabin()',
             'WRAITH_ON && mid >= 80', 'WRAITH_ON && mid >= 61', 'JET_ON && mid >= 30'):
    assert gate in material, 'Missing material family guard: ' + gate
mesh = read('src/aircraft_mesh.cpp')
hull = read('src/aircraft_hull.cpp')
assert 'afModelOf(trafK >= 0 ? fp.traffic[trafK].t : fp.plane.M, trafK >= 0 ? -1 : fp.plane.model)' in mesh, 'Lost per-aircraft mesh dispatch'
assert 'afMeshProgram(own)' in mesh and 'planeMeshFSAssembly(aircraftDefines(model, M))' in mesh, 'Lost the aircraft\'s own mesh build'
assert 'afBakePrograms(own, slot, hullBakeProg)' in hull and 'hullBakeFSAssembly(bakeDefines(model, slot))' in mesh, 'Lost the aircraft\'s own bake'
assert '"#define AF_OUTSIDE\\n"' in mesh, 'The outside body\'s builder must leave the cabin out'
assert 'meshStamp(afModelOf(M, pv.model), inside ? 1 : 0)' in mesh, 'The bodies\' cache must be stamped per aircraft and body'
assembly = read('src/shaders.h')
assert 'defines + "#define AF_MESH\\n"' in assembly
assert '"#define AF_MODEL "' in assembly and '"#define AF_PACKED_MODEL vec4[24]("' in assembly
assert re.search(r'#ifdef AF_MODEL\nconst vec4 gM\[24\] = AF_PACKED_MODEL;\n#else\nvec4 gM\[24\];\n#endif', read('src/shaders/scene_uniforms.glsl')), 'The own build\'s packed model must be constant'
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
    own = [f'plane_mesh_af{m}.frag' for m in range(13)]
    mesh_names = ('plane_mesh.frag', 'plane_mesh_safegear.frag') + tuple(own)
    for name in mesh_names + ('terrain.frag', 'map.frag'):
        path = a.shader_dir / name
        source = subprocess.run([a.validator, '-E', str(path)], check=True, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
        for helper in terrain_helpers:
            present = bool(re.search(r'\b' + helper + r'\s*\(', source))
            assert present == (name not in mesh_names), (name, helper, present)
        subprocess.run([a.validator, '-l', str(path)], check=True, stdout=subprocess.PIPE)
    print('PASS: the shared and all thirteen own aircraft mesh builds exclude terrain helpers; terrain/map retain them; all link')
    # each aircraft's own code (a function only it runs) in its own programs, and in no other aircraft's: the mesh
    # pass's and the bake's, as the driver gets them
    own_code = {
        'mapFleetPanel': range(10), 'shadeFleetCabin': range(10), 'fuselagePaint': (*range(10), 11),
        'swiftSeatPadded': (7,), 'swiftPowerFurniture': (7,), 'swiftYokeWheel': (7,), 'swiftDisplayRearSupports': (7,),
        'bushSeatPadded': (2,), 'bushmasterPowerFurniture': (2,), 'bushmasterStickGrip': (2,), 'bushRaisedPrimaryRisers': (2,),
        'mapOspreyCabinTrim': (8,), 'ospreyCabinAlbedo': (8,), 'twinPowerBankField': (3, 8),
        'mapMantisCockpit': (11,),
        'mapJet': (10,), 'mapJetCockpit': (10,), 'jetScreen': (10,), 'jtNozzleShape': (10,),
        'mapWraith': (12,), 'mapWraithCockpit': (12,), 'wraithScreen': (12,), 'shadeWraithCockpit': (12,),
    }
    shading = ('shadeFleetCabin', 'fuselagePaint', 'ospreyCabinAlbedo', 'jetScreen', 'wraithScreen', 'shadeWraithCockpit')
    cabin = ('mapFleetPanel', 'swiftSeatPadded', 'swiftPowerFurniture', 'swiftYokeWheel', 'swiftDisplayRearSupports', 'bushSeatPadded',
             'bushmasterPowerFurniture', 'bushmasterStickGrip', 'bushRaisedPrimaryRisers', 'mapOspreyCabinTrim', 'twinPowerBankField',
             'mapMantisCockpit', 'mapJetCockpit', 'mapWraithCockpit')
    for m in range(13):
        for prog in (f'plane_mesh_af{m}.frag', f'hullbake_af{m}.frag', f'hullbake_out_af{m}.frag', f'objects_af{m}.frag', f'shadow_proxy_af{m}.frag'):
            src = (a.shader_dir / 'pruned' / prog).read_text()
            for fn, owners in own_code.items():
                defined = bool(re.search(r'^\w+\s+' + fn + r'\s*\(', src, re.M))
                want = m in owners and not (prog.startswith(('hullbake', 'shadow_proxy')) and fn in shading)   # (the march shades what it finds, as the mesh pass does)
                want = want and not (prog.startswith('hullbake_out') and fn in cabin)   # (the outside body's builder: no cabin)
                assert defined == want, (prog, fn, 'defined' if defined else 'missing')
    for m in range(13):   # (and the outside body's builder has no cabin at all, while the cockpit's has it)
        out = (a.shader_dir / 'pruned' / f'hullbake_out_af{m}.frag').read_text()
        ck = (a.shader_dir / 'pruned' / f'hullbake_af{m}.frag').read_text()
        for fn in ('loadCabinFit', 'interiorAO'):
            assert not re.search(r'^\w+\s+' + fn + r'\s*\(', out, re.M), (m, fn, 'in the outside builder')
            assert re.search(r'^\w+\s+' + fn + r'\s*\(', ck, re.M), (m, fn, 'missing from the cockpit builder')
    print('PASS: each aircraft\'s own mesh, bake (outside and cockpit), march and shadow programs hold its own code and no other aircraft\'s')
    # the programs the launch builds hold no airframe at all, so no edit to an aircraft compiles anything at launch
    # (each aircraft's are made as it is drawn); and each scenery class's holds only its class's surfaces
    for prog in ('objects_noaf.frag', 'shadow_proxy_maps.frag', 'effects_light.frag', 'part_pose.frag'):
        src = (a.shader_dir / 'pruned' / prog).read_text()
        for fn in ('mapPlaneBody', 'mapPlane', 'mapJet', 'mapWraith', 'planeMaterialN', 'mapFleetPanel'):
            assert not re.search(r'^\w+\s+' + fn + r'\s*\(', src, re.M), (prog, fn)
    classes = {0: ('P_LEAFCARD', 'P_NEEDLE'), 1: ('P_ROCK',), 2: ('K_HANGAR', 'P_FENCE')}
    for c in range(3):
        src = (a.shader_dir / 'pruned' / f'entities_c{c}.frag').read_text()
        for k, words in classes.items():
            for w in words:
                used = bool(re.search(r'part\s*==\s*' + w + r'|uKind\s*>=\s*' + w, src))
                assert used == (k == c), (c, w, used)
    print('PASS: the launch\'s programs hold no airframe; each scenery class\'s program holds its own surfaces alone')
