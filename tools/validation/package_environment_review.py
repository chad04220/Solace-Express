#!/usr/bin/env python3
"""Package bounded inspection JPEGs with exact native-capture provenance.
No renderer image pixels are retouched; only aspect-preserving downsampling/labels.
"""
import argparse,csv,hashlib,json
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
p=argparse.ArgumentParser();p.add_argument('captures',type=Path);p.add_argument('repository',type=Path);a=p.parse_args();a.captures=a.captures.resolve();a.repository=a.repository.resolve()
out=a.repository/'docs/living-islands/previews/final-review';out.mkdir(parents=True,exist_ok=True)
font='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf';title=ImageFont.truetype(font,23);body=ImageFont.truetype(font,16);small=ImageFont.truetype(font,14)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def capture(batch,name,label):return {'batch':batch,'name':name,'label':label}
boards=[
 ('city-day-night','Port Verde: production day and night',[capture('final-world-v7-city-airport','city_port_verde','Day 14:18'),capture('world-city-night-v7','city_port_verde_night','Night 22:00')]),
 ('capital-day-night','Capital airport: production day and night',[capture('final-world-v7-city-airport','airport_capital','Day 14:18'),capture('final-world-v7-city-airport','airport_capital_night','Night 22:00')]),
 ('coast-volcano','Coastal water and Mount Kaleo',[capture('final-world-v7-coast-volcano','shore_low','Low coastal water'),capture('final-world-v7-coast-volcano','volcano_bowl','Volcano bowl')]),
 ('house-car','Final unit-scale house and sedan geometry',[capture('hero-diagnostic-v6','kind_13_front_oblique','House: production420m shadow coverage'),capture('hero-diagnostic-v6','kind_45_front_oblique','Sedan: final continuous fascia and seated optics')]),
 ('parked-ga','Final parked GA: seated windshield edges',[capture('hero-final-mesh-v6','kind_42_front_oblique','Front oblique'),capture('hero-final-mesh-v6','kind_42_side','Side'),capture('hero-final-mesh-v6','kind_42_rear_oblique','Rear oblique')]),
 ('conifers','Final fir and spruce: irregular connected branchlets',[capture('hero-final-mesh-v6','kind_0_front_oblique','Fir front'),capture('hero-final-mesh-v6','kind_0_side','Fir side'),capture('hero-final-mesh-v6','kind_1_front_oblique','Spruce front'),capture('hero-final-mesh-v6','kind_1_side','Spruce side')]),
]
accepted={str(p.relative_to(a.repository)):sha(p) for p in sorted((a.repository/'src').rglob('*')) if p.is_file() and (p.suffix in ('.glsl','.inc') or p.name in ['entity_mesh.cpp','entity_render.cpp','entities.h','entities.cpp','renderer.cpp','renderer.h','world.cpp','world.h'])}
for material in sorted((a.repository/'assets/materials').rglob('*')):
 if material.is_file():accepted[str(material.relative_to(a.repository))]=sha(material)
