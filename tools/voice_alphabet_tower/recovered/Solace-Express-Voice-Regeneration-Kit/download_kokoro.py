import hashlib
import json
from pathlib import Path
import urllib.request

root = Path(__file__).resolve().parent
models = root / 'models'
models.mkdir(exist_ok=True)
assets = json.loads((root / 'kokoro-model-release.json').read_text())['assets']
for name in ['kokoro-v1.0.onnx', 'voices-v1.0.bin']:
    asset = next(a for a in assets if a['name'] == name)
    output = models / name
    expected = asset['digest'].split(':', 1)[1]
    if output.exists() and hashlib.sha256(output.read_bytes()).hexdigest() == expected:
        print('Already verified', name, flush=True)
        continue
    temp = output.with_suffix(output.suffix + '.download')
    print('Downloading', name, asset['size'], 'bytes', flush=True)
    digest = hashlib.sha256()
    done = 0
    with urllib.request.urlopen(asset['browser_download_url'], timeout=40) as source, temp.open('wb') as target:
        while chunk := source.read(1024 * 1024):
            target.write(chunk)
            digest.update(chunk)
            done += len(chunk)
    assert done == asset['size'], (name, done, asset['size'])
    assert digest.hexdigest() == expected, (name, digest.hexdigest(), expected)
    temp.replace(output)
    print('Verified', name, digest.hexdigest(), flush=True)
