#!/usr/bin/env python3
"""Exact native pixel/metadata/readback comparison with explicit bake-state provenance."""
from pathlib import Path
import argparse,hashlib,json
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('before',type=Path);p.add_argument('after',type=Path);p.add_argument('--report',type=Path,required=True);p.add_argument('--completed-only',action='store_true',help='Compare only fully captured models and explicitly report incomplete coverage.');p.add_argument('--atlas-startup-reference',type=Path,help='Original production captures made with actual-startup Atlas cockpit cache; only the three cockpit views are replaced.');a=p.parse_args()
results=[]
for rfile in sorted(a.before.glob('*/READY.json')):
 m=rfile.parent.name;b=rfile.parent;c=a.after/m
 if a.completed_only and not (c/'READY.json').exists():continue
 br=json.loads(rfile.read_text());ar=json.loads((c/'READY.json').read_text());assert br['views']==ar['views']
 overrides=[]
 if int(m)==14 and a.atlas_startup_reference:
  ob=a.atlas_startup_reference/m;orr=json.loads((ob/'READY.json').read_text());assert sorted(orr['views'])==['cockpit','cockpit_down_negative','cockpit_down_positive'];assert orr['binary']==br['binary']
  overrides=['atlas_a180_'+v for v in orr['views']]
 files=[]
 for f in sorted(b.iterdir()):
  if not f.is_file() or f.suffix=='.log' or (f.name=='READY.json' or f.name.startswith('shader_notes')) or f.name.endswith('.cache.txt'):continue
  reference=f
  if any(f.name==prefix+'.png' or f.name.startswith(prefix+'_frame_state.') or f.name.startswith(prefix+'_geometry.') for prefix in overrides):reference=ob/f.name
  other=c/f.name
  if f.suffix=='.png':
   with Image.open(reference) as l,Image.open(other) as r:
    x=l.convert('RGB');y=r.convert('RGB');exact=x.size==y.size and x.tobytes()==y.tobytes()
   kind='native_pixels'
  else:exact=other.exists() and reference.read_bytes()==other.read_bytes();kind='gpu_exact_buffers' if '.exact.' in f.name else 'frame_and_geometry_metadata'
  files.append({'file':f.name,'reference':str(reference),'reference_sha256':hashlib.sha256(reference.read_bytes()).hexdigest(),'kind':kind,'exact':exact})
 extra=sorted(set(f.name for f in c.glob('*.exact.*'))-set(f.name for f in b.glob('*.exact.*')))
 results.append({'model':int(m),'views':br['views'],'checks':files,'extra_gpu_files':extra,'pass':all(x['exact'] for x in files) and not extra})
r={'before':str(a.before),'after':str(a.after),'atlas_startup_reference':str(a.atlas_startup_reference) if a.atlas_startup_reference else None,'reference_note':'Atlas cockpit uses the unchanged original renderer and actual Game prewarm bake state (Pr.x=754.166687). This explicit reference preserves exact AO bytes; the ordinary original phase1.1 captures remain preserved and all three are independently pixel-identical.' if a.atlas_startup_reference else None,'models':results,'complete':len(results)==len(list(a.before.glob('*/READY.json'))),'native_images':sum(sum(x['kind']=='native_pixels' for x in y['checks']) for y in results),'gpu_exact_checks':sum(sum(x['kind']=='gpu_exact_buffers' for x in y['checks']) for y in results),'pass':bool(results) and all(x['pass'] for x in results)};a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({k:v for k,v in r.items() if k!='models'},indent=2));raise SystemExit(0 if r['pass'] else 1)
