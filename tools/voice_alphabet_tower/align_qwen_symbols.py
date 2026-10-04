"""Refine carrier cuts with an independent CTC model, after raw speech verification.

Forced alignment locates words; it is not treated as a pronunciation test.
"""
import os,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent;KIT=ROOT/'recovered/Solace-Express-Voice-Regeneration-Kit'
sys.path.insert(0,str(KIT));os.environ.update(HF_HUB_OFFLINE='1',HF_HUB_DISABLE_TELEMETRY='1',TOKENIZERS_PARALLELISM='false')
import offline_guard
import argparse,hashlib,json,re
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly
import torch,torchaudio
from audio_master import master,clean_fragment
from headset_filter import process
from crop_qwen_symbols import ALIASES,norm

def main():
 p=argparse.ArgumentParser();p.add_argument('--only',action='append');p.add_argument('--write',action='store_true');a=p.parse_args()
 torch.set_num_threads(4);torch.set_num_interop_threads(1)
 bundle=torchaudio.pipelines.WAV2VEC2_ASR_BASE_960H
 model=bundle.get_model(dl_kwargs={'model_dir':str(ROOT/'work/align-model')}).eval();labels=bundle.get_labels();vocab={c:i for i,c in enumerate(labels)}
 m=json.loads((ROOT/'work/expanded-input.json').read_text());src=ROOT/'work/carriers';pack=ROOT/'work/pack';checks=json.loads((ROOT/'work/carrier-crop-checks.json').read_text())['checks']
 out=ROOT/'work/ctc-crop-checks.json';report=json.loads(out.read_text()) if out.exists() else {'method':'Raw carrier checked by unprompted Whisper; independent Wav2Vec2 CTC word-boundary alignment locates the cut. Alignment is not a recognition pass.','checks':{}}
 rows=[r for r in m['lines'] if 'Qwen' in m['cast'][r['speaker_id']]['synthesis'] and (not a.only or r['id'] in a.only)]
 for r in rows:
  raw,sr=sf.read(src/(r['id']+'.wav'),dtype='float32');source_sha=hashlib.sha256((src/(r['id']+'.wav')).read_bytes()).hexdigest()
  verified=checks[r['id']];assert source_sha==verified['carrier_sha256'],r['id']+' stale recognition'
  body=norm(verified['recognized']);prefix='the letter ' if r['character'].isalpha() else 'the number '
  aliases=ALIASES.get(r['character'],[r['character'],r['spoken_name'].lower()])
  matching=[s for s in aliases if (body+' ').startswith(prefix+s+' ')]
  if not matching:print('REVIEW',r['id'],'raw target is unverified',flush=True);continue
  audio=torch.from_numpy(resample_poly(raw,2,3).astype('float32')).unsqueeze(0)
  with torch.inference_mode():emission,_=model(audio);logp=emission.log_softmax(-1)
  greedy=torch.unique_consecutive(logp[0].argmax(-1));greedy_code=''.join(labels[i] for i in greedy.tolist() if i!=0);greedy=greedy_code.replace('|',' ').strip();greedy_body=norm(greedy)
  gre_match=[s for s in aliases if (greedy_body+' ').startswith(prefix+s+' ')]
  if gre_match:
   reference=greedy_body;target_name=max(gre_match,key=len)
  else:
   target_name=r['spoken_name'].lower();reference=prefix+target_name
   tail=verified.get('discarded_tail','')
   if tail:reference+=' '+tail
  numbers=['zero','one','two','three','four','five','six','seven','eight','nine']
  reference=' '.join(numbers[int(w)] if re.fullmatch('[0-9]',w) else w for w in reference.split())
  target_name=' '.join(numbers[int(w)] if re.fullmatch('[0-9]',w) else w for w in target_name.split())
  reference=reference.upper().replace(' ','|');prefix_code=prefix.strip().upper().replace(' ','|')+'|';target_code=target_name.upper().replace(' ','|')
  if gre_match and prefix_code+target_code in greedy_code:reference=greedy_code
  elif greedy_code.endswith('|'):reference+='|'
  prefix_at=reference.find(prefix_code);assert prefix_at>=0 and reference[prefix_at:].startswith(prefix_code+target_code)
  token_ids=torch.tensor([[vocab[c] for c in reference]],dtype=torch.int32)
  path,score=torchaudio.functional.forced_align(logp,token_ids,blank=0)
  spans=torchaudio.functional.merge_tokens(path[0],score[0].exp());assert len(spans)==len(reference)
  step=len(raw)/sr/logp.shape[1];first=prefix_at+len(prefix_code);last=first+len(target_code)-1
  # The separator labels the boundary after the carrier. Preserve nearby quiet
  # frames, while leaving the carrier's final consonant behind.
  begin=max(0,(spans[first-1].end*step-.01) if r['speaker_id']=='instructor' else (spans[first-1].start*step+.01));end=min(len(raw)/sr,spans[last].end*step+.08)
  rec={'id':r['id'],'raw_whisper':verified['recognized'],'unprompted_ctc':greedy,'alignment_reference':reference.replace('|',' '),'carrier_sha256':source_sha,'crop_start':begin,'crop_end':end,'target_mean_confidence':float(np.mean([s.score for s in spans[first:last+1]])),'seconds':end-begin,'written':False}
  if a.write and end-begin>.15:
   x=resample_poly(master(clean_fragment(raw[int(begin*sr):int(end*sr)],sr),sr),2,1).astype('float32');y=process(x,48000,r['filter_profile'],r['id'],True)
   for field,data in [('audio_file',y),('master_file',x)]:
    dest=pack/r[field];dest.parent.mkdir(parents=True,exist_ok=True);sf.write(dest,data,48000,subtype='PCM_16')
   meta=json.loads((src/(r['id']+'.json')).read_text());meta.update(crop_start=begin,crop_end=end,carrier_recognized=verified['recognized'],carrier_sha256=source_sha,crop_method='Verified carrier and independent CTC word boundary',ctc_greedy=greedy,ctc_alignment_reference=rec['alignment_reference'],sha256=hashlib.sha256((pack/r['audio_file']).read_bytes()).hexdigest(),seconds=len(y)/48000)
   (pack/r['audio_file']).with_suffix('.render.json').write_text(json.dumps(meta,indent=2)+'\n');rec.update(written=True,sha256=meta['sha256'])
  report['checks'][r['id']]=rec;out.write_text(json.dumps(report,indent=2)+'\n')
  print(r['id'],repr(greedy),'cut',round(begin,3),round(end,3),'confidence',round(rec['target_mean_confidence'],3),'WROTE' if rec['written'] else '',flush=True)

if __name__=='__main__':main()
