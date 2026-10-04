"""Extract a recognized final letter/digit from a complete synthetic carrier statement."""
import os,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent;KIT=ROOT/'recovered/Solace-Express-Voice-Regeneration-Kit'
sys.path.insert(0,str(KIT));os.environ.update(HF_HUB_OFFLINE='1',HF_HUB_DISABLE_TELEMETRY='1',TOKENIZERS_PARALLELISM='false')
import offline_guard
import argparse,hashlib,json,re
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly
from faster_whisper import WhisperModel
from audio_master import master,clean_fragment
from headset_filter import process

ALIASES={'A':['a','ay'],'B':['b','bee','be'],'C':['c','see','sea'],'D':['d','dee'],'E':['e','ee'],'F':['f','eff'],'G':['g','gee'],'H':['h','aitch'],'I':['i','eye'],'J':['j','jay'],'K':['k','kay'],'L':['l','ell','el'],'M':['m','em'],'N':['n','en'],'O':['o','oh'],'P':['p','pee'],'Q':['q','cue','queue'],'R':['r','ar','are'],'S':['s','ess'],'T':['t','tee','tea'],'U':['u','you'],'V':['v','vee'],'W':['w','double u','double you'],'X':['x','ex'],'Y':['y','why'],'Z':['z','zee','zed']}
def norm(t):return ' '.join(re.findall('[a-z0-9]+',t.lower()))
def main():
 p=argparse.ArgumentParser();p.add_argument('--only',action='append');a=p.parse_args()
 m=json.loads((ROOT/'work/expanded-input.json').read_text());pack=ROOT/'work/pack';src=ROOT/'work/carriers'
 model=WhisperModel(str(ROOT/'work/asr-model'),device='cpu',compute_type='int8',cpu_threads=4,num_workers=1,local_files_only=True)
 report=ROOT/'work/carrier-crop-checks.json';results=json.loads(report.read_text()) if report.exists() else {'method':'Unprompted local carrier recognition and word timestamps; the provided transcript is not passed to ASR','checks':{}}
 rows=[r for r in m['lines'] if 'Qwen' in m['cast'][r['speaker_id']]['synthesis'] and (src/(r['id']+'.wav')).exists()]
 if a.only:rows=[r for r in rows if r['id'] in a.only]
 for i,r in enumerate(rows):
  source=src/(r['id']+'.wav');raw,sr=sf.read(source,dtype='float32');audio=resample_poly(raw,2,3)
  segs,_=model.transcribe(audio,language='en',beam_size=5,temperature=0,condition_on_previous_text=False,vad_filter=False,word_timestamps=True)
  segs=list(segs);text=' '.join(s.text.strip() for s in segs);words=[w for s in segs for w in (s.words or [])]
  body=norm(text);prefix='the letter ' if r['character'].isalpha() else 'the number '
  aliases=ALIASES.get(r['character'],[r['character'],r['spoken_name'].lower()])
  if r['character']=='1':aliases+=['won']
  if r['character']=='2':aliases+=['to','too']
  if r['character']=='4':aliases+=['for']
  if r['character']=='8':aliases+=['ate']
  tokens=[(token,w) for w in words for token in norm(w.word).split()]
  prefix_tokens=prefix.split();matched=[]
  if [t[0] for t in tokens[:2]]==prefix_tokens:
   for alias in sorted(aliases,key=lambda x:len(x.split()),reverse=True):
    candidate=alias.split()
    if [t[0] for t in tokens[2:2+len(candidate)]]==candidate:
     matched=tokens[2:2+len(candidate)];break
  ok=bool(matched)
  rec={'id':r['id'],'expected':prefix+r['spoken_name'],'recognized':text,'raw_target_match':ok,'accepted':ok,'carrier_sha256':hashlib.sha256(source.read_bytes()).hexdigest()}
  if ok:
   begin=max(0,matched[0][1].start-.015);end=min(len(raw)/sr,matched[-1][1].end+.045)
   rec['discarded_tail']=' '.join(t[0] for t in tokens[2+len(matched):])
   meta=json.loads((src/(r['id']+'.json')).read_text())
   if meta.get('paused_carrier'):
    hop=int(sr*.01);energy=np.array([np.sqrt(np.mean(raw[j:j+hop]**2)) for j in range(0,len(raw),hop)])
    quiet=energy<max(.0005,float(energy.max())*.008);runs=[];start=None
    for j,is_quiet in enumerate(np.r_[quiet,False]):
     if is_quiet and start is None:start=j
     if not is_quiet and start is not None:
      lo,hi=start*.01,j*.01
      if hi-lo>=.10:runs.append((lo,hi))
      start=None
    word_start=matched[0][1].start;prefix_start=tokens[1][1].start
    gaps=[(lo,hi) for lo,hi in runs if lo>prefix_start+.08 and word_start-.6<hi<word_start+.20 and hi<matched[-1][1].end-.10]
    if gaps:
     gap=min(gaps,key=lambda z:abs(z[1]-word_start));begin=max(gap[0],gap[1]-.03);rec['isolation_gap']=list(gap)
    else:rec['accepted']=False;rec['reason']='No clear silent boundary before the target'
   if not .2<begin<end or end-begin<.12:rec['accepted']=False;rec['reason']='Unusable word boundary'
   if rec['accepted']:
    cropped=clean_fragment(raw[int(begin*sr):int(end*sr)],sr);x=resample_poly(master(cropped,sr),2,1).astype('float32')
    y=process(x,48000,r['filter_profile'],r['id'],True)
    for field,data in [('master_file',x),('audio_file',y)]:
     target=pack/r[field];target.parent.mkdir(parents=True,exist_ok=True);sf.write(target,data,48000,subtype='PCM_16')
    meta.update(crop_start=begin,crop_end=end,carrier_recognized=text,carrier_sha256=rec['carrier_sha256'],isolation_gap=rec.get('isolation_gap'),crop_method='Silent carrier boundary plus unprompted recognizer word timestamps',sha256=hashlib.sha256((pack/r['audio_file']).read_bytes()).hexdigest(),seconds=len(y)/48000)
    (pack/r['audio_file']).with_suffix('.render.json').write_text(json.dumps(meta,indent=2)+'\n')
    rec.update(crop_start=begin,crop_end=end,sha256=meta['sha256'],seconds=meta['seconds'])
  results['checks'][r['id']]=rec;report.write_text(json.dumps(results,indent=2)+'\n')
  print('Carrier crop',i+1,'/',len(rows),r['id'],repr(text),'ACCEPTED' if rec['accepted'] else 'REVIEW',flush=True)
 print('Carrier extraction complete;',sum(not x['accepted'] for x in results['checks'].values()),'cases need review.',flush=True)

if __name__=='__main__':main()
