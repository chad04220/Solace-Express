"""Unprompted local recognition screen; differences require listening, not automatic acceptance."""
import os,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'recovered/Solace-Express-Voice-Regeneration-Kit'))
os.environ.update(HF_HUB_OFFLINE='1',HF_HUB_DISABLE_TELEMETRY='1',TOKENIZERS_PARALLELISM='false')
import offline_guard
import argparse,json,re,time,hashlib
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly
from faster_whisper import WhisperModel

def norm(text):
 text=' '.join(re.findall(r'[a-z0-9]+',text.lower()))
 return re.sub(r'\bstand by\b','standby',re.sub(r'\bcall sign\b','callsign',text))
def main():
 p=argparse.ArgumentParser();p.add_argument('--limit',type=int);p.add_argument('--only',action='append');p.add_argument('--calls-only',action='store_true');a=p.parse_args()
 m=json.loads((ROOT/'work/expanded-input.json').read_text());pack=ROOT/'work/pack'
 model=WhisperModel(str(ROOT/'work/asr-model'),device='cpu',compute_type='int8',cpu_threads=4,num_workers=1,local_files_only=True)
 rows=[r for r in m['lines'] if (pack/r['audio_file']).exists()]
 if a.only:rows=[r for r in rows if r['id'] in a.only]
 if a.calls_only:rows=[r for r in rows if r['kind']=='line']
 if a.limit:rows=rows[:a.limit]
 out=ROOT/'work/speech-screen.json';existing=json.loads(out.read_text()) if out.exists() else {'method':'faster-whisper small.en CPU int8, English, no transcript prompt or supplied reference words','checks':{}}
 count=0
 for r in rows:
  data,sr=sf.read(pack/r['audio_file'],dtype='float32');audio=resample_poly(data,1,3)
  # Symmetric quiet context reduces isolated-word truncation; no expected-text prompt is supplied.
  audio=np.pad(audio,(int(.3*16000),int(.45*16000)))
  segments,info=model.transcribe(audio,language='en',beam_size=5,temperature=0,condition_on_previous_text=False,vad_filter=False,no_speech_threshold=.9)
  segments=list(segments);text=' '.join(s.text.strip() for s in segments).strip()
  expected=r['spoken_text'];accepted=norm(expected)==norm(text)
  if r['id'].startswith('symbol.'):
   char=r['character'];name=r['spoken_name'];aliases={norm(name),char.lower()}
   alias={'a':'ay','b':'bee|be','c':'see|sea','d':'dee','e':'ee','f':'eff','g':'gee','h':'aitch','i':'eye','j':'jay','k':'kay','l':'ell|el','m':'em','n':'en','o':'oh','p':'pee','q':'cue|queue','r':'ar|are','s':'ess','t':'tee|tea','u':'you','v':'vee','w':'double u','x':'ex','y':'why','z':'zee'}.get(char.lower())
   if alias:aliases.update(alias.split('|'))
   if char=='1':aliases.add('won')
   if char=='2':aliases.update(('to','too'))
   if char=='4':aliases.add('for')
   if char=='8':aliases.add('ate')
   if char=='W':aliases.update(('double you','w'))
   accepted=norm(text) in aliases
  result={'id':r['id'],'expected':expected,'recognized':text,'matched':accepted,'audio_file':r['audio_file'],'sha256':hashlib.sha256((pack/r['audio_file']).read_bytes()).hexdigest()}
  existing['checks'][r['id']]=result;out.write_text(json.dumps(existing,indent=2)+'\n')
  count+=1
  print('Speech screen',count,'/',len(rows),r['id'],repr(text),'MATCH' if accepted else 'REVIEW',flush=True)
 print('Screen complete:',len(rows),'assets.',flush=True)

if __name__=='__main__':main()