manifest={'native_capture_root':'living-islands-review/captures (original lossless PNGs retained outside this preview directory)','quality':'Medium, native1920x1080, renderScale1, normal production shadow coverage, full production startup34programs, seven high-resolution and seven fallback environment material layers','performance':'Mesa software captures. CPU submission counts and wall times do not establish target-GPU FPS.','accepted_runtime_hashes':accepted,'boards':[],'known_limitations':['Coarse close house eave and apartment balcony shadow artifacts remain under the retained original cascade footprint.','96/160/256m footprint experiments and the adaptive proposal were rejected for production; no such image is selected here.','Geometry findings are defect-specific; these previews are not a blanket photorealistic-quality certification.','Parked vehicle lamps are not forced on by the fixture.']}
for stem,heading,items in boards:
 cols=2 if len(items)!=3 else 3;tilew=1280//cols;tileh=round(tilew*1080/1920);rows=(len(items)+cols-1)//cols;h=92+rows*(tileh+30)+43
 board=Image.new('RGB',(1280,h),(13,23,31));d=ImageDraw.Draw(board);d.text((16,12),heading,font=title,fill=(237,244,249));d.text((16,43),'Native1920x1080 originals | Medium / scale1 | Production420m near cascade | Source/camera hashes in manifest',font=small,fill=(177,202,218));entry={'file':stem+'.jpg','title':heading,'captures':[]}
 for i,item in enumerate(items):
  batch=a.captures/item['batch'];path=batch/(item['name']+'.png');im=Image.open(path).convert('RGB');assert im.size==(1920,1080),path
  with (batch/'views.csv').open() as f:camera=next(r for r in csv.DictReader(f) if r['name']==item['name'])
  assert float(camera.get('near_shadow_radius_override',0))==0,camera
  assert int(camera.get('debug_mask',0))==0,camera
  isworld=(batch/'world_source_postbuild.json').exists();source_path=batch/('world_source_postbuild.json' if isworld else 'source_postbuild.json')
  source=json.loads(source_path.read_text());differences={k:{'captured':v,'accepted':accepted[k]} for k,v in source['files_sha256'].items() if k in accepted and accepted[k]!=v}
  if differences:
   assert set(differences)=={'src/shaders/ent_fs2.glsl'},differences
   assert int(camera['kind']) in [0,1,13,42,45],camera
  x=(i%cols)*tilew;y=85+(i//cols)*(tileh+30);d.text((x+10,y-23),item['label'],font=body,fill=(224,235,239));board.paste(im.resize((tilew,tileh),Image.Resampling.LANCZOS),(x,y))
  entry['captures'].append({**item,'native_png':item['batch']+'/'+path.name,'native_sha256':sha(path),'camera':camera,'snapshot_source_sha256':source['combined_source_sha256'],'binary':source.get('binary'),'accepted_geometry_matches':True,'runtime_differences':differences,'difference_scope':('Only the later office/skyscraper night-emission branch differs; these daytime kinds do not execute that branch.' if differences else 'Selected accepted renderer, world, mesh and shader inputs match byte-for-byte.')})
 d.text((16,h-32),'Staged geometry fixtures are labelled; full-world views use actual generated scenery. Close-shadow limitations remain.',font=small,fill=(167,190,203))
 dest=out/entry['file'];board.save(dest,quality=91,optimize=True);entry.update({'sha256':sha(dest),'size_bytes':dest.stat().st_size,'dimensions':list(board.size)});manifest['boards'].append(entry)
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
readme='''# Final render-evidence selection\n\nThese small inspection boards preserve the actual renderer output, with only aspect-preserving downsampling and labels. Lossless1920×1080 originals remain in the existing capture folder. `manifest.json` records every native hash, camera, source snapshot, executable and selected accepted-runtime hash.\n\nAll images use Medium quality, scale1 and the original production near-cascade footprint420m. Full startup compiled34programs and loaded seven high-resolution plus seven fallback material layers. Software-render wall times are not GPU performance measurements.\n\nThe world views use the immutable v7 renderer: its selected renderer/world/mesh/shader/material inputs match the restored accepted checkout. The v6 house/car/GA/conifer views have the final geometry; their sole shader difference is the later office/skyscraper night-emission branch, which these daytime kinds do not execute. This exception is recorded per image, rather than relabelling those captures as v7.\n\nKnown close-shadow lobes and serration remain, especially below house eaves and apartment balconies. Narrower96/160/256m experiments improved close sampling but lost acceptable farther ground-shadow coverage or retained visible lobes, so the adaptive production proposal was rejected. None of those diagnostic-radius images are selected here. The assets have defect-specific geometry checks; this selection is not a blanket photorealistic-quality claim.\n\n'''
for stem,heading,items in boards:readme+=f'## {heading}\n\n![{heading}]({stem}.jpg)\n\n'
(out/'README.md').write_text(readme);print(json.dumps({'boards':len(boards),'bytes':sum(x['size_bytes'] for x in manifest['boards']),'output':str(out)}))
