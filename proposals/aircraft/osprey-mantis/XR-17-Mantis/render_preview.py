"""Standalone software-GL preview of exact custom shader; NOT the game renderer.
Uses system Mesa via ctypes only. Binds provisional part/rig contract from parts.json.
"""
import os, ctypes as C, json, pathlib, math, sys
import numpy as np
from PIL import Image,ImageDraw
os.environ.setdefault('MESA_SHADER_CACHE_DIR',str(pathlib.Path(__file__).parent/'mesa-cache'));os.environ.setdefault('GALLIVM_PERF','nopt')
os.environ.setdefault('EGL_PLATFORM','surfaceless');os.environ.setdefault('LIBGL_ALWAYS_SOFTWARE','1')
D=pathlib.Path(__file__).parent
E=C.CDLL('libEGL.so.1'); G=C.CDLL('libGL.so.1')
def ef(n,rest,args):
 f=getattr(E,n); f.restype=rest; f.argtypes=args; return f
ptr=C.c_void_p; I=C.c_int; U=C.c_uint; F=C.c_float
get=ef('eglGetDisplay',ptr,[ptr]);init=ef('eglInitialize',U,[ptr,C.POINTER(I),C.POINTER(I)])
d=get(None); a=I();b=I();assert init(d,C.byref(a),C.byref(b))
ef('eglBindAPI',U,[U])(0x30A2)
attrs=(I*11)(0x3033,1,0x3040,8,0x3024,8,0x3023,8,0x3022,8,0x3038)
cfg=ptr();n=I();assert ef('eglChooseConfig',U,[ptr,C.POINTER(I),C.POINTER(ptr),I,C.POINTER(I)])(d,attrs,C.byref(cfg),1,C.byref(n))
pattr=(I*5)(0x3057,1000,0x3056,700,0x3038)
surf=ef('eglCreatePbufferSurface',ptr,[ptr,ptr,C.POINTER(I)])(d,cfg,pattr)
ctx=ef('eglCreateContext',ptr,[ptr,ptr,ptr,C.POINTER(I)])(d,cfg,None,(I*1)(0x3038))
assert ef('eglMakeCurrent',U,[ptr,ptr,ptr,ptr])(d,surf,surf,ctx)
def gl(n,rest,args):
 f=getattr(G,n);f.restype=rest;f.argtypes=args;return f
create=gl('glCreateShader',U,[U]); source=gl('glShaderSource',None,[U,I,C.POINTER(C.c_char_p),C.POINTER(I)]);compile=gl('glCompileShader',None,[U]);getiv=gl('glGetShaderiv',None,[U,U,C.POINTER(I)])
def shader(kind,s):
 sh=create(kind);buf=C.c_char_p(s.encode());source(sh,1,C.byref(buf),None);compile(sh);ok=I();getiv(sh,0x8B81,C.byref(ok))
 if not ok.value:
  msg=C.create_string_buffer(65536);gl('glGetShaderInfoLog',None,[U,I,C.POINTER(I),ptr])(sh,len(msg),None,msg);raise RuntimeError(msg.value.decode())
 return sh
prim=(D/'preview_primitives.glsl').read_text()
parts=json.loads((D/'parts.json').read_text())['parts']
rig='uniform vec4 gPS,gCtl; uniform float uCustom[8]; uniform int onlyPart;\nbool partOn(int id){if(onlyPart>=0)return id==onlyPart;if(onlyPart==-2&&id>=40)return false; if(id==34&&uCustom[4]<.5)return false;if(id==35&&uCustom[5]<.5)return false;if(id==36&&uCustom[6]<.5)return false;return true;}\n'
rig+='vec3 rigHinge(vec3 p,int id){vec3 o=vec3(0),a=vec3(1,0,0);float t=0.;\n'
def v(x):return 'vec3('+','.join(str(float(z)) for z in x)+')'
for p in parts:
 if p['kind']=='HINGE':rig+=f"if(id=={p['id']}){{o={v(p['pivot'])};a=normalize({v(p['axis'])});t=-({p['k']}*{p['state']}+{p['c']});}}\n"
rig+='vec3 q=p-o;return o+q*cos(t)+cross(a,q)*sin(t)+a*dot(a,q)*(1.-cos(t));}\nvec3 rigSlide(vec3 p,int id){\n'
for p in parts:
 if p['kind']=='SLIDE':rig+=f"if(id=={p['id']})return p-normalize({v(p['axis'])})*({p['k']}*{p['state']}+{p['c']});\n"
