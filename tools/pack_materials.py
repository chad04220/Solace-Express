#!/usr/bin/env python3
"""Packs scanned CC0 PBR textures (Poly Haven, ambientCG) into the game's material layers: assets/materials.

Each layer the manifest below names becomes three 512x512 JPEGs the renderer loads in place of the procedural layer
(Renderer::genMaterials, materials.cpp):
  NN_name_c.jpg  albedo, stored as the square root of linear (the shaders square it)
  NN_name_n.jpg  normal x, y (DirectX's: green down) and height
  NN_name_m.jpg  roughness (red) and ambient occlusion (green)
A layer the manifest leaves out stays procedural.

The scan is fitted to the tile the shaders lay the layer at (repeated a whole number of times, then box-filtered, so
it stays seamless), and its average colour, roughness and relief are matched to the procedural layer it replaces:
the same palette everywhere the world was tuned with it, the detail of a photograph. The procedural layers come from
the material_dump tool (cmake --build build --target material_dump; build/material_dump DIR).

  tools/pack_materials.py --old DIR [--out assets/materials] [--cache DIR] [--only 0,5,19]
Needs Python 3 with numpy and Pillow, and curl; downloads are cached (default ~/.cache/solace-materials).
"""
import argparse, io, json, os, subprocess, sys, zipfile
import numpy as np
from PIL import Image

TS = 512
NAMES = ["grass", "forest", "rock", "sand", "snow", "asphalt", "gravel", "dirt", "concrete", "tiles", "slate", "plaster",
         "brick", "leaves", "needles", "paint", "metal", "rubber", "plastic", "fabric", "carpet", "leather", "corrugated",
         "crop", "wheat", "bark", "planks", "litter", "shingles", "siding"]

# layer: (source "provider:id", the scan's real width in metres, the tile the shaders lay the layer at in metres,
#         options: stretch - fit the whole scan to the tile whatever its size (a rock face read as a cliff);
#                  colour - keep this much of the scan's own colour variation about the matched mean (0..1, default 1);
#                  contrast - scale its variation about the mean (default 1: a dark scan brought up to a light layer's
#                             mean keeps a dark scan's contrast, which on white siding is too much);
#                  bump - relief relative to the procedural layer's (default 1);
#                  flip - upside down: the walls' triplanar sample runs v up the wall, so a wall layer whose look
#                         has an up (lap siding: each board's shadow line under the one above) is stored flipped)
MANIFEST = {
    0:  ("acg:Grass004", 2.0, 5.0, {}),
    2:  ("ph:rock_face_03", 2.6, 18.0, {"stretch": True, "bump": 1.0}),
    3:  ("ph:aerial_beach_01", 30.0, 6.0, {}),
    4:  ("acg:Snow006", 2.0, 8.0, {}),
    5:  ("ph:asphalt_04", 4.0, 6.5, {}),
    6:  ("acg:Gravel022", 1.0, 2.0, {}),
    7:  ("ph:dry_ground_rocks", 4.0, 5.0, {}),
    8:  ("ph:concrete_floor_worn_001", 3.0, 3.5, {}),
    9:  ("ph:clay_roof_tiles_02", 2.5, 3.0, {"contrast": 0.5}),
    10: ("ph:roof_slates_03", 3.0, 3.0, {}),
    11: ("acg:PaintedPlaster017", 2.0, 2.5, {}),
    12: ("ph:red_brick", 1.3, 1.4, {}),
    16: ("acg:Metal009", 1.0, 0.8, {}),
    18: ("acg:Plastic012A", 1.0, 0.3, {"stretch": True}),
    19: ("acg:Fabric030", 0.5, 0.3, {"stretch": True, "bump": 0.6}),
    20: ("acg:Carpet012", 0.5, 0.15, {"stretch": True}),
    21: ("acg:Leather030", 0.5, 0.3, {"stretch": True}),
    22: ("ph:corrugated_iron", 1.1, 2.0, {}),
    25: ("ph:bark_brown_02", 0.9, 0.9, {}),
    26: ("ph:weathered_brown_planks", 1.7, 2.0, {"contrast": 0.7}),
    27: ("ph:forest_floor", 2.1, 4.0, {}),
    28: ("ph:grey_roof_tiles_02", 1.5, 3.0, {"contrast": 0.7}),
    29: ("acg:WoodSiding008", 2.0, 3.0, {"contrast": 0.6, "flip": True}),
}


