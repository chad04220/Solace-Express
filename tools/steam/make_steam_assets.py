#!/usr/bin/env python3
"""Somnium Volandi's Steam assets, generated (Pillow only):

  steam/achievements.csv            the Steamworks upload table, read from src/profile.cpp's kAchievements
  steam/achievements/ACH_<ID>.png   every achievement's icon, earned, 256 px, in the identity's navy, gold and white
  steam/achievements/ACH_<ID>_locked.png   ... and its locked (grey) twin
  steam/store/library_logo.png      the title on transparency (1280x720), for the library's hero
  steam/store/community_icon.jpg    the identity at 184 px

Run it again after adding or renaming an achievement: tests/steam_test.cpp checks the table and the icons against
the game's own list. The capsules and screenshots are not made here: they want the game's best views on real
hardware (steam/README.md)."""
import csv
import math
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import generate_icon  # noqa: E402  (the identity: its wing, its colours)

GOLD = (232, 188, 120, 255)
WHITE = (244, 243, 232, 255)
TEAL = (81, 120, 133, 255)
NAVY_TOP, NAVY_BOTTOM = (27, 40, 59), (7, 15, 29)
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
N = 1024   # drawn at 4x, kept at 256
K = N / 256


def font(px):
    try:
        return ImageFont.truetype(FONT, int(px * K))
    except OSError:
        return ImageFont.load_default()


def achievements():
    src = (ROOT / 'src' / 'profile.cpp').read_text()
    body = src[src.index('const Achievement kAchievements[]'):]
    body = body[:body.index('};')]
    out, group = [], ''
    for line in body.splitlines():
        c = re.match(r'\s*//\s*(.+)', line)
        if c:
            group = c.group(1).strip()
            continue
        m = re.match(r'\s*\{"([a-z0-9_]+)", "([^"]*)", "([^"]*)", (true|false)\}', line)
        if m:
            out.append({'id': m.group(1), 'name': m.group(2), 'desc': m.group(3), 'hidden': m.group(4) == 'true', 'group': group})
    return out


def api(aid):
    return 'ACH_' + aid.upper()


# ---- the glyphs: drawn in the 256-unit box, centred on (128, 120)
def P(*pts):
    return [(x * K, y * K) for x, y in pts]


def line(d, pts, w=8, col=WHITE):
    d.line(P(*pts), fill=col, width=int(w * K), joint='curve')
    for x, y in (pts[0], pts[-1]):
        r = w * K / 2
        d.ellipse((x * K - r, y * K - r, x * K + r, y * K + r), fill=col)


def circle(d, cx, cy, r, fill=None, outline=None, w=6):
    d.ellipse((K * (cx - r), K * (cy - r), K * (cx + r), K * (cy + r)), fill=fill, outline=outline, width=int(w * K))


def arc(d, cx, cy, r, a0, a1, w=8, col=WHITE):
    d.arc((K * (cx - r), K * (cy - r), K * (cx + r), K * (cy + r)), a0, a1, fill=col, width=int(w * K))


def text(d, s, cx, cy, px, col=WHITE):
    f = font(px)
    d.text((cx * K, cy * K), s, font=f, fill=col, anchor='mm')


def star(d, cx, cy, r, col=GOLD, points=5, inner=0.45):
    pts = []
    for i in range(points * 2):
        a = -math.pi / 2 + i * math.pi / points
        rr = r if i % 2 == 0 else r * inner
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    d.polygon(P(*pts), fill=col)


def plane(d, cx, cy, s=1.0, col=WHITE, ang=0.0):
    """A small aircraft from above: fuselage, wings, tail."""
    shape = [(0, -34), (5, -26), (5, -8), (36, 4), (36, 10), (5, 4), (4, 22), (14, 30), (14, 35), (0, 31),
             (-14, 35), (-14, 30), (-4, 22), (-5, 4), (-36, 10), (-36, 4), (-5, -8), (-5, -26)]
    ca, sa = math.cos(ang), math.sin(ang)
    d.polygon(P(*[(cx + s * (x * ca - y * sa), cy + s * (x * sa + y * ca)) for x, y in shape]), fill=col)


