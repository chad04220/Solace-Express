#!/usr/bin/env python3
from pathlib import Path
import argparse,hashlib,json,os,re,statistics,subprocess,time
D=Path(__file__).resolve().parent;p=argparse.ArgumentParser();p.add_argument('--package',type=Path);p.add_argument('--repeats',type=int,default=5);p.add_argument('--adapter',default='candidate');p.add_argument('--mode',choices=['all','driver_local','packaged'],default='all');p.add_argument('--out',default='body-benchmarks');a=p.parse_args();O=D/a.out;O.mkdir(exist_ok=True)
assert len(list((D/'baseline-cache').glob('mesh_*.bin')))==30
if a.mode!='driver_local':assert a.package and len(list(a.package.glob('*.mesh')))==30
summary=O/'summary.json';modes=json.loads(summary.read_text())['modes'] if summary.exists() else []
for mode,adapter,cache,package in [('driver_local','baseline',D/'baseline-cache',''),('packaged',a.adapter,O/'empty-local-cache',str(a.package.resolve()) if a.package else '')]:
 if a.mode!='all' and a.mode!=mode:continue
 assert mode not in {m['mode'] for m in modes},'Refuse duplicate mode'
 cache.mkdir(parents=True,exist_ok=True);binary=D/'adapters'/adapter/'body-benchmark/body_benchmark';trials=[]
 for repeat in range(a.repeats):
  log=O/f'{mode}-{repeat}.log';assert not log.exists(),f'Refuse to overwrite {log}'
  env=os.environ.copy();env.update(LIBGL_ALWAYS_SOFTWARE='1',EGL_PLATFORM='surfaceless',LP_NUM_THREADS='1',HULLDBG='1',MESA_SHADER_CACHE_DIR=str(O/'mesa'))
  start=time.monotonic()
  with log.open('w') as f:subprocess.run([str(binary),str(cache.resolve()),package],env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
  elapsed=time.monotonic()-start;t=log.read_text()
  bodies=[{'model':int(m),'slot':int(slot),'seconds':float(s),'shader_hits':int(h),'shader_misses':int(miss),'built':int(b)} for m,slot,s,h,miss,b in re.findall(r'REVIEW_BODY_END model=(\d+) slot=(\d+) seconds=([\d.]+) shader_hits=(\d+) shader_misses=(\d+) built=(\d+)',t)]
  assert len(bodies)==30 and all(x['built']==0 and x['shader_hits']==0 and x['shader_misses']==0 for x in bodies)
  assert len(re.findall(r'BENCH_GL model=\d+ slot=\d+ error=0\n',t))==30
  if mode=='packaged':assert 'REVIEW_PREBUILT hits=30 misses=0' in t
  trials.append({'repeat':repeat,'wall_seconds':elapsed,'body_seconds':sum(x['seconds'] for x in bodies),'bodies':bodies,'log':str(log),'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
  print(mode,repeat,'total body seconds',trials[-1]['body_seconds'],flush=True)
 per_body=[]
 for model in range(15):
  for slot in range(2):
   times=[next(x['seconds'] for x in trial['bodies'] if x['model']==model and x['slot']==slot) for trial in trials];per_body.append({'model':model,'slot':slot,'first_process_seconds':times[0],'median_seconds':statistics.median(times),'min_seconds':min(times),'max_seconds':max(times)})
 modes.append({'mode':mode,'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'first_process_body_seconds':trials[0]['body_seconds'],'median_total_body_seconds':statistics.median(x['body_seconds'] for x in trials),'median_process_wall_seconds':statistics.median(x['wall_seconds'] for x in trials),'per_body':per_body,'trials':trials})
 (O/'summary.json').write_text(json.dumps({'modes':modes,'pass':len(modes)==2,'scope':'Each trial is a new EGL process and new GPU allocations; operating-system file cache is warm and was not flushed. Production load/checksum/upload path only, no scene/world/display programs or SDF geometry work. No RTX 3070/hardware startup guarantee.'},indent=2)+'\n')
