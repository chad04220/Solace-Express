#!/usr/bin/env python3
from pathlib import Path
import argparse,struct,hashlib,json
import numpy as np
from compare_mesh_payloads import read
p=argparse.ArgumentParser();p.add_argument('a',type=Path);p.add_argument('b',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
def parts(path):
 meta,payload=read(path);q=payload[sum(meta['sections'][k]['bytes'] for k in ['vertices','indices','moving_hull_and_eye']):];at=0;out={}
 while at<len(q):
  typ,nf,ni=struct.unpack_from('<3I',q,at);at+=12;out[typ]=np.frombuffer(q[at:at+nf*4],dtype='<f4').reshape(-1,8).copy();at+=(nf+ni)*4
 return meta,out
ma,pa=parts(a.a);mb,pb=parts(a.b);results=[]
for typ in pa:
 x,y=pa[typ],pb[typ];assert x.shape==y.shape
 diff=x!=y
 if not diff.any():continue
 assert np.array_equal(x[:,:7].view('<u4'),y[:,:7].view('<u4')),'Non-AO difference found'
 v=x[:,7];w=y[:,7];d=np.abs(v.astype(float)-w.astype(float));sel=d>0;ulps=np.abs(v.view('<u4').astype(np.int64)-w.view('<u4').astype(np.int64))
 results.append({'part':typ,'vertices':len(v),'ao_changed_vertices':int(sel.sum()),'changed_percent':float(sel.mean()*100),'max_absolute_ao_difference':float(d.max()),'mean_absolute_ao_difference_all':float(d.mean()),'median_absolute_ao_difference_changed':float(np.median(d[sel])),'p95_absolute_ao_difference_changed':float(np.percentile(d[sel],95)),'max_ulp_difference':int(ulps.max()),'a_ao_range':[float(v.min()),float(v.max())],'b_ao_range':[float(w.min()),float(w.max())],'positions_normals_material_exact':True,'sample_max_difference_vertex':int(d.argmax()),'sample_max_values':[float(v[d.argmax()]),float(w[d.argmax()])]})
r={'a':str(a.a),'b':str(a.b),'differing_parts':results,'static_exact':ma['static']==mb['static'],'hull_and_eye_exact':ma['sections']['moving_hull_and_eye']==mb['sections']['moving_hull_and_eye'],'indices_exact':ma['sections']['indices']==mb['sections']['indices'],'part_topology_exact':[(p['type'],p['vertices'],p['triangles'],p['index_sha256']) for p in ma['parts']]==[(p['type'],p['vertices'],p['triangles'],p['index_sha256']) for p in mb['parts']],'interpretation':'State-dependent baked fan AO in unchanged baseline source. This comparison does not establish pixel impact.'}
a.report.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