def runway(d, y=168, col=WHITE):
    d.polygon(P((70, y), (186, y), (200, y + 22), (56, y + 22)), fill=col)
    for i in range(5):
        x = 82 + i * 24
        d.rectangle((K * x, K * (y + 9), K * (x + 12), K * (y + 13)), fill=NAVY_BOTTOM + (255,))


def touchdown(d, flat=1.0):
    """The approach and its flare, levelling off onto the runway (flatter: a later, softer flare)."""
    pts = [(44 + 112 * t, 66 + 96 * (1 - (1 - t) ** (2 * flat))) for t in (i / 16 for i in range(17))]
    d.line(P(*pts), fill=GOLD, width=int(6 * K), joint='curve')
    circle(d, pts[-1][0], pts[-1][1], 6, fill=GOLD)


def epaulette(d, stripes):
    d.polygon(P((70, 78), (186, 78), (196, 168), (60, 168)), fill=(20, 30, 46, 255), outline=GOLD)
    d.line(P((70, 78), (186, 78), (196, 168), (60, 168), (70, 78)), fill=GOLD, width=int(5 * K))
    gap = 70 / max(stripes, 1)
    for i in range(stripes):
        y = 96 + i * gap
        d.rectangle((K * 72, K * y, K * 184, K * (y + gap * 0.5)), fill=GOLD)
    circle(d, 128, 64, 10, fill=GOLD)


def medal(d, col):
    d.polygon(P((96, 40), (120, 40), (140, 100), (116, 100)), fill=TEAL)
    d.polygon(P((160, 40), (136, 40), (116, 100), (140, 100)), fill=(110, 150, 160, 255))
    circle(d, 128, 136, 44, fill=col)
    circle(d, 128, 136, 33, outline=(255, 255, 255, 120), w=4)
    star(d, 128, 136, 22, col=WHITE)


def stamp(d, label, check=False):
    circle(d, 128, 120, 66, outline=GOLD, w=8)
    circle(d, 128, 120, 52, outline=GOLD, w=3)
    if check:
        line(d, [(98, 122), (120, 144), (160, 98)], w=12, col=WHITE)
    else:
        text(d, label, 128, 120, 52)


def pin(d, label):
    d.polygon(P((128, 186), (92, 120), (164, 120)), fill=GOLD)
    circle(d, 128, 104, 40, fill=GOLD)
    circle(d, 128, 104, 26, fill=NAVY_BOTTOM + (255,))
    text(d, label, 128, 104, 30)


