#!/usr/bin/env python3
from pathlib import Path
import argparse,csv,json,struct,hashlib
p=argparse.ArgumentParser();p.add_argument('identities',type=Path);p.add_argument('package',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
checks=[]
for r in csv.DictReader(a.identities.open()):
 f=a.package/r['filename'];assert f.exists(),str(f);b=f.read_bytes();model,slot=struct.unpack_from('<2I',b,24)
 checks.append({'model':model,'slot':slot,'filename':f.name,'identity':b[72:104].hex(),'source_digest':b[136:168].hex(),'sha256':hashlib.sha256(b).hexdigest(),'pass':model==int(r['model']) and slot==int(r['slot']) and b[72:104].hex()==r['identity'] and b[136:168].hex()==r['source_digest']})
out={'identities':str(a.identities),'package':str(a.package),'checks':checks,'pass':len(checks)==30 and all(r['pass'] for r in checks)};a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(out,indent=2)+'\n');print('Runtime identity checks',len(checks),'pass',out['pass']);raise SystemExit(0 if out['pass'] else 1)
