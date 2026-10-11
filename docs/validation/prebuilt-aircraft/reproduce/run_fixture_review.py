#!/usr/bin/env python3
from pathlib import Path
import argparse,csv,hashlib,json,os,shutil,subprocess,time
from PIL import Image
D=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--adapter',required=True);p.add_argument('--out',required=True);p.add_argument('--cache',type=Path,required=True);p.add_argument('--mesa-cache',type=Path,required=True);p.add_argument('--package',type=Path);p.add_argument('--plan',type=Path,default=D/'fixtures.json');p.add_argument('--frames',type=int,default=4);p.add_argument('--threads',type=int,default=1);a=p.parse_args()
R=D/'adapters'/a.adapter;O=D/a.out;O.mkdir(parents=True,exist_ok=True);P=json.loads((R/'build_provenance.json').read_text());S=Path(P['source']);binary=R/'aircraft_review';assert hashlib.sha256(binary.read_bytes()).hexdigest()==P['binary_sha256'];assert P['source_stable_during_build']
for f,h in P['sources'].items():assert hashlib.sha256((S/f).read_bytes()).hexdigest()==h,f
shutil.copy2(R/'build_provenance.json',O/'build_provenance.json')
env=os.environ.copy();env.update(LIBGL_ALWAYS_SOFTWARE='1',EGL_PLATFORM='surfaceless',LP_NUM_THREADS=str(a.threads),HULLDBG='1',MESA_SHADER_CACHE_DIR=str(a.mesa_cache.resolve()),SHADERCACHE=str(a.cache.resolve()))
if a.package:env['REVIEW_PACKAGE_DIR']=str(a.package.resolve())
a.cache.mkdir(parents=True,exist_ok=True);a.mesa_cache.mkdir(parents=True,exist_ok=True)
progress={'binary':P['binary_sha256'],'started':time.time(),'completed':[]};dedup={}
for old in O.rglob('*.exact.*'):
 if old.is_file():dedup.setdefault(hashlib.sha256(old.read_bytes()).hexdigest(),old)
for entry in json.loads(a.plan.read_text()):
 model=entry['model'];views=entry['views'];folder=O/f'{model:02}';folder.mkdir(exist_ok=True);ready=folder/'READY.json'
 if ready.exists():
  r=json.loads(ready.read_text());assert r['binary']==P['binary_sha256'] and r['views']==views
  for f,h in r['files'].items():assert hashlib.sha256((folder/f).read_bytes()).hexdigest()==h
  progress['completed'].append(r);continue
 progress.update(active_model=model,active_views=views);(O/'progress.json').write_text(json.dumps(progress,indent=2))
 start=time.time()
 rows=[];all_logs=[]
 for phase in ['exterior','cockpit']:
  phaseviews=[v for v in views if v.startswith('cockpit')==(phase=='cockpit')]
  if not phaseviews:continue
  progress.update(active_phase=phase);(O/'progress.json').write_text(json.dumps(progress,indent=2))
  with (folder/f'render_{phase}.log').open('w') as log:code=subprocess.call([str(binary),str(folder),str(model),','.join(phaseviews),str(a.frames),'1920','1080','world'],cwd=S,env=env,stdout=log,stderr=subprocess.STDOUT)
  if code:progress.update(status='failed',exit_code=code);(O/'progress.json').write_text(json.dumps(progress,indent=2));raise SystemExit(code)
  rows += list(csv.DictReader((folder/'views.csv').open()))
  shutil.copy2(folder/'views.csv',folder/f'views_{phase}.csv')
  shutil.copy2(folder/'shader_notes.txt',folder/f'shader_notes_{phase}.txt')
  all_logs.append((folder/f'render_{phase}.log').read_text())
 with (folder/'views.csv').open('w',newline='') as f:
  w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
 logs='\n'.join(all_logs);(folder/'render.log').write_text(logs);assert 'GL0' in logs and not __import__('re').search(r'GL[1-9a-fA-F][0-9a-fA-F]*',logs)
 images=[]
 for f in sorted(folder.glob('*.png')):
  with Image.open(f) as im:assert im.size==(1920,1080);images.append({'file':f.name,'pixel_sha256':hashlib.sha256(im.convert('RGB').tobytes()).hexdigest()})
 assert len(images)==len(views)
 # Exact duplicate readbacks become hard links after capture to keep the evidence compact; every byte is retained.
 for f in sorted(folder.glob('*.exact.*')):
  h=hashlib.sha256(f.read_bytes()).hexdigest()
  if h in dedup and dedup[h]!=f:f.unlink();os.link(dedup[h],f)
  else:dedup[h]=f
 r={'model':model,'views':views,'frames':a.frames,'binary':P['binary_sha256'],'seconds':time.time()-start,'images':images,'files':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(folder.iterdir()) if f.is_file()}}
 ready.write_text(json.dumps(r,indent=2)+'\n');progress['completed'].append(r);(O/'progress.json').write_text(json.dumps(progress,indent=2));print('READY',model,len(views),round(r['seconds'],2),flush=True)
progress.update(status='complete',finished=time.time());(O/'progress.json').write_text(json.dumps(progress,indent=2)+'\n');print('COMPLETE',len(progress['completed']),flush=True)
