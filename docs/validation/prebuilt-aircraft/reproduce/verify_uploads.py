#!/usr/bin/env python3
from pathlib import Path
import argparse,json,hashlib,struct
from compare_mesh_payloads import read
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('--portable-dir',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args();results=[]
portable={}
if a.portable_dir:
 for f in a.portable_dir.glob('*.mesh'):
  b=f.read_bytes()[:192];m,slot=struct.unpack_from('<2I',b,24);portable[m,slot]=f
for marker in sorted(a.run.glob('*/*.cache.txt')):
 base=Path(str(marker)[:-10]);stem=base.name;model=int(base.parent.name);slot=int('_cockpit' in stem)
 source=portable[model,slot] if a.portable_dir else Path(marker.read_text().strip());meta,payload=read(source)
 checks=[];at=0
 def check(suffix,wanted):
  f=Path(str(base)+'.exact.'+suffix);checks.append({'file':f.name,'bytes':len(wanted),'exact':f.exists() and f.read_bytes()==wanted})
 nv=meta['sections']['vertices']['bytes'];ni=meta['sections']['indices']['bytes'];nh=meta['sections']['moving_hull_and_eye']['bytes']
 check('static.vertices',payload[:nv]);at=nv;check('static.indices',payload[at:at+ni]);at+=ni
 hull=payload[at:at+nh];check('moving_hull.vertices',hull[:-4]);at+=nh;parts=payload[at:];i=0;n=0
 while i<len(parts):
  typ,nf,nix=struct.unpack_from('<3I',parts,i);i+=12;check(f'part{typ}.vertices',parts[i:i+4*nf]);i+=4*nf;check(f'part{typ}.indices',parts[i:i+4*nix]);i+=4*nix;n+=1
 eye=int(struct.unpack('<f',hull[-4:])[0]>.5);check('meta',struct.pack('<4I',ni//4,eye,n,(nh//4-1)//3))
 results.append({'fixture':stem,'source':str(source),'source_sha256':meta['file_sha256'],'checks':checks,'pass':all(x['exact'] for x in checks)})
r={'run':str(a.run),'portable':str(a.portable_dir) if a.portable_dir else None,'fixtures':results,'checks':sum(len(x['checks']) for x in results),'pass':bool(results) and all(x['pass'] for x in results)};a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'fixtures':len(results),'checks':r['checks'],'pass':r['pass']}));raise SystemExit(0 if r['pass'] else 1)
