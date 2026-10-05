import json,pathlib,itertools,numpy as np
D=pathlib.Path(__file__).parent
lines=['    if(partOn(40)) {','        // MANTIS angular graphite/amber research station. All details static.']
records=[]
def v(x):return 'vec3('+','.join(f'{t:.6f}' for t in x)+')'
def box(name,c,b,m=113,r=.008):
 lines.append(f'        // {name}\n        r=opU(r,vec2(sdRoundBox(p-{v(c)},{v(b)},{r:.6f}),{float(m):.1f}));')
 records.append(dict(name=name,center=c,half=b,kind='box',material=m,radius=r))
def cap(name,a,b,r=.012,m=119):
 lines.append(f'        // {name}\n        r=opU(r,vec2(sdCapsule(p,{v(a)},{v(b)},{r:.6f}),{float(m):.1f}));')
 records.append(dict(name=name,center=((np.array(a)+b)/2).tolist(),half=(abs(np.array(a)-b)/2+r).tolist(),kind='capsule',material=m,radius=r,a=a,b=b))
box('floor cassette',(0,-.385,-3.35),(.38,.025,.70))
for s in [-1,1]:
 box('seat floor rail',(s*.205,-.32,-2.99),(.022,.045,.28),119)
box('seat pan carbon shell',(0,-.205,-2.98),(.275,.045,.30))
box('seat pan cushion',(0,-.14,-2.98),(.235,.055,.265),114,.025)
box('seat back shell',(0,.175,-2.615),(.28,.32,.055))
box('seat back pad',(0,.175,-2.68),(.218,.30,.040),114,.018)
for s in [-1,1]:
 box('lumbar side bolster',(s*.25,.04,-2.755),(.035,.175,.09),114,.016)
 box('pan side bolster',(s*.245,-.10,-2.99),(.022,.055,.23),114,.015)
 box('shoulder harness',(s*.10,.205,-2.726),(.026,.265,.010),116,.004)
 cap('harness lower diagonal',(s*.10,-.055,-2.726),(s*.055,-.11,-2.83),.014,116)
 box('lap webbing',(s*.115,-.075,-2.86),(.090,.010,.032),116,.004)
box('harness release buckle',(0,-.07,-2.84),(.036,.018,.040),119)
box('buckle release face',(0,-.050,-2.84),(.025,.009,.027),111,.004)
box('headrest shell',(0,.562,-2.69),(.178,.100,.065))
box('headrest cushion',(0,.562,-2.762),(.15,.083,.022),114,.014)
for s in [-1,1]:box('headrest datum',(s*.075,.58,-2.788),(.013,.045,.009),115,.004)
box('pitch fixed pivot socket',(.32,-.315,-3.15),(.050,.060,.055))
box('throttle fixed pivot socket',(-.37,-.3025,-3.12),(.050,.0725,.055))
for s in [-1,1]:box('pedal fixed slider rail',(s*.17,-.335,-3.90),(.030,.022,.24),119)
# Side consoles reserve all pitch/throttle lever swept volume.
for s in [-1,1]:
 box('side avionics console',(s*.488,-.16,-3.49),(.067,.070,.37))
 box('side service touch slab',(s*.489,-.080,-3.66),(.049,.012,.13),117)
 for z in [-3.40,-3.34,-3.28]:
  box('static test key',(s*.489,-.075,z),(.029,.015,.019),119,.005)
 cap('console amber task strip',(s*.42,-.095,-3.83),(s*.42,-.095,-3.49),.010,115)
 # rear equipment ribs and fully enclosed vent bays
 box('rear vent housing',(s*.384,.12,-2.66),(.065,.16,.040))
 for y in [-.005,.045,.095,.145,.195,.245]:
  box('rear vent louver',(s*.384,y,-2.708),(.050,.010,.010),118,.004)
 # vertical sill, outside side pane
 cap('side structural longeron',(s*.552,.095,-3.97),(s*.552,.095,-2.94),.018,119)
# aft pressure arch, below curved roof envelope
for z in [-3.02]:
 for s in [-1,1]:
  cap('arch leg',(s*.555,.06,z),(s*.555,.42,z),.020,113)
  cap('arch chamfer',(s*.555,.42,z),(s*.365,.675,z),.020,113)
 cap('arch crown',(-.365,.675,z),(.365,.675,z),.020,113)
