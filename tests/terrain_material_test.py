#!/usr/bin/env python3
"""Extract and test terrain-only GLSL arithmetic plus source contracts without a graphics context.
Requires a C++17 compiler; uses only the Python standard library. This is not a render/FPS test.
"""
from pathlib import Path
import argparse, hashlib, os, re, shlex, shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--extract-only', type=Path, help='Validate source contracts and write a CMake-native C++ include; do not compile.')
parser.add_argument('--source-only', action='store_true', help='Run only source contracts; do not compile.')
parser.add_argument('--cxx', help='C++ compiler executable, e.g. CMAKE_CXX_COMPILER (including MSVC cl.exe).')
args=parser.parse_args()

def function(text,name):
    hit=re.search(r'\b(?:float|vec2|vec3|vec4)\s+'+re.escape(name)+r'\s*\(',text)
    assert hit, 'Missing production helper: '+name
    start=hit.start();brace=text.index('{',hit.end());depth=0
    for end in range(brace,len(text)):
        depth+=(text[end]=='{')-(text[end]=='}')
        if depth==0:return text[start:end+1]
    raise AssertionError('Unclosed helper: '+name)

mat=(ROOT/'src/shaders/material_common.glsl').read_text()
terrain=(ROOT/'src/shaders/terrain_material.glsl').read_text()
fs=(ROOT/'src/shaders/terrain_fs.glsl').read_text()
mp=(ROOT/'src/shaders/map_main.glsl').read_text()
# These shared aircraft/cockpit helpers must stay byte-identical to the reviewed v3.39 release e9ec52d base.
# triSample intentionally retains v3.39's negligible-projection skip; do not restore the older a7f8d30 helper.
RELEASE_HASHES = {
    'matSample': 'f9b5b57084cc5c332533c95869ced96e19e8de06c983cb371e9504b9805653ec',
    'triSample': '07161344e5286b88b0a874f7b0f8febd399c561206ef2b3251dd1fd147f45eaa',
    'applyTS': '3723b506417f49e5f0001188b0b7f87276d209825c488913647b6a5ebbcc00ca',
}
for name,digest in RELEASE_HASHES.items():
    assert hashlib.sha256(function(mat,name).encode()).hexdigest()==digest, 'Aircraft shared helper changed: '+name
assert fs.index('dFdx(vW.xz)') < fs.index('discard;')
assert fs.index('dFdy(vW.xz)') < fs.index('discard;')
assert 'dFdx(' not in terrain and 'dFdy(' not in terrain and 'fwidth(' not in terrain
assert 'vec2(foot, 0.0), vec2(0.0, foot)' in mp, 'Map must supply its analytic footprint'
assert 'terrainBumpStrength(t)' in fs and 't < 2000.0 ? 0.6 : 0.25' not in fs
assert 'terrainTriSample(p, n, M_ROCK, 18.0, nr)' in terrain
assert function(mat,'terrainTriSample').count('matSample(')==3
terrain_tri=function(mat,'terrainTriSample')
assert 'weight = max(weight - 0.02, 0.0); weight /= dot(weight, vec3(1.0));' in terrain_tri
assert 'vec3 nx = vec3(0.0), ny = vec3(0.0), nz = vec3(0.0);' in terrain_tri
for axis,plane,normal in (('x','zy','nx'),('y','xz','ny'),('z','xy','nz')):
    assert f'if (weight.{axis} > 0.0) r += matSample(p.{plane}, layer, scale, {normal})*weight.{axis};' in terrain_tri

assert function(mat,'triSample').count('matSample(')==3
assert function(mat,'groundSample').count('texA(')==3 and function(mat,'groundSample').count('texN(')==3
assert 'n1b.xy = groundRotatedNormal(n1b.xy*2.0 - 1.0)*0.5 + 0.5;' in mat
assert 'm.alb *= 1.0 - 0.35*uWet*(1.0-wSnow);' in terrain
assert 'm.rough = mix(m.rough, m.rough*0.35, uWet*(1.0-wSnow));' in terrain
assert 'terrainLineCoverage(14.0 - sx, 0.15, footprint.x)' in terrain
assert 'terrainLineCoverage(14.0 - sz, 0.15, footprint.y)' in terrain
assert 'terrainTownPaint(p.xz, sx, sz, groundFoot)' in terrain
assert 'float sx = mod(li.x, 3.0) == 0.0 ? e.x : 0.0, sz = mod(li.y, 2.0) == 0.0 ? e.y : 0.0;' in terrain
assert 'sx > 10.5 && sz <= 10.5' in terrain and 'sz > 10.5 && sx <= 10.5' in terrain
for path in (ROOT/'src/shaders').glob('*.glsl'):
    if path.name in ('material_common.glsl','terrain_material.glsl','terrain_fs.glsl','map_main.glsl'):continue
    body=path.read_text()
    assert 'terrainTriSample(' not in body and 'groundSample(' not in body and 'terrainBumpStrength(' not in body, 'Environment helper leaked into '+path.name
# Execute arithmetic extracted from the production file, never a hand-copied mirror.
functions=[function(mat,n) for n in ('groundRotatedNormal','terrainTriNormal','applyTS','terrainBumpStrength')]
# Test the actual production projection weights, including the release's skip and renormalization.
weight_body=terrain_tri.split('{',1)[1].split('vec3 nx',1)[0]
functions += ['vec3 terrainProjectionWeights(vec3 n){'+weight_body+'return weight; }']
functions += [function(terrain,n) for n in ('terrainLineCoverage','terrainStripeIntegral','terrainStripeCoverage','terrainDetailWeight','terrainTownPaint')]
# GLSL's unsuffixed floating literals are float; retain that arithmetic in the C++ host.
arithmetic=re.sub(r'(?<![\w.])((?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?|\d+[eE][+-]?\d+)(?![\w.])', r'\1f', '\n\n'.join(functions))+'\n'
if args.source_only:
    print('PASS: terrain material source contracts')
    raise SystemExit(0)
if args.extract_only:
    output=args.extract_only.resolve();output.parent.mkdir(parents=True,exist_ok=True)
    if not output.exists() or output.read_text()!=arithmetic:output.write_text(arithmetic)
    print('PASS: terrain material source contracts; extracted production arithmetic')
    raise SystemExit(0)
with tempfile.TemporaryDirectory(prefix='solace-terrain-material-') as tmp:
    out=Path(tmp)
    (out/'terrain_material_actual.inc').write_text(arithmetic)
    compiler=[args.cxx] if args.cxx else shlex.split(os.environ.get('CXX') or ('cl' if os.name=='nt' and shutil.which('cl') else 'c++'))
    msvc=Path(compiler[0]).name.lower() in ('cl','cl.exe')
    binary=out/('test.exe' if os.name=='nt' else 'test')
    if msvc:
        command=compiler+['/nologo','/std:c++17','/O2','/EHsc','/I'+str(ROOT/'src'),'/I'+str(out),str(ROOT/'tests/terrain_material_math_test.cpp'),'/Fe:'+str(binary)]
    else:
        command=compiler+['-std=c++17','-O2','-I'+str(ROOT/'src'),'-I'+str(out),str(ROOT/'tests/terrain_material_math_test.cpp'),'-o',str(binary)]
    subprocess.run(command,cwd=out,check=True)
    subprocess.run([str(binary)],check=True)
print('PASS: terrain material source contracts; shared aircraft functions unchanged; texture fetch counts unchanged')
