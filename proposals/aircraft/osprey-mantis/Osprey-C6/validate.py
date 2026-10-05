#!/usr/bin/env python3
"""Read the delivered C++ rows, check the authoring contract, emit validation.json."""
import ast,json,math,re
from pathlib import Path
ROOT=Path(__file__).resolve().parent

def row(name):
 s=(ROOT/name).read_text();s=re.sub(r'//[^\n]*','',s).strip().rstrip(',')
 s=re.sub(r'(?<=[0-9])f\b','',s);s=re.sub(r'vec3\(([^()]*)\)',r'[\1]',s)
 for a,b in {'ENG_PISTON':'0','LIC_CPL':'2','false':'False','true':'True'}.items():s=re.sub(r'\b'+a+r'\b',b,s)
 return ast.literal_eval(s.replace('{','[').replace('}',']'))
a=row('AircraftSpec.inc');d=row('ModelDef.inc')
names='id name role engineType engines cylinders blades idleRpm maxRpm emptyMass maxFuel cargoKg pax wingArea span chord CL0 CLa CLmax flapCL CD0 gearCD flapCD oswald power v0 vr vref cruise rangeKm runwayM roughOK taildragger retract Ixx Iyy Izz elevPow ailPow rudPow license price rentFee fusLen fusRad wingY wingZ engLayout tail colBase colStripe special'.split()
assert len(a)==len(names),(len(a),len(names))
s=dict(zip(names,a)); assert len(d)==35,len(d)
st,roundness,wing=d[:3]
# ModelDef top-level positions follow the actual C++ aggregate, not a secondary shape definition.
ht,ttail,vt=d[9:12];engine,nx,ny,nr,nz,nlen,sr,pr=d[12:20];gear,wr,pod=d[20:23];wc,wz0,wz1,wy,ww,wh=d[23:29];eye,cockpit,w0,w1,wbase,side=d[29:35]

def section(z):
 z=max(st[0][0],min(st[-1][0],z));i=next((k for k in range(7) if z<=st[k+1][0]),6)
 a,b=st[i:i+2];h=b[0]-a[0];t=(z-a[0])/h
 def slope(d0,d1,h0,h1):return 0 if d0*d1<=0 else 3*(h0+h1)/((2*h1+h0)/d0+(h1+2*h0)/d1)
 out=[]
 for c in range(1,4):
  dd=(b[c]-a[c])/h;ma=mb=dd*.5
  if i>0:
   p=st[i-1];h0=a[0]-p[0];ma=slope((a[c]-p[c])/h0,dd,h0,h)
  if i<6:
   n=st[i+2];h1=n[0]-b[0];mb=slope(dd,(n[c]-b[c])/h1,h,h1)
  out.append(a[c]*(2*t**3-3*t*t+1)+ma*h*(t**3-2*t*t+t)+b[c]*(-2*t**3+3*t*t)+mb*h*(t**3-t*t))
 return out
