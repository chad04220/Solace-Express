"""Replay frozen recipes offline. Model downloads belong to a separate process."""
import os, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent
KIT=ROOT/'recovered/Solace-Express-Voice-Regeneration-Kit'
sys.path.insert(0,str(KIT));os.environ.update(HF_HUB_OFFLINE='1',HF_HUB_DISABLE_TELEMETRY='1',TOKENIZERS_PARALLELISM='false')
import offline_guard
import argparse, gc, hashlib, io, json, zipfile
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly
from audio_master import master, master_kokoro, clean_fragment
from headset_filter import process

def main():
    p=argparse.ArgumentParser();p.add_argument('--models',required=True,type=Path);p.add_argument('--output',type=Path,default=ROOT/'regenerated-pack');p.add_argument('--original-pack',type=Path);p.add_argument('--only',action='append');p.add_argument('--engine',choices=['kokoro','qwen','all'],default='all');a=p.parse_args()
    manifest_path=ROOT/'frozen_manifest.json'
    if not manifest_path.exists():manifest_path=ROOT/'work/pack/manifest.json'
    m=json.loads(manifest_path.read_text());idx={r['id']:r for r in m['lines']};a.output.mkdir(parents=True,exist_ok=True)
    rows=[r for r in m['lines'] if not a.only or r['id'] in a.only]
    hashes=[]
    def save(r,x,y):
        for field,data in [('audio_file',y),('master_file',x)]:
            path=a.output/r[field];path.parent.mkdir(parents=True,exist_ok=True);sf.write(path,data,48000,subtype='PCM_16')
        digest=hashlib.sha256((a.output/r['audio_file']).read_bytes()).hexdigest()
        hashes.append({'id':r['id'],'sha256':digest,'reference_sha256':r['speech_render']['sha256'],'matches_reference':digest==r['speech_render']['sha256']})
        print('Replayed',r['id'],'HASH MATCH' if hashes[-1]['matches_reference'] else 'CHECK REQUIRED',flush=True)
    kr=[r for r in rows if 'Qwen' not in m['cast'][r['speaker_id']]['synthesis']] if a.engine!='qwen' else []
    if kr:
        import onnxruntime as ort
        from kokoro_onnx import Kokoro
        opts=ort.SessionOptions();opts.intra_op_num_threads=4;opts.inter_op_num_threads=1
        session=ort.InferenceSession(str(a.models/'kokoro-v1.0.onnx'),sess_options=opts,providers=['CPUExecutionProvider'])
        model=Kokoro.from_session(session,str(a.models/'voices-v1.0.bin'))
        original=zipfile.ZipFile(a.original_pack) if a.original_pack else None
        for r in kr:
            c=m['cast'][r['speaker_id']];meta=r['speech_render'];fragment=r['kind']=='fragment'
            if r['reuse_line_id'] and original:
                for field in ('audio_file','master_file'):
                    path=a.output/r[field];path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(original.read(meta['source_paths'][field]))
                digest=hashlib.sha256((a.output/r['audio_file']).read_bytes()).hexdigest();assert digest==meta['source_sha256']
                hashes.append({'id':r['id'],'sha256':digest,'reference_sha256':meta['sha256'],'matches_reference':True});continue
            if r['reuse_line_id']:
                prior=meta['source_recipe'];raw,sr=model.create(prior['spoken_text'],voice=prior['preset'],speed=prior['speed'],lang=prior['language'])
                x=resample_poly(master_kokoro(raw,sr,True),2,1).astype('float32');y=process(x,48000,r['filter_profile'],r['reuse_line_id'],True)
            else:
                text=meta.get('carrier_phonemes',meta['render_input']);raw,sr=model.create(text,voice=meta['voice'],speed=meta['speed'],lang=meta['language'],is_phonemes=meta.get('is_phonemes',False))
                if 'carrier_phonemes' in meta:raw=raw[int(meta['crop_start']*sr):int(meta['crop_end']*sr)]
                if fragment:raw=clean_fragment(raw,sr)
                x=resample_poly(master_kokoro(raw,sr,fragment),2,1).astype('float32');y=process(x,48000,r['filter_profile'],r['id'],fragment)
            save(r,x,y)
        if original:original.close()
        del model,session;gc.collect()
    qr=[r for r in rows if 'Qwen' in m['cast'][r['speaker_id']]['synthesis']] if a.engine!='kokoro' else []
    if qr:
        import torch
        from qwen_tts import Qwen3TTSModel
        torch.set_num_threads(6);torch.set_num_interop_threads(1)
        model=Qwen3TTSModel.from_pretrained(str(a.models.resolve()),device_map='cpu',dtype=torch.bfloat16,attn_implementation='sdpa',local_files_only=True)
        batches={}
        for r in qr:
            meta=r['speech_render'];key=(meta['seed'],tuple(meta['batch_ids']))
            batches.setdefault(key,[]).append(r)
        for (seed,ids),selected in batches.items():
            meta=selected[0]['speech_render'];torch.manual_seed(seed)
            directions=meta.get('batch_instructions') or [m['cast'][idx[id]['speaker_id']]['direction']+' Read the provided short statement exactly, with crisp emphasis on the final English letter name or number. Do not repeat any words.' for id in ids]
            speakers=[m['cast'][idx[id]['speaker_id']]['voice'].title() for id in ids]
            raws,sr=model.generate_custom_voice(text=meta['batch_texts'],language='English',speaker=speakers,instruct=directions,**meta['generation_options'])
            lookup={r['id']:r for r in selected}
            for id,raw in zip(ids,raws):
                if id not in lookup:continue
                r=lookup[id];recipe=r['speech_render']
                # Carrier input to the crop pass was PCM16. Preserve that quantization.
                buf=io.BytesIO();sf.write(buf,raw,sr,format='WAV',subtype='PCM_16');buf.seek(0);raw,_=sf.read(buf,dtype='float32')
                raw=raw[int(recipe['crop_start']*sr):int(recipe['crop_end']*sr)]
                x=resample_poly(master(clean_fragment(raw,sr),sr),2,1).astype('float32');y=process(x,48000,r['filter_profile'],id,True)
                save(r,x,y)
    (a.output/'replay-checks.json').write_text(json.dumps({'reference_hashes_are_frozen':True,'files':hashes,'mismatch_count':sum(not h['matches_reference'] for h in hashes)},indent=2)+'\n')
    if not a.only and a.engine=='all':
        (a.output/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
    print('Replay completed. Hash differences require speech review before use.',flush=True)

if __name__=='__main__':main()
