#!/usr/bin/env python3
"""Record exact production, material, harness and executable SHA-256 identities.
Run once immediately before and after the coordinated build; retain both manifests.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,datetime
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);p.add_argument('--binary',type=Path);p.add_argument('--compare',type=Path);a=p.parse_args()
a.source=a.source.resolve();files=[a.source/'CMakeLists.txt']
for name in ('src','assets/materials'):
 files.extend(f for f in (a.source/name).rglob('*') if f.is_file())
files.extend((a.source/'tests'/name) for name in ('environment_asset_review_harness.cpp','environment_review_harness.cpp','aircraft_scale_review_harness.cpp','environment_review_diagnostics.h') if (a.source/'tests'/name).exists())
files.extend((a.source/'tools/validation'/name) for name in ('prepare_environment_review_adapter.py','record_review_snapshot.py'))
def sha(f):
 h=hashlib.sha256()
 with f.open('rb') as s:
  while q:=s.read(1<<20):h.update(q)
 return h.hexdigest()
manifest={str(f.relative_to(a.source)):sha(f) for f in sorted(set(files))}
report={'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source':str(a.source),'git_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=a.source,text=True).strip(),'files_sha256':manifest}
report['combined_source_sha256']=hashlib.sha256(json.dumps(manifest,sort_keys=True,separators=(',',':')).encode()).hexdigest()
if a.binary:report['binary']={'path':str(a.binary.resolve()),'sha256':sha(a.binary),'bytes':a.binary.stat().st_size}
if a.compare:
 old=json.loads(a.compare.read_text())['files_sha256'];report['build_source_changes']=[f for f in sorted(old.keys()|manifest.keys()) if old.get(f)!=manifest.get(f)]
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='files_sha256'},indent=2))
