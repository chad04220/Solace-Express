#!/usr/bin/env python3
"""Run real renderer methods against CPU mock GL; no driver/context required."""
import argparse, json, os, shlex, subprocess, sys, tempfile, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE.parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, help='Keep generated includes, logs and executables here (default: temporary directory).')
p.add_argument('--no-sanitize', action='store_true', help='Build without ASan/UBSan when unavailable.')
args = p.parse_args()
env = dict(os.environ)
env.setdefault('ASAN_OPTIONS', 'detect_leaks=0:halt_on_error=1')
env.setdefault('UBSAN_OPTIONS', 'halt_on_error=1')

def run(out):
    out.mkdir(parents=True, exist_ok=True)
    common = shlex.split(env.get('CXX','g++')) + ['-std=c++17','-O1','-g','-I'+str(SRC/'src'),'-I'+str(out)]
    if not args.no_sanitize:
        common += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    steps=[('extract',[sys.executable,str(HERE/'extract.py'),str(out)])]
    for name in ['bake_dispatch','shader_lifetime']:
        steps += [('build_'+name,common+[str(HERE/(name+'_test.cpp')),str(SRC/'src/gl.cpp'),'-o',str(out/name)]),
                  ('run_'+name,[str(out/name)])]
    results=[]
    for name,command in steps:
        start=time.monotonic()
        with (out/(name+'.log')).open('w') as log:
            result=subprocess.run(command,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT)
        print((out/(name+'.log')).read_text(),end='')
        results.append({'step':name,'command':command,'exit':result.returncode,'seconds':round(time.monotonic()-start,3)})
        if result.returncode: break
    passed=len(results)==len(steps) and all(x['exit']==0 for x in results)
    (out/'result.json').write_text(json.dumps({'passed':passed,'sanitized':not args.no_sanitize,'steps':results},indent=2)+'\n')
    return 0 if passed else 1

if args.output:
    raise SystemExit(run(args.output.resolve()))
with tempfile.TemporaryDirectory(prefix='solace-renderer-validation-') as folder:
    raise SystemExit(run(Path(folder)))
