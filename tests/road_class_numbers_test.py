#!/usr/bin/env python3
"""The road classes as the terrain shader knows them (a source contract). road_network.h's roadSpec gives each class
its paved and platform half widths and roadBankMax its banks' reach; roads.glsl repeats them as a chain of literals by
the class's number (roadHalfPaved, roadHalfPlatform, roadBankMax) to grade and paint the ground, and names where the
racetrack's start line is painted (circuitStartLine, CIRCUIT_START_LINE). A class added to one and not the other is
graded or painted at another class's width."""
import pathlib, re, sys

root = pathlib.Path(__file__).resolve().parents[1]
read = lambda p: (root / p).read_text(encoding='utf-8')
failures = 0
def fail(msg):
    global failures
    print('FAIL: ' + msg); failures += 1

h = read('src/road_network.h')
enum = re.search(r'enum RoadClass[^{]*\{([^}]*)\}', h).group(1)
classes = re.findall(r'\b(RC_\w+)\s*=\s*(\d+)', enum)
n = len(classes)
table = h[h.index('static const RoadSpec k[RC_COUNT]'):]
table = table[:table.index('};')]
rows = re.findall(r'\{\s*([\d.]+)f,\s*([\d.]+)f,', table)
if len(rows) != n:
    fail('roadSpec has %d rows for %d classes' % (len(rows), n))
bank = re.search(r'inline float roadBankMax\(int cls\) \{ return ([^;]*);', h).group(1)
def cpp_bank(c):
    name = [k for k, v in classes if int(v) == c][0]
    expr = re.sub(r'\bRC_\w+', lambda m: str(dict(classes)[m.group(0)]), bank).replace('cls', str(c)).replace('ROAD_BANK_MAX', '50.0')
    expr = expr.replace('||', ' or ').replace('&&', ' and ').replace('.f', '.0').replace('f ', ' ')
    expr = re.sub(r'([\d.]+)f\b', r'\1', expr)
    # (a ? b : c chains, read right to left)
    while '?' in expr:
        expr = re.sub(r'([^?:]+)\?\s*([^?:]+):\s*([^?:]+)$', lambda m: '(%s if %s else %s)' % (m.group(2), m.group(1), m.group(3)), expr)
    return float(eval(expr))

glsl = read('src/shaders/roads.glsl')
def chain(fn):
    body = re.search(r'float ' + fn + r'\(int c\)\{ return ([^;]*);', glsl).group(1)
    return body
def shader_value(fn, c):
    expr = chain(fn).replace('ROAD_BANK_MAX', '50.0').replace('c ==', 'c==')
    expr = re.sub(r'\bc\b', str(c), expr).replace('||', ' or ')
    while '?' in expr:
        expr = re.sub(r'([^?:]+)\?\s*([^?:]+):\s*([^?:]+)$', lambda m: '(%s if %s else %s)' % (m.group(2), m.group(1), m.group(3)), expr)
    return float(eval(expr))

for name, v in classes:
    c = int(v)
    if c >= len(rows): continue
    paved, platform = float(rows[c][0]), float(rows[c][1])
    for fn, want in (('roadHalfPaved', paved), ('roadHalfPlatform', platform), ('roadBankMax', cpp_bank(c))):
        got = shader_value(fn, c)
        if abs(got - want) > 1e-4:
            fail('roads.glsl %s(%d) = %g, but %s is %g' % (fn, c, got, name, want))

m = re.search(r'CIRCUIT_START_LINE\s*=\s*([\d.]+)f', h)
g = re.search(r'float circuitStartLine\(\)\{ return ([\d.]+); \}', glsl)
if not m or not g or float(m.group(1)) != float(g.group(1)):
    fail('the circuit\'s start line differs between road_network.h and roads.glsl')

print('road_class_numbers: %d classes; %d failures' % (n, failures))
sys.exit(1 if failures else 0)
