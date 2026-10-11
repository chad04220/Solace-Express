#!/usr/bin/env python3
"""Isolated runtime route probes. Does not mutate the source package or baseline cache."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,struct,subprocess,csv
D=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--adapter',required=True);p.add_argument('--package',type=Path,required=True);a=p.parse_args();root=D/'fallbacks';root.mkdir(exist_ok=True)
plan=D/'fallback-plan.json';plan.write_text('[{"model":0,"views":["front"]}]\n')
asset=next(f for f in a.package.glob('*.mesh') if struct.unpack_from('<2I',f.read_bytes(),24)==(0,0))
row=next(x for x in csv.DictReader((D/'baseline-identities/identities.csv').open()) if x['model']=='0' and x['slot']=='0');legacy=D/'baseline-cache'/row['cache_name']
hashes=lambda folder:{f.name:{'sha256':hashlib.sha256(f.read_bytes()).hexdigest() if f.is_file() else '<directory>','mode':f.stat().st_mode & 0o777} for f in sorted(folder.iterdir())}
source_hash=hashlib.sha256(asset.read_bytes()).hexdigest();baseline_hash=hashlib.sha256(legacy.read_bytes()).hexdigest();results=[]
for mode in ['packaged_over_legacy','missing_to_legacy','stale_to_legacy','corrupt_to_legacy','missing_to_bake']:
 case=root/mode;assert not case.exists(),f'Refuse to overwrite {case}';package=case/'package';cache=case/'cache';mesa=case/'mesa';package.mkdir(parents=True);cache.mkdir();mesa.mkdir()
 # Driver program binaries and world caches are warm; cold geometry is isolated in missing_to_bake.
 for f in (D/'baseline-cache').glob('*.bin'):
  if not f.name.startswith('mesh_'):shutil.copy2(f,cache/f.name)
 if mode!='missing_to_bake':shutil.copy2(legacy,cache/legacy.name)
 if mode.startswith('packaged') or mode.startswith('stale') or mode.startswith('corrupt'):
  b=bytearray(asset.read_bytes())
  if mode.startswith('stale'):
   b[72]^=1;b[104:136]=bytes(32);b[104:136]=hashlib.sha256(b).digest()
  if mode.startswith('corrupt'):b[-1]^=1
  (package/asset.name).write_bytes(b)
 for f in package.iterdir():f.chmod(0o444)
 package.chmod(0o555)
 before=hashes(package);(case/'package_before.json').write_text(json.dumps(before,indent=2))
 output='fallbacks/'+mode+'/captures'
 subprocess.run(['python3',str(D/'run_fixture_review.py'),'--adapter',a.adapter,'--out',output,'--cache',str(cache),'--mesa-cache',str(mesa),'--package',str(package),'--plan',str(plan),'--frames','4'],check=True)
 after=hashes(package);assert before==after,'Runtime mutated package directory';assert hashlib.sha256(asset.read_bytes()).hexdigest()==source_hash;assert hashlib.sha256(legacy.read_bytes()).hexdigest()==baseline_hash
 target=case/'captures/00';reference=D/'baseline-cold/00';checks=[]
 from PIL import Image
 for f in sorted(target.iterdir()):
  if not f.name.startswith('kestrel_front') or f.name.endswith('.cache.txt'):continue
  other=reference/f.name
  if f.suffix=='.png':
   with Image.open(f) as im,Image.open(other) as prior:exact=im.size==prior.size and im.convert('RGB').tobytes()==prior.convert('RGB').tobytes()
  else:exact=other.exists() and f.read_bytes()==other.read_bytes()
  checks.append({'file':f.name,'exact':exact})
 log=(target/'render.log').read_text();import re
 hits,misses=map(int,re.search(r'REVIEW_PREBUILT hits=(\d+) misses=(\d+)',log).groups());built=int(re.search(r'meshes_built=(\d+)',log).group(1))
 expected=(1,0,0) if mode=='packaged_over_legacy' else (0,1,1 if mode=='missing_to_bake' else 0)
 result={'case':mode,'package_unchanged':before==after,'package_directory_mode':oct(package.stat().st_mode & 0o777),'hits':hits,'misses':misses,'built':built,'expected_route':list(expected),'checks':checks,'pass':bool(checks) and all(x['exact'] for x in checks) and (hits,misses,built)==expected}
 results.append(result);(case/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(mode,result['pass'],flush=True);assert result['pass']
(root/'summary.json').write_text(json.dumps({'results':results,'pass':all(r['pass'] for r in results)},indent=2)+'\n')
