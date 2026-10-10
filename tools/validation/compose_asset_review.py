#!/usr/bin/env python3
"""Label four real asset screenshots without retouching their pixels.
Native originals remain separate. Tiles are downscaled only for the review board.
"""
from pathlib import Path
import argparse,csv,json
from collections import defaultdict
from PIL import Image,ImageDraw,ImageFont
p=argparse.ArgumentParser();p.add_argument('captures',type=Path);p.add_argument('output',type=Path);p.add_argument('--max-width',type=int,default=1280);p.add_argument('--kinds',default='all');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
with (a.captures/'mesh_inventory.csv').open() as f:mesh={(int(r['kind']),int(r['lod'])):r for r in csv.DictReader(f)}
with (a.captures/'views.csv').open() as f:
    views=defaultdict(list)
    for r in csv.DictReader(f):views[int(r['kind'])].append(r)
fontpath='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf';title=ImageFont.truetype(fontpath,30);body=ImageFont.truetype(fontpath,20)
summary=[]
for kind,rows in sorted(views.items()):
    if a.kinds!='all' and kind not in [int(x) for x in a.kinds.split(',')]:continue
    valid=[r for r in rows if (a.captures/(r['name']+'.png')).exists()]
    if not valid:continue
    atlas=mesh[(kind,3)];width,height=1920,1280;board=Image.new('RGB',(width,height),(13,23,31));d=ImageDraw.Draw(board)
    d.text((24,16),f"{atlas['name']} | actual production asset, unit scale",font=title,fill=(235,243,247))
    first=valid[0];d.text((24,58),f"Hero mesh: {atlas['triangles']} triangles | {float(first['width']):.2f}m wide x {float(first['height_above_grade']):.2f}m above grade x {float(first['depth']):.2f}m deep",font=body,fill=(169,217,198))
    for n,r in enumerate(valid[:4]):
        path=a.captures/(r['name']+'.png');im=Image.open(path).convert('RGB')
        if im.size!=(1920,1080):raise SystemExit('Native1080 invariant failed: '+str(path))
        if int(r['selected_lod'])!=3:raise SystemExit('HeroLOD invariant failed: '+r['name'])
        x=(n%2)*960;y=105+(n//2)*578
        d.text((x+20,y),r['angle'].replace('_',' ').title(),font=body,fill=(233,239,241));board.paste(im.resize((960,540),Image.Resampling.LANCZOS),(x,y+30))
    d.text((24,1245),'Staged review fixture | Full production renderer | Native originals retained | Software capture, no GPU FPS claim',font=body,fill=(179,192,203))
    dest=a.output/f'kind_{kind}_multiangle.png';board.thumbnail((a.max_width,round(a.max_width*height/width)),Image.Resampling.LANCZOS);board.save(dest,optimize=True);board.save(dest.with_suffix('.jpg'),quality=94);summary.append({'kind':kind,'name':atlas['name'],'views':len(valid),'hero_triangles':int(atlas['triangles']),'board':dest.name})
(a.output/'index.json').write_text(json.dumps(summary,indent=2)+'\n');print(f'Created {len(summary)} real-image asset boards.')
