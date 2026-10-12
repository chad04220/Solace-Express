#!/usr/bin/env python3
"""Bounded scalar-reference study. No pack build, no downloaded source contents saved."""
import argparse, csv, hashlib, json, math, os, platform, random, resource, sys, time
from pathlib import Path
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'baseline'))
from tools.earth import cube, elevation
BASE='7954f2b71567021b207c9ddb28f3e2e1591d922c'
LEVELS=[0,2,4,6,8]
NS=[1,2,4,8,16,32,64]

def scalar(source, face, level, i, j, n):
    count=256*(1<<level)
    total=weight_sum=0.0
    # Deliberately retain baseline operation order and midpoint coordinates.
    for sy in range(n):
        v=2*(j+(sy+.5)/n)/count-1
        for sx in range(n):
            u=2*(i+(sx+.5)/n)/count-1
            lat,lon=cube.direction_to_latlon(cube.face_uv_to_direction(face,u,v))
            weight=cube.cube_area_weight(u,v) if n>1 else 1.0
            total+=source.sample(lat,lon)*weight
            weight_sum+=weight
    return total/weight_sum

def address(lat,lon,level):
    f,u,v=cube.direction_to_face_uv(cube.latlon_to_direction(lat,lon))
    count=256*(1<<level)
    return f,level,min(count-1,int((u+1)*count/2)),min(count-1,int((v+1)*count/2))

def latlon(addr, dx=.5,dy=.5):
    f,l,i,j=addr;c=256*(1<<l)
    return cube.direction_to_latlon(cube.face_uv_to_direction(f,2*(i+dx)/c-1,2*(j+dy)/c-1))