# roof recessed task fixture, forward of pilot eye
for s in [-1,1]:
 box('ceiling task housing',(s*.215,.731,-3.58),(.070,.020,.20))
 box('ceiling task diffuser',(s*.215,.706,-3.58),(.045,.010,.17),115,.005)
# Forward avionics blade and protected switch bank
box('instrument blade',(0,.050,-4.055),(.435,.175,.045))
for s in [-1,1]:
 box('instrument surround',(s*.266,.07,-4.00),(.140,.115,.025),119)
 box('instrument face',(s*.266,.07,-3.970),(.118,.093,.012),117)
 for x in [.18,.25,.32]:
  cap('static switch stem',(s*x,-.100,-3.997),(s*x,-.100,-3.967),.011,119)
 box('switch guard',(s*.27,-.145,-3.98),(.12,.010,.025),111,.004)
box('center systems panel',(0,.05,-3.99),(.082,.112,.027))
for y in [-.025,.025,.075,.125]:
 box('center annunciator',(0,y,-3.953),(.051,.012,.011),115,.004)
# closed feed slabs with individual four-piece bezels
box('front camera slab',(0,.410,-4.135),(.410,.177,.012),112,.006)
for x in [-.428,.428]:box('front pane side frame',(x,.410,-4.128),(.012,.190,.023),119,.004)
for y in [.215,.605]:box('front pane horizontal frame',(0,y,-4.128),(.436,.010,.023),119,.004)
for s in [-1,1]:
 box('side camera slab',(s*.559,.365,-3.51),(.012,.172,.370),112,.006)
 for z in [-3.896,-3.124]:box('side pane end frame',(s*.551,.365,z),(.023,.185,.010),119,.004)
 for y in [.176,.554]:box('side pane horizontal frame',(s*.551,y,-3.51),(.023,.011,.390),119,.004)
# Load paths: fixtures, consoles and pane frames are physically bracketed.
for s in [-1,1]:
 cap('ceiling fixture carrier',(s*.215,.715,-3.58),(s*.215,.675,-3.02),.014,113)
 for z in [-3.75,-3.35]:
  cap('console floor outrigger',(s*.47,-.20,z),(s*.34,-.365,z),.018,113)
  cap('sill console support',(s*.55,-.16,z),(s*.552,.095,z),.016,113)
 for z in [-3.8,-3.2]:cap('pane sill bracket',(s*.551,.17,z),(s*.552,.095,z),.015,119)
 cap('instrument floor support',(s*.30,-.10,-4.02),(s*.30,-.365,-3.98),.018,113)
 cap('vent seat bracket',(s*.25,.12,-2.65),(s*.38,.12,-2.65),.016,113)
lines+=['    }']
S='\n'.join(lines)+'\n';(D/'part40_replacement.glsl').write_text(S)
base=(D/'mantis.glsl').read_text();start=base.index('    if(partOn(40))');end=base.index('    if(partOn(41))',start)
(D/'mantis.glsl').write_text(base[:start]+S+base[end:])
# Convex cavity: if all AABB vertices lie inside, every contained primitive lies inside.
rows=[]
for r in records:
 pts=np.array(r['center'])+np.array(list(itertools.product([-1,1],repeat=3)))*r['half']
 q=abs(pts-[0,.16,-3.35])-[.65,.66,1.05]+.31
 d=np.linalg.norm(np.maximum(q,0),axis=1)+np.minimum(np.max(q,axis=1),0)-.31
 rows.append(dict(name=r['name'],max_cavity_sdf_m=float(d.max()),guaranteed_clearance_m=float(-d.max())))
result=dict(method='Exact convex roundBox cavity signed distance at all primitive AABB corners. Contains all geometry, conservative for rounded boxes/capsules.',component_count=len(records),minimum_guaranteed_clearance_m=min(r['guaranteed_clearance_m'] for r in rows),all_inside=all(r['guaranteed_clearance_m']>0 for r in rows),rows=rows)
(D/'cavity_clearance.json').write_text(json.dumps(result,indent=2));(D/'components.json').write_text(json.dumps(records,indent=2))
print('count',len(records),'min clearance',result['minimum_guaranteed_clearance_m']);print([r for r in rows if r['guaranteed_clearance_m']<.008])
