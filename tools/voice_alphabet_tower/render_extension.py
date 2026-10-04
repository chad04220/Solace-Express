"""Offline extension renderer. One engine per process avoids competing large models."""
import os,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent
KIT=ROOT/'recovered/Solace-Express-Voice-Regeneration-Kit'
sys.path.insert(0,str(KIT))
os.environ.update(HF_HUB_OFFLINE='1',HF_HUB_DISABLE_TELEMETRY='1',GRADIO_ANALYTICS_ENABLED='False',TOKENIZERS_PARALLELISM='false')
import offline_guard
import argparse,hashlib,json,time,shutil
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly
from audio_master import master,master_kokoro,clean_fragment
from headset_filter import process

# Explicit letter phonemes avoid an isolated A being read as the article "a".
# Kokoro uses eSpeak-style IPA; language differences are retained for O/R/Z.
IPA={'A':'ˈeɪ','B':'bˈiː','C':'sˈiː','D':'dˈiː','E':'ˈiː','F':'ˈɛf','G':'dʒˈiː','H':'ˈeɪtʃ','I':'ˈaɪ','J':'dʒˈeɪ','K':'kˈeɪ','L':'ˈɛl','M':'ˈɛm','N':'ˈɛn','O':'ˈoʊ','P':'pˈiː','Q':'kˈjuː','R':'ˈɑːɹ','S':'ˈɛs','T':'tˈiː','U':'jˈuː','V':'vˈiː','W':'dˈʌbəl jˈuː','X':'ˈɛks','Y':'wˈaɪ','Z':'zˈiː'}

