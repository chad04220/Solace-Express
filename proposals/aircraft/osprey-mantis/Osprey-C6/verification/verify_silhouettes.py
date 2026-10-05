#!/usr/bin/env python3
"""Inspect rendered masks; known left pitot remains visible in orthographic masks."""
from pathlib import Path
from PIL import Image
import numpy as np,json
root=Path(__file__).resolve().parents[1]
out={}
for view in ['front','rear','top','underside']:
 a=np.asarray(Image.open(root/'previews'/f'osprey_c6-silhouette-{view}.png'))[:,:,0]>127
 diff=a!=a[:,::-1]; yy,xx=np.nonzero(diff)
 out[view]={'differing_pixels':int(diff.sum()),'total_pixels':int(a.size),'difference_bbox':None if not len(xx) else [int(xx.min()),int(yy.min()),int(xx.max()),int(yy.max())]}
print(json.dumps(out,indent=2))