def glyph(d, aid):
    g = {
        'first_flight': lambda: (line(d, [(50, 178), (206, 178)], 5, GOLD), plane(d, 128, 112, 1.9, WHITE, -0.35)),
        'butter': lambda: (runway(d), touchdown(d, 1.0)),
        'greaser': lambda: (runway(d), touchdown(d, 1.8), star(d, 190, 70, 18, GOLD, 4, 0.3)),
        'butter25': lambda: (runway(d), touchdown(d, 1.0), text(d, '25', 176, 82, 46, GOLD)),
        'grade_s': lambda: (circle(d, 128, 120, 66, outline=GOLD, w=8), text(d, 'S', 128, 118, 96)),
        'flawless': lambda: (star(d, 78, 92, 26), star(d, 128, 74, 30), star(d, 178, 92, 26), text(d, 'S', 128, 158, 56)),
        'crosswind': lambda: (runway(d), [line(d, [(40 + i * 18, 70 + i * 22), (110 + i * 18, 90 + i * 22)], 7, TEAL) for i in range(3)],
                              d.polygon(P((122, 104), (138, 116), (116, 118)), fill=TEAL)),
        'night': lambda: (runway(d), circle(d, 150, 88, 42, fill=GOLD), circle(d, 170, 76, 38, fill=NAVY_TOP + (255,)),
                          star(d, 84, 70, 10, WHITE, 4, 0.35), star(d, 108, 112, 7, WHITE, 4, 0.35)),
        'weather': lambda: (circle(d, 100, 100, 32, fill=WHITE), circle(d, 140, 86, 40, fill=WHITE), circle(d, 172, 108, 28, fill=WHITE),
                            d.rectangle((K * 70, K * 104, K * 190, K * 132), fill=WHITE),
                            [line(d, [(90 + i * 28, 150), (80 + i * 28, 178)], 6, TEAL) for i in range(4)]),
        'deadstick': lambda: ([d.polygon(P(*[(128 + r * math.cos(a + t), 120 + r * math.sin(a + t)) for r, t in ((10, -0.5), (74, -0.12), (78, 0.08), (10, 0.5))]), fill=WHITE)
                               for a in (math.radians(-90), math.radians(30), math.radians(150))],
                              circle(d, 128, 120, 16, fill=GOLD)),
        'by_the_book': lambda: (d.rounded_rectangle((K * 76, K * 66, K * 180, K * 170), radius=int(14 * K), fill=TEAL), text(d, 'P', 128, 118, 84)),
        'go_around': lambda: (arc(d, 128, 120, 58, 200, 520 - 30, 10, WHITE), d.polygon(P((150, 52), (184, 72), (148, 88)), fill=GOLD), runway(d, 182)),
        'long_haul': lambda: ([circle(d, 52 + i * 152 / 9, 160 - 90 * math.sin(math.pi * i / 9), 4, fill=WHITE) for i in range(10)],
                              circle(d, 52, 160, 12, fill=GOLD), circle(d, 204, 160, 12, fill=GOLD)),
        'warp': lambda: ([d.polygon(P((60 + i * 46, 70), (96 + i * 46, 120), (60 + i * 46, 170), (78 + i * 46, 170), (114 + i * 46, 120), (78 + i * 46, 70)),
                                    fill=(GOLD if i == 2 else WHITE)) for i in range(3)]),
        'lic_ppl': lambda: epaulette(d, 2),
        'lic_cpl': lambda: epaulette(d, 3),
        'lic_atp': lambda: epaulette(d, 4),
        'owner': lambda: (circle(d, 92, 120, 32, outline=GOLD, w=12), line(d, [(124, 120), (200, 120)], 12, GOLD),
                          line(d, [(176, 120), (176, 146)], 10, GOLD), line(d, [(196, 120), (196, 140)], 10, GOLD)),
        'fleet3': lambda: (plane(d, 128, 84, 1.0), plane(d, 80, 150, 1.0), plane(d, 176, 150, 1.0)),
        'airline': lambda: (line(d, [(64, 160), (120, 80)], 6, TEAL), line(d, [(120, 80), (192, 128)], 6, TEAL), line(d, [(64, 160), (192, 128)], 6, TEAL),
                            circle(d, 64, 160, 14, fill=GOLD), circle(d, 120, 80, 14, fill=GOLD), circle(d, 192, 128, 14, fill=GOLD)),
        'tycoon': lambda: ([d.rectangle((K * (64 + i * 34), K * (170 - 26 - i * 28), K * (88 + i * 34), K * 170), fill=(GOLD if i == 3 else WHITE)) for i in range(4)],
                           line(d, [(60, 150), (110, 120), (140, 128), (196, 64)], 6, GOLD)),
        'millionaire': lambda: (circle(d, 128, 120, 66, fill=GOLD), text(d, '$', 128, 120, 90, NAVY_BOTTOM + (255,))),
        'campaign': lambda: (arc(d, 128, 124, 64, 110, 250, 10, GOLD), arc(d, 128, 124, 64, -70, 70, 10, GOLD),
                             [circle(d, 128 + s * 64 * math.cos(math.radians(a)), 124 + 64 * math.sin(math.radians(a)), 9, fill=GOLD)
                              for a in (-50, -10, 30) for s in (1, -1)],
                             star(d, 128, 118, 40, WHITE)),
        'legend': lambda: (d.polygon(P((64, 160), (64, 84), (96, 118), (128, 66), (160, 118), (192, 84), (192, 160)), fill=GOLD),
                           circle(d, 128, 138, 10, fill=NAVY_BOTTOM + (255,))),
        'airports10': lambda: stamp(d, '10'),
        'airports_all': lambda: stamp(d, '', True),
        'sites5': lambda: pin(d, '5'),
        'sites_all': lambda: (circle(d, 128, 120, 66, outline=GOLD, w=6), star(d, 128, 120, 60, WHITE, 4, 0.22),
                              star(d, 128, 120, 34, GOLD, 4, 0.3)),
        'region_first': lambda: (arc(d, 128, 260, 130, 205, 335, 8, TEAL), plane(d, 128, 104, 1.5, WHITE, 0.5),
                                 star(d, 196, 66, 14, GOLD, 4, 0.3)),
        'regions5': lambda: ([circle(d, 128 + 62 * math.cos(math.radians(-90 + 72 * i)), 122 + 62 * math.sin(math.radians(-90 + 72 * i)), 16,
                                     fill=(GOLD if i == 0 else WHITE)) for i in range(5)], text(d, '5', 128, 122, 56, GOLD)),
        'region_all': lambda: (d.polygon(P((60, 74), (104, 58), (152, 74), (196, 58), (196, 168), (152, 184), (104, 168), (60, 184)), fill=WHITE),
                               line(d, [(104, 58), (104, 168)], 3, TEAL), line(d, [(152, 74), (152, 184)], 3, TEAL),
                               line(d, [(92, 124), (118, 148), (164, 96)], 12, GOLD)),
        'discover20': lambda: (circle(d, 128, 120, 66, outline=GOLD, w=6), d.polygon(P((128, 62), (142, 120), (128, 178), (114, 120)), fill=WHITE),
                               d.polygon(P((70, 120), (128, 106), (186, 120), (128, 134)), fill=TEAL), circle(d, 128, 120, 10, fill=GOLD)),
        'photo': lambda: (d.rounded_rectangle((K * 56, K * 80, K * 200, K * 168), radius=int(14 * K), fill=WHITE),
                          d.rectangle((K * 96, K * 66, K * 140, K * 82), fill=WHITE), circle(d, 128, 124, 30, fill=NAVY_BOTTOM + (255,)),
                          circle(d, 128, 124, 18, fill=TEAL), circle(d, 180, 96, 6, fill=GOLD)),
        'medal': lambda: medal(d, (196, 132, 82, 255)),
        'gold': lambda: medal(d, GOLD),
        'gold_all': lambda: (medal(d, GOLD), d.polygon(P((84, 136), (36, 112), (40, 128), (84, 150)), fill=WHITE),
                             d.polygon(P((172, 136), (220, 112), (216, 128), (172, 150)), fill=WHITE)),
        'sortie': lambda: ([d.polygon(P((64, 78 + i * 34), (128, 112 + i * 34), (192, 78 + i * 34), (192, 98 + i * 34), (128, 132 + i * 34), (64, 98 + i * 34)),
                                      fill=(GOLD if i == 0 else WHITE)) for i in range(3)]),
        'ufo': lambda: (d.ellipse((K * 56, K * 104, K * 200, K * 146), fill=WHITE), d.chord((K * 92, K * 70, K * 164, K * 138), 180, 360, fill=TEAL),
                        [circle(d, 84 + i * 30, 125, 6, fill=GOLD) for i in range(4)],
                        d.polygon(P((112, 146), (144, 146), (172, 200), (84, 200)), fill=(232, 188, 120, 90))),
        'display': lambda: (plane(d, 100, 104, 1.25, WHITE, -0.2), plane(d, 160, 140, 1.25, GOLD, -0.2),
                            line(d, [(78, 180), (120, 150)], 4, TEAL), line(d, [(140, 214), (180, 186)], 4, TEAL)),
        'classified': lambda: (d.rounded_rectangle((K * 72, K * 104, K * 184, K * 186), radius=int(12 * K), fill=GOLD),
                               arc(d, 128, 104, 36, 180, 360, 12, WHITE), line(d, [(92, 104), (92, 112)], 12, WHITE), line(d, [(164, 104), (164, 112)], 12, WHITE),
                               circle(d, 128, 140, 10, fill=NAVY_BOTTOM + (255,)), line(d, [(128, 140), (128, 162)], 7, NAVY_BOTTOM + (255,))),
        'supersonic': lambda: (plane(d, 150, 120, 1.3, WHITE, math.pi / 2), arc(d, 170, 120, 80, 130, 230, 6, GOLD), arc(d, 196, 120, 120, 145, 215, 4, TEAL)),
    }.get(aid)
    if g:
        g()
    else:
        star(d, 128, 120, 50)


