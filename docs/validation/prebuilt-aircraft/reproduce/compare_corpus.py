#!/usr/bin/env python3
from pathlib import Path
import argparse,csv,json
from compare_mesh_payloads import read
D=Path(__file__).resolve().parent;p=argparse.ArgumentParser();p.add_argument('package',type=Path);p.add_argument('--atlas-startup',action='store_true');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
refs={(int(x['model']),int(x['slot'])):D/'baseline-cache'/x['cache_name'] for x in csv.DictReader((D/'baseline-identities/identities.csv').open())}
if a.atlas_startup:refs[14,1]=D/'prewarm-cache'/refs[14,1].name
results=[];seen=set()
for f in sorted(a.package.glob('*.mesh')):
 new,np=read(f);key=new['model'],new['slot'];assert key in refs and key not in seen;seen.add(key);old,op=read(refs[key]);exact=op==np and old['fine_start']==new['fine_start']
 entry={'model':key[0],'slot':key[1],'baseline':str(refs[key]),'asset':str(f),'baseline_sha256':old['file_sha256'],'asset_sha256':new['file_sha256'],'identity':new['identity'],'source_digest':new['source_digest'],'payload_bytes':len(np),'payload_sha256':new['payload_sha256'],'exact_all_payload_bytes':op==np,'exact_fine_start':old['fine_start']==new['fine_start'],'exact_static':old['static']==new['static'],'exact_parts':old['parts']==new['parts'],'exact_hull_and_eye':old['sections']['moving_hull_and_eye']==new['sections']['moving_hull_and_eye'],'pass':exact}
 results.append(entry)
r={'package':str(a.package),'baseline_commit':'72d0af0','atlas_cockpit_reference':'actual Game::prewarm phase754.166687' if a.atlas_startup else 'diagnostic phase1.1','expected_body_count':30,'actual_body_count':len(seen),'bodies':results,'total_payload_bytes':sum(r['payload_bytes'] for r in results),'pass':len(seen)==30 and all(x['pass'] for x in results)};a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({k:v for k,v in r.items() if k!='bodies'},indent=2));raise SystemExit(0 if r['pass'] else 1)
