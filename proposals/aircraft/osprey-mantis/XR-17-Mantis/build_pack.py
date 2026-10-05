import json, math, pathlib
D=pathlib.Path(__file__).parent
parts=[]
def part(i,name,kind='STATIC',pivot=None,axis=None,state=None,k=0,c=0,**kw):
 d=dict(id=i,name=name,kind=kind,**kw)
 if kind!='STATIC':d.update(pivot=pivot or [0,0,0],axis=axis or [0,1,0],state=state,k=k,c=c)
 parts.append(d)
part(0,'pressure hull and permanent cavities')
part(1,'fixed wings and rear fin')
part(2,'fixed jet nacelles and sensor details')
for i,sg in [(10,-1),(11,1)]:part(i,('port' if sg<0 else 'starboard')+' all-moving canard','HINGE',[sg*.85,-.15,-3.6],[1,0,0],'gCtl.x',.22)
for i,sg in [(12,-1),(13,1)]:part(i,('port' if sg<0 else 'starboard')+' aileron','HINGE',[sg*(3.45+.9363291776*1.16+.3511234416*.62),-.62,.85-.3511234416*1.16+.9363291776*.62],[.9363291776*sg,0,-.3511234416],'gCtl.y',-.3)
for i,sg in [(14,-1),(15,1)]:part(i,('port' if sg<0 else 'starboard')+' flap','HINGE',[sg*(3.45-.9363291776*1.13+.3511234416*.62),-.62,.85+.3511234416*1.13+.9363291776*.62],[.9363291776*sg,0,-.3511234416],'gPS.y',sg*.48)
part(16,'rudder','HINGE',[0,1.22+.342020*.49,5.17+.939693*.49],[0,.939693,-.342020],'gCtl.z',.3)
for i,x,z in [(20,-1.6,.6),(21,1.6,.6),(22,0,-5.76)]:part(i,'landing gear '+str(i),'SLIDE',axis=[0,-1,0],state='gPS.x',k=1.295,rest_center=[x,-.28,z])
for i,x,z in [(26,-1.6,.6),(27,1.6,.6),(28,0,-5.76)]:part(i,'telescopic upper support '+str(i),'STRETCH',pivot=[x,-.35,z],axis=[0,-1,0],state='gPS.x',k=1.295,c=.25,rest_length=.25)
for i,x,z in [(23,-1.6,.6),(24,1.6,.6),(25,0,-5.76)]:part(i,'gear shutter '+str(i),'SLIDE',axis=([-1,0,0] if i==23 else ([1,0,0] if i==24 else [0,0,1])),state='gPS.x',k=(.64 if i!=25 else 1.06),rest_center=[x,-.84,z])
for i,sg in [(30,-1),(31,1)]:part(i,'bay door '+('port' if sg<0 else 'starboard'),'HINGE',[sg*.59,-.94,1.6],[0,0,1],'uCustom[0]',sg*1.38)
for i,sg in [(32,-1),(33,1)]:part(i,'recessed missile cradle '+str(i),'SLIDE',axis=[0,-1,0],state=f'uCustom[{i-31}]',k=.48,side=sg)
for i,sg in [(34,-1),(35,1)]:part(i,'fictional dart missile '+str(i),'SLIDE',axis=[0,-1,0],state=f'uCustom[{i-33}]',k=.48,side=sg,visibility_channel=i-30)
part(36,'internal fictional gravity store','SLIDE',axis=[0,-1,0],state='uCustom[3]',k=.9,visibility_channel=6)
part(40,'sealed cockpit shell instruments and seat')
part(41,'pitch stick','HINGE',[.32,-.23,-3.15],[1,0,0],'gCtl.x',.2)
part(42,'throttle lever','HINGE',[-.37,-.2,-3.12],[1,0,0],'gCtl.w',-.6,.3)
for i,sg in [(43,-1),(44,1)]:part(i,'rudder pedal '+str(i),'SLIDE',axis=[0,0,1],state='gCtl.z',k=sg*.06,side=sg)
(D/'parts.json').write_text(json.dumps({'name':'XR-17 Mantis','coordinate_system':'+x right,+y up,+z aft; metres','parts':parts,'channels':{'0':'bay opening 0..1','1':'port missile cradle deployment 0..1','2':'starboard missile cradle deployment 0..1','3':'bomb ejection animation 0..1','4':'port missile occupancy: 1 present, 0 released (runtime part visibility only)','5':'starboard missile occupancy: 1 present, 0 released','6':'bomb occupancy: 1 present, 0 released','7':'reserved, must be zero'}},indent=2))
lines=['// Provisional renderer contract, radians. Rig helper returns body-space REST coordinates.']
for p in parts:
 if p['kind']=='STATIC':args='STATIC'
 elif p['kind']=='HINGE':args=f"HINGE(pivot={p['pivot']}, axis={p['axis']}, angle={p['k']}*{p['state']}+{p['c']})"
 elif p['kind']=='STRETCH':args=f"STRETCH(origin={p['pivot']}, axis={p['axis']}, length={p['k']}*{p['state']}+{p['c']})"
 else:args=f"SLIDE(dir={p['axis']}, dist={p['k']}*{p['state']}+{p['c']})"
 lines.append(f'PART({p["id"]}, "{p["name"]}", {args})')
(D/'PARTS.txt').write_text('\n'.join(lines)+'\n')
