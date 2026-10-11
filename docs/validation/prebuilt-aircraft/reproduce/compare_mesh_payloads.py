#!/usr/bin/env python3
"""Independent exact transport audit: legacy baseline payload versus portable asset."""
from pathlib import Path
import argparse,hashlib,json,struct
import numpy as np
sha=lambda b:hashlib.sha256(b).hexdigest()
def read(path):
 b=Path(path).read_bytes()
 if b[:8]==b'SEAMSH01':
  version,header,flags,algorithm,model,slot,profile,res,nv,ni,nh,fine,np_,res2=struct.unpack_from('<14I',b,8)
  assert (version,header,flags,algorithm,profile,res,res2)==(1,192,1,24,1,0,0)
  assert b[168:192]==bytes(24)
  length,=struct.unpack_from('<Q',b,64);assert length==len(b)-192
  checksum=sha(b[:104]+bytes(32)+b[136:]);assert checksum==b[104:136].hex()
  meta={'kind':'portable','model':model,'slot':slot,'identity':b[72:104].hex(),'source_digest':b[136:168].hex(),'checksum':checksum};p=b[192:]
 else:
  magic,nv,ni,nh,fine,np_=struct.unpack_from('<6I',b);assert magic==0x4d455348+24
  meta={'kind':'legacy'};p=b[24:]
 assert len(p)==4*(nv+ni+nh+np_) and nv%8==0 and ni%3==0 and nh%9==1 and fine%3==0 and fine<=ni
 cur=0;sections={}
 for name,n in [('vertices',nv),('indices',ni),('moving_hull_and_eye',nh),('part_blob',np_)]:sections[name]=p[cur:cur+4*n];cur+=4*n
 stats={'static_vertices':nv//8,'static_triangles':ni//3,'moving_hull_vertices':(nh-1)//3,'eye_flag_bits':sections['moving_hull_and_eye'][-4:].hex(),'fine_start':fine,'payload_bytes':len(p),'payload_sha256':sha(p),'sections':{k:{'bytes':len(v),'sha256':sha(v)} for k,v in sections.items()}}
 def mesh(vb,ib):
  v=np.frombuffer(vb,dtype='<f4').reshape((-1,8));ix=np.frombuffer(ib,dtype='<u4');assert np.all(np.isfinite(v)) and (not len(ix) or ix.max()<len(v))
  assert np.all((v[:,7]>=0)&(v[:,7]<=1.001)) and np.all((np.sum(v[:,3:6]**2,axis=1)>=.98)&(np.sum(v[:,3:6]**2,axis=1)<=1.02))
  return {'vertices':len(v),'triangles':len(ix)//3,'position_sha256':sha(v[:,:3].tobytes()),'normal_sha256':sha(v[:,3:6].tobytes()),'material_sha256':sha(v[:,6].tobytes()),'ao_sha256':sha(v[:,7].tobytes()),'vertex_sha256':sha(vb),'index_sha256':sha(ib)}
 stats['static']=mesh(sections['vertices'],sections['indices']);parts=[];q=sections['part_blob'];at=0
 while at<len(q):
  typ,pnf,pni=struct.unpack_from('<3I',q,at);at+=12;assert typ<=46 and pnf%8==0 and pni%3==0 and at+4*(pnf+pni)<=len(q)
  vb=q[at:at+pnf*4];at+=pnf*4;ib=q[at:at+pni*4];at+=pni*4
  parts.append({'type':typ,**mesh(vb,ib)})
 stats['parts']=parts;meta.update(file=str(path),file_bytes=len(b),file_sha256=sha(b),**stats);return meta,p
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('baseline',type=Path);p.add_argument('portable',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 left,lp=read(a.baseline);right,rp=read(a.portable);r={'baseline':left,'portable':right,'exact_payload_bytes':lp==rp,'exact_fine_start':left['fine_start']==right['fine_start'],'pass':lp==rp and left['fine_start']==right['fine_start']};a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'pass':r['pass'],'baseline':str(a.baseline),'portable':str(a.portable),'payload_bytes':len(lp)}));raise SystemExit(0 if r['pass'] else 1)