def stats(values):
    a=sorted(values);n=len(a)
    return dict(n=n,maximum=a[-1],mean=sum(a)/n,rms=math.sqrt(sum(v*v for v in a)/n),p50=a[n//2],p95=a[min(n-1,int(.95*n))],p99=a[min(n-1,int(.99*n))])

def source_span(addr,source):
    la,lo=latlon(addr)
    pts=[latlon(addr,x,y) for x in (0,.5,1) for y in (0,.5,1)]
    lats=[p[0] for p in pts];lons=[lo+(p[1]-lo+180)%360-180 for p in pts]
    # This is a footprint bounding span diagnostic, not a no-alias theorem.
    x=(max(lons)-min(lons))/source.lon_step;y=(max(lats)-min(lats))/source.lat_step
    return x,y,max(x,y)

def probes():
    points=[
      ('mountain','everest',27.9881,86.9250),('mountain','k2',35.8808,76.5158),
      ('mountain','denali',63.0695,-151.0074),('mountain','aconcagua',-32.6532,-70.0109),
      ('mountain','alps',45.8326,6.8652),('mountain','andes-coast',-23.0,-70.4),
      ('mountain','himalaya',29.0,84.0),('mountain','new-zealand',-43.594,170.141),
      ('coast','bermuda',32.3,-64.78),('coast','hawaii',19.65,-155.0),
      ('coast','fiji',-17.8,178.1),('coast','norway',68.0,13.5),
      ('coast','chile-fjords',-49.2,-74.1),('coast','greenland',69.2,-49.8),
      ('coast','california',36.3,-121.9),('coast','japan',35.1,139.8),
      ('coast','aleutians',52.0,179.0),('coast','indonesia',-8.4,116.5),
      ('bathymetry','mariana',11.35,142.2),('bathymetry','puerto-rico',19.7,-67.3),
      ('plain','amazon',-3.,-60.),('plain','sahara',24.,12.),
      ('plain','central-us',39.,-100.),('plain','ocean',-20.,-130.),
      ('polar','north-89.9',89.9,0.),('polar','south-89.9',-89.9,90.),
      ('polar','north-pole',90.,0.),('polar','south-pole',-90.,0.),
      ('antimeridian','dateline-north',40.,180.),('antimeridian','dateline-south',-40.,-180.)]
    out={}
    def add(addr,category,name):
      if addr not in out: out[addr]=dict(category=category,label=name)
    for cat,name,lat,lon in points:
      for level in LEVELS:
        f,l,i,j=address(lat,lon,level);count=256*(1<<l)
        for dx,dy in ((0,0),(-1,1),(1,-1)):
          add((f,l,max(0,min(count-1,i+dx)),max(0,min(count-1,j+dy))),cat,name)
    for level in LEVELS:
      count=256*(1<<level)
      for face in cube.FACES:
        for i,j in ((0,0),(count-1,0),(0,count-1),(count-1,count-1),(0,count//2),(count-1,count//2),(count//2,0),(count//2,count-1)):
          add((face,level,i,j),'face-edge-corner',face)
    rng=random.Random(20261012)
    for p in range(24):
      lat=math.degrees(math.asin(rng.uniform(-1,1)));lon=rng.uniform(-180,180)
      for level in LEVELS: add(address(lat,lon,level),'seeded-sphere',str(p))
    return out

def synthetic():
    f,l,i,j='px',0,127,127
    west=latlon((f,l,i,j),0,.5)[1];east=latlon((f,l,i,j),1,.5)[1]
    records=[]
    for ratio in (16,64,128):
      step=(east-west)/ratio;row=[10000. if k==ratio//2 else 0. for k in range(ratio+1)]
      grid=elevation.Grid(dict(version=1,lon_min=west,lat_min=-1,lon_step=step,lat_step=1,periodic=False,heights=[row,row,row]))
      values={str(n):scalar(grid,f,l,i,j,n) for n in (1,2,4,8,16,32,64,128,256)}
      records.append(dict(name='one-source-node-ridge',source_cells_per_target=ratio,values=values,description='Bilinear triangular ridge of height 10000 m; no source error or discontinuous sampler. All smaller power-of-two rules can miss the ridge identically.'))
    result=dict(baseline=BASE,cases=records)
    (ROOT/'synthetic-results.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2),flush=True)

def real(path):
    start=time.perf_counter();src=elevation.EnviGrid(path)
    identity=dict(path=str(path),bytes=Path(path).stat().st_size,sha256=elevation.sha256_file(path) if hasattr(elevation,'sha256_file') else None)
    # Pin the input without a full in-memory read.
    h=hashlib.sha256()
    with open(path,'rb') as fh:
      for b in iter(lambda:fh.read(8*1024*1024),b''):h.update(b)
    identity['sha256']=h.hexdigest()
    identity.update(width=src.storage_width,height=src.height,lon_step=src.lon_step,lat_step=src.lat_step)
    pts=probes();rows=[];timings={str(n):0. for n in NS}
    for index,(addr,meta) in enumerate(pts.items()):
      row=dict(**meta,face=addr[0],level=addr[1],i=addr[2],j=addr[3]);row['lat'],row['lon']=latlon(addr)
      row['source_span_x'],row['source_span_y'],row['source_span_max']=source_span(addr,src)
      for n in NS:
        t=time.perf_counter();row[f'n{n}']=scalar(src,*addr,n);timings[str(n)]+=time.perf_counter()-t
      rows.append(row)
      if (index+1)%100==0:print(f'Completed {index+1}/{len(pts)} scalar probes',flush=True)
    # Deepen the largest residual cases; N128/N256 remain numerical references.
    chosen=sorted(range(len(rows)),key=lambda k:abs(rows[k]['n32']-rows[k]['n64']),reverse=True)[:32]
    chosen+=sorted(range(len(rows)),key=lambda k:abs(rows[k]['n16']-rows[k]['n64']),reverse=True)[:16]
    chosen=sorted(set(chosen));deep=[]
    for idx in chosen:
      row=rows[idx];addr=(row['face'],row['level'],row['i'],row['j'])
      x=dict(row)
      for n in (128,256):
        t=time.perf_counter();x[f'n{n}']=scalar(src,*addr,n);timings[str(n)]=timings.get(str(n),0.)+time.perf_counter()-t
      deep.append(x)
    comparisons={}
    for a,b in ((1,64),(2,8),(2,64),(4,8),(4,64),(8,16),(8,64),(16,32),(16,64),(32,64)):
      comparisons[f'N{a}_to_N{b}']=stats([abs(r[f'n{a}']-r[f'n{b}']) for r in rows])
    for a,b in ((16,256),(32,256),(64,128),(128,256)):
      comparisons[f'deep_N{a}_to_N{b}']=stats([abs(r[f'n{a}']-r[f'n{b}']) for r in deep])
    bylevel={str(l):{f'N{a}_to_N{b}':stats([abs(r[f'n{a}']-r[f'n{b}']) for r in rows if r['level']==l]) for a,b in ((2,64),(8,64),(16,64),(32,64))} for l in LEVELS}
    bycategory={c:{f'N{a}_to_N{b}':stats([abs(r[f'n{a}']-r[f'n{b}']) for r in rows if r['category']==c]) for a,b in ((2,64),(8,64),(16,64))} for c in sorted({r['category'] for r in rows})}
    adaptive={}
    # Diagnostic choices only; no production settings change.
    for multiplier in (1,2,4):
      vals=[];calls=0;clipped=0;counts={}
      for r in rows:
        target=max(2,multiplier*r['source_span_max']);n=next((k for k in NS[1:] if k>=target),64)
        clipped+=target>64;counts[n]=counts.get(n,0)+1;calls+=n*n;vals.append(abs(r[f'n{n}']-r['n64']))
      adaptive[str(multiplier)]=dict(difference_vs_N64=stats(vals),chosen_n_counts=counts,source_sample_calls=calls,clipped_at_64=clipped)
    result=dict(baseline=BASE,source=identity,probe_count=len(rows),deep_probe_count=len(deep),levels=LEVELS,ns=NS,comparisons=comparisons,by_level=bylevel,by_category=bycategory,diagnostic_spacing_choices=adaptive,timing_seconds_by_n=timings,total_elapsed_seconds=time.perf_counter()-start,peak_rss_kib=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,python=platform.python_version(),platform=platform.platform(),limitation='Bounded same-interpolant quadrature convergence, not source accuracy, exact area integration, a global error bound or a production adaptive guarantee.')
    (ROOT/'real-results.json').write_text(json.dumps(result,indent=2)+'\n')
    for name,rr in (('probes.csv',rows),('deep-probes.csv',deep)):
      with (ROOT/name).open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(rr[0]),lineterminator='\n');writer.writeheader();writer.writerows(rr)
    src.close();print(json.dumps(result,indent=2),flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path);p.add_argument('--synthetic',action='store_true');a=p.parse_args()
    if a.synthetic:synthetic()
    if a.source:real(a.source)
