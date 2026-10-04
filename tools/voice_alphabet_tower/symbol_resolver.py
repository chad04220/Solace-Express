"""Pure audio lookup/composition; the game must supply ATC state and authorization."""
from pathlib import Path
import json,re

class VoiceBank:
 def __init__(self,root):
  self.root=Path(root)
  manifest=self.root/'manifest.json'
  if not manifest.exists():manifest=self.root.parent/'expanded-input.json'
  self.manifest=json.loads(manifest.read_text())
  self.lines={r['id']:r for r in self.manifest['lines']}
 def character(self,voice,char):
  if not isinstance(char,str) or not re.fullmatch(r'[A-Za-z0-9]',char):raise ValueError('Expected one English letter or digit')
  return self.manifest['symbol_lookup'][voice][char.upper()]
 def file(self,id):return self.root/self.lines[id]['audio_file']
 def spell(self,voice,text,radio=False):
  ids=[]
  if radio and voice not in self.manifest['tower_voices']:raise ValueError('Radio spelling needs a new tower voice')
  text=text.strip()
  if not text:raise ValueError('Empty identifier')
  if radio and re.match(r'^Solace(?:\s|$)',text,re.I):
   ids.append(self.manifest['tower_atoms'][voice]['solace']);text=re.sub(r'^Solace\s*','',text,flags=re.I)
  if not re.fullmatch(r'[A-Za-z0-9 -]+',text):raise ValueError('Unsupported identifier character')
  for char in text.upper():
   if char in ' -':continue
   if char not in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789':raise ValueError('Unsupported identifier character: '+char)
   if not radio:ids.append(self.character(voice,char))
   elif char.isdigit():ids.append(self.manifest['radio_digits'][voice][char])
   else:ids.append(self.manifest['radio_alphabet'][voice][char])
  if not ids:raise ValueError('Empty identifier')
  return ids
 def number(self,voice,value,width=0):
  text=str(value)
  if not re.fullmatch(r'[0-9]{1,4}',text) or not isinstance(width,int) or not 0<=width<=4:raise ValueError('Expected a nonnegative integer with at most four digits')
  return [self.manifest['radio_digits'][voice][c] for c in text.zfill(width)]
 def runway(self,voice,value):
  text=str(value).upper().strip();match=re.fullmatch(r'0?([1-9]|[12][0-9]|3[0-6])([LRC]?)',text)
  if not match:raise ValueError('Runway must be 1–36 with optional L/R/C')
  ids=self.number(voice,int(match[1]))
  if match[2]:ids.append(self.manifest['tower_atoms'][voice][{'L':'left','R':'right','C':'center'}[match[2]]])
  return ids
 def compose(self,voice,template,**context):
  if voice not in self.manifest['tower_voices']:raise ValueError('Select a tower voice')
  if template=='wind_gusting' and all(k in context for k in ('wind_knots','gust_knots')):
   if not all(re.fullmatch(r'[0-9]+',str(context[k])) for k in ('wind_knots','gust_knots')) or int(context['gust_knots'])<=int(context['wind_knots']):raise ValueError('Gust must exceed sustained wind')
  rule=self.manifest['tower_templates'][template];atoms=self.manifest['tower_atoms'][voice]
  ids=self.spell(voice,context['callsign'],radio=True) if context.get('callsign') else []
  if context.get('station_code'):
   ids+=self.spell(voice,context['station_code'],radio=True)+[atoms['tower']]
  for segment in rule['segments']:
   if not segment.startswith('{'):ids.append(atoms[segment]);continue
   key=segment[1:-1]
   if key not in context:raise ValueError('Missing '+key)
   val=context[key]
   if key=='runway':ids+=self.runway(voice,val)
   elif key=='wind_heading':
    if not re.fullmatch(r'[0-9]+',str(val)) or not 1<=int(val)<=360:raise ValueError('Wind heading must be 1–360')
    ids+=self.number(voice,int(val),3)
   elif key in ('wind_knots','gust_knots'):
    if not re.fullmatch(r'[0-9]+',str(val)) or not 0<=int(val)<=150:raise ValueError('Wind speed outside supported range')
    ids+=self.number(voice,int(val))
   elif key=='frequency':
    text=str(val)
    if not re.fullmatch(r'[0-9]{3}\.[0-9]{1,3}',text) or not 118<=float(text)<137:raise ValueError('Expected a modeled VHF frequency')
    whole,decimal=text.split('.')
    ids+=self.number(voice,whole)+[atoms['point']]+self.number(voice,decimal)
   elif key=='taxiways':
    if not isinstance(val,list) or not val:raise ValueError('Expected actual taxiway route')
    for i,taxiway in enumerate(val):
     if i:ids.append(atoms['then'])
     ids+=self.spell(voice,str(taxiway),radio=True)
   else:raise ValueError('Unknown field '+key)
  return {'ids':ids,'files':[str(self.file(id)) for id in ids],
          'spoken_text':' '.join(self.lines[id]['spoken_text'] for id in ids),
          'phase':rule['phase'],'condition':rule['condition'],'priority':rule['priority']}

if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser();p.add_argument('bank',type=Path);p.add_argument('voice');p.add_argument('text');p.add_argument('--radio',action='store_true');a=p.parse_args()
 b=VoiceBank(a.bank)
 for id in b.spell(a.voice,a.text,radio=a.radio):print(b.file(id))
