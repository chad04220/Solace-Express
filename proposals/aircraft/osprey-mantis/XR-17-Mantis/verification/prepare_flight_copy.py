"""Create a disposable flight-only test fixture; never installs the custom renderer."""
import pathlib, shutil, sys
baseline=pathlib.Path(sys.argv[1]).resolve()
dest=pathlib.Path(sys.argv[2]).resolve()
if dest.exists(): raise SystemExit('Destination must not exist; baseline will not be changed.')
pack=pathlib.Path(__file__).resolve().parent.parent
shutil.copytree(baseline,dest,ignore=shutil.ignore_patterns('.git','build','build-*','mesa-cache'))
rows=(pack/'rows.cpp.inc').read_text()
row=rows[rows.index('{"xr17"'):rows.index('// models.cpp')].strip()
p=dest/'src/aircraft.cpp';t=p.read_text();needle='  // hidden research model:'
assert needle in t and '"xr17"' not in t,'Fixture expects pristine a298771 baseline.'
p.write_text(t.replace(needle,row+'\n'+needle,1))
p=dest/'src/aircraft.h';t=p.read_text();assert 'kResearchJet = 7' in t and 'kWraith = 8' in t
p.write_text(t.replace('kResearchJet = 7','kResearchJet = 8').replace('kWraith = 8','kWraith = 9'))
shutil.copy2(pack/'verification/mantis_max_weight_test.cpp',dest/'tests/mantis_max_weight_test.cpp')
print('Flight-only fixture created:',dest)
print('Do not render this fixture: ModelDef dispatch is deliberately not installed.')
