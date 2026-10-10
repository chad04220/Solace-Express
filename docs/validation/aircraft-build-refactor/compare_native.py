#!/usr/bin/env python3
"""Verify exact production-frame, uploaded geometry and native-pixel interface parity."""
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
import argparse,hashlib,json
p=argparse.ArgumentParser();p.add_argument('before',type=Path);p.add_argument('after',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
fixtures={
 '10_xr30_specter':['refactor_specter_neutral_rear','refactor_specter_vector_rear','cockpit_refactor_specter_vector'],
 '12_xr40_wraith':['refactor_wraith_hover_belly','refactor_wraith_weapons_belly','refactor_wraith_weapons_empty_belly','refactor_wraith_cloak_front','cockpit_refactor_wraith_hover']}
r={'before':str(a.before.resolve()),'after':str(a.after.resolve()),'fixtures':[],'errors':[]}
for folder,views in fixtures.items():
 for view in views:
  stem=folder[3:]+'_'+view;entry={'model_folder':folder,'view':view,'exact_files':{}};r['fixtures'].append(entry)
  for suffix in ['_frame_state.csv','_geometry.csv','_geometry.csv.topology.csv']:
   x=a.before/folder/(stem+suffix);y=a.after/folder/(stem+suffix)
   same=x.exists() and y.exists() and x.read_bytes()==y.read_bytes();entry['exact_files'][suffix]=same
   if not same:r['errors'].append(stem+suffix+': missing or changed')
  x=a.before/folder/(stem+'.png');y=a.after/folder/(stem+'.png')
  if not x.exists() or not y.exists():r['errors'].append(stem+'.png: missing');continue
  with Image.open(x) as im:before=im.convert('RGB')
  with Image.open(y) as im:after=im.convert('RGB')
  entry['native_1080p']=before.size==after.size==(1920,1080)
  if not entry['native_1080p']:r['errors'].append(stem+': resolution mismatch');continue
  diff=ImageChops.difference(before,after);entry['pixels_identical']=diff.getbbox() is None
  entry['mean_absolute_rgb_error']=ImageStat.Stat(diff).mean
  entry['before_pixel_sha256']=hashlib.sha256(before.tobytes()).hexdigest();entry['after_pixel_sha256']=hashlib.sha256(after.tobytes()).hexdigest()
  if not entry['pixels_identical']:r['errors'].append(stem+': native pixels changed')
r['fixture_count']=len(r['fixtures']);r['pass']=not r['errors'];a.report.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'pass':r['pass'],'fixtures':len(r['fixtures']),'errors':r['errors']},indent=2));raise SystemExit(0 if r['pass'] else 1)
