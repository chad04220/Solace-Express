"""Colour analytic sample grids; these are diagnostics, not game screenshots."""
from PIL import Image, ImageDraw
from pathlib import Path
import numpy as np
root=Path(__file__).resolve().parent
colours=[(0,5,[216,211,166]),(5,35,[121,156,98]),(35,250,[74,121,87]),(250,700,[101,123,103]),(700,1400,[149,154,146]),(1400,3000,[221,224,217])]
for file in root.glob('*.bin'):
    a=np.fromfile(file,dtype='<f4',offset=4).reshape(512,512,4)[:,:,0]
    rgb=np.zeros((512,512,3),dtype=np.uint8);rgb[:]=[24,62,89]
    for lo,hi,colour in colours:rgb[(a>=lo)&(a<hi)]=colour
    Image.fromarray(rgb).save(file.with_suffix('.png'))
for name,id in [('atolls','pr1-1-9-19'),('fjords','pr1-4-17-7')]:
    page=Image.new('RGB',(1040,566),(240,242,241));d=ImageDraw.Draw(page)
    d.text((10,8),f'{id} | Analytic base-height diagnostic, 76 km square. Not a game screenshot.',fill=(30,40,40))
    for i,label in enumerate(['before','after']):
        d.text((10+520*i,31),label.title(),fill=(30,40,40))
        page.paste(Image.open(root/f'{name}-{label}.png'),(4+520*i,50))
    page.save(root/f'{name}-comparison.png')
