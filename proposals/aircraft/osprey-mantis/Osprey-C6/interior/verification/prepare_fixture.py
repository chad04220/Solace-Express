#!/usr/bin/env python3
"""Run ONLY on an isolated already-Osprey-equipped test copy, never baseline."""
from pathlib import Path
import argparse, shutil
p=argparse.ArgumentParser();p.add_argument('test_copy');p.add_argument('--isolated-copy',action='store_true',required=True);a=p.parse_args()
w=Path(a.test_copy).resolve();r=Path(__file__).resolve().parents[1]
t=w/'src/shaders/raytrace_fs.glsl';s=t.read_text()
assert 'mapOspreyCabinTrim' not in s,'Already installed; begin from a clean isolated copy.'
assert 'osprey_c6' in (w/'src/aircraft.cpp').read_text(),'Prepare base Osprey rows first.'
assert s.count('vec2 mapPlaneBody(vec3 p);')==1
s=s.replace('vec2 mapPlaneBody(vec3 p);',(r/'mapOspreyCabinTrim.glsl').read_text()+'\nvec2 mapPlaneBody(vec3 p);')
assert s.count('    float compass =')==1
s=s.replace('    float compass =','    res = opU(res, mapOspreyCabinTrim(p)); // OSPREY-ONLY TEST FIXTURE\n    float compass =')
t.write_text(s);shutil.copyfile(r/'verification/aircraft_visual_test.cpp',w/'tests/aircraft_visual_test.cpp')
print('Isolated fixture installed. Render ONLY Osprey index7. Never transplant this unconditional fixture hook into production.')
