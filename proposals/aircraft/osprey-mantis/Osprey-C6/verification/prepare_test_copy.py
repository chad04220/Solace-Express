#!/usr/bin/env python3
"""Create an isolated test copy; never modify the source directory."""
from pathlib import Path
import shutil,sys
if len(sys.argv)!=3:raise SystemExit('Usage: python prepare_test_copy.py BASELINE_REPO NEW_TEST_DIRECTORY')
src=Path(sys.argv[1]).resolve();dst=Path(sys.argv[2]).resolve();pkg=Path(__file__).resolve().parents[1]
assert src.is_dir() and not dst.exists(), 'Source must exist; output must not exist'
assert src!=dst and src not in dst.parents,'Choose output outside the baseline source'
shutil.copytree(src,dst,ignore=shutil.ignore_patterns('.git','build','build-*'))
p=dst/'src/aircraft.cpp';s=p.read_text();assert '"osprey_c6"' not in s
marker='  // hidden research model:';assert s.count(marker)==1;s=s.replace(marker,(pkg/'AircraftSpec.inc').read_text()+marker,1);p.write_text(s)
p=dst/'src/models.cpp';s=p.read_text();marker='  // ---------------------------------------------------------------- XR-9';assert s.count(marker)==1;s=s.replace(marker,(pkg/'ModelDef.inc').read_text()+marker,1);p.write_text(s)
p=dst/'src/aircraft.h';s=p.read_text();assert 'kResearchJet = 7' in s and 'kWraith = 8' in s;s=s.replace('kResearchJet = 7','kResearchJet = 8').replace('kWraith = 8','kWraith = 9');p.write_text(s)
for name in ['osprey_max_weight_test.cpp','aircraft_visual_test.cpp']:shutil.copyfile(pkg/'verification'/name,dst/'tests'/name)
print(dst)
