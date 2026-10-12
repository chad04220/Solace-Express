#!/usr/bin/env python3
"""Generate Somnium Volandi's split-wing/horizon identity (SVG, PNG, Windows ICO).
Pillow is the only dependency. All outputs come from these vector coordinates;
small taskbar frames are rendered individually with 4x coverage antialiasing.
"""
from pathlib import Path
from PIL import Image, ImageDraw

ASSETS = Path(__file__).resolve().parents[1] / 'assets'
SIZES = [16, 24, 32, 48, 64, 128, 256]
LEFT = [(123, 65), (113, 119), (42, 157), (42, 172), (106, 151), (123, 199), (116, 137)]
RIGHT = [(256-x, y) for x, y in LEFT]
GOLD = (232, 188, 120, 255)
WHITE = (244, 243, 232, 255)

def render(size):
    scale = size * 4 / 256
    n = size * 4
    out = Image.new('RGBA', (n, n))
    d = ImageDraw.Draw(out)
    def box(v): return tuple(round(x * scale) for x in v)
    d.rounded_rectangle(box((8, 8, 248, 248)), radius=round(50*scale), fill=GOLD)
    mask = Image.new('L', (n, n))
    ImageDraw.Draw(mask).rounded_rectangle(box((12, 12, 244, 244)), radius=round(46*scale), fill=255)
    bg = Image.new('RGBA', (n, n))
    bd = ImageDraw.Draw(bg)
    for y in range(n):
        f = y/n
        bd.line((0, y, n, y), fill=tuple(round(a+(b-a)*f) for a,b in zip((27,40,59),(7,15,29)))+(255,))
    out.paste(bg, (0, 0), mask)
    d = ImageDraw.Draw(out)
    d.arc(box((38, 38, 218, 218)), 205, 335, fill=GOLD, width=max(1,round(3*scale)))
    d.arc(box((38, 38, 218, 218)), 18, 162, fill=(81,120,133,255), width=max(1,round(2*scale)))
    for wing in (LEFT, RIGHT):
        d.polygon([(round(x*scale),round(y*scale)) for x,y in wing],fill=WHITE)
    d.line(box((128, 67, 128, 194)),fill=GOLD,width=max(1,round(3*scale)))
    d.polygon([(round(x*scale),round(y*scale)) for x,y in [(128,28),(132,36),(128,44),(124,36)]],fill=GOLD)
    d.line(box((72, 219, 184, 219)), fill=GOLD, width=max(1,round(2*scale)))
    return out.resize((size,size),Image.Resampling.LANCZOS)

def main():
    def points(p): return ' '.join(f'{x},{y}' for x,y in p)
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">
  <title>Somnium Volandi — the dream of flight</title>
  <desc>A platinum split wing rises through a brass orbital horizon on midnight blue.</desc>
  <defs><linearGradient id="night" x2="0" y2="1"><stop stop-color="#1b283b"/><stop offset="1" stop-color="#070f1d"/></linearGradient></defs>
  <rect x="8" y="8" width="240" height="240" rx="50" fill="#e8bc78"/>
  <rect x="12" y="12" width="232" height="232" rx="46" fill="url(#night)"/>
  <path d="M46.4 90 A90 90 0 0 1 209.6 90" fill="none" stroke="#e8bc78" stroke-width="3"/>
  <path d="M213.6 155.8 A90 90 0 0 1 42.4 155.8" fill="none" stroke="#517885" stroke-width="2"/>
  <polygon points="{points(LEFT)}" fill="#f4f3e8"/><polygon points="{points(RIGHT)}" fill="#f4f3e8"/>
  <path d="M128 67V194 M72 219H184" fill="none" stroke="#e8bc78" stroke-width="2.5"/>
  <path d="M128 28L132 36L128 44L124 36Z" fill="#e8bc78"/>
</svg>'''
    (ASSETS/'icon.svg').write_text(svg+'\n')
    frames=[render(n) for n in SIZES]
    frames[-1].save(ASSETS/'icon.ico',sizes=[(n,n) for n in SIZES],append_images=frames[:-1])
    render(512).save(ASSETS/'icon.png')
    ico=Image.open(ASSETS/'icon.ico')
    assert ico.ico.sizes()=={(n,n) for n in SIZES}
    for n in SIZES:
        assert any(0<a<255 for a in ico.ico.getimage((n,n)).getchannel('A').getdata())
    print('Generated Somnium Volandi SVG, 512px PNG and seven antialiased ICO frames')
if __name__=='__main__': main()
