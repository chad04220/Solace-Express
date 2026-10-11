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
    hit=None
    for candidate in re.finditer(r'\b(?:float|vec2|vec3|vec4)\s+'+re.escape(name)+r'\s*\(',text):
        brace=text.index('{',candidate.end())
        if ';' not in text[candidate.end():brace]:
            hit=candidate;break   # skip a forward declaration
    assert hit, 'Missing production helper: '+name
    start=hit.start();depth=0
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
# the place the ground's material is worked out at keeps the height in y: v3.45.0 built it as vec3(wrapW(p.xz), p.y) -
# x, z, y - and every hillside came out snow, the runways, fields and towns gone
arg=re.search(r'terrainMaterial\((\w+)\s*,',fs).group(1)
if arg not in ('p','vW'):
    built=re.search(r'vec3\s+'+arg+r'\s*=\s*vec3\(([^;]*)\)\s*;',fs)
    assert built, 'terrain FS: the material position must be built as vec3(x, height, z)'
    parts=[x.strip() for x in built.group(1).split(',')]
    assert len(parts)==3 and re.fullmatch(r'(p|vW)\.y',parts[1]), 'terrain FS: the material position must keep the height in y, got '+built.group(1)
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
# the settlements' streets are the network's roads (class 4: kerbs, pavements, lamps); their ground between, by the mask
assert 'communityGrid' not in terrain and 'COMMUNITY_META' not in terrain
assert 'if (cls == 4) {   // a street\'s pavement' in terrain
assert 'if (cls == 4 && uNight > 0.01) {' in terrain
assert 'float yard = smoothstep(0.25, 0.55, msk.z' in terrain
assert 'len, wid, footprint, m)' in terrain, 'Airport paint needs local analytical footprints'
assert 'fieldMaterial(p.xz, msk.w*(1.0 - wRock), pixelDx, pixelDy, m)' in terrain
assert 'float onAccess = ' not in terrain  # topology is boolean, coverage only filters paint
ent=(ROOT/'src/shaders/ent_fs2.glsl').read_text()
ent_cut=(ROOT/'src/shaders/ent_fs1.glsl').read_text()
water=(ROOT/'src/shaders/water.glsl').read_text()
assert hashlib.sha256(function(ent,'triSAircraft').replace('triSAircraft(', 'triS(', 1).encode()).hexdigest()=='ecb3bf6d93dae12914231d9be007d5b265e86d3d661cc16e487eb714ead9f677', 'Parked-aircraft sampling changed'
assert ent.index('entDx = dFdx(vL)') < ent.index('discard;')
assert ent.index('entDy = dFdy(vL)') < ent.index('discard;')
assert ent.index('entUvDx = dFdx(vAux.zw)') < ent.index('discard;')
assert '#if ENT_TREES || ENT_ROCKS' not in ent, 'Close-slot crossfade must cover buildings too'
assert 'if (vAux.z < 0.5)' in ent, 'Fitted facades must suppress procedural windows'
assert 'ENTITY_GLASS = 16, ENTITY_CLEARCOAT = 32' in ent
assert 'float(surfaceFlags)/255.0' in ent
assert 'if (vAux.z >= 1.0 && !vehicle && uKind != K_PUMP)' in ent, 'Vehicle canopies and instrument faces must not acquire architectural room cues'
assert 'part == P_TRIM && (uKind == K_CAR || uKind == K_TRUCK)' in ent, 'Registration plates need their own enamel finish'
# Real pane geometry and the cheaper facade approximation must not draw two layouts
# over each other at the close-detail transition. Retain legacy parked-aircraft
# paint below the close slot; it is intentionally not remapped to the new mesh.
assert 'uLod != 3 && part == P_GLASS && vAux.z < 1.0 && (uKind == K_SHOP || uKind == K_GAS)' in ent
assert 'uLod != 3 && (win || ck)' in ent
assert 'float glassU = sideX ? nominal.z : nominal.x;' in ent
assert 'glassU = dot(nominal,vec3(-n0.z,0.0,n0.x));' in ent
assert 'fract((nominal.y - base)/3.7)' in ent
assert not re.search(r'\b(for|while)\s*\(|texture(?:Grad|Lod)?\s*\(', function(ent, 'entityFacadePane')), 'Coarse pane layout must stay bounded arithmetic'
room = function(ent, 'entityRoom')
assert not re.search(r'\b(for|while)\s*\(|texture(?:Grad|Lod)?\s*\(|dFdx\s*\(|dFdy\s*\(|fwidth\s*\(', room), 'Room depth must stay analytic, texture-free and derivative-safe'
assert 'if (detail <= 0.0) return average;' in room
assert not re.search(r'\b(for|while)\s*\(|texture(?:Grad|Lod)?\s*\(', function(ent_cut,'coniferShootCoverage')), 'Fine conifer sprays must use one bounded analytic branchlet, not per-needle loops'
assert function(ent,'triSGrad').count('environmentAlbGrad(')==3
assert function(ent,'triSGrad').count('environmentNrmGrad(')==3
for sampler in ('environmentAlbGrad','environmentNrmGrad'):
    assert function(ent,sampler).count('textureGrad(')==2
    assert 'if (uEnvMaterials != 0 && env >= 0) return ' in function(ent,sampler)
