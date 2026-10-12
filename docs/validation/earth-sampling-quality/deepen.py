import csv,json,sys,time,resource
from pathlib import Path
import study
R=study.ROOT
rows=list(csv.DictReader((R/'probes.csv').open()))
for r in rows:
 for k in r:
  if k in ('face','label','category'):continue
  r[k]=int(r[k]) if k in ('level','i','j') else float(r[k])
old=list(csv.DictReader((R/'deep-probes.csv').open()))
keys=lambda r:(r['face'],int(r['level']),int(r['i']),int(r['j']))
seen={keys(r) for r in old}
chosen=[]
for level in study.LEVELS:
 rr=[r for r in rows if r['level']==level]
 for a,b in ((2,64),(8,64),(16,64),(32,64)):
  for r in sorted(rr,key=lambda x:abs(x[f'n{a}']-x[f'n{b}']),reverse=True)[:5]:
   if keys(r) not in seen: chosen.append(r);seen.add(keys(r))
source=study.elevation.EnviGrid(sys.argv[1]);start=time.perf_counter()
for r in chosen:
 for n in (128,256):r[f'n{n}']=study.scalar(source,*keys(r),n)
allrows=[]
for r in old:
 r={k:(v if k in ('face','label','category') else int(v) if k in ('level','i','j') else float(v)) for k,v in r.items()};allrows.append(r)
allrows+=chosen
with (R/'deep-probes-all.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=list(allrows[0]),lineterminator='\n');w.writeheader();w.writerows(allrows)
comparisons={str(l):{f'N{a}_to_N{b}':study.stats([abs(r[f'n{a}']-r[f'n{b}']) for r in allrows if r['level']==l]) for a,b in ((2,256),(8,256),(16,256),(32,256),(64,256),(128,256))} for l in study.LEVELS}
res=dict(additional_probes=len(chosen),total_deep_probes=len(allrows),total_elapsed_seconds=time.perf_counter()-start,peak_rss_kib=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,by_level=comparisons)
(R/'deep-results.json').write_text(json.dumps(res,indent=2)+'\n');print(json.dumps(res,indent=2))
