"""Screen shuffled symbol lists without text prompts or expected-word hints."""
import os,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'recovered/Solace-Express-Voice-Regeneration-Kit'))
os.environ.update(HF_HUB_OFFLINE='1',HF_HUB_DISABLE_TELEMETRY='1',TOKENIZERS_PARALLELISM='false')
import offline_guard
import argparse,hashlib,json,random,re
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly
from faster_whisper import WhisperModel

def main():
 p=argparse.ArgumentParser();p.add_argument('--voice',action='append');p.add_argument('--group-size',type=int,default=12);a=p.parse_args()
 m=json.loads((ROOT/'work/expanded-input.json').read_text());pack=ROOT/'work/pack';index={r['id']:r for r in m['lines']}
 model=WhisperModel(str(ROOT/'work/asr-model'),device='cpu',compute_type='int8',cpu_threads=4,num_workers=1,local_files_only=True)
 voices=a.voice or list(m['cast']);result={'method':'Randomized symbol-order groups, small.en CPU int8, English, beam 5, no prompt/reference words','groups':[]}
 out=ROOT/'work/symbol-group-screen.json'
 if out.exists():result=json.loads(out.read_text());result['groups']=[r for r in result['groups'] if r['voice'] not in voices]
 for voice in voices:
  chars=list(m['letters']+m['digits']);random.Random('solace-symbol-screen:'+voice).shuffle(chars)
  for start in range(0,len(chars),a.group_size):
   group=chars[start:start+a.group_size];rows=[index[m['symbol_lookup'][voice][c]] for c in group]
   if not all((pack/r['audio_file']).exists() for r in rows):continue
   pieces=[np.zeros(int(.3*16000),dtype='float32')]
   for r in rows:
    data,sr=sf.read(pack/r['audio_file'],dtype='float32');pieces.extend((resample_poly(data,1,3),np.zeros(int(.24*16000),dtype='float32')))
   audio=np.concatenate(pieces)
   segments,_=model.transcribe(audio,language='en',beam_size=5,temperature=0,condition_on_previous_text=False,vad_filter=False)
   text=' '.join(s.text.strip() for s in segments)
   entry={'voice':voice,'ids':[r['id'] for r in rows],'expected_characters':group,'expected_names':[r['spoken_name'] for r in rows],'recognized':text,'audio_sha256s':[hashlib.sha256((pack/r['audio_file']).read_bytes()).hexdigest() for r in rows]}
   result['groups'].append(entry);out.write_text(json.dumps(result,indent=2)+'\n')
   print(voice,group,repr(text),flush=True)

if __name__=='__main__':main()
