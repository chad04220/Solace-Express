import csv,json,hashlib
import study
R=study.ROOT
rows=list(csv.DictReader((R/'probes.csv').open()))
for r in rows:
 for k in r:
  if k not in ('face','label','category'):r[k]=int(r[k]) if k in ('level','i','j') else float(r[k])
adapt={}
for a,b in ((2,4),(4,8),(8,16)):
 accepted=[r for r in rows if abs(r[f'n{a}']-r[f'n{b}'])<=.25]
 failed=[r for r in accepted if abs(r[f'n{b}']-r['n64'])>.5]
 adapt[f'N{a}_to_N{b}']=dict(acceptance_difference_m=.25,accepted_count=len(accepted),residual_threshold_m=.5,over_threshold_count=len(failed),maximum_residual_vs_N64_m=max(abs(r[f'n{b}']-r['n64']) for r in accepted))
quantized={}
for level in study.LEVELS:
 rr=[r for r in rows if r['level']==level]
 quantized[str(level)]={f'N{n}':dict(probes=len(rr),different_integer_heights_vs_N64=sum(study.elevation.round_metre(r[f'n{n}'])!=study.elevation.round_metre(r['n64']) for r in rr),maximum_integer_difference_m=max(abs(study.elevation.round_metre(r[f'n{n}'])-study.elevation.round_metre(r['n64'])) for r in rr)) for n in (2,8,16,32)}
r=dict(consecutive_rule_acceptance=adapt,quantized_differences=quantized)
(R/'diagnostics.json').write_text(json.dumps(r,indent=2)+'\n')
