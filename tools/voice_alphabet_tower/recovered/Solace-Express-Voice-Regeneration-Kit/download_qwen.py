"""Download only public model files, without loading game content."""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parent
MODEL = 'Qwen/Qwen3-TTS-12Hz-1.7B-CustomVoice'

def main():
    metadata = json.loads((ROOT/'model-metadata.json').read_text())
    revision = metadata['sha']
    def fetch(row):
        name = row['rfilename']
        if name.startswith('.'):
            return
        target = ROOT/'models'/name
        target.parent.mkdir(parents=True, exist_ok=True)
        expected = row.get('lfs', {}).get('sha256')
        if target.exists() and target.stat().st_size == row['size']:
            if not expected or hashlib.file_digest(target.open('rb'), 'sha256').hexdigest() == expected:
                return
        request = urllib.request.Request(f'https://huggingface.co/{MODEL}/resolve/{revision}/{name}', headers={'User-Agent':'Solace-voice-evaluation/1.0'})
        temporary = target.with_name(target.name+'.part')
        digest = hashlib.sha256()
        with urllib.request.urlopen(request, timeout=120) as response, temporary.open('wb') as output:
            while chunk := response.read(2**20):
                output.write(chunk); digest.update(chunk)
        assert temporary.stat().st_size == row['size'], name
        assert not expected or digest.hexdigest() == expected, name
        temporary.replace(target)
        print('Verified public file', name, target.stat().st_size, flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(fetch, metadata['siblings']))
    print('Public model download complete:', revision, flush=True)

if __name__ == '__main__':
    main()
