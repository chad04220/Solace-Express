#!/usr/bin/env python3
"""Pack natural-colour CC0 environment scans, independent of legacy aircraft materials.

python tools/pack_environment_scans.py --cache /path/outside/repository [--only 0]
Downloads seven official 2K scan sets (six Poly Haven, the grass ambientCG). Produces matched 512 px
(environment/) and 2048 px (high/) c/n/m JPGs plus a reproducibility manifest; --only repacks the
listed layers and keeps the rest of the manifest as it is.
No roughness or normal-strength matching is performed. The grass alone is colour-matched (PALETTE_MATCH).
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import urllib.request
import zipfile

import numpy as np
from PIL import Image, ImageFile

ASSETS = [(0, 'grass', 'acg:Grass004', 2.0),
          (5, 'asphalt', 'asphalt_04', 4.04),
          (8, 'concrete', 'concrete_floor_worn_001', 3.0),
          (12, 'brick', 'red_brick', 1.4),
          (11, 'plaster', 'painted_plaster_wall', 2.0),
          (9, 'tiles', 'clay_roof_tiles_02', 2.5),
          (25, 'bark', 'bark_brown_02', 1.0)]
MAPS = ('Diffuse', 'nor_dx', 'arm', 'Displacement')
# The grass's mean colour is brought to the islands' own green, the legacy layer's (the scan's variation kept about
# it), and its roughness to the legacy layer's (Grass004's map, 0.26 on average, made a lawn shine like wet tarmac):
# the owner's choice after the first photographic set, whose "Leafy Grass" is mostly fallen leaves, turned every
# meadow on the islands khaki. Every other layer keeps its scan's natural colour and roughness.
PALETTE_MATCH = {0: ('00_grass_c.jpg', '00_grass_m.jpg')}
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
    image = Image.open(path) if not isinstance(path, (bytes, bytearray)) else Image.open(io.BytesIO(path))
    if image.mode == 'L':
        return np.asarray(image, dtype=np.float32)[..., None] / 255.0
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
    ImageFile.MAXBLOCK = max(ImageFile.MAXBLOCK, 1 << 26)   # (optimize=True on a 2K map outgrew the default buffer: "Suspension not allowed here")
    Image.fromarray(np.uint8(np.clip(array, 0, 1) * 255 + .5)).save(
        path, quality=quality, subsampling=0, optimize=True)


def load_polyhaven(cache, layer, name, aid, metres):
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
    entry = {'layer': layer, 'name': name, 'asset': aid, 'provider': 'Poly Haven', 'page': 'https://polyhaven.com/a/' + aid,
             'authors': info.get('authors', {}), 'tile_metres': metres,
             'source_metadata': metadata,
             'source_dimensions_mm': info.get('dimensions'), 'source_files': sources}
    return entry, maps


# An ambientCG set: its 2K JPG zip (colour, DirectX normal, roughness, ambient occlusion, displacement), the ARM map
# assembled as Poly Haven's (R occlusion, G roughness). ambientCG publishes no measured size: the tile is the set's
# documented 2 m (the legacy packer's, tools/pack_materials.py)
def load_ambientcg(cache, layer, name, aid, metres):
    url = 'https://ambientcg.com/get?file=%s_2K-JPG.zip' % aid
    zpath = fetch(url, cache / ('%s_2K-JPG.zip' % aid))
    metadata = [{'kind': 'zip', 'url': url, 'cache_file': zpath.name, 'bytes': zpath.stat().st_size, 'sha256': sha256(zpath)}]
    maps, sources = {}, []
    with zipfile.ZipFile(zpath) as z:
        raw = {}
        for part in ('Color', 'NormalDX', 'Roughness', 'AmbientOcclusion', 'Displacement'):
            member = '%s_2K-JPG_%s.jpg' % (aid, part)
            raw[part] = z.read(member)
            sources.append({'map': part, 'url': url + '#' + member, 'sha256': hashlib.sha256(raw[part]).hexdigest(),
                            'bytes': len(raw[part])})
    maps['Diffuse'] = read_map(raw['Color'])
    maps['nor_dx'] = read_map(raw['NormalDX'])
    maps['Displacement'] = read_map(raw['Displacement'])
    maps['arm'] = np.concatenate([read_map(raw['AmbientOcclusion']), read_map(raw['Roughness']),
                                  np.zeros_like(maps['Displacement'])], axis=-1)
    for key in MAPS:
        if maps[key].shape[:2] != (2048, 2048):
            raise RuntimeError('Expected actual 2048 px source: %s %s' % (aid, key))
    entry = {'layer': layer, 'name': name, 'asset': aid, 'provider': 'ambientCG', 'page': 'https://ambientcg.com/view?id=' + aid,
             'authors': {'ambientCG (Lennart Demes)': 'All'}, 'tile_metres': metres,
             'source_metadata': metadata,
             'source_dimensions_mm': [int(round(metres * 1000))] * 2, 'source_files': sources}
    return entry, maps


def pack(cache, out, only=None):
    cache.mkdir(parents=True, exist_ok=True)
    manifest_path = out / 'environment' / 'manifest.json'
    old = {a['layer']: a for a in json.loads(manifest_path.read_text())['assets']} if only and manifest_path.exists() else {}
    manifest = {'schema': 2, 'provider': 'Poly Haven, ambientCG', 'license': 'CC0-1.0',
                'license_url': 'https://polyhaven.com/license',
                'license_text_url': 'https://creativecommons.org/publicdomain/zero/1.0/legalcode',
                'photographic_source_policy': 'https://docs.polyhaven.com/en/technical-standards/textures',
                'packing': {'albedo': 'sqrt(sRGB_to_linear(source)); shader squares RGB',
                            'normal': 'DirectX tangent X,Y in R,G; displacement in B',
                            'material': 'source roughness in R; source ambient occlusion in G',
                            'filter': 'linear-light box filter; filtered normal vectors renormalized',
                            'palette_matching': {'layers': sorted(PALETTE_MATCH),
                                                 'to': 'the legacy layer\'s mean linear albedo (assets/materials), the scan\'s variation kept'},
                            'normal_gain': 1.0},
                'assets': []}
    for layer, name, aid, metres in ASSETS:
        if only and layer not in only:
            old[layer].setdefault('provider', 'Poly Haven')
            manifest['assets'].append(old[layer])
            continue
        if aid.startswith('acg:'):
            entry, maps = load_ambientcg(cache, layer, name, aid[4:], metres)
        else:
            entry, maps = load_polyhaven(cache, layer, name, aid, metres)
        colour = srgb_to_linear(maps['Diffuse'])
        normals = maps['nor_dx'] * 2 - 1
        arm = maps['arm']
        entry['mean_linear_albedo'] = colour.mean(axis=(0, 1), dtype=np.float64).tolist()
        entry['mean_roughness'] = float(arm[..., 1].mean(dtype=np.float64))
        if layer in PALETTE_MATCH:
            ref_c, ref_m = PALETTE_MATCH[layer]
            legacy = np.asarray(Image.open(out / ref_c).convert('RGB'), dtype=np.float64) / 255
            target = (legacy ** 2).mean(axis=(0, 1))   # (stored as the square root of linear)
            colour = colour * (target / np.array(entry['mean_linear_albedo'])).astype(np.float32)
            legacy_r = np.asarray(Image.open(out / ref_m).convert('RGB'), dtype=np.float64)[..., 0] / 255
            r = arm[..., 1]
            arm = arm.copy()
            arm[..., 1] = np.clip(legacy_r.mean() + (r - r.mean()) * (legacy_r.std() / max(float(r.std()), 1e-6)), 0, 1)
            entry['palette_match'] = {'reference': [ref_c, ref_m], 'packed_mean_linear_albedo': target.tolist(),
                                      'packed_mean_roughness': float(arm[..., 1].mean(dtype=np.float64)),
                                      'clipped_fraction': float((colour > 1).any(axis=-1).mean())}
        entry['outputs'] = []
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
                      'm': (np.stack([material[..., 1], material[..., 0], np.zeros((size, size))], axis=-1), 95)}   # (95: the unused blue picked up JPEG noise from a matte grass's red at 92)
            for suffix, (array, quality) in packed.items():
                path = target / ('%02d_%s_%s.jpg' % (layer, name, suffix))
                save_jpg(path, array, quality)
                entry['outputs'].append({'path': path.relative_to(out).as_posix(), 'size': size,
                                         'bytes': path.stat().st_size, 'sha256': sha256(path)})
            print('%02d %s: %dpx, one %.2fm source tile, %s' %
                  (layer, aid, size, metres, 'mean colour matched to the legacy layer' if layer in PALETTE_MATCH else 'unchanged source colour/roughness'), flush=True)
        manifest['assets'].append(entry)
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', required=True, type=Path, help='download cache outside the source repository')
    parser.add_argument('--out', type=Path, default=Path(__file__).resolve().parents[1] / 'assets/materials')
    parser.add_argument('--only', help='comma-separated layers to repack (the rest of the manifest is kept)')
    args = parser.parse_args()
    pack(args.cache, args.out, {int(v) for v in args.only.split(',')} if args.only else None)


if __name__ == '__main__':
    main()
