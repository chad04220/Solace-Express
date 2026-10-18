#!/usr/bin/env python3
"""The hardware matrix: one row per tester's PC, from the diagnostics they send (diagnostics.bat's zip or its folder).

  python tools/hardware_matrix.py diagnostics_v3.50.0.zip other_pc/ ... [--csv matrix.csv] [--md matrix.md]

Each report gives the PC (GPU, driver, video memory, CPU, memory, Windows, display), the first-run shader compile, the
soak's verdict when the report has one (diagnostics.bat soak) and the benchmark: every scene's frame time, frame rate and 1% low at 1920x1080, full screen. A scene is judged against
the 60 fps target: OK when its 1% low holds 50 fps, SLOW when the average does but the 1% low doesn't, else BELOW.
docs/HARDWARE_MATRIX.md says which PCs to collect and how to read the result."""
import argparse, csv, io, re, sys, zipfile
from pathlib import Path

SCENE_LINE = re.compile(r'^(\S+)\s+([\d.]+) ms/frame \(\s*([\d.]+) fps\)\s+GPU\s+([\d.]+) ms')
SCENE_HEAD = re.compile(r'^Scene (\S+);')
LOW = re.compile(r'app present interval: n=\d+.* 1%low=([\d.]+) fps')
TARGET, LOW_OK = 60.0, 50.0


class Report:
    """A diagnostics report's files, from a zip or a folder (the files may sit in a "diagnostics" subfolder)."""
    def __init__(self, path):
        self.path, self.files = Path(path), {}
        if self.path.is_dir():
            for p in self.path.rglob('*'):
                if p.suffix in ('.txt', '.log') and 'shots' not in p.parts and 'analysis' not in p.parts:
                    self.files[p.name] = p.read_text(encoding='utf-8-sig', errors='replace')
        else:
            with zipfile.ZipFile(self.path) as z:
                for n in z.namelist():
                    if n.endswith(('.txt', '.log')) and '/shots/' not in n and '/analysis/' not in n:
                        self.files[Path(n).name] = z.read(n).decode('utf-8-sig', errors='replace')

    def text(self, name): return self.files.get(name, '')


def system(rep):
    """The PC, from system.txt: the first GPU listed is taken as the one the game used unless startup.log names it."""
    s, out, section = rep.text('system.txt'), {}, ''
    for line in s.splitlines():
        if line.startswith('Somnium Volandi'): out['version'] = line.split()[2] if len(line.split()) > 2 else ''
        if line.startswith('---'):
            section = line[3:].strip()
            if section.startswith('Memory'): out['memory'] = section.split(None, 1)[1].strip()
            elif section.startswith('Windows'): out['windows'] = section.split(None, 1)[1].strip()
            continue
        line = line.strip()
        if not line: continue
        if section == 'GPU':
            m = re.match(r'(.+?)\s+driver (\S+)\s+\((\d+) MB\)', line)
            if m: out.setdefault('gpus', []).append({'name': m.group(1), 'driver': m.group(2), 'vram_mb': int(m.group(3))})
        elif section == 'CPU' and 'cpu' not in out: out['cpu'] = re.sub(r'\s+\d+ MHz$', '', line)
        elif section == 'Monitors' and line.startswith('current mode') and 'display' not in out: out['display'] = line[len('current mode '):]
    used = re.search(r'^GPU: (.+?) / ', rep.text('startup.log') or rep.text('startup_firstrun.log'), re.M)
    gpus = out.get('gpus', [])
    gpu = next((g for g in gpus if used and g['name'] in used.group(1)), gpus[0] if gpus else None)
    if gpu: out.update(gpu=gpu['name'], driver=gpu['driver'], vram_mb=gpu['vram_mb'])
    m = re.search(r'first run with an empty shader cache: ([\d.]+) s', rep.text('compile_time.txt'))
    if m: out['first_compile_s'] = float(m.group(1))
    m = re.search(r'Trend after the first quarter: memory ([+-][\d.]+) MB/hour, handles ([+-][\d.]+)/hour: (\w+)', rep.text('soak.txt'))
    if m: out['soak'] = f'{m.group(3)} ({m.group(1)} MB/h)'
    return out


def scenes(rep):
    """Every benchmarked scene: (ms/frame, fps, GPU ms, 1% low fps)."""
    out = {}
    for name in ('bench_1080p.txt', 'bench_research_1080p.txt'):
        low, cur = None, None
        for line in rep.text(name).splitlines():
            h = SCENE_HEAD.match(line)
            if h: cur, low = h.group(1), None; continue
            m = LOW.search(line)
            if m: low = float(m.group(1)); continue
            m = SCENE_LINE.match(line)
            if m: out[m.group(1)] = (float(m.group(2)), float(m.group(3)), float(m.group(4)), low)
    return out


def verdict(fps, low):
    if low is not None and low >= LOW_OK and fps >= TARGET * .95: return 'OK'
    if fps >= TARGET * .95: return 'SLOW'
    return 'BELOW'


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('reports', nargs='+'); ap.add_argument('--csv'); ap.add_argument('--md')
    a = ap.parse_args(argv)
    rows, names = [], []
    for r in a.reports:
        rep = Report(r)
        rows.append((Path(r).stem, system(rep), scenes(rep)))
        for n in rows[-1][2]:
            if n not in names: names.append(n)
    if a.csv:
        with open(a.csv, 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['report', 'version', 'gpu', 'driver', 'vram_mb', 'cpu', 'memory', 'windows', 'display', 'first_compile_s', 'soak'] +
                       [f'{n} {k}' for n in names for k in ('fps', '1% low', 'gpu ms', 'verdict')])
            for label, s, sc in rows:
                w.writerow([label] + [s.get(k, '') for k in ('version', 'gpu', 'driver', 'vram_mb', 'cpu', 'memory', 'windows', 'display', 'first_compile_s', 'soak')] +
                           [x for n in names for x in ((sc[n][1], sc[n][3] if sc[n][3] is not None else '', sc[n][2], verdict(sc[n][1], sc[n][3])) if n in sc else ('', '', '', ''))])
    md = io.StringIO()
    md.write('| PC | GPU (driver, VRAM) | CPU | RAM | Display | First compile | Soak |' + ''.join(f' {n} |' for n in names) + '\n')
    md.write('|---|---|---|---|---|---|---|' + '---|' * len(names) + '\n')
    for label, s, sc in rows:
        gpu = f"{s.get('gpu', '?')} ({s.get('driver', '?')}, {s.get('vram_mb', '?')} MB)"
        cells = []
        for n in names:
            if n not in sc: cells.append(' - |'); continue
            ms, fps, gms, low = sc[n]
            cells.append(f" {fps:.0f} fps{f' / {low:.0f}' if low is not None else ''} {verdict(fps, low)} |")
        fc = f"{s['first_compile_s']:.0f} s" if 'first_compile_s' in s else '-'
        md.write(f"| {label} | {gpu} | {s.get('cpu', '?')} | {s.get('memory', '?')} | {s.get('display', '?')} | {fc} | {s.get('soak', '-')} |" + ''.join(cells) + '\n')
    md.write(f'\nfps / 1% low at 1920x1080 full screen; OK: 1% low >= {LOW_OK:.0f} fps; SLOW: the average holds {TARGET:.0f} fps, the 1% low doesn\'t; BELOW: the average doesn\'t.\n')
    if a.md: Path(a.md).write_text(md.getvalue())
    sys.stdout.write(md.getvalue())
    return 0


if __name__ == '__main__':
    sys.exit(main())
