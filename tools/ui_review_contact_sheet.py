#!/usr/bin/env python3
"""Contact sheet of actual captures. Labels keep scene omissions explicit."""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw
p=argparse.ArgumentParser()
p.add_argument('directory',type=Path)
p.add_argument('output',type=Path)
p.add_argument('--pattern',default='*.png')
p.add_argument('--columns',type=int,default=3)
p.add_argument('--title',default='Production UI layout previews (3D backdrop omitted)')
a=p.parse_args();files=sorted(f for f in a.directory.glob(a.pattern) if f.resolve()!=a.output.resolve())
if not files: raise SystemExit('No capture images found')
w,h=640,360;rowh=h+40;rows=(len(files)+a.columns-1)//a.columns
sheet=Image.new('RGB',(a.columns*w,rows*rowh+56),'#091323');d=ImageDraw.Draw(sheet)
d.text((16,16),a.title,fill='#e8eff8')
for i,f in enumerate(files):
    im=Image.open(f).convert('RGB');im.thumbnail((w,h))
    x=(i%a.columns)*w;y=56+(i//a.columns)*rowh
    sheet.paste(im,(x+(w-im.width)//2,y+(h-im.height)//2));d.text((x+12,y+h+10),f.stem,fill='#a9c8d8')
a.output.parent.mkdir(parents=True,exist_ok=True);sheet.save(a.output)
print(a.output)
