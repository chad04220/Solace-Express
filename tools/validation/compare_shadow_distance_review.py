#!/usr/bin/env python3
"""Compare real, fixed-unit shadow-distance captures. Does not alter originals."""
import argparse, csv, json, math
from collections import defaultdict
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
p=argparse.ArgumentParser();p.add_argument('baseline',type=Path);p.add_argument('diagnostic',type=Path);p.add_argument('output',type=Path);p.add_argument('--require-complete',action='store_true');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
def rows(path,name):
 with (path/name).open() as f:return list(csv.DictReader(f))
b={r['name']:r for r in rows(a.baseline,'views.csv')};q={r['name']:r for r in rows(a.diagnostic,'views.csv')}
bf=defaultdict(list);qf=defaultdict(list)
for r in rows(a.baseline,'frames.csv'):bf[r['view']].append(r)
for r in rows(a.diagnostic,'frames.csv'):qf[r['view']].append(r)
font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',17);small=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',14)
groups=defaultdict(list);summary=[]
for name,r in b.items():
 if name not in q or not (a.baseline/(name+'.png')).exists() or not (a.diagnostic/(name+'.png')).exists():
  if a.require_complete:raise SystemExit('Incomplete pair: '+name)
  continue
 s=q[name]
 for field in ('kind','camera_x','camera_y','camera_z','target_x','target_y','target_z','fov','actual_distance','selected_lod','crossfade_lod','hour','quality','debug_mask'):
  if r[field]!=s[field]:raise SystemExit('Pair mismatch '+name+' '+field)
 for folder in (a.baseline,a.diagnostic):
  with Image.open(folder/(name+'.png')) as im:
   if im.size!=(1920,1080):raise SystemExit('Native image invariant failed')
 counts_equal=all(bf[name][-1][f]==qf[name][-1][f] for f in ('instances','entity_triangles','entity_draws'))
 all_ready=all(int(x['pending'])==0 for x in bf[name]+qf[name])
 d=float(r['camera_distance_override']);kind=int(r['kind'])
 # A stated digital inspection crop, never a geometry/camera change. Preserve pixels with nearest-neighbour magnification.
 crop20={45:600,39:750,13:1920}.get(kind,1000)
 w=max(40,min(1920,round(crop20*20/d)));h=max(30,round(w*.38))
 cx,cy=960,540+round(20*20/d);box=(cx-w//2,cy-h//2,cx-w//2+w,cy-h//2+h)
 record={'name':name,'kind':kind,'camera_to_target_m':d,'camera_to_origin_m':float(r['actual_distance']),'selected_lod':int(r['selected_lod']),'crossfade_lod':int(r['crossfade_lod']),'same_visible_counts':counts_equal,'zero_pending':all_ready,'inspection_crop_native_pixels':box,'baseline_near_radius_override':r['near_shadow_radius_override'],'diagnostic_near_radius_override':s['near_shadow_radius_override'],'baseline_shadow_near_max':max(int(x['shadow_near_triangles']) for x in bf[name]),'diagnostic_shadow_near_max':max(int(x['shadow_near_triangles']) for x in qf[name])}
 summary.append(record);groups[kind].append((r,record))
for kind,entries in groups.items():
 entries.sort(key=lambda x:float(x[0]['camera_distance_override']))
 for page in range(0,len(entries),4):
  batch=entries[page:page+4];board=Image.new('RGB',(1280,100+len(batch)*276),(14,23,31));draw=ImageDraw.Draw(board)
  draw.text((15,12),f'Kind {kind}: matched unit-scale distance sequence; native originals retained',font=font,fill='white')
  draw.text((15,39),f"Left: production radius420 | Right: radius{batch[0][1]['diagnostic_near_radius_override']} diagnostic, unchanged2048 map and far cascade",font=small,fill=(185,215,231))
  draw.text((15,61),'Digital inspection crops may be magnified; production geometry, FOV and natural LOD are unchanged.',font=small,fill=(205,211,216))
  for index,(r,record) in enumerate(batch):
   y=92+276*index;box=record['inspection_crop_native_pixels'];w=box[2]-box[0]
   label=f"{r['camera_distance_override']}m target | LOD{r['selected_lod']} + fade{r['crossfade_lod']} | {w}px native crop, x{620/w:.2f} inspection"
   draw.text((15,y),label,font=font,fill='white')
   for col,folder in enumerate((a.baseline,a.diagnostic)):
    with Image.open(folder/(r['name']+'.png')) as im:
     crop=im.convert('RGB').crop(box);crop=crop.resize((620,236),Image.Resampling.NEAREST if w<620 else Image.Resampling.LANCZOS)
     board.paste(crop,(10+col*640,y+27))
  board.save(a.output/f'kind_{kind}_distance_{page//4+1:02}.jpg',quality=94)
manifest={'diagnostic_only':True,'native_resolution':[1920,1080],'pair_count':len(summary),'all_zero_pending':all(r['zero_pending'] for r in summary),'same_visible_counts':all(r['same_visible_counts'] for r in summary),'views':summary}
(a.output/'summary.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({k:v for k,v in manifest.items() if k!='views'}))
