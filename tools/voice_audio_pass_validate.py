#!/usr/bin/env python3
"""Offline, unprompted ASR and signal checks for audio_pass.json.
Usage: python tools/voice_audio_pass_validate.py /path/to/faster-whisper-small.en
The ASR model/revision is pinned in voice_lesson_helpers/asr-model.json.
This is an automated intelligibility screen, not human listening approval.
"""
import os
os.environ['ORT_DISABLE_TELEMETRY']='1'
from voice_lesson_helpers import offline_guard
import hashlib,json,re,sys,wave
from pathlib import Path
import numpy as np
from scipy.signal import resample_poly
from math import gcd
from faster_whisper import WhisperModel
root=Path(__file__).resolve().parents[1]/'assets/voice'
m=json.loads((root/'audio_pass.json').read_text())
model=WhisperModel(sys.argv[1],device='cpu',compute_type='int8',cpu_threads=3,local_files_only=True)
def words(s):
 s=s.lower().replace('mach 1','mach one').replace('real-time','real time')
 s=s.replace('auto-land','autoland').replace('auto land','autoland').replace('check ride','checkride').replace('breakaway','break away')
 return re.findall(r"[a-z0-9]+(?:'[a-z]+)?",s)
def edits(a,b):
 row=list(range(len(b)+1))
 for i,x in enumerate(a):
  prev=row;row=[i+1]
  for j,y in enumerate(b):row.append(min(row[-1]+1,prev[j+1]+1,prev[j]+(x!=y)))
 return row[-1]
report=[]
for row in m['lines']:
 p=root/row['file']
 with wave.open(str(p),'rb') as w:
  sr=w.getframerate();assert w.getnchannels()==1 and w.getsampwidth()==2
  x=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(np.float32)/32768
 assert hashlib.sha256(p.read_bytes()).hexdigest()==row['sha256']
 divisor=gcd(sr,16000)
 pcm=resample_poly(x,16000//divisor,sr//divisor).astype(np.float32)
 segments,info=model.transcribe(pcm,language='en',beam_size=5,condition_on_previous_text=False,vad_filter=False,temperature=0)
 heard=' '.join(s.text.strip() for s in segments)
 expected=words(row['text']);got=words(heard);err=edits(expected,got)
 item=dict(id=row['id'],expected=row['text'],heard=heard,word_edits=err,words=len(expected),peak=float(np.max(np.abs(x))),rms=float(np.sqrt(np.mean(x*x))),seconds=round(len(x)/sr,3),sha256=row['sha256'])
 report.append(item);print(row['id'],err,'/',len(expected),heard,flush=True)
result=dict(method='Unprompted local faster-whisper small.en, int8 CPU, beam 5; no text hints',model=json.loads((Path(__file__).parent/'voice_lesson_helpers/asr-model.json').read_text()),human_listening_approved=False,clips=report)
(root/'audio_pass_validation.json').write_text(json.dumps(result,indent=2)+'\n')
m['validation']['independent_asr']=True;m['validation']['report']='audio_pass_validation.json'
(root/'audio_pass.json').write_text(json.dumps(m,indent=2)+'\n')
print('Total:',sum(r['word_edits'] for r in report),'edits /',sum(r['words'] for r in report),'words',flush=True)