def curl(url, path):
    tmp = path + ".part"
    r = subprocess.run(["curl", "-sS", "-f", "-L", "--retry", "5", "--retry-all-errors", "--max-time", "300", "-o", tmp, url])
    if r.returncode != 0:
        raise RuntimeError("download failed: " + url)
    os.replace(tmp, path)


def load(data_or_path):
    im = Image.open(io.BytesIO(data_or_path) if isinstance(data_or_path, bytes) else data_or_path)
    if im.mode in ("I;16", "I;16B", "I"):
        a = np.asarray(im, dtype=np.float64) / 65535.0
        return a
    return np.asarray(im.convert("RGB"), dtype=np.float64) / 255.0


def fetch(src, cache):
    """The scan's maps at 1K: dict of float arrays (albedo sRGB rgb, normal dx rgb, rough, ao, disp - some may be None)."""
    prov, aid = src.split(":")
    d = os.path.join(cache, prov + "_" + aid)
    os.makedirs(d, exist_ok=True)
    maps = {}
    if prov == "ph":
        meta = os.path.join(d, "files.json")
        if not os.path.exists(meta):
            curl("https://api.polyhaven.com/files/" + aid, meta)
        files = json.load(open(meta))
        col = "Diffuse" if "Diffuse" in files else next(k for k in files if k.startswith("col"))
        want = {"col": col, "nrm": "nor_dx", "rough": "Rough", "ao": "AO", "disp": "Displacement", "arm": "arm"}
        for key, name in want.items():
            if name not in files or "1k" not in files[name]:
                continue
            f = files[name]["1k"]
            url = (f.get("jpg") or f.get("png"))["url"]
            path = os.path.join(d, key + os.path.splitext(url)[1])
            if not os.path.exists(path):
                curl(url, path)
            maps[key] = load(path)
        if "arm" in maps:   # (ambient occlusion, roughness, metalness)
            maps.setdefault("ao", maps["arm"][..., 0:1].repeat(3, -1))
            maps.setdefault("rough", maps["arm"][..., 1:2].repeat(3, -1))
    else:
        z = os.path.join(d, aid + ".zip")
        if not os.path.exists(z):
            curl("https://ambientcg.com/get?file=%s_1K-JPG.zip" % aid, z)
        zf = zipfile.ZipFile(z)
        for n in zf.namelist():
            for key, tag in (("col", "_Color."), ("nrm", "_NormalDX."), ("rough", "_Roughness."), ("ao", "_AmbientOcclusion."),
                             ("disp", "_Displacement.")):
                if tag in n:
                    maps[key] = load(zf.read(n))
    for k in list(maps):
        if maps[k].ndim == 2:
            maps[k] = maps[k][..., None].repeat(3, -1)
    if "col" not in maps or "nrm" not in maps:
        raise RuntimeError(src + ": no colour or normal map")
    return maps


