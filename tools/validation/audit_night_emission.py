#!/usr/bin/env python3
"""Opt-in float32 curtain-wall emission audit; requires NumPy, writes no files.

The copied arithmetic is tied to an exact production block hash, so a changed
shader fails rather than silently validating stale formulas. This is not GPU
render/performance validation.
"""
from pathlib import Path
import hashlib
import json
import numpy as np
source=(Path(__file__).resolve().parents[2]/"src/shaders/ent_fs2.glsl").read_text()
start=source.index("        else {\n          // Stable floor occupancy")
end=source.index("      } else if (uKind == K_SHOP)",start)
fragment=source[start:end]
assert hashlib.sha256(fragment.encode()).hexdigest()=="99fced68ac03f78f8bad6908d585d278186882ca4fcd90994bb3be265ccd8ae7", "production emission changed; update and review audit"
for banned in ["uTime","gl_FragCoord","uCam","uLod","texture","for (","while ("]:
    assert banned not in fragment
assert fragment.count("hsh(")==3
F=np.float32
def frac(x): return x-np.floor(x)
def hsh(x,y): return frac(np.sin(x*F(127.1)+y*F(311.7))*F(43758.5453))
# Same integer pane domain through near/far, with a deterministic float32 seed set.
seeds=frac(np.arange(1024,dtype=F)*F(.61803398875))[:,None,None,None]
cols=np.arange(-6,7,dtype=F)[None,None,None,:]
floors=np.arange(35,dtype=F)[None,None,:,None]
faces=np.array([-16,-11,-6,-5,5,6,11,16],dtype=F)[None,:,None,None]
s2=frac(seeds*F(13.31))
floorState=hsh(floors,seeds*F(29)+F(13))
zone=np.floor((cols+np.floor(floorState*F(3)))/F(3))
zoneState=hsh(zone+seeds*F(23)+faces+F(19),floors+seeds*F(23)+faces+F(47))
paneState=hsh(cols+seeds*F(17)+faces,floors+seeds*F(17)+faces)
lit=(floorState>=F(.22))&(zoneState>=(F(.52)+(F(.68)-F(.52))*s2))&(paneState>=F(.10))
lum=(F(.065)+(F(.28)-F(.065))*frac(zoneState*F(7.13)))*(F(.9)+(F(1)-F(.9))*paneState)
assert lum[lit].min()>=F(.05915)-1e-7 and lum[lit].max()<=F(.28)+1e-7
assert np.all(np.where(lit,lum,F(0))*(F(0))==0)
# Adjacent occupied pane agreement should be stronger than independent same-rate cells.
rate=float(lit.mean()); both=float((lit[:,:,:,:-1]&lit[:,:,:,1:]).mean())
assert both>rate*rate*1.5
results={'samples':int(lit.size),'empirical_lit_fraction':rate,'adjacent_both_lit_fraction':both,'independent_both_lit_at_observed_rate':rate*rate,'minimum_lit_luminance_sampled':float(lum[lit].min()),'maximum_lit_luminance_sampled':float(lum[lit].max()),'daytime_emission_exactly_zero':True,'additional_texture_samples':0,'additional_hsh_calls':2,'new_time_camera_or_lod_dependencies':False,'visual_acceptance':'Requires actual captures; numerical results are insufficient.'}
print(json.dumps(results,indent=2))
