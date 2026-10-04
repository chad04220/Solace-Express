"""Download and hash public English recognizer files before the offline speech screen."""
from pathlib import Path
import concurrent.futures,hashlib,json,urllib.request
ROOT=Path(__file__).resolve().parent
MODEL='Systran/faster-whisper-small.en'
with urllib.request.urlopen('https://huggingface.co/api/models/'+MODEL+'?blobs=true',timeout=30) as r:meta=json.load(r)
dest=ROOT/'work/asr-model';dest.mkdir(parents=True,exist_ok=True)
rows=[r for r in meta['siblings'] if r['rfilename'] in ('config.json','model.bin','tokenizer.json','vocabulary.txt')]
assert len(rows)==4
def fetch(row):
 target=dest/row['rfilename'];expected=row.get('lfs',{}).get('sha256')
 if target.exists() and target.stat().st_size==row['size']:
  if not expected or hashlib.sha256(target.read_bytes()).hexdigest()==expected:return
 temp=target.with_suffix('.part');digest=hashlib.sha256()
 with urllib.request.urlopen('https://huggingface.co/'+MODEL+'/resolve/'+meta['sha']+'/'+row['rfilename'],timeout=60) as r,temp.open('wb') as out:
  while b:=r.read(1024*1024):out.write(b);digest.update(b)
 assert temp.stat().st_size==row['size'];assert not expected or expected==digest.hexdigest()
 temp.replace(target);print('Verified recognizer file',row['rfilename'],target.stat().st_size,flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:list(pool.map(fetch,rows))
(ROOT/'work/asr-model-metadata.json').write_text(json.dumps({'model':MODEL,'revision':meta['sha'],'files':rows},indent=2)+'\n')
