#!/usr/bin/env python3
"""Rebuild the original Solace badge as an antialiased vector/raster asset.

Requires Pillow. Run from any directory: python3 tools/generate_icon.py
The geometry and colors preserve the existing badge; raster frames use 4x
coverage sampling and Lanczos downsampling, including the small Windows sizes.
"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"
PLANE = [(128, 30), (140, 64), (140, 110), (230, 140), (230, 156),
         (140, 140), (136, 200), (166, 218), (166, 230), (128, 222),
         (90, 230), (90, 218), (120, 200), (116, 140), (26, 156),
         (26, 140), (116, 110), (116, 64)]
SIZES = [16, 32, 48, 64, 128, 256]


def render(size):
    scale = size * 4 / 256
    n = size * 4
    im = Image.new("RGBA", (n, n))
    d = ImageDraw.Draw(im)
    box = tuple(round(v * scale) for v in (8, 8, 248, 248))
    d.rounded_rectangle(box, radius=round(49 * scale), fill=(255, 184, 56, 255))
    mask = Image.new("L", (n, n))
    md = ImageDraw.Draw(mask)
    md.rounded_rectangle(tuple(round(v * scale) for v in (14, 14, 242, 242)),
                         radius=round(43 * scale), fill=255)
    inside = Image.new("RGBA", (n, n), (14, 28, 48, 255))
    di = ImageDraw.Draw(inside)
    for y in range(round(150 * scale), n):
        f = max(0, min(1, (y / scale - 150) / 92))
        color = tuple(round(a + (b - a) * f) for a, b in zip((255, 151, 35), (204, 96, 65))) + (255,)
        di.line((0, y, n, y), fill=color)
    im.paste(inside, (0, 0), mask)
    d = ImageDraw.Draw(im)
    d.polygon([(round(x * scale), round(y * scale)) for x, y in PLANE], fill=(245, 245, 250, 255))
    return im.resize((size, size), Image.Resampling.LANCZOS)


def main():
    points = " ".join(f"{x},{y}" for x, y in PLANE)
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">
  <title>Solace Express application icon</title>
  <defs>
    <linearGradient id="sunset" x1="0" y1="150" x2="0" y2="242" gradientUnits="userSpaceOnUse">
      <stop stop-color="#ff9723"/><stop offset="1" stop-color="#cc6041"/>
    </linearGradient>
    <clipPath id="badge"><rect x="14" y="14" width="228" height="228" rx="43"/></clipPath>
  </defs>
  <rect x="8" y="8" width="240" height="240" rx="49" fill="#ffb838"/>
  <rect x="14" y="14" width="228" height="228" rx="43" fill="#0e1c30"/>
  <path d="M14 150H242V242H14Z" fill="url(#sunset)" clip-path="url(#badge)"/>
  <polygon points="{points}" fill="#f5f5fa"/>
</svg>
'''
    (ASSETS / "icon.svg").write_text(svg)
    frames = [render(size) for size in SIZES]
    frames[-1].save(ASSETS / "icon.ico", sizes=[(n, n) for n in SIZES], append_images=frames[:-1])
    render(512).save(ASSETS / "icon.png")
    ico = Image.open(ASSETS / "icon.ico")
    assert ico.ico.sizes() == {(n, n) for n in SIZES}
    for n in SIZES:
        alpha = ico.ico.getimage((n, n)).getchannel("A")
        assert any(0 < a < 255 for a in alpha.getdata()), f"Missing edge coverage at {n}px"
    print("Generated SVG, 512px PNG, and six antialiased Windows ICO frames")


if __name__ == "__main__":
    main()
