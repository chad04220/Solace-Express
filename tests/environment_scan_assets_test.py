#!/usr/bin/env python3
"""Audit photographed maps; --source-only requires only Python's standard library.

Default mode adds colour/roughness/normal pixel checks and needs NumPy + Pillow.
Add --source-cache /path/to/cache to verify original downloaded PNGs and API metadata too.
No mode uses network access or installs dependencies.
"""
import argparse
import hashlib
import json
import math
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-only', action='store_true', help='stdlib hashes, provenance and JPEG dimensions only')
parser.add_argument('--source-cache', type=Path, help='optional external cache: audit original PNG and API hashes offline')
args = parser.parse_args()
if not args.source_only:
    try:
        import numpy as np
        from PIL import Image
    except ImportError:
        raise SystemExit('Full pixel audit needs NumPy and Pillow; --source-only has no third-party dependencies.')

ROOT = Path(__file__).resolve().parents[1]
MATERIALS = ROOT / 'assets/materials'
manifest = json.loads((MATERIALS / 'environment/manifest.json').read_text())
checks = 0


def check(value, label):
    global checks
    checks += 1
    assert value, label


def jpeg_shape(data):
    """Read baseline/progressive JPEG SOF without decoding or importing Pillow."""
    if data[:2] != b'\xff\xd8':
        raise ValueError('Not a JPEG')
    offset = 2
    sof = {0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF}
    while offset < len(data):
        if data[offset] != 0xFF:
            raise ValueError('Bad JPEG marker')
        while offset < len(data) and data[offset] == 0xFF:
            offset += 1
        if offset >= len(data):
            break
        marker = data[offset]
        offset += 1
        if marker in (0xD9, 0xDA):
            break
        if marker == 0x01 or 0xD0 <= marker <= 0xD8:
            continue
        if offset + 2 > len(data):
            break
        length = int.from_bytes(data[offset:offset + 2], 'big')
        if length < 2 or offset + length > len(data):
            raise ValueError('Truncated JPEG segment')
        if marker in sof:
            if length < 8:
                raise ValueError('Short JPEG SOF')
            bits = data[offset + 2]
            height = int.from_bytes(data[offset + 3:offset + 5], 'big')
            width = int.from_bytes(data[offset + 5:offset + 7], 'big')
            components = data[offset + 7]
            return width, height, components, bits
        offset += length
    raise ValueError('JPEG dimensions not found')


workflow = (ROOT / '.github/workflows/build.yml').read_text()
check('Copy-Item -Recurse assets/materials/*' in workflow, 'Windows package includes high/environment subdirectories')
check(manifest['license'] == 'CC0-1.0', 'CC0 provenance')
check(manifest['packing']['palette_matching'] is False, 'never recolour photographs to the procedural palette')
layers = [0, 5, 8, 12, 11, 9, 25]
check([a['layer'] for a in manifest['assets']] == layers, 'seven selected environment layers in shader order')
check(manifest['schema'] == 2, 'metadata-backed provenance schema')
check(manifest['photographic_source_policy'] == 'https://docs.polyhaven.com/en/technical-standards/textures',
      'official photo-based texture policy recorded')
header = (ROOT / 'src/materials.h').read_text()
match = re.search(r'kEnvMaterialSources\[kEnvMatLayers\]\s*=\s*\{([^}]+)\}', header)
check(match is not None and [int(v.strip()) for v in match[1].split(',')] == layers,
      'CPU array upload order agrees with the asset manifest')