assert 'texture(' not in function(ent,'triSGrad')
assembly=(ROOT/'src/shaders.h').read_text()
assert assembly.count('#define ENV_MATERIALS\\n')==2, 'High-resolution selection must stay environment-only'
for axis in 'xyz': assert f'if (w.{axis} > 0.0)' in function(ent,'triSGrad')
assert 'if (uKind == K_GAPLANE || uKind == K_AIRLINER) return triSAircraft' in function(ent,'triS')
assert function(water,'waterBottomColor').count('textureGrad(')==4
assert 'waterDetailFrame(dx0)/1.13' in water and 'waterDetailFrame(dy0)/1.13' in water
assert 'waterWarpFootprint(pixelDx, warpA.yz, warpB.yz)' in water
assert 'waterWarpFootprint(pixelDy, warpA.yz, warpB.yz)' in water
assert function(water,'waterBottomColor').count('noised(')==2
assert '0.24 + 0.52*smoothstep' in function(water,'waterBottomColor')
assert function(water,'waterShade').count('groundH(')==2
assert 'texture(uGB' not in water and 'texelFetch(uGB' not in water
assert 's0*uWaveRms.x*a0 + s1*uWaveRms.y*a1 + s2*uWaveRms.z*a2' in water
assert 'craterRadius < 550.0 && p.y > 1300.0' in terrain
assert 'smoothstep(1784.0, 1795.0, p.y)' in terrain
assert function(terrain,'craterRockPlane').count('textureGrad(')==4
assert function(terrain,'craterRockMaterial').count('craterRockPlane(')==3
assert 'if (rubble > 0.005)' in terrain

for path in (ROOT/'src/shaders').glob('*.glsl'):
    if path.name in ('material_common.glsl','terrain_material.glsl','terrain_fs.glsl','map_main.glsl'):continue
    body=path.read_text()
    assert 'terrainTriSample(' not in body and 'groundSample(' not in body and 'terrainBumpStrength(' not in body, 'Environment helper leaked into '+path.name
# Environment diffuse irradiance is isolated in the deferred light pass. The aircraft's
# legacy ambient/direct-light functions remain exact, and all nonambient terms match.
lighting=(ROOT/'src/shaders/light_common.glsl').read_text()
light_fs=(ROOT/'src/shaders/light_fs.glsl').read_text()
def compact(body):
    return re.sub(r'\s+', '', re.sub(r'//[^\n]*', '', body))
