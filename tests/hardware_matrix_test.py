"""tools/hardware_matrix.py reads a diagnostics report as diagnostics.bat and the --bench run write it (a zip and a
folder), takes the GPU the game used, and judges each scene against the 60 fps target."""
import io, sys, tempfile, zipfile
from contextlib import redirect_stdout
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import hardware_matrix as hm

SYSTEM = """Somnium Volandi v3.50.0  (diagnostics v4)
date Sun 10/18/2026 14:00:00.00
--- GPU
  Intel(R) UHD Graphics 770  driver 31.0.101.5186  (128 MB)
  NVIDIA GeForce RTX 3060  driver 32.0.15.6094  (12288 MB)
--- CPU
  12th Gen Intel(R) Core(TM) i5-12400F  6 cores / 12 threads  2500 MHz
--- Memory  31.8 GB
--- Windows  Microsoft Windows 11 Pro  build 22631
--- Monitors
  \\\\.\\DISPLAY1  2560x1440  (primary)
  current mode 2560x1440 @ 144 Hz
--- Power
  Power Scheme GUID: 381b4222  (Balanced)
"""
STARTUP = "GPU: NVIDIA GeForce RTX 3060/PCIe/SSE2 / 4.6.0 NVIDIA 560.94\nFragment texture units: 32\n"
COMPILE = "first run with an empty shader cache: 74.2 s (compiles the shaders, then times the menu scene once; no aircraft bodies built)\n"
def scene(name, ms, low, gpu):
    return (f"\nScene {name}; requested WX=(scene/default)\n  capture complete=yes\n"
            f"  frame work wall: n=600 min=1 median=2 p95=3 p99=4 max=5 mean=2 ms\n"
            f"  app present interval: n=600 min=1 median=2 p95=3 p99=4 max=5 mean=2 ms 1%low={low:.2f} fps >16.667ms=3/600\n"
            f"{name:<22} {ms:6.2f} ms/frame ({1000 / ms:5.1f} fps)   GPU {gpu:6.2f} ms: world 1.00  displays 0.10\n"
            f"{'':<22} (warmed 30 frames until built; 0 bodies built during it)\n")
BENCH = "GPU: NVIDIA GeForce RTX 3060\nDesktop 2560x1440, render 1920x1080, quality 1\n" + scene('menu', 8.0, 98.0, 6.5) + scene('forest', 15.9, 41.0, 14.0) + scene('storm', 21.0, 33.0, 19.0)
RESEARCH = scene('rjetc', 12.5, 61.0, 11.0)
SOAK = "Somnium Volandi soak, 120 minutes\n\n   0.17 min  air  60.0 fps\n\nTrend after the first quarter: memory +3.2 MB/hour, handles +0.0/hour: steady\n"

fails = 0
def check(c, what):
    global fails
    if not c: print('  !!', what); fails += 1

with tempfile.TemporaryDirectory() as d:
    folder = Path(d) / 'pc_folder' / 'diagnostics'; folder.mkdir(parents=True)
    for n, t in (('system.txt', SYSTEM), ('startup.log', STARTUP), ('compile_time.txt', COMPILE), ('bench_1080p.txt', BENCH), ('bench_research_1080p.txt', RESEARCH), ('soak.txt', SOAK)):
        (folder / n).write_text(t, encoding='utf-8')
    zpath = Path(d) / 'diagnostics_v3.50.0.zip'
    with zipfile.ZipFile(zpath, 'w') as z:
        for n, t in (('diagnostics/system.txt', SYSTEM), ('diagnostics/startup.log', STARTUP), ('diagnostics/compile_time.txt', COMPILE), ('diagnostics/bench_1080p.txt', BENCH), ('diagnostics/bench_research_1080p.txt', RESEARCH), ('diagnostics/soak.txt', SOAK)):
            z.writestr(n, t)
    rep = hm.Report(zpath)
    s, sc = hm.system(rep), hm.scenes(rep)
    check(hm.system(hm.Report(Path(d) / 'pc_folder')) == s, 'a folder reads as its zip does')
    check(s.get('gpu') == 'NVIDIA GeForce RTX 3060' and s.get('vram_mb') == 12288, f"the GPU the game used, not the first listed: {s.get('gpu')}")
    check(s.get('cpu', '').startswith('12th Gen') and s.get('memory') == '31.8 GB' and s.get('display') == '2560x1440 @ 144 Hz', f"the PC: {s}")
    check(abs(s.get('first_compile_s', 0) - 74.2) < 1e-6 and s.get('version') == 'v3.50.0', 'the version and the first compile')
    check(s.get('soak') == 'steady (+3.2 MB/h)', f"the soak's verdict: {s.get('soak')}")
    check(set(sc) == {'menu', 'forest', 'storm', 'rjetc'}, f'every scene, the research file too: {sorted(sc)}')
    check(sc['forest'][3] == 41.0 and abs(sc['forest'][1] - 62.9) < .1, f"a scene's fps and 1% low: {sc['forest']}")
    check(hm.verdict(*sc['menu'][1::2]) == 'OK' and hm.verdict(*sc['forest'][1::2]) == 'SLOW' and hm.verdict(*sc['storm'][1::2]) == 'BELOW', 'the verdicts')
    out = io.StringIO()
    with redirect_stdout(out): hm.main([str(zpath), str(Path(d) / 'pc_folder'), '--csv', str(Path(d) / 'm.csv')])
    table = out.getvalue()
    check(table.count('\n| ') >= 2 and 'forest |' in table and '63 fps / 41 SLOW' in table, f'the table:\n{table}')
    check((Path(d) / 'm.csv').read_text().count('\n') == 3, 'the CSV: a header and a row per report')
print('Hardware matrix:', 'FAIL' if fails else 'ok')
sys.exit(1 if fails else 0)
