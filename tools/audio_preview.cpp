// Offline audition through the production mixer and speech decoder, no audio device required.
#include "../src/audio.h"
#include "../src/atc.h"
#include <fstream>
#include <cstdio>

static void word(std::ofstream& f,uint32_t value,int bytes){for(int i=0;i<bytes;++i)f.put(char(value>>(8*i)));}
int main(int argc,char** argv){
 if(argc!=3){fprintf(stderr,"Usage: audio_preview assets/voice output.wav\n");return 2;}
 g_audio.init(48000);AtcVoice voice;if(!voice.load(argv[1]))return 3;
 const int frames=48000*25;std::vector<float> pcm(frames*2);AudioParams p;p.scene=true;p.sceneId=1;p.master=.72f;
 auto say=[&](const char* text){AtcVoice::Tx tx;if(voice.resolve(text,"",false,tx)){voice.say(tx);voice.update(1);}else fprintf(stderr,"No preview voice: %s\n",text);};
 for(int n=0;n<frames;n+=480){
  float t=float(n)/48000;int tick=n/480;p.listener=AudioListener();p.aircraftPosition=vec3();p.aircraftVelocity=vec3();p.aircraftRotation=quat();
  p.inFlight=t<10;p.interior=t<6;p.cabinEye=vec3(0,1,-2);p.listener.position=t<6?p.cabinEye:vec3(15,3,-20);
  p.engines=1;p.engineVol=.8f;p.sfxVol=.8f;p.engineType=t<3?0:1;p.research=false;p.rpm=1700;p.n1=75;p.spool=.55f;p.throttle=.6f;p.running=true;
  p.enginePosition[0]=vec3(0,0,-4);p.airspeed=t<3?25:65;p.gearDown=0;p.rain=0;p.turbulence=.25f;p.emitterCount=0;
  if(t>=6 && t<10){p.interior=false;p.research=true;p.engines=4;p.spool=.5f+(t-6)*.1f;p.nozzle=.3f;p.airspeed=130;
   for(int i=0;i<4;++i)p.enginePosition[i]=vec3(i%2?6:-6,0,i<2?-3:5);
   p.listener.position=vec3(18*cosf((t-6)*.9f),3,18*sinf((t-6)*.9f));p.listener.forward=normalize(-p.listener.position);p.listener.right=normalize(cross(p.listener.forward,vec3(0,1,0)));p.listener.up=cross(p.listener.right,p.listener.forward);
  }
  if(t>=10){p.engines=0;p.airspeed=0;p.inFlight=false;p.interior=false;p.listener=AudioListener();}
  if(t>=10 && t<13){auto&e=p.emitters[0];e.id=1;e.kind=AudioEmitter::Engine;e.position=vec3((t-11.5f)*230,10,-45);e.velocity=vec3(230,0,0);e.forward=vec3(1,0,0);e.spool=.9f;e.n1=90;e.level=.8f;e.radius=40;p.emitterCount=1;}
  g_audio.setParams(p);
  if(tick==25)say("Full tanks. Release the parking brake when ready.");
  if(tick==1340){g_audio.triggerAt(SFX_METAL,vec3(-12,0,-8),.65f);g_audio.triggerAt(SFX_CRASH,vec3(-20,0,-15),.65f);}
  if(tick==1440)g_audio.triggerAt(SFX_IMPACT,vec3(30,0,-20),.65f);
  if(tick==1520)g_audio.triggerAt(SFX_CRASH,vec3(220,0,-170),.65f);
  if(tick==1660)g_audio.triggerAt(SFX_SPLASH,vec3(18,0,-12),.65f);
  if(tick==1820)g_audio.triggerAt(SFX_BOOM,vec3(-70,40,-110),.8f);
  if(tick==2000)say("OBJECTIVES COMPLETE - RETURN TO EXTRACTION");
  voice.update(.01f);g_audio.render(pcm.data()+n*2,480);
 }
 float peak=0;for(float& v:pcm){if(!std::isfinite(v))return 4;peak=std::max(peak,fabsf(v));}
 std::ofstream f(argv[2],std::ios::binary);if(!f)return 5;
 f.write("RIFF",4);word(f,36+frames*4,4);f.write("WAVEfmt ",8);word(f,16,4);word(f,1,2);word(f,2,2);word(f,48000,4);word(f,192000,4);word(f,4,2);word(f,16,2);f.write("data",4);word(f,frames*4,4);
 for(int i=0;i<frames;++i){float fade=clampf(std::min(i,frames-1-i)/960.f,0,1);for(int c=0;c<2;++c)word(f,uint16_t(int16_t(clampf(pcm[i*2+c]*fade,-1,1)*32767)),2);}
 printf("25-second stereo preview: peak %.6f; %s\n",peak,argv[2]);return f?0:6;
}
