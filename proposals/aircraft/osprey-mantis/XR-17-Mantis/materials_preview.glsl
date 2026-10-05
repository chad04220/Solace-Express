// STATIC ILLUSTRATIVE MATERIALS ONLY. No live camera, telemetry or functional switches.
// Body-space p. Invoke ahead of host fallback for ids112..119.
float mantisLine(vec2 p,vec2 a,vec2 b,float w){vec2 q=p-a;vec2 d=b-a;return 1.-smoothstep(w,w+.002,length(q-d*clamp(dot(q,d)/dot(d,d),0.,1.)));}
float mantisGlyph(vec2 p,int c){
 // 3x5 static block lettering: C A M S T I R E F O 0 1 2
 if(any(lessThan(p,vec2(0)))||any(greaterThanEqual(p,vec2(3,5))))return 0.;
 int k=int(floor(p.x))+3*(4-int(floor(p.y)));int b=0;
 if(c==0)b=29263; // C
 if(c==1)b=23530; // A
 if(c==2)b=23549; // M
 if(c==3)b=31183; // S
 if(c==4)b=9367;  // T
 if(c==5)b=29847; // I
 if(c==6)b=23275; // R
 if(c==7)b=29391; // E
 if(c==8)b=4815;  // F
 if(c==9)b=31599; // O / zero
 if(c==10)b=29850;// one
 if(c==11)b=29671;// two
 return float((b>>k)&1);
}
float mantisWord(vec2 uv,int word){
 vec2 t=uv/.009;int col=int(floor(t.x/4.));vec2 q=vec2(mod(t.x,4.),t.y);int c=-1;
 if(word==0){if(col==0)c=0;if(col==1)c=1;if(col==2)c=2;}
 if(word==1){if(col==0)c=3;if(col==1)c=4;if(col==2)c=1;if(col==3)c=4;if(col==4)c=5;if(col==5)c=0;}
 if(word==2){if(col==0)c=6;if(col==1)c=7;if(col==2)c=8;}
 return c<0?0.:mantisGlyph(q,c);
}
vec3 mantisCabinMaterial(float m,vec3 p){
 vec3 amber=vec3(1.,.48,.08), graph=vec3(.035,.045,.054);
 if(m==113.)return graph*(.93+.07*step(.5,fract(p.z*65.)));
 if(m==114.)return vec3(.11,.125,.135)*(.92+.08*step(.5,fract(p.y*90.)));
 if(m==115.)return amber;
 if(m==116.)return vec3(.41,.29,.13)*(.85+.15*step(.5,fract(p.y*120.)));
 if(m==118.)return vec3(.009,.014,.017);
 if(m==119.)return vec3(.25,.29,.31);
 if(m==112.){
  bool front=p.z < -4.08;vec2 uv=front?vec2(p.x,p.y-.410):vec2(p.x<0.?-p.z-3.51:p.z+3.51,p.y-.365);
  vec2 sz=front?vec2(.410,.177):vec2(.370,.172);
  float grid=max(1.-smoothstep(.001,.002,abs(mod(uv.x+.035,.070)-.035)),1.-smoothstep(.001,.002,abs(mod(uv.y+.035,.070)-.035)));
  vec3 c=mix(vec3(.024,.073,.082),vec3(.050,.14,.15),grid*.6);
  float crosshair=max(mantisLine(uv,vec2(-.025,0),vec2(.025,0),.0015),mantisLine(uv,vec2(0,-.025),vec2(0,.025),.0015));
  float text=max(mantisWord(uv-vec2(-sz.x+.025,sz.y-.060),0),mantisWord(uv-vec2(-.104,-sz.y+.025),1));
  float datum=1.-smoothstep(.002,.004,abs(abs(uv.x)-sz.x+.02));datum*=step(abs(uv.y),.055);
  return mix(c,amber,max(text,max(crosshair,datum)));
 }
 if(m==117.){
  bool side=abs(p.x)>.42;vec2 uv=side?vec2(p.z+3.66,(abs(p.x)-.489)*1.7):vec2(p.x-sign(p.x)*.266,p.y-.07);
  if(side && p.x<0.)uv=-uv;
  float text=mantisWord(uv-vec2(-.095,.025),2);
  float bars=0.;for(int i=0;i<4;i++){float y=-.015-float(i)*.017;bars=max(bars,step(abs(uv.y-y),.003)*step(-.095,uv.x)*step(uv.x,.055-float(i)*.028));}
  return mix(vec3(.015,.035,.042),amber,max(text,bars));
 }
 return graph;
}
