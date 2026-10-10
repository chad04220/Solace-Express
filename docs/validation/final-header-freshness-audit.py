from pathlib import Path
import re, struct, hashlib, json
from datetime import datetime, timezone
ROOT=Path('/workspace/scratch/87b29863f015')
REPO=ROOT/'solace-new-aircraft'
THRESHOLD_NS=int(datetime.fromisoformat('2026-10-10T21:36:00+00:00').timestamp()*1_000_000_000)
OUT=ROOT/'new-aircraft-review/validation/final-header-freshness-independent.json'
NAMES=('solace-new-aircraft-build','solace-new-aircraft-windows','solace-new-aircraft-sanitize')
def iso(ns):return datetime.fromtimestamp(ns/1e9,timezone.utc).isoformat()
def digest(b):return hashlib.sha256(b).hexdigest()
def deps_parse(b):
    assert b[:12]==b'# ninjadeps\n' and struct.unpack_from('<I',b,12)[0]==4
    pos=16;paths=[];deps={};error=None;records=0
    while pos<len(b):
        at=pos
        if pos+4>len(b):error=f'incomplete record header at byte {pos}';break
        size=struct.unpack_from('<I',b,pos)[0];pos+=4;isdep=bool(size&0x80000000);size&=0x7fffffff
        if size%4 or pos+size>len(b):error=f'incomplete/invalid record at byte {at}, size {size}, available {len(b)-pos}';break
        record=b[pos:pos+size];pos+=size;records+=1
        if isdep:
            ids=struct.unpack('<'+'I'*(size//4),record)
            if len(ids)<3 or ids[0]>=len(paths) or any(i>=len(paths) for i in ids[3:]):error=f'invalid dependency path reference at byte {at}';break
            deps[paths[ids[0]]]=[paths[i] for i in ids[3:]]
        else:
            path_id=~struct.unpack_from('<I',record,size-4)[0]&0xffffffff
            if path_id!=len(paths):error=f'path checksum mismatch at byte {at}: expected path ID {len(paths)}, found {path_id}';break
            paths.append(record[:-4].rstrip(b'\0').decode())
    return deps,{'raw_sha256':digest(b),'raw_bytes':len(b),'records_read':records,'path_count':len(paths),'object_records':len(deps),'parse_error':error}
closures={}
def closure(source,base):
    key=(source,str(base))
    if key in closures:return closures[key]
    seen=set();unresolved=[]
    def visit(p):
        p=p.resolve()
        if p in seen:return
        seen.add(p)
        try:src=p.read_text()
        except (OSError,UnicodeError):return
        for delimiter,header in re.findall(r'^\s*#\s*include\s*(["<])([^">]+)[">]',src,re.M):
            candidates=(p.parent/header,REPO/'src'/header,base/'gen'/header)
            for q in candidates:
                if q.is_file():visit(q);break
            else:
                if delimiter=='"':unresolved.append({'file':str(p),'include':header})
    visit(Path(source));closures[key]=(seen,unresolved);return seen,unresolved
report={'review_utc':datetime.now(timezone.utc).isoformat(),'threshold_utc':iso(THRESHOLD_NS),'method':'Read-only binary dependency parser plus conservative recursive local include closure for every on-disk object in the current build graph. Conditional local includes are included conservatively. No Ninja command was run against original metadata. No builds, object cleaning, touches or production edits. Compile start is cross-checked with Ninja v7 command timestamps and object mtime minus command duration.','source':{},'builds':{}}
for source in ('src/aircraft.h','src/aircraft.cpp'):
    p=REPO/source;report['source'][source]={'sha256':digest(p.read_bytes()),'mtime_utc':iso(p.stat().st_mtime_ns)}
for name in NAMES:
    base=ROOT/name;p=base/'.ninja_deps';before=p.stat();raw=p.read_bytes();after=p.stat()
    assert (before.st_size,before.st_mtime_ns)==(after.st_size,after.st_mtime_ns),'Dependency DB changed while reading '+name
    deps,metadata=deps_parse(raw)
    ninja=(base/'build.ninja').read_text()
    nodes=dict(re.findall(r'^build (\S+\.(?:o|obj)): CXX_COMPILER\S* (\S+)',ninja,re.M))
    logs={}
    for line in (base/'.ninja_log').read_text().splitlines()[1:]:
        fields=line.split('\t')
        if len(fields)==5:logs[fields[3]]=fields
    actual={str(p.relative_to(base)) for ext in ('*.o','*.obj') for p in base.rglob(ext)}
    consumers=[];older_nonconsumers=[];unresolved=[]
    for obj,source in nodes.items():
        p=base/obj
        if not p.exists():continue
        includes,missing=closure(source,base)
        mtime=p.stat().st_mtime_ns
        known=any(Path(x).name=='aircraft.h' for x in deps.get(obj,[]))
        is_consumer=known or (REPO/'src/aircraft.h') in includes
        row={'object':obj,'source':str(Path(source).relative_to(REPO)),'mtime_utc':iso(mtime),'mtime_after_cutoff':mtime>=THRESHOLD_NS,'dependency_record_present':obj in deps,'dependency_record_includes_aircraft_h':known}
        if is_consumer:
            log=logs.get(obj)
            if log:
                command_ns=int(log[2]);estimated_ns=mtime-(int(log[1])-int(log[0]))*1_000_000
                row.update({'ninja_v7_command_timestamp_utc':iso(command_ns),'estimated_compile_start_utc':iso(estimated_ns),'command_and_estimate_after_cutoff':min(command_ns,estimated_ns)>=THRESHOLD_NS})
            else:row['command_and_estimate_after_cutoff']=False
            consumers.append(row)
        elif mtime<THRESHOLD_NS:
            row['local_include_closure']=[str(x.relative_to(REPO)) if x.is_relative_to(REPO) else str(x) for x in sorted(includes)]
            older_nonconsumers.append(row)
        unresolved.extend(missing)
    known_consumers=[o for o,inc in deps.items() if any(Path(x).name=='aircraft.h' for x in inc) and (base/o).exists()]
    stale=[row for row in consumers if not row['mtime_after_cutoff'] or not row['command_and_estimate_after_cutoff']]
    report['builds'][name]={'dependency_metadata':metadata,'object_count':len(actual),'unmapped_objects':sorted(actual-nodes.keys()),'known_dependency_consumers':len(known_consumers),'conservative_plane_consumer_count':len(consumers),'stale_or_unproven_consumers':stale,'all_consumers':consumers,'older_nonconsumers':older_nonconsumers,'unresolved_quoted_local_includes':sorted({json.dumps(x,sort_keys=True) for x in unresolved}),'precompiled_or_unity_detected':bool(re.search('cmake_pch|PRECOMPILE_HEADERS|UNITY_BUILD',ninja)),'forced_include_flag_detected':bool(re.search(r'(?:-include\s|/FI)',ninja))}
    print(name,'objects',len(actual),'Plane consumers',len(consumers),'stale',len(stale),'old independent',len(older_nonconsumers),'metadata error',metadata['parse_error'])
report['result']='PASS: no stale or unproven Plane ABI consumer' if all(not x['stale_or_unproven_consumers'] and not x['unmapped_objects'] and not x['unresolved_quoted_local_includes'] for x in report['builds'].values()) else 'INVESTIGATE'
OUT.parent.mkdir(parents=True,exist_ok=True);OUT.write_text(json.dumps(report,indent=2)+'\n')
print(report['result']);print(OUT)
