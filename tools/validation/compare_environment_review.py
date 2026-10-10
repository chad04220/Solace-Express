#!/usr/bin/env python3
"""Make labeled boards and quantified geometry comparisons from actual captures.
No image generation/retouching: boards contain downscaled screenshots only.
"""
from pathlib import Path
from collections import defaultdict
import argparse,csv,json,statistics,struct
from PIL import Image,ImageDraw,ImageFont
p=argparse.ArgumentParser();p.add_argument('baseline',type=Path);p.add_argument('proposed',type=Path);p.add_argument('output',type=Path);p.add_argument('--require-complete',action='store_true');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
def rows(path):
    with path.open() as f:return list(csv.DictReader(f))
def mesh(path):return {(r['name'],int(r['lod'])):r for r in rows(path/'mesh_inventory.csv')}
before,after=mesh(a.baseline),mesh(a.proposed)
base_views={r['name']:r for r in rows(a.baseline/'views.csv')}
prop_views={r['name']:r for r in rows(a.proposed/'views.csv')}
base_images={p.stem for p in a.baseline.glob('*.png')}
prop_images={p.stem for p in a.proposed.glob('*.png')}
matched=sorted(base_images & prop_images)
if a.require_complete and base_images!=prop_images:
    raise SystemExit('Incomplete image set: '+str(sorted(base_images ^ prop_images)))
if a.require_complete:
    for label,images,views in [('baseline',base_images,base_views),('proposed',prop_images,prop_views)]:
        if images!=set(views):raise SystemExit(label+' camera manifest/image set mismatch: '+str(sorted(images^set(views))))
for name in matched:
    if name not in base_views or name not in prop_views:raise SystemExit('Missing camera record: '+name)
    for k in base_views[name]:
        if k!='name' and struct.pack('!f',float(base_views[name][k]))!=struct.pack('!f',float(prop_views[name][k])):raise SystemExit('Camera/weather mismatch: '+name+' '+k)
for root in [a.baseline,a.proposed]:
    for r in rows(root/'frames.csv'):
        if r['scene'] not in matched:continue
        if int(r['pending'])!=0:raise SystemExit('Incomplete scene generation: '+r['scene'])
        if (int(r['width']),int(r['height']),float(r['render_scale']),int(r['quality']))!=(1920,1080,1.0,1):raise SystemExit('Native1080 Medium invariant failed: '+r['scene'])
changes=[]
for k in sorted(set(before)|set(after)):
    x,y=before.get(k,{}),after.get(k,{})
    old,new=int(x.get('triangles',0)),int(y.get('triangles',0))
    if old!=new:changes.append({'name':k[0],'lod':k[1],'baseline_triangles':old,'proposed_triangles':new,'change_percent':round((new/old-1)*100,2) if old else None})
with (a.output/'mesh_changes.csv').open('w') as f:
    w=csv.DictWriter(f,fieldnames=['name','lod','baseline_triangles','proposed_triangles','change_percent']);w.writeheader();w.writerows(changes)
def frames(path):
    result=defaultdict(list)
    for r in rows(path/'frames.csv'):
        if int(r['frame'])>=3:result[r['scene']].append(r)
    return result
bf,af=frames(a.baseline),frames(a.proposed);scene_stats=[]
for name in sorted(set(bf)&set(af)):
    item={'scene':name}
    for label,data in [('baseline',bf[name]),('proposed',af[name])]:
        for k in ['wall_ms','entity_triangles','entity_draws','instances','upload_bytes']:
            item[label+'_'+k]=round(statistics.median(float(r[k])for r in data),3)
    item['triangle_change_percent']=round((item['proposed_entity_triangles']/max(1,item['baseline_entity_triangles'])-1)*100,2)
    scene_stats.append(item)
if scene_stats:
    with (a.output/'scene_costs.csv').open('w') as f:
        w=csv.DictWriter(f,fieldnames=scene_stats[0]);w.writeheader();w.writerows(scene_stats)
def font(size):
    return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',size)
small,title=font(20),font(30)
for path in sorted(a.baseline.glob('*.png')):
    q=a.proposed/path.name
    if not q.exists():continue
    images=[Image.open(path).convert('RGB'),Image.open(q).convert('RGB')]
    if any(im.size!=(1920,1080) for im in images):raise SystemExit('PNG dimensions mismatch: '+path.stem)
    tw,th=960,540
    board=Image.new('RGB',(tw*2,th+142),(13,23,31));d=ImageDraw.Draw(board)
    name=path.stem.replace('_',' ').title();d.text((24,16),name,font=title,fill=(233,240,245))
    d.text((24,63),'BASELINE 52317bb1',font=small,fill=(186,199,210));d.text((tw+24,63),'PROPOSED LOCAL CHANGES',font=small,fill=(147,222,194))
    for i,im in enumerate(images):board.paste(im.resize((tw,th),Image.Resampling.LANCZOS),(i*tw,96))
    d.text((24,645),'Actual production environment passes | Identical camera, weather and quality | Software-renderer review',font=small,fill=(168,185,197))
    board.save(a.output/(path.stem+'-comparison.png'))
renderer_modes={label:((root/'renderer_mode.txt').read_text().strip() if (root/'renderer_mode.txt').exists() else 'Legacy environment adapter; see capture log for omitted unused startup programs.')for label,root in [('baseline',a.baseline),('proposed',a.proposed)]}
summary={'renderer_modes':renderer_modes,'matched_images':len(matched),'complete_image_set':base_images==prop_images,'frozen_camera_weather_verified':True,'native1080_medium_verified':True,'all_chunks_generated':True,'baseline_mesh_bytes':sum(int(r['bytes'])for r in before.values()),'proposed_mesh_bytes':sum(int(r['bytes'])for r in after.values()),'changed_kind_lods':len(changes),'scene_costs':scene_stats,'limitations':['Frozen baseline uses the disclosed legacy environment-only startup adapter; proposed startup mode is recorded in renderer_modes.','Software Mesa timings are diagnostic wall times, not RTX 3070 FPS measurements.','Identical native output/render resolutions; no dynamic-resolution scaling.','Comparison boards are downscaled from unretouched actual screenshots. Native originals retained.']}
(a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