def badge(a):
    img = Image.new('RGBA', (N, N))
    mask = Image.new('L', (N, N))
    ImageDraw.Draw(mask).rounded_rectangle((K * 8, K * 8, K * 248, K * 248), radius=int(44 * K), fill=255)
    bg = Image.new('RGBA', (N, N))
    bd = ImageDraw.Draw(bg)
    for y in range(N):
        f = y / N
        bd.line((0, y, N, y), fill=tuple(round(p + (q - p) * f) for p, q in zip(NAVY_TOP, NAVY_BOTTOM)) + (255,))
    img.paste(bg, (0, 0), mask)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((K * 8, K * 8, K * 248, K * 248), radius=int(44 * K), outline=GOLD, width=int(6 * K))
    d.line((K * 70, K * 222, K * 186, K * 222), fill=GOLD, width=int(3 * K))   # (the identity's horizon line)
    glyph(d, a['id'])
    return img.resize((256, 256), Image.Resampling.LANCZOS)


def locked(img):
    g = ImageOps.grayscale(img.convert('RGB'))
    g = g.point(lambda v: int(18 + v * 0.42))
    out = Image.merge('RGBA', (g, g, g, img.getchannel('A')))
    return out


def library_logo():
    w, h = 1280, 720
    img = Image.new('RGBA', (w, h))
    d = ImageDraw.Draw(img)
    emblem = generate_icon.render(256)
    img.alpha_composite(emblem, ((w - 256) // 2, 70))
    title = 'SOMNIUM VOLANDI'
    size = 120
    while True:   # (the largest that fits with a margin)
        f1 = ImageFont.truetype(FONT, size)
        spacing = size * 0.12
        widths = [d.textlength(c, font=f1) for c in title]
        total = sum(widths) + spacing * (len(title) - 1)
        if total <= w - 160 or size <= 40:
            break
        size -= 2
    x = (w - total) / 2
    for c, cw in zip(title, widths):
        d.text((x + 4, 384 + 4), c, font=f1, fill=(0, 0, 0, 150))
        d.text((x, 384), c, font=f1, fill=WHITE)
        x += cw + spacing
    f2 = ImageFont.truetype(FONT, 38)
    sub = 'THE DREAM OF FLIGHT'
    d.text((w / 2, 560), sub, font=f2, fill=GOLD, anchor='mm')
    return img


def main():
    out = ROOT / 'steam'
    (out / 'achievements').mkdir(parents=True, exist_ok=True)
    (out / 'store').mkdir(parents=True, exist_ok=True)
    rows = achievements()
    with open(out / 'achievements.csv', 'w', newline='') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(['api_name', 'display_name', 'description', 'hidden', 'group', 'icon', 'icon_locked'])
        for a in rows:
            n = api(a['id'])
            w.writerow([n, a['name'], a['desc'], 1 if a['hidden'] else 0, a['group'], f'achievements/{n}.png', f'achievements/{n}_locked.png'])
    keep = set()
    for a in rows:
        n = api(a['id'])
        img = badge(a)
        img.save(out / 'achievements' / f'{n}.png', optimize=True)
        locked(img).save(out / 'achievements' / f'{n}_locked.png', optimize=True)
        keep |= {f'{n}.png', f'{n}_locked.png'}
    for p in (out / 'achievements').glob('*.png'):   # (an achievement renamed or removed: its icons go too)
        if p.name not in keep:
            p.unlink()
    library_logo().save(out / 'store' / 'library_logo.png', optimize=True)
    generate_icon.render(184).convert('RGB').save(out / 'store' / 'community_icon.jpg', quality=95)
    # a contact sheet of every icon, earned over locked, to look them over at a glance
    cols = 10
    sheet = Image.new('RGB', (cols * 72, ((len(rows) + cols - 1) // cols) * 144), (12, 16, 24))
    for i, a in enumerate(rows):
        n = api(a['id'])
        e = Image.open(out / 'achievements' / f'{n}.png').resize((64, 64), Image.Resampling.LANCZOS)
        l = Image.open(out / 'achievements' / f'{n}_locked.png').resize((64, 64), Image.Resampling.LANCZOS)
        x, y = (i % cols) * 72 + 4, (i // cols) * 144 + 4
        sheet.paste(e, (x, y), e)
        sheet.paste(l, (x, y + 68), l)
    sheet.save(out / 'achievements' / 'contact_sheet.jpg', quality=90)
    print(f'{len(rows)} achievements: steam/achievements.csv, {2 * len(rows)} icons, the library logo, the community icon')


if __name__ == '__main__':
    main()
