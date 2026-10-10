#!/usr/bin/env python3
"""Small review contact sheets: three native-render views per asset, without retouching.
Each page stays under 1400px in both dimensions to keep visual QA context bounded.
"""
import argparse,csv,json
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
p=argparse.ArgumentParser();p.add_argument('captures',type=Path);p.add_argument('output',type=Path);p.add_argument('--kinds',default='all');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
with (a.captures/'mesh_inventory.csv').open() as f:mesh={int(r['kind']):r for r in csv.DictReader(f) if r['lod']=='3'}
kinds=sorted(mesh) if a.kinds=='all' else [int(x) for x in a.kinds.split(',')]
kinds=[k for k in kinds if (a.captures/f'kind_{k}_front_oblique.png').exists()]
font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',16);small=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',12)
records=[]
for page in range((len(kinds)+5)//6):
 group=kinds[page*6:(page+1)*6];board=Image.new('RGB',(1380,34+len(group)*226),(14,22,28));d=ImageDraw.Draw(board)
 d.text((12,8),'Production asset review: front / side / rear | unit scale | originals 1920x1080 | software capture',font=font,fill='white')
 for row,k in enumerate(group):
  for col,angle in enumerate(('front_oblique','side','rear_oblique')):
   path=a.captures/f'kind_{k}_{angle}.png';x=col*460;y=34+row*226
   d.text((x+7,y+3),f"{k}: {mesh[k]['name']} | {angle.replace('_',' ')} | {mesh[k]['triangles']} tri",font=small,fill=(221,232,236))
   if path.exists():
    im=Image.open(path).convert('RGB');assert im.size==(1920,1080),path
    im.thumbnail((460,207),Image.Resampling.LANCZOS);board.paste(im,(x+(460-im.width)//2,y+19))
   else:d.text((x+20,y+85),'NOT CAPTURED',font=font,fill=(255,180,140))
 out=a.output/f'overview_{page+1:02d}.jpg';board.save(out,quality=94);records.append({'page':out.name,'kinds':group})
(a.output/'overview_index.json').write_text(json.dumps(records,indent=2)+'\n');print(f'Created {len(records)} contact sheets.')
