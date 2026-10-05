"""Exact GLSL SDF query harness, software Mesa, no reimplemented geometry."""
import ctypes as C
import numpy as np
import render_preview as r
I,U,F,ptr=r.I,r.U,r.F,r.ptr
gl=r.gl
frag='''#version 330 core
out vec4 color; uniform sampler2D positions; uniform int queryMode;
'''+r.prim+r.rig+(r.D/'mantis.glsl').read_text()+'''
void main(){vec3 p=texelFetch(positions,ivec2(gl_FragCoord.xy),0).xyz;vec2 d=mapCustom_Mantis(p);
if(queryMode==1)d=vec2(min(mantisWingSolid(p,-1.),mantisWingSolid(p,1.)),0.);
if(queryMode==2)d=vec2(mantisFinSolid(p),0.);
if(queryMode==3)d=vec2(mantisHullSolid(p),0.);
if(queryMode==4)d=vec2(mantisCabinCavity(p),0.);
color=vec4(d,0.,1.);}
'''
fs=r.shader(0x8B30,frag); prog=gl('glCreateProgram',U,[])();r.att(prog,r.vs);r.att(prog,fs);gl('glLinkProgram',None,[U])(prog);gl('glUseProgram',None,[U])(prog);r.prog=prog
tex=(U*2)();gl('glGenTextures',None,[I,C.POINTER(U)])(2,tex)
fbo=U();gl('glGenFramebuffers',None,[I,C.POINTER(U)])(1,C.byref(fbo));gl('glBindFramebuffer',None,[U,U])(0x8D40,fbo)
def query(points,part=-2,gear=0,flap=0,ctl=(0,0,0,0),custom=(0,0,0,0,1,1,1,0),mode=0):
 p=np.asarray(points,dtype=np.float32).reshape(-1,3); n=len(p);w=512;h=(n+w-1)//w
 data=np.zeros((h*w,4),np.float32);data[:n,:3]=p
 gl('glActiveTexture',None,[U])(0x84C0)
 gl('glBindTexture',None,[U,U])(0x0DE1,tex[0]);gl('glTexImage2D',None,[U,I,I,I,I,I,U,U,ptr])(0x0DE1,0,0x8814,w,h,0,0x1908,0x1406,data.ctypes.data)
 for key in (0x2800,0x2801):gl('glTexParameteri',None,[U,U,I])(0x0DE1,key,0x2600)
 gl('glBindTexture',None,[U,U])(0x0DE1,tex[1]);gl('glTexImage2D',None,[U,I,I,I,I,I,U,U,ptr])(0x0DE1,0,0x8814,w,h,0,0x1908,0x1406,None)
 gl('glFramebufferTexture2D',None,[U,U,U,U,I])(0x8D40,0x8CE0,0x0DE1,tex[1],0)
 assert gl('glCheckFramebufferStatus',U,[U])(0x8D40)==0x8CD5
 gl('glBindTexture',None,[U,U])(0x0DE1,tex[0]);gl('glViewport',None,[I,I,I,I])(0,0,w,h)
 for name,val in [('onlyPart',part),('positions',0),('queryMode',mode)]:gl('glUniform1i',None,[I,I])(r.loc(prog,name.encode()),val)
 r.uniform('gPS',(gear,flap,0,0));r.uniform('gCtl',ctl)
 for i,a in enumerate(custom):r.uniform(f'uCustom[{i}]',(a,))
 gl('glDrawArrays',None,[U,I,I])(4,0,3);gl('glFinish',None,[])()
 out=np.zeros((h*w,4),np.float32);gl('glReadPixels',None,[I,I,I,I,U,U,ptr])(0,0,w,h,0x1908,0x1406,out.ctypes.data)
 return out[:n,0]