rig+='return p;}\nvec3 rigStretch(vec3 p,int id){\n'
for p in parts:
 if p['kind']=='STRETCH':rig+=f"if(id=={p['id']}){{vec3 o={v(p['pivot'])};vec3 a=normalize({v(p['axis'])});float sc=({p['k']}*{p['state']}+{p['c']})/{p['rest_length']};vec3 q=p-o;return p+a*dot(q,a)*(1./sc-1.);}}\n"
rig+='return p;}\n'
frag='''#version 330 core
out vec4 color; uniform vec3 eye,target;uniform vec2 resolution; uniform float focal; uniform vec3 cameraUp; uniform float ortho, renderMode;
'''+prim+rig+(D/'mantis.glsl').read_text()+(D/'materials_preview.glsl').read_text()+'''
vec3 shade(float m,vec3 p){
 if(m>=112. && m<=119.)return mantisCabinMaterial(m,p);
 if(m==111.)return vec3(.95,.43,.055);if(m==110.||m==8.||m==60.)return vec3(.34,.39,.43);
 if(m==112.){float line=step(.96,fract(p.x*20.))*step(.96,fract(p.y*20.));return mix(vec3(.035,.17,.20),vec3(.2,.7,.8),line);}
 if(m==67.)return vec3(.07,.25,.28);
 if(m==6.||m==61.)return vec3(.025);if(m==112.||m==67.)return vec3(.06,.33,.42);
 if(m==17.)return vec3(.09,.14,.16);if(m==21.)return vec3(.14);if(m==18.)return p.x<0.?vec3(.8,.04,.02):vec3(.03,.65,.2);
 if(m==12.)return vec3(.13,.15,.16);return vec3(.065,.085,.105);}
void main(){
 vec2 uv=(gl_FragCoord.xy-.5*resolution)/resolution.y;
 vec3 w=normalize(target-eye),u=normalize(cross(w,cameraUp)),v=cross(u,w),rd=normalize(w*focal+u*uv.x+v*uv.y);
 vec3 rayEye=eye; if(ortho>0.){rayEye+=ortho*(u*uv.x+v*uv.y);rd=w;}
 float t=0.;vec2 hit=vec2(1e4);vec3 p;bool yes=false;
 for(int i=0;i<420;i++){p=rayEye+rd*t;hit=mapCustom_Mantis(p);if(hit.x<.0015){yes=true;break;}t+=max(.001,hit.x*.83);if(t>65.)break;}
 vec3 col=mix(vec3(.07,.085,.11),vec3(.21,.24,.28),gl_FragCoord.y/resolution.y);
 if(yes){vec2 e=vec2(.003,0);vec3 n=normalize(vec3(mapCustom_Mantis(p+e.xyy).x-mapCustom_Mantis(p-e.xyy).x,mapCustom_Mantis(p+e.yxy).x-mapCustom_Mantis(p-e.yxy).x,mapCustom_Mantis(p+e.yyx).x-mapCustom_Mantis(p-e.yyx).x));vec3 l=normalize(vec3(-.6,1,-.4));float ao=1.;for(int i=1;i<5;i++){float h=float(i)*.11;ao-=max(0.,h-mapCustom_Mantis(p+n*h).x)*.35;}col=shade(hit.y,p)*(.26+.74*max(0.,dot(n,l)))*ao;if(hit.y==115.||hit.y==112.||hit.y==117.)col=mix(col,shade(hit.y,p),.62);col+=vec3(.10)*pow(max(0.,dot(reflect(-l,n),-rd)),40.);if(renderMode==2.){col=vec3(.6)*(.65+.35*abs(dot(n,rd)));}}
 if(renderMode==1.) col=yes?vec3(.65)*(.7+.3*1.):vec3(.035);
 color=vec4(pow(col,vec3(1./2.2)),1.);}
'''
(D/'preview_adapter.glsl').write_text(frag)
vs=shader(0x8B31,'#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}')
fs=shader(0x8B30,frag)
prog=gl('glCreateProgram',U,[])();att=gl('glAttachShader',None,[U,U]);att(prog,vs);att(prog,fs);gl('glLinkProgram',None,[U])(prog);gl('glUseProgram',None,[U])(prog)
vao=U();gl('glGenVertexArrays',None,[I,C.POINTER(U)])(1,C.byref(vao));gl('glBindVertexArray',None,[U])(vao)
loc=gl('glGetUniformLocation',I,[U,C.c_char_p])
def uniform(n,vals):
 l=loc(prog,n.encode()); gl('glUniform'+str(len(vals))+'f',None,[I]+[F]*len(vals))(l,*vals)