assert function(light_fs,'environmentAmbientLight').count('skyColor(')==2
environment_surface = function(light_fs,'shadeEnvironmentSurface').replace('shadeEnvironmentSurface(', 'shadeSurface(', 1).replace('environmentAmbientLight(n)', 'ambientLight(n)')
environment_surface = environment_surface.replace('*(1.0 - m.metal)', '').replace('environmentReflection(r)', 'skyColor(normalize(vec3(r.x, abs(r.y), r.z)))')
assert compact(environment_surface)==compact(function(lighting,'shadeSurface')), 'Only diffuse irradiance/metal energy and reflected environment may differ from shared aircraft shading'
assert 'cls == GB_TERRAIN || cls == GB_ENTITY || cls == GB_FOLIAGE' in light_fs
assert hashlib.sha256(function(lighting,'ambientLight').encode()).hexdigest()=='3c6f5f484c7431cc7b9fa0e62a9935a58e3ede3061eef241e4c6105277b1a3db', 'Shared aircraft light changed'
assert hashlib.sha256(function(lighting,'shadeSurface').encode()).hexdigest()=='9e6030fc4c2d379a189d8fadd6a67339e43a6005e5b5739eda3ef77628f60176', 'Shared aircraft light changed'
assert 'cls == GB_ENTITY && (flags & GBF_ENV_GLASS) != 0' in light_fs
assert 'cls == GB_ENTITY && (flags & GBF_ENV_CLEARCOAT) != 0' in light_fs
assert function(light_fs, 'shadeEnvironmentGlazing').count('for (') == 1
assert function(light_fs, 'environmentReflection').count('skyColor(') == 1
assert 'max(direction.y, 0.025)' in function(light_fs, 'environmentReflection'), 'Glass must not mirror sky through the ground'
assert 'offsets.y/6000.0' in function(light_fs, 'environmentShadowCascade')
assert function(light_fs, 'environmentShadowCascade').count('shTap(')==10, 'One comparison per cascade choice, five only on mixed close edges'
assert 'wallPlane || (contact > 0.5 && center > 0.01 && center < 0.99)' in function(light_fs, 'environmentShadowCascade')
assert 'if (!wallPlane) center =' in function(light_fs, 'environmentShadowCascade'), 'Exact wall path must skip the redundant first lookup'
assert 'gEnvironmentReceiverEntity = cls == GB_ENTITY;' in light_fs, 'Extra wall comparisons must remain entity-only'
assert 'if (distance >= 180.0) return entShadow(p, n);' in function(light_fs, 'environmentShadow')
assert light_fs.index('receiverDx = dFdx(rd*g0.x)') < light_fs.index('if (cls == GB_SKY)'), 'Receiver derivatives must precede divergent exits'
assert function(light_fs, 'environmentShadowCascade').count('environmentShadowTapDepth(') == 4, 'Every extra PCF tap must follow the receiver plane'
# Execute arithmetic extracted from the production file, never a hand-copied mirror.
functions=[function(mat,n) for n in ('groundRotatedNormal','terrainTriNormal','applyTS','terrainBumpStrength')]
# Test the actual production projection weights, including the release's skip and renormalization.
weight_body=terrain_tri.split('{',1)[1].split('vec3 nx',1)[0]
functions += ['vec3 terrainProjectionWeights(vec3 n){'+weight_body+'return weight; }']
functions += [function(terrain,n) for n in ('terrainLineCoverage','terrainStripeIntegral','terrainStripeCoverage','terrainDetailWeight')]
functions += [function(ent,n) for n in ('entityProjectionWeights','entityDetailNormal','entityDetailFade','entityLine','entityRoomFaceDistances')]
functions += [function(ent_cut,'coniferShootCoverage')]
functions += [function(light_fs,n) for n in ('environmentShadowOffsets','environmentShadowPlaneGradient','environmentShadowTapDepth','environmentWallShadowFilter')]
functions += [function(water,n) for n in ('waterTransmission','waterDetailFrame','waterWarpFootprint')]
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
print('PASS: terrain/environment surface contracts; aircraft helpers unchanged; filtered paint/rows, reduced entity fetches, bounded shallow-water transmission')