m=s['emptyMass']+s['maxFuel']+s['cargoKg']+90*s['pax']+90
vs1=math.sqrt(2*m*9.81/(1.225*s['wingArea']*s['CLmax']));vs0=math.sqrt(2*m*9.81/(1.225*s['wingArea']*(s['CLmax']+s['flapCL'])))
ix=.12*m*(s['span']/2)**2;iy=.18*m*(s['fusLen']/2)**2;gh=s['fusRad']*1.3+.75
hw,hh,cy=section(eye[2]);panelz=eye[2]-.68
# Conservative upper-surface bound uses full thickness ratio times root chord.
wingtop=wing[4]+wing[1]*wing[7];floor=eye[1]-1.08
track=max(1.2,.13*s['span']);f=track/wing[0];le=wing[5]+wing[3]*f;te=le+wing[1]+(wing[2]-wing[1])*f
v={'reference_mass_kg':m,'game_full_load_85kg_people_mass_kg':m-5*(s['pax']+1),'wing_loading_N_m2':m*9.81/s['wingArea'],'vs1_m_s':vs1,'vs0_m_s':vs0,'rotate_ratio':s['vr']/vs0,'approach_ratio':s['vref']/vs0,'cruise_approach_ratio':s['cruise']/s['vref'],'power_loading_kg_kW':m/(s['power']/1000*s['engines']),'inertia_reference':[ix,iy,ix+iy],'inertia_supplied':[s['Ixx'],s['Iyy'],s['Izz']],'wing_trapezoid_area_m2':wing[0]*(wing[1]+wing[2]),'area_difference_percent':100*abs(wing[0]*(wing[1]+wing[2])-s['wingArea'])/s['wingArea'],'gear_height_m':gh,'prop_ground_clearance_m':gh+ny-pr,'prop_fuselage_lateral_gap_m':nx-pr-max(q[1] for q in st),'eye_centreline_roof_clearance_m':cy+hh-eye[1],'eye_roof_clearance_m':cy+(hh-.035)*math.sqrt(max(1-(abs(eye[0])/(hw-.035))**2,0))-eye[1],'eye_side_clearance_m':hw-abs(eye[0]),'panel_z_m':panelz,'panel_half_width_m':.93*section(panelz)[0],'floor_y_m':floor,'wing_root_conservative_upper_y_m':wingtop,'floor_wing_clearance_m':floor-wingtop,'main_gear_xz':[track,.04*s['fusLen']],'wing_LE_TE_at_main_gear_z':[le,te]}
# Full gear-bay envelope, not merely its centre, must remain ahead of flap hinge.
bay_aft=.04*s['fusLen']+wr+.08
bay_x=[track-.17,track+.17]
hinges=[wing[5]+wing[3]*(x/wing[0])+.74*(wing[1]+(wing[2]-wing[1])*(x/wing[0])) for x in bay_x]
v['main_bay_aft_z_m']=bay_aft
v['main_bay_min_fixed_hinge_z_m']=min(hinges)
v['main_bay_fixed_panel_clearance_m']=min(hinges)-bay_aft
v['main_bay_previous_clearance_m']=min(hinges)-bay_aft-.55
v['spec_model_wing_z_match']=s['wingZ']==wing[5]
checks={
 'schema_field_counts':len(a)==52 and len(d)==35,
 'wing_loading':400<=v['wing_loading_N_m2']<=1200,
 'rotate_ratio':1.10<=v['rotate_ratio']<=1.15,
 'approach_ratio':1.25<=v['approach_ratio']<=1.35,
 'speed_order':s['cruise']>=1.6*s['vref']>s['vr']>vs0,
 'power_loading':5<=v['power_loading_kg_kW']<=9,
 'inertias':all(.6<=actual/ref<=1.4 for actual,ref in zip(v['inertia_supplied'],v['inertia_reference'])),
 'station_order':len(st)==8 and all(st[i][0]<st[i+1][0] for i in range(7)),
 'tip_closure':max(st[0][1:3]+st[-1][1:3])<=.1,
 'length_radius_span':abs(st[-1][0]-st[0][0]-s['fusLen'])<.01 and abs(max(q[2] for q in st)-s['fusRad'])<.01 and abs(2*wing[0]-s['span'])<.01,
 'wing_area':v['area_difference_percent']<10,
 'wing_thickness':.1<=wing[7]<=.16,
 'prop_clearance':v['prop_ground_clearance_m']>=.25 and v['prop_fuselage_lateral_gap_m']>0,
 'eye_clearance':v['eye_roof_clearance_m']>=.08 and v['eye_side_clearance_m']>=.08,
 'windshield_before_panel':w0<w1<panelz<eye[2] and wbase<section(w1)[1]+section(w1)[2],
 'floor_above_wing':floor>wingtop,
 'gear_bay_clear_of_controls':v['main_bay_fixed_panel_clearance_m']>=.05,
 'matching_wing_root':s['wingY']==wing[4] and s['wingZ']==wing[5],
 'gear_supported':gear==4 and le<.04*s['fusLen']<te and s['retract'],
 'windows_inside':all(wy+wh<.8*section(wz0+(wz1-wz0)*i/100)[1] for i in range(101)),
 'conventional_controls':.38<=s['elevPow']<=.45 and .045<=s['ailPow']<=.07 and .05<=s['rudPow']<=.07,
 'tier_a':s['special']==0 and engine==2 and s['engLayout']==1 and s['tail']==ttail==0,
 'career':40<=s['rangeKm']<=300 and s['pax']==5 and s['license']==2 and 40000<s['price']<85000 and 450<s['rentFee']<900,
}
out={'aircraft':s['name'],'baseline':'a298771','checks':checks,'measurements':v,'all_passed':all(checks.values()),'scope':'Analytic checks parsed from delivered rows; no simulation assertions implied.'}
(ROOT/'validation.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2));assert out['all_passed']
