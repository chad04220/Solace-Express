#!/usr/bin/env python3
"""Pack natural-colour CC0 environment scans, independent of legacy aircraft materials.

python tools/pack_environment_scans.py --cache /path/outside/repository
Downloads seven official Poly Haven 2K scan sets. Produces matched 512 px
(environment/) and 2048 px (high/) c/n/m JPGs plus a reproducibility manifest.
No procedural colour, roughness or normal-strength matching is performed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request

import numpy as np
from PIL import Image

ASSETS = [(0, 'grass', 'leafy_grass', 2.0),
          (5, 'asphalt', 'asphalt_04', 4.04),
          (8, 'concrete', 'concrete_floor_worn_001', 3.0),
          (12, 'brick', 'red_brick', 1.4),
          (11, 'plaster', 'painted_plaster_wall', 2.0),
          (9, 'tiles', 'clay_roof_tiles_02', 2.5),
          (25, 'bark', 'bark_brown_02', 1.0)]
MAPS = ('Diffuse', 'nor_dx', 'arm', 'Displacement')
USER_AGENT = 'SolaceExpress-material-packer/1.0 (CC0 offline material preparation)'


def fetch(url, path):
    if not path.exists():
        request = urllib.request.Request(url, headers={'User-Agent': USER_AGENT})
        with urllib.request.urlopen(request, timeout=120) as response:
            data = response.read()
        tmp = path.with_suffix(path.suffix + '.part')
        tmp.write_bytes(data)
        tmp.replace(path)
    return path


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_map(path):
    image = Image.open(path)
    if image.mode in ('I', 'I;16', 'I;16B'):
        return np.asarray(image, dtype=np.float32)[..., None] / 65535.0
    return np.asarray(image.convert('RGB'), dtype=np.float32) / 255.0


def srgb_to_linear(colour):
    return np.where(colour <= .04045, colour / 12.92, ((colour + .055) / 1.055) ** 2.4)


def resize_linear(array, size):
    if array.shape[:2] == (size, size):
        return array.copy()
    return np.stack([np.asarray(Image.fromarray(array[..., c]).resize((size, size), Image.Resampling.BOX))
                     for c in range(array.shape[-1])], axis=-1)


def save_jpg(path, array, quality):
    Image.fromarray(np.uint8(np.clip(array, 0, 1) * 255 + .5)).save(
        path, quality=quality, subsampling=0, optimize=True)


def pack(cache, out):
    cache.mkdir(parents=True, exist_ok=True)
    manifest = {'schema': 2, 'provider': 'Poly Haven', 'license': 'CC0-1.0',
                'license_url': 'https://polyhaven.com/license',
                'license_text_url': 'https://creativecommons.org/publicdomain/zero/1.0/legalcode',
                'photographic_source_policy': 'https://docs.polyhaven.com/en/technical-standards/textures',
                'packing': {'albedo': 'sqrt(sRGB_to_linear(source)); shader squares RGB',
                            'normal': 'DirectX tangent X,Y in R,G; displacement in B',
                            'material': 'source roughness in R; source ambient occlusion in G',
                            'filter': 'linear-light box filter; filtered normal vectors renormalized',
                            'palette_matching': False, 'normal_gain': 1.0},
                'assets': []}
    for layer, name, aid, metres in ASSETS:
        files_path = fetch('https://api.polyhaven.com/files/' + aid, cache / (aid + '_files.json'))
        info_path = fetch('https://api.polyhaven.com/info/' + aid, cache / (aid + '_info.json'))
        files, info = json.loads(files_path.read_text()), json.loads(info_path.read_text())
        dimensions = info.get('dimensions', [])
        if len(dimensions) != 2 or any(abs(d / 1000.0 - metres) > .005 for d in dimensions):
            raise RuntimeError('Source dimensions differ from selected physical tile: ' + aid)
        metadata = [{'kind': kind, 'url': 'https://api.polyhaven.com/' + kind + '/' + aid,
                     'cache_file': path.name, 'bytes': path.stat().st_size, 'sha256': sha256(path)}
                    for kind, path in [('files', files_path), ('info', info_path)]]
        maps, sources = {}, []
        for key in MAPS:
            source = files[key]['2k']['png']
            url = source['url']
            if not url.startswith('https://dl.polyhaven.org/file/ph-assets/'):
                raise RuntimeError('Unexpected asset download host: ' + url)
            path = fetch(url, cache / (aid + '_' + key + '.png'))
            raw = path.read_bytes()
            if len(raw) != source['size'] or hashlib.md5(raw).hexdigest() != source['md5']:
                raise RuntimeError('Provider size/checksum mismatch: ' + str(path))
            maps[key] = read_map(path)
            if maps[key].shape[:2] != (2048, 2048):
                raise RuntimeError('Expected actual 2048 px source: ' + str(path))
            sources.append({'map': key, 'url': url, 'cache_file': path.name, 'sha256': sha256(path), 'provider_md5': source['md5'], 'bytes': path.stat().st_size})
        colour = srgb_to_linear(maps['Diffuse'])
        normals = maps['nor_dx'] * 2 - 1
        arm = maps['arm']
        entry = {'layer': layer, 'name': name, 'asset': aid, 'page': 'https://polyhaven.com/a/' + aid,
                 'authors': info.get('authors', {}), 'tile_metres': metres,
                 'source_metadata': metadata,
                 'source_dimensions_mm': info.get('dimensions'), 'source_files': sources,
                 'mean_linear_albedo': colour.mean(axis=(0, 1), dtype=np.float64).tolist(),
                 'mean_roughness': float(arm[..., 1].mean(dtype=np.float64)), 'outputs': []}
        for size, folder in [(512, 'environment'), (2048, 'high')]:
            target = out / folder
            target.mkdir(parents=True, exist_ok=True)
            c = resize_linear(colour, size)
            n = resize_linear(normals, size)
            n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)
            h = resize_linear(maps['Displacement'][..., :1], size)
            material = resize_linear(arm, size)
            packed = {'c': (np.sqrt(c), 92),
                      'n': (np.concatenate([n[..., :2] * .5 + .5, h], axis=-1), 96),
                      'm': (np.stack([material[..., 1], material[..., 0], np.zeros((size, size))], axis=-1), 92)}
            for suffix, (array, quality) in packed.items():
                path = target / ('%02d_%s_%s.jpg' % (layer, name, suffix))
                save_jpg(path, array, quality)
                entry['outputs'].append({'path': path.relative_to(out).as_posix(), 'size': size,
                                         'bytes': path.stat().st_size, 'sha256': sha256(path)})
            print('%02d %s: %dpx, one %.2fm source tile, unchanged source colour/roughness' %
                  (layer, aid, size, metres), flush=True)
        manifest['assets'].append(entry)
    (out / 'environment' / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', required=True, type=Path, help='download cache outside the source repository')
    parser.add_argument('--out', type=Path, default=Path(__file__).resolve().parents[1] / 'assets/materials')
    args = parser.parse_args()
    pack(args.cache, args.out)


if __name__ == '__main__':
    main()