def srgb_to_lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def fit(a, reps):
    """Repeat a square map reps x reps and box-filter it down to TS (a whole number of source texels a texel: seamless)."""
    n = min(a.shape[:2]); a = np.tile(a[:n, :n], (reps, reps, 1))
    k = max(1, n * reps // TS)
    if k * TS != n * reps:   # (a scan whose size isn't a multiple of TS: resampled to one first)
        a = np.stack([np.asarray(Image.fromarray(a[..., i].astype(np.float32), "F").resize((k * TS, k * TS), Image.LANCZOS))
                      for i in range(a.shape[2])], -1)
    return a.reshape(TS, k, TS, k, a.shape[2]).mean(axis=(1, 3))


def read_old(old_dir, l):
    base = os.path.join(old_dir, "%02d_%s_" % (l, NAMES[l]))
    alb = np.asarray(Image.open(base + "alb.ppm"), dtype=np.float64) / 255.0
    nrm = np.asarray(Image.open(base + "nrm.ppm"), dtype=np.float64) / 255.0
    ra = np.asarray(Image.open(base + "ra.ppm"), dtype=np.float64) / 255.0
    return alb ** 2, nrm, ra   # (albedo back to linear)


def pack(l, src, size, tile, opt, old_dir, cache, out):
    m = fetch(src, cache)
    if opt.get("flip"):   # (rows reversed: the normal's green, a slope down the image, turns over with them)
        m = {k: v[::-1].copy() for k, v in m.items()}
        m["nrm"][..., 1] = 1.0 - m["nrm"][..., 1]
    reps = 1 if opt.get("stretch") else max(1, int(round(tile / size)))
    content = tile / (reps * size)
    col = fit(srgb_to_lin(m["col"]), reps)
    nx = fit(m["nrm"][..., 0:1] * 2 - 1, reps)[..., 0]
    ny = fit(m["nrm"][..., 1:2] * 2 - 1, reps)[..., 0]
    rough = fit(m["rough"][..., 0:1], reps)[..., 0] if "rough" in m else np.full((TS, TS), 0.8)
    disp = fit(m["disp"][..., 0:1], reps)[..., 0] if "disp" in m else None
    ao = fit(m["ao"][..., 0:1], reps)[..., 0] if "ao" in m else None
    oalb, onrm, ora = read_old(old_dir, l)
    # colour: the procedural layer's mean, per channel, with the scan's variation about it
    mean_new = col.reshape(-1, 3).mean(0); mean_old = oalb.reshape(-1, 3).mean(0)
    lum = col @ np.array([0.2126, 0.7152, 0.0722]); lum_mean = lum.mean()
    k = opt.get("colour", 1.0)
    rel = col / np.maximum(mean_new, 1e-4)                       # (the scan's own variation, per channel)
    rel_l = (lum / max(lum_mean, 1e-4))[..., None]               # (its brightness variation alone)
    col = mean_old * (1 + opt.get("contrast", 1.0) * (k * rel + (1 - k) * rel_l - 1))
    col = np.clip(col, 0, 1)
    # roughness: the procedural mean, the scan's variation
    rough = np.clip(rough - rough.mean() + ora[..., 0].mean(), 0.02, 1)
    # relief: toward the procedural layer's mean slope, times bump - but no more than doubled or halved: a flat scan
    # (asphalt, concrete) raised to the generator's noise would only amplify its grain into speckle
    sl_old = np.hypot(onrm[..., 0] * 2 - 1, onrm[..., 1] * 2 - 1).mean()
    sl_new = np.hypot(nx, ny).mean()
    g = opt.get("bump", 1.0) * float(np.clip(sl_old / max(sl_new, 1e-4), 0.5, 2.0))
    nx, ny = nx * g, ny * g
    r = np.hypot(nx, ny); s = np.where(r > 0.95, 0.95 / np.maximum(r, 1e-6), 1.0); nx, ny = nx * s, ny * s
    # height: the displacement (or the albedo's brightness), stretched to 0..1
    h = disp if disp is not None else lum_from(col)
    lo, hi = np.percentile(h, 1), np.percentile(h, 99)
    h = np.clip((h - lo) / max(hi - lo, 1e-4), 0, 1)
    if ao is None:   # (as the generator does: how far a texel sits below its neighbours)
        nb = (np.roll(h, 2, 0) + np.roll(h, -2, 0) + np.roll(h, 2, 1) + np.roll(h, -2, 1)) * 0.25
        ao = np.clip(1 - (nb - h) * 2, 0, 1)
    base = os.path.join(out, "%02d_%s_" % (l, NAMES[l]))
    def save(path, rgb, q):
        Image.fromarray((np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB").save(path, quality=q, subsampling=0)
    save(base + "c.jpg", np.sqrt(col), 90)
    save(base + "n.jpg", np.stack([nx * 0.5 + 0.5, ny * 0.5 + 0.5, h], -1), 92)
    save(base + "m.jpg", np.stack([rough, ao, np.zeros_like(ao)], -1), 90)
    print("%02d %-10s %-28s x%d (content at %.2f of its size)  colour %s -> %s  relief x%.2f" %
          (l, NAMES[l], src, reps, content, np.round(mean_new, 3), np.round(mean_old, 3), g))
    return src


def lum_from(col):
    return col @ np.array([0.2126, 0.7152, 0.0722])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--old", required=True, help="the procedural layers (material_dump's output)")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "materials"))
    ap.add_argument("--cache", default=os.path.join(os.path.expanduser("~"), ".cache", "solace-materials"))
    ap.add_argument("--only", default="", help="comma-separated layer numbers")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True); os.makedirs(a.cache, exist_ok=True)
    only = {int(x) for x in a.only.split(",") if x}
    for l, (src, size, tile, opt) in sorted(MANIFEST.items()):
        if only and l not in only:
            continue
        pack(l, src, size, tile, opt, a.old, a.cache, a.out)


if __name__ == "__main__":
    main()
