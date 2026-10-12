import sys,csv,json,time,resource
import study
R=study.ROOT
rows=list(csv.DictReader((R/'deep-probes-all.csv').open()))
for r in rows:
 for k in r:
  if k not in ('face','label','category'):r[k]=int(r[k]) if k in ('level','i','j') else float(r[k])
chosen=sorted(rows,key=lambda r:abs(r['n128']-r['n256']),reverse=True)[:3]
chosen+= [max([r for r in rows if r['level']==l],key=lambda r:abs(r['n128']-r['n256'])) for l in (4,8)]
source=study.elevation.EnviGrid(sys.argv[1]);start=time.perf_counter()
for r in chosen:
 for n in (512,1024):r[f'n{n}']=study.scalar(source,r['face'],r['level'],r['i'],r['j'],n)
res=dict(probes=chosen,elapsed_seconds=time.perf_counter()-start,peak_rss_kib=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)
(R/'reference-check.json').write_text(json.dumps(res,indent=2)+'\n');print(json.dumps(res,indent=2))