check(re.search(r'kEnvMatLayers\s*=\s*7\b', header) is not None, 'bounded seven-layer GPU array')
for asset in manifest['assets']:
    check(asset['page'].startswith('https://polyhaven.com/a/'), 'official asset source')
    check(bool(asset['authors']), 'source artists recorded')
    check(len(asset['source_files']) == 4, 'diffuse, DX normal, ARM and displacement source hashes')
    check(len(asset['outputs']) == 6, '512 fallback and actual 2K each have c/n/m maps')
    check(math.isfinite(asset['tile_metres']) and asset['tile_metres'] > 0, 'documented finite physical tile scale')
    check(all(math.isfinite(v) and 0 <= v <= 1 for v in asset['mean_linear_albedo']), 'finite source linear albedo')
    check(len(asset['source_dimensions_mm']) == 2 and
          all(abs(d / 1000 - asset['tile_metres']) < .005 for d in asset['source_dimensions_mm']),
          'sampling scale matches measured source dimensions, without physical stretching')
    check([m['kind'] for m in asset['source_metadata']] == ['files', 'info'], 'original API records identified')
    for metadata in asset['source_metadata']:
        check(metadata['url'] == 'https://api.polyhaven.com/' + metadata['kind'] + '/' + asset['asset'],
              'official per-asset API metadata URL')
        check(len(metadata['sha256']) == 64 and metadata['bytes'] > 0, 'original API metadata hash recorded')
        if args.source_cache:
            raw = (args.source_cache / metadata['cache_file']).read_bytes()
            check(len(raw) == metadata['bytes'] and hashlib.sha256(raw).hexdigest() == metadata['sha256'],
                  'cached original API record agrees with manifest')
            record = json.loads(raw)
            if metadata['kind'] == 'files':
                source_records = record
            else:
                check(record['authors'] == asset['authors'] and record['dimensions'] == asset['source_dimensions_mm'],
                      'credits and dimensions agree with the original API metadata')
    for source in asset['source_files']:
        check(source['url'].startswith('https://dl.polyhaven.org/file/ph-assets/'), 'official download source')
        check(len(source['provider_md5']) == 32, 'provider checksum verified during packing')
        check(len(source['sha256']) == 64 and all(c in '0123456789abcdef' for c in source['sha256']), 'source checksum recorded')
        if args.source_cache:
            source_path = args.source_cache / source['cache_file']
            raw = source_path.read_bytes()
            check(len(raw) == source['bytes'], 'cached source byte count')
            check(hashlib.md5(raw).hexdigest() == source['provider_md5'], 'cached source provider MD5')
            check(hashlib.sha256(raw).hexdigest() == source['sha256'], 'cached source SHA-256')
            check(raw[:8] == b'\x89PNG\r\n\x1a\n' and
                  (int.from_bytes(raw[16:20], 'big'), int.from_bytes(raw[20:24], 'big')) == (2048, 2048),
                  'actual source PNG dimensions are 2048 square')
            provider = source_records[source['map']]['2k']['png']
            check((provider['url'], provider['size'], provider['md5']) ==
                  (source['url'], source['bytes'], source['provider_md5']), 'source matches original provider file record')
            if not args.source_only and source['map'] in ('Diffuse', 'arm'):
                pixels = np.asarray(Image.open(source_path).convert('RGB'), dtype=np.float32) / 255
                if source['map'] == 'Diffuse':
                    linear = np.where(pixels <= .04045, pixels / 12.92, ((pixels + .055) / 1.055) ** 2.4)
                    check(np.max(np.abs(linear.mean(axis=(0, 1), dtype=np.float64) - np.array(asset['mean_linear_albedo']))) < 1e-6,
                          'recorded linear albedo measured from verified source photograph')
                else:
                    check(abs(float(pixels[..., 1].mean(dtype=np.float64)) - asset['mean_roughness']) < 1e-6,
                          'recorded roughness measured from verified source ARM')
    for item in asset['outputs']:
        path = MATERIALS / item['path']
        check(path.exists(), str(path))
        data = path.read_bytes()
        check(len(data) == item['bytes'], 'packed byte count')
        check(hashlib.sha256(data).hexdigest() == item['sha256'], 'packed file hash')
        check(jpeg_shape(data) == (item['size'], item['size'], 3, 8), 'actual 8-bit three-channel JPEG dimensions')
        check(item['size'] in (512, 2048), 'bounded delivered map resolution')
        if args.source_only:
            continue
        image = Image.open(path)
        pixels = np.asarray(image, dtype=np.float64) / 255
        check(np.isfinite(pixels).all(), 'finite packed texels')
        if path.stem.endswith('_c'):
            mean = (pixels ** 2).mean(axis=(0, 1))
            check(np.max(np.abs(mean - np.array(asset['mean_linear_albedo']))) < .005,
                  'mean source linear albedo preserved through encoding/filtering')
        if path.stem.endswith('_m'):
            check(abs(float(pixels[..., 0].mean()) - asset['mean_roughness']) < .006, 'source roughness retained')
            check(float(pixels[..., 2].mean()) <= .012, 'unused blue channel stays near zero after JPEG')
        if path.stem.endswith('_n'):
            slope2 = np.sum((pixels[..., :2] * 2 - 1) ** 2, axis=-1)
            check(float(np.mean(slope2 > 1.12)) < .001, 'normal XY remains a valid hemisphere (JPEG tolerance)')
    print('%s: matched 512/2048 maps, verified packed hashes and recorded source hashes' % asset['asset'])
check(sum(i['bytes'] for a in manifest['assets'] for i in a['outputs']) < 80 * 1024 * 1024,
      'all 42 packed runtime images fit 80 MiB source budget')
print('environment_scan_assets_test: %d %s checks passed' % (checks, 'source-only' if args.source_only else 'full'))