def main():
 p=argparse.ArgumentParser();p.add_argument('--engine',choices=['kokoro','qwen'],required=True);p.add_argument('--limit',type=int);p.add_argument('--only',action='append');p.add_argument('--attempt',type=int,default=0);p.add_argument('--carrier',action='store_true');p.add_argument('--pause',action='store_true');p.add_argument('--pending-paused',action='store_true');p.add_argument('--letters-only',action='store_true');p.add_argument('--output',type=Path,default=ROOT/'work/pack');a=p.parse_args()
 m=json.loads((ROOT/'work/expanded-input.json').read_text());a.output.mkdir(parents=True,exist_ok=True)
 index={r['id']:r for r in json.loads((ROOT/'work/original-manifest.json').read_text())['lines']}
 for r in m['lines']:
  if not r['reuse_line_id']:continue
  prior=index[r['reuse_line_id']]
  for field in ('audio_file','master_file'):
   target=a.output/r[field];target.parent.mkdir(parents=True,exist_ok=True)
   if not target.exists():shutil.copy2(ROOT/'work/original'/prior[field],target)
  meta={'engine':'exact original asset','source_id':prior['id'],'sha256':hashlib.sha256((a.output/r['audio_file']).read_bytes()).hexdigest(),'source_sha256':prior['speech_render']['sha256'],'seconds':sf.info(a.output/r['audio_file']).duration}
  assert meta['sha256']==meta['source_sha256']
  (a.output/r['audio_file']).with_suffix('.render.json').write_text(json.dumps(meta,indent=2)+'\n')
 rows=[r for r in m['lines'] if not r['reuse_line_id'] and (('Qwen' in m['cast'][r['speaker_id']]['synthesis'])==(a.engine=='qwen'))]
 if a.letters_only:rows=[r for r in rows if r['id'].startswith('symbol.') and r['character'].isalpha()]
 if a.only:rows=[r for r in rows if r['id'] in a.only]
 elif a.attempt==0:
  if a.carrier:rows=[r for r in rows if not (ROOT/'work/carriers'/(r['id']+'.wav')).exists()]
  else:rows=[r for r in rows if not (a.output/r['audio_file']).exists()]
 if a.pending_paused:
  assert a.engine=='qwen'
  def needs_pause(r):
   path=ROOT/'work/carriers'/(r['id']+'.json')
   return not path.exists() or not json.loads(path.read_text()).get('paused_carrier')
  rows=[r for r in rows if needs_pause(r)]
 if a.limit:rows=rows[:a.limit]
 if not rows:print('No pending '+a.engine+' assets.',flush=True);return
 def save(r,raw,sr,metadata):
  is_fragment=r['kind']=='fragment'
  if is_fragment:raw=clean_fragment(raw,sr)
  x=master(raw,sr) if a.engine=='qwen' else master_kokoro(raw,sr,is_fragment)
  x=resample_poly(x,2,1).astype('float32')
  y=process(x,48000,r['filter_profile'],r['id'],is_fragment)
  assert np.isfinite(y).all() and np.max(np.abs(y))<.8 and len(y)>.15*48000,r['id']
  assert len(y)<(5 if is_fragment else 20)*48000,r['id']
  for field,data in [('master_file',x),('audio_file',y)]:
   target=a.output/r[field];target.parent.mkdir(parents=True,exist_ok=True);sf.write(target,data,48000,subtype='PCM_16')
  metadata.update(sha256=hashlib.sha256((a.output/r['audio_file']).read_bytes()).hexdigest(),seconds=len(y)/48000,attempt=a.attempt)
  (a.output/r['audio_file']).with_suffix('.render.json').write_text(json.dumps(metadata,indent=2)+'\n')
 if a.engine=='kokoro':
  import onnxruntime as ort
  from kokoro_onnx import Kokoro
  opts=ort.SessionOptions();opts.intra_op_num_threads=4;opts.inter_op_num_threads=1
  session=ort.InferenceSession(str(KIT/'models/kokoro-v1.0.onnx'),sess_options=opts,providers=['CPUExecutionProvider'])
  model=Kokoro.from_session(session,str(KIT/'models/voices-v1.0.bin'))
  for i,r in enumerate(rows):
   c=m['cast'][r['speaker_id']];text=r['spoken_text'];phonemes=False
   if r['id'].startswith('symbol.') and r['character'].isalpha():
    text=IPA[r['character']]
    if c['lang']=='en-gb':text={'O':'ˈəʊ','R':'ˈɑː','Z':'zˈɛd'}.get(r['character'],text)
    text+='.';phonemes=True
   t=time.perf_counter();crop={}
   if a.carrier and (r['kind']=='line' or (r['id'].startswith('symbol.') and r['character'].isalpha())):
    if r['kind']=='line':text=model.tokenizer.phonemize(text,c['lang']).strip();phonemes=True
    carrier_prefix='The instruction is' if r['kind']=='line' else 'The letter'
    prefix=model.tokenizer.phonemize(carrier_prefix+('.' if a.pause else ''),c['lang']).strip()+' '
    full=prefix+text
    raw,sr,timing=model.create_timed(full,voice=c['voice'],speed=c['speed'],lang=c['lang'],is_phonemes=True)
    first=len(model.tokenizer.known(prefix));assert timing and first<len(timing)
    begin=max(0,timing[first].start-(.055 if a.pause else .012));end=min(len(raw)/sr,timing[-1].end+.020)
    raw=raw[int(begin*sr):int(end*sr)]
    crop={'carrier_phonemes':full,'crop_start':begin,'crop_end':end,'timing_source':'Kokoro exported phoneme durations'}
   else:raw,sr=model.create(text,voice=c['voice'],speed=c['speed'],lang=c['lang'],is_phonemes=phonemes)
   save(r,raw,sr,{'engine':'Kokoro-82M v1.0','voice':c['voice'],'language':c['lang'],'speed':c['speed'],'render_input':text,'is_phonemes':phonemes,'elapsed':time.perf_counter()-t,**crop})
   print('Generated',i+1,'/',len(rows),r['id'],flush=True)
 else:
  import torch
  from qwen_tts import Qwen3TTSModel
  torch.set_num_threads(6);torch.set_num_interop_threads(1)
  model=Qwen3TTSModel.from_pretrained(str((KIT/'models').resolve()),device_map='cpu',dtype=torch.bfloat16,attn_implementation='sdpa',local_files_only=True)
  for start in range(0,len(rows),4):
   batch=rows[start:start+4];seed=int(hashlib.sha256(('alphabet-v1:'+str(a.attempt)+':'+batch[0]['id']).encode()).hexdigest()[:8],16)
   torch.manual_seed(seed)
   speakers=[m['cast'][r['speaker_id']]['voice'].title() for r in batch]
   instructions=[m['cast'][r['speaker_id']]['direction']+(' Read the provided short statement exactly, with crisp emphasis on the final English letter name or number. Do not repeat any words.' if a.carrier else ' Speak just the single English letter name or number shown. Use a clear spelling-list pronunciation. Do not say letter, number, or any other words. No repetitions.') for r in batch]
   if a.pause:instructions=[i+' Pause silently between the two sentences. Speak the final letter or number on its own, clearly and completely, then stop.' for i in instructions]
   options={'non_streaming_mode':True,'max_new_tokens':180,'temperature':.55,'repetition_penalty':1.15}
   if a.carrier:texts=[('The letter '+('Zed' if r['spoken_name']=='Zed' else 'Zee' if r['character']=='Z' else r['character'])+'.') if r['character'].isalpha() else 'The number '+r['spoken_text'] for r in batch]
   else:texts=[(r['character']+'.') if r['character'].isalpha() else r['spoken_text'] for r in batch]
   if a.pause:texts=[s.replace('The letter ','The letter. ').replace('The number ','The number. ') for s in texts]
   t=time.perf_counter()
   wavs,sr=model.generate_custom_voice(text=texts,language='English',speaker=speakers,instruct=instructions,**options)
   assert len(wavs)==len(batch) and sr==24000
   elapsed=time.perf_counter()-t
   for r,raw,instruction,speaker in zip(batch,wavs,instructions,speakers):
    metadata={'engine':'Qwen3-TTS-12Hz-1.7B-CustomVoice','voice':speaker,'instruction':instruction,'seed':seed,'batch_ids':[x['id'] for x in batch],'batch_texts':texts,'batch_instructions':instructions,'generation_options':options,'elapsed_batch':elapsed,'paused_carrier':a.pause}
    if a.carrier:
     dest=ROOT/'work/carriers';dest.mkdir(exist_ok=True)
     sf.write(dest/(r['id']+'.wav'),raw,sr,subtype='PCM_16')
     metadata['carrier_text']=texts[batch.index(r)];(dest/(r['id']+'.json')).write_text(json.dumps(metadata,indent=2)+'\n')
    else:save(r,raw,sr,metadata)
    print('Generated',min(start+4,len(rows)),'/',len(rows),r['id'],'batch_seconds',round(elapsed,2),flush=True)
 print('Engine complete:',a.engine,len(rows),'assets.',flush=True)

if __name__=='__main__':main()
