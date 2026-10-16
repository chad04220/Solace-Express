// Render production stereo PCM offline: behavior checks, not snapshots of DSP internals.
#include "../src/audio.h"
#include <chrono>
#include <cstdio>
#include <limits>
#include "../src/audio_stream.h"

namespace {
int failures=0,checks=0;
void check(bool ok,const char* name){++checks;if(!ok)++failures;printf("Audio: %s: %s\n",name,ok?"PASS":"FAIL");}
AudioParams quiet(){AudioParams p;p.scene=true;p.sceneId=1;p.master=.6f;p.engineVol=0;p.engines=0;return p;}
std::vector<float> render(AudioEngine& a,float seconds){std::vector<float> out(size_t(seconds*a.sampleRate)*2);for(size_t i=0;i<out.size()/2;i+=480)a.render(out.data()+2*i,int(std::min(size_t(480),out.size()/2-i)));return out;}
double energy(const std::vector<float>& x,int channel=-1,size_t begin=0){double e=0;for(size_t i=begin*2;i<x.size();++i)if(channel<0 || int(i%2)==channel)e+=double(x[i])*x[i];return e;}
float peak(const std::vector<float>& x){float p=0;for(float v:x){if(!std::isfinite(v))return 1e9f;p=std::max(p,std::fabs(v));}return p;}
double correlation(const std::vector<float>&a,const std::vector<float>&b){double x=0,y=0,z=0;for(size_t i=0;i<std::min(a.size(),b.size());++i){x+=a[i]*b[i];y+=a[i]*a[i];z+=b[i]*b[i];}return x/std::sqrt(std::max(1e-30,y*z));}
std::vector<float> shot(vec3 at,int type=SFX_CRASH,float intensity=.5f,AudioParams p=quiet(),bool propagation=false,int rate=48000){
 AudioEngine a;a.init(rate);a.setParams(p);render(a,.5f);AudioSource s;s.position=at;s.radius=20;s.range=6000;s.propagation=propagation;
 a.triggerSource(type,s,intensity);return render(a,2.f);
}
std::shared_ptr<std::vector<float>> tone(int sr,int hz=440){auto x=std::make_shared<std::vector<float>>(sr);for(int i=0;i<sr;++i)(*x)[i]=.15f*sinf(2*PI*hz*i/sr);return x;}
}
int main(){
 // Every shipped one-shot is exercised through PCM, including the less common
 // UFO, mechanical, UI and payload effects. Tails must end and no cue can bypass spatial rendering.
 for(int type=0;type<SFX_COUNT;++type) {
  bool valid=true;
  for(int rate:{44100,48000,96000}) {
   AudioEngine a;a.init(rate);a.setParams(quiet());render(a,.5f);
   AudioSource s;s.position=vec3(-8,0,-3);s.radius=12;s.range=100;s.propagation=false;
   a.triggerSource(type,s,.5f);auto x=render(a,7);
   valid=valid && peak(x)<=.92001f && energy(x)>.00001 && energy(x,0)>energy(x,1)*1.5 && energy(x,-1,size_t(6.9f*rate))<1e-9;
  }
  check(valid,fmt("effect %d is spatial, finite, audible and ends cleanly at 44.1/48/96 kHz",type).c_str());
 }
 auto left=shot(vec3(-30,0,-10)),right=shot(vec3(30,0,-10));
 check(energy(left,0)>energy(left,1)*2 && energy(right,1)>energy(right,0)*2,"an impact stays on its physical side");
 AudioParams turned=quiet();turned.listener.right=vec3(-1,0,0);turned.listener.forward=vec3(0,0,1);
 auto back=shot(vec3(-30,0,-10),SFX_CRASH,.5f,turned);
 check(energy(back,1)>energy(back,0)*2,"turning the listener reverses the apparent side");
 auto near=shot(vec3(0,0,-40)),far=shot(vec3(0,0,-4000));
 check(energy(far)>0 && energy(far)<energy(near)*.0005,"distant impacts have no artificial loudness floor");
 auto out=shot(vec3(0,0,-7000));check(energy(out)<1e-12,"out-of-range effects are silent");
 auto low=shot(vec3(0,0,-20),SFX_CRASH,.2f),high=shot(vec3(0,0,-20),SFX_CRASH,.4f);
 check(std::fabs(energy(high)/energy(low)-4)<.02,"intensity is applied exactly once");
 check(std::fabs(correlation(shot(vec3(0,0,-20),SFX_CANNON),shot(vec3(0,0,-20),SFX_HEAVY)))<.8,"kinetic cannon and charged heavy shot have separate signatures");
 auto delayed=shot(vec3(0,0,-343),SFX_METAL,.8f,quiet(),true);
 size_t onset=0;while(onset<delayed.size()/2 && std::fabs(delayed[onset*2])+std::fabs(delayed[onset*2+1])<1e-6)++onset;
 check(onset>=47900 && onset<49000,"343 metre impact arrives about one second later");
 // Elevation and rear discrimination must affect actual spectral output, not just panning metadata.
 auto front=shot(vec3(0,0,-30),SFX_METAL),rear=shot(vec3(0,0,30),SFX_METAL),above=shot(vec3(0,30,0),SFX_METAL);
 check(correlation(front,rear)<.9995 && correlation(front,above)<.99995,"rear and elevated sources have distinct spectral cues");
 {AudioEngine a;a.init(48000);a.setParams(quiet());render(a,.5f);
  a.triggerLocal(SFX_CRASH,vec3(0,0,-10),.5f);auto first=render(a,4);
  a.triggerLocal(SFX_CRASH,vec3(0,0,-10),.5f);auto second=render(a,4);
  check(std::fabs(correlation(first,second))<.8,"repeated explosions have different waveforms");
 }
 // Similar positions across the repeating-world seam should sound identical.
 AudioParams wrap=quiet();wrap.wrapPeriod=100000;wrap.listener.position=vec3(49990,0,0);
 auto seam=shot(vec3(-49980,0,-10),SFX_CRASH,.5f,wrap);
 check(std::fabs(energy(seam)/energy(right)-1)<.001,"world seam preserves distance and direction");
 {AudioEngine a;a.init(48000);auto p=quiet();a.setParams(p);render(a,.5f);
  AudioSource s;s.position=vec3(0,0,-343);s.radius=100;s.range=3000;a.triggerSource(SFX_CRASH,s,.7f);render(a,.25f);
  p.paused=true;a.setParams(p);render(a,.1f);p.paused=false;a.setParams(p);
  check(energy(render(a,2))<1e-12,"pause cancels pending shock waves instead of replaying them");
  a.triggerAt(SFX_CRASH,vec3(0,0,-343));render(a,.1f);++p.sceneId;a.setParams(p);
  check(energy(render(a,2))<1e-12,"new flight cannot inherit a previous flight's delayed sounds");
 }
 // Doppler is listener-relative: co-moving engine/listener has no artificial pitch shift.
 AudioListener l;auto approach=audioSpatialCue(l,vec3(0,0,-100),vec3(0,0,100),10,1000);
 auto recede=audioSpatialCue(l,vec3(0,0,-100),vec3(0,0,-100),10,1000);
 l.velocity=vec3(0,0,100);auto together=audioSpatialCue(l,vec3(0,0,-100),l.velocity,10,1000);
 check(approach.pitch>1.3f && recede.pitch<.8f && fabsf(together.pitch-1)<1e-6,"Doppler follows relative radial velocity");
 check(audioMachCone(vec3(),vec3(0,0,-686),vec3(100,0,100),343)<0 && audioMachCone(vec3(),vec3(0,0,-686),vec3(100,0,200),343)>0 && audioMachCone(vec3(),vec3(0,0,-300),vec3(0,0,200),343)<-1e8f,"sonic shock follows the Mach cone and requires supersonic flight");
 for(int engine=0;engine<4;++engine){
  AudioEngine a;a.init(48000);auto p=quiet();p.inFlight=true;p.engines=4;p.engineVol=.8f;p.research=true;p.running=true;p.spool=.7f;
  for(int i=0;i<4;++i){p.engineHealth[i]=i==engine?1:0;p.enginePosition[i]=vec3(i%2?8:-8,0,i<2?-4:4);}
  a.setParams(p);render(a,.5f);auto x=render(a,1);
  check(energy(x)>.1 && (engine%2?energy(x,1)>energy(x,0)*2:energy(x,0)>energy(x,1)*2),"each of four engine mounts is independently audible and positioned");
  p.running=false;p.spool=0;p.rpm=p.n1=0;a.setParams(p);render(a,2);
  check(energy(render(a,.3f))<1e-7,"research engine becomes silent after spool-down");
 }
 for(int type=0;type<3;++type){AudioEngine a;a.init(48000);auto p=quiet();p.engines=1;p.engineVol=.8f;p.inFlight=true;p.engineType=type;a.setParams(p);
  check(energy(render(a,1))<1e-10,"stopped conventional engine has no permanent hiss");
 }
 {AudioEngine a;a.init(48000);auto p=quiet();p.inFlight=true;p.airspeed=150;p.interior=true;a.setParams(p);render(a,1);auto x=render(a,1);
  check(energy(x)>1,"airspeed produces continuous canopy wind");
  double lr=0,ll=0,rr=0;for(size_t i=0;i<x.size();i+=2){lr+=x[i]*x[i+1];ll+=x[i]*x[i];rr+=x[i+1]*x[i+1];}
  check(lr/std::sqrt(ll*rr)<.98,"left and right airflow are not the same mono noise");
  p.airspeed=0;a.setParams(p);render(a,2);check(energy(render(a,.3f))<1e-6,"wind follows airspeed down to rest");
 }
 {std::vector<std::vector<float>> landings,rolling;
  for(auto surface:{AudioSurface::Paved,AudioSurface::Grass,AudioSurface::Gravel,AudioSurface::Snow,AudioSurface::Sand}) {
   AudioEngine a;a.init(48000);auto p=quiet();a.setParams(p);render(a,.5f);
   a.touchdown(vec3(0,0,-3),.5f,40,surface);landings.push_back(render(a,1));
   a.init(48000);p.inFlight=true;p.onGround=true;p.groundSpeed=30;p.rough=.6f;p.groundSurface=surface;
   a.setParams(p);render(a,.5f);rolling.push_back(render(a,1));
  }
  bool distinct=true;for(size_t i=0;i<landings.size();++i)for(size_t j=0;j<i;++j) {
   double landing=correlation(landings[i],landings[j]),roll=correlation(rolling[i],rolling[j]);
   if(landing>=.99 || roll>=.999)printf("Surface pair %zu/%zu: touchdown %.6f, rolling %.6f\n",i,j,landing,roll);
   distinct=distinct && landing<.99 && roll<.999;
  }
  check(distinct,"paved, grass, gravel, snow and sand have distinct touchdown and rolling spectra");
  AudioEngine a;a.init(48000);a.setParams(quiet());render(a,.5f);a.touchdown(vec3(0,0,-3),.5f,0,AudioSurface::Paved);auto vertical=render(a,1);
  check(energy(vertical)>.001 && energy(vertical)<energy(landings[0])*.9,"stationary VTOL landing retains impact without a high-speed tyre squeal");
 }
 for(int sr:{44100,48000,96000}){AudioEngine a;a.init(sr);auto p=quiet();p.interior=true;p.cabinEye=vec3(0,2,-10);p.listener.position=p.cabinEye;a.setParams(p);
  a.voicePlay(tone(sr),vec3(.65f,0,-.2f));auto x=render(a,1.1f);
  check(peak(x)>.01 && peak(x)<=.921f && energy(x,1)>energy(x,0)*2,"speech is spatial and finite at supported device rates");
  p.voiceVol=0;a.setParams(p);render(a,.2f);a.voicePlay(tone(sr));check(energy(render(a,1.2f))<1e-10,"voice slider mutes the complete speech path");
 }
 {AudioEngine a;a.init(48000);auto p=quiet();p.master=0;a.setParams(p);a.triggerUI(SFX_SUCCESS,-.8f,.2f);a.voicePlay(tone(48000));check(energy(render(a,1.2f))<1e-12,"master zero mutes effects and speech");}
 {AudioEngine a;a.init(48000);auto p=quiet();p.scene=false;a.setParams(p);a.trigger(SFX_BEEP);check(energy(render(a,.5f))>.001,"menu notification beep remains audible at its virtual source");}
 {AudioEngine a;a.init(48000);auto p=quiet();a.setParams(p);render(a,.5f);auto token=a.musicBegin();a.musicVolume(.5f);
  std::vector<float> pcm(40000*2);for(int i=0;i<40000;++i)pcm[i*2]=.15f*sinf(2*PI*440*i/48000);
  check(a.musicPush(token,pcm.data(),40000)==16384,"stream buffer is bounded under producer backpressure");
  auto music=render(a,.2f);check(energy(music,0)>energy(music,1)*1.7,"radio channel uses its spatial speaker");
  check(a.musicPush(token,pcm.data(),9600)==9600,"stream buffer wraps and accepts consumed capacity");
  a.musicStop();check(a.musicPush(token,pcm.data(),100)==0 && a.musicBuffered(token)<0,"stopped stream tokens cannot leak old station PCM");
  render(a,.1f);check(energy(render(a,.2f))<1e-10,"stopped stream tail clears");
  token=a.musicBegin();a.musicVolume(0);a.musicPush(token,pcm.data(),9600);render(a,.1f);
  check(energy(render(a,.1f))<1e-10,"radio mute clears the entire spatial route after its short click-free ramp");
  float bad[]={std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()};a.musicVolume(1);a.musicPush(token,bad,1);check(peak(render(a,.1f))<1,"decoder non-finite samples cannot poison the mixer");
 }
 // Decoder packets have arbitrary boundaries. Conversion must remain continuous and keep pitch/duration.
 {AudioStreamResampler converter;std::vector<float> result;int count=44100;
  for(int offset=0;offset<count;) {
    int n=std::min(137,count-offset);std::vector<float> packet(n*2);
    for(int i=0;i<n;++i)packet[i*2]=packet[i*2+1]=.2f*sinf(2*PI*1000*(offset+i)/44100);
    converter.append(packet.data(),n,2);offset+=n;
    for(;;){auto part=converter.take(44100,48000);if(part.empty())break;result.insert(result.end(),part.begin(),part.end());}
  }
  float padding[64]={};converter.append(padding,16,2);auto tail=converter.take(44100,48000);result.insert(result.end(),tail.begin(),tail.end());
  int crossings=0;float delta=0;for(size_t i=2;i<result.size();i+=2){crossings+=result[i-2]<0 && result[i]>=0;delta=std::max(delta,std::fabs(result[i]-result[i-2]));}
  check(std::abs(int(result.size()/2)-48000)<=1 && std::abs(crossings-1000)<=2 && delta<.04f,"44.1 to 48 kHz streaming preserves duration/pitch without packet-edge clicks");
  AudioStreamResampler down;std::vector<float> packet(4000,.1f);down.append(packet.data(),2000,2);auto converted=down.take(192000,8000);
  check(!converted.empty() && peak(converted)<1,"extreme decoder rate conversion stays in bounds");
 }
 {auto convertedEnergy=[](int rate,int target,int hz) {
   AudioStreamResampler converter;std::vector<float> x(rate*2);
   for(int i=0;i<rate;++i)x[i*2]=x[i*2+1]=.2f*sinf(2*PI*hz*double(i)/rate);
   converter.append(x.data(),rate,2);std::vector<float> output;
   for(;;){auto packet=converter.take(rate,target);if(packet.empty())break;output.insert(output.end(),packet.begin(),packet.end());}
   return energy(output,-1,256);
  };
  check(convertedEnergy(192000,8000,18000)<convertedEnergy(192000,8000,1000)*.0001 &&
        convertedEnergy(96000,16000,18000)<convertedEnergy(96000,16000,1000)*.0001,
        "high-rate music removes out-of-band energy before downsampling (over 40 dB rejection)");
 }
 // Worst bounded polyphony: four engines, twenty nearby traffic voices, and a dense debris burst.
 {AudioEngine a;a.init(48000);auto p=quiet();p.master=1;p.inFlight=true;p.engines=4;p.research=true;p.engineVol=1;p.spool=.9f;p.airspeed=260;
  for(int i=0;i<20;++i){auto&e=p.emitters[i];e.id=i+1;e.position=vec3((i-10)*3,1,-10);e.level=.4f;}p.emitterCount=20;a.setParams(p);
  for(int i=0;i<128;++i)a.triggerLocal(SFX_CRASH,vec3((i-64)*2,0,0),2);
  auto start=std::chrono::steady_clock::now();auto x=render(a,2);double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
  printf("Audio stress: 2 seconds rendered in %.3f seconds, peak %.6f\n",seconds,peak(x));
  check(peak(x)<=.92001f && energy(x)>1,"dense crash mix remains finite and peak-limited");
 }
 printf("Audio spatial: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
