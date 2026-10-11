#!/usr/bin/env python3
"""The scenery's kinds as the shaders number them (a source contract). entities.h's EntKind is the order of the instance
lists and of the meshes; the entity programs know a kind by its number - ent_fs1.glsl's K_ constants, and a literal
here and there in ent_vs.glsl (the windsock, the guardrail, the pole) - and the bridges' material comes after every
kind (K_BRIDGE, kBridgeKind). Adding a kind anywhere but at the end renumbers the rest: each of these must follow, or
a kind is drawn with another's material (v3.48's city towers moved twelve). The pole's wires' part number is shared
the same way (entity_mesh.h P_WIRE)."""
import pathlib, re, sys

root = pathlib.Path(__file__).resolve().parents[1]
read = lambda p: (root / p).read_text(encoding='utf-8')
failures = 0
def fail(msg):
    global failures
    print('FAIL: ' + msg); failures += 1

# EntKind, in order
h = read('src/entities.h')
body = h[h.index('enum EntKind {'):h.index('EK_COUNT')]
body = re.sub(r'//[^\n]*', '', body)
kinds = re.findall(r'\b(EK_\w+)', body)
index = {k: i for i, k in enumerate(kinds)}
index['EK_COUNT'] = len(kinds)
if len(kinds) < 50:
    fail('EntKind not read (%d kinds)' % len(kinds))

# ent_fs1.glsl's K_ constants
fs1 = read('src/shaders/ent_fs1.glsl')
consts = dict((n, int(v)) for n, v in re.findall(r'\b(K_\w+)\s*=\s*(\d+)', fs1))
alias = {'K_HIP': 'EK_HOUSE_HIP', 'K_LHOUSE': 'EK_HOUSE_L', 'K_FARM': 'EK_FARMHOUSE', 'K_APART': 'EK_APARTMENT',
         'K_SKY': 'EK_SKYSCRAPER', 'K_ROUND': 'EK_ROUNDTOWER', 'K_GAS': 'EK_GASSTATION', 'K_ARCH': 'EK_ARCH_HANGAR',
         'K_THANGAR': 'EK_T_HANGAR', 'K_CTRL': 'EK_CTRL_TOWER', 'K_FUELTANK': 'EK_FUEL_TANK', 'K_PUMP': 'EK_FUEL_PUMP',
         'K_GAPLANE': 'EK_GA_PLANE', 'K_LOC': 'EK_LOCALIZER', 'K_FLOOD': 'EK_FLOODMAST', 'K_LAMPPOST': 'EK_STREETLAMP',
         'K_RAIL': 'EK_GUARDRAIL', 'K_BRIDGE': 'EK_COUNT'}
for name, value in consts.items():
    ek = alias.get(name, 'E' + name)
    if ek not in index:
        fail('ent_fs1.glsl %s names no kind (%s)' % (name, ek)); continue
    if index[ek] != value:
        fail('ent_fs1.glsl %s = %d, but %s is %d' % (name, value, ek, index[ek]))
if len(consts) < 50:
    fail('ent_fs1.glsl K_ constants not read (%d)' % len(consts))
m = re.search(r'kBridgeKind\s*==\s*(\d+)', h)
if not m or int(m.group(1)) != len(kinds):
    fail('entities.h: kBridgeKind\'s static_assert is not EK_COUNT (%d)' % len(kinds))

# the vertex shader's literals, each named in its comment: "uKind == 44 ... // (EK_WINDSOCK"
vs = read('src/shaders/ent_vs.glsl')
literals = re.findall(r'uKind\s*==\s*(\d+)[^\n]*//\s*\((EK_\w+)', vs)
for value, ek in literals:
    if index.get(ek) != int(value):
        fail('ent_vs.glsl: uKind == %s is said to be %s, which is %s' % (value, ek, index.get(ek)))
for need in ('EK_WINDSOCK', 'EK_GUARDRAIL', 'EK_POLE'):
    if need not in [ek for _, ek in literals]:
        fail('ent_vs.glsl: no literal for %s found' % need)

# the wires' part
mesh = read('src/entity_mesh.h')
pw = re.search(r'P_WIRE\s*=\s*(\d+)', mesh)
fw = re.search(r'P_WIRE\s*=\s*(\d+)', fs1)
if not pw or not fw or pw.group(1) != fw.group(1) or ('aAux.x - %s.0' % pw.group(1)) not in vs:
    fail('P_WIRE differs between entity_mesh.h, ent_fs1.glsl and ent_vs.glsl')

print('entity_kind_numbers: %d kinds, %d shader constants, %d literals; %d failures' % (len(kinds), len(consts), len(literals), failures))
sys.exit(1 if failures else 0)