def frame(name,eye,target,gear=0,flap=0,ctl=(0,0,0,0),custom=(0,0,0,0,1,1,1,0),part=-1,up=(0,1,0),ortho=0,mode=0):
 w,h=1000,700;gl('glViewport',None,[I,I,I,I])(0,0,w,h);uniform('eye',eye);uniform('target',target);uniform('resolution',(w,h));uniform('focal',(.70 if 'cockpit' in name else 1.95,));uniform('gPS',(gear,flap,0,0));uniform('gCtl',ctl)
 uniform('cameraUp',up);uniform('ortho',(ortho,));uniform('renderMode',(mode,))
 gl('glUniform1i',None,[I,I])(loc(prog,b'onlyPart'),part)
 for i,a in enumerate(custom):uniform(f'uCustom[{i}]',(a,))
 gl('glDrawArrays',None,[U,I,I])(4,0,3);gl('glFinish',None,[])();buf=(C.c_ubyte*(w*h*3))();gl('glReadPixels',None,[I,I,I,I,U,U,ptr])(0,0,w,h,0x1907,0x1401,buf)
 im=Image.frombytes('RGB',(w,h),bytes(buf)).transpose(Image.Transpose.FLIP_TOP_BOTTOM);draw=ImageDraw.Draw(im);draw.text((24,22),'XR-17 MANTIS / '+name.replace('_',' ').upper(),fill=(240,180,90));draw.text((24,h-30),'ACTUAL SDF GEOMETRY / SOFTWARE GL ADAPTER / NOT IN-GAME',fill=(180,190,200));im.save(D/(name+'.png'));print(name,flush=True)
if __name__=='__main__':
 frame('cockpit_front',(0,.65,-3.35),(0, 0.2, -4.17))
 frame('cockpit_left',(0,.65,-3.35),(-0.55, 0.22, -3.48))
 frame('cockpit_right',(0,.65,-3.35),(0.55, 0.22, -3.48))
 frame('cockpit_down',(0,.65,-3.35),(0, -0.23, -3.43))
 frame('cockpit_aft',(0,.65,-3.35),(0, 0.17, -2.64))
 frame('01_front_quarter',(15,9,-20),(0,0,0),gear=1)
 frame('02_top_plan',(0,24,0),(0,0,0),up=(0,0,1),ortho=19)
 frame('03_bay_open',(12,-12,-15),(0,-.3,0),gear=1,flap=1,ctl=(.6,.6,.5,.5),custom=(1,1,1,0,1,1,1,0))
 frame('04_rear_quarter',(-14,7,18),(0,0,0),gear=0)
 frame('05_cockpit',(0,.65,-3.35),(0,.28,-4.25))
 frame('07_controls_negative',(10,12,-20),(0,0,0),gear=1,ctl=(-1,-1,-1,0))
 frame('08_controls_positive',(10,12,-20),(0,0,0),gear=1,flap=1,ctl=(1,1,1,1),custom=(1,1,1,0,1,1,1,0))
 frame('06_release_clearance',(8,-8,-9),(0,-.5,0),custom=(1,1,1,1,1,1,1,0))
 (D/'preview_status.json').write_text(json.dumps({'shader_compile':'PASS','renderer':'system Mesa software GL EGL','views':22 if '--audit' in sys.argv else 13,'in_game':False},indent=2))

 if '--audit' in sys.argv:
  for suffix,eye,up in [('front',(0,0,-24),(0,1,0)),('rear',(0,0,24),(0,1,0)),('top',(0,24,0),(0,0,1)),('belly',(0,-24,0),(0,0,-1))]:
   frame('audit_neutral_'+suffix,eye,(0,0,0),up=up,ortho=19,mode=1)
   frame('audit_even_'+suffix,eye,(0,0,0),up=up,ortho=19,mode=2)
  frame('audit_gear_belly',(0,-24,0),(0,0,0),gear=1,up=(0,0,-1),ortho=19,mode=1)
