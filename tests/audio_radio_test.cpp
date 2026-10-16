// Native Media Foundation decode/cancellation, using generated local WAVs.
// No network connection, sound device or graphics context is required.
#include "../src/audio.h"
#include "../src/radio.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {
void word(std::ofstream& out,uint32_t v,int n){for(int i=0;i<n;++i)out.put(char(v>>(8*i)));}
bool fixture(const std::filesystem::path& path,int rate,int channel) {
 std::ofstream out(path,std::ios::binary);int frames=rate*4;
 out.write("RIFF",4);word(out,36+frames*4,4);out.write("WAVEfmt ",8);word(out,16,4);word(out,1,2);word(out,2,2);
 word(out,rate,4);word(out,rate*4,4);word(out,4,2);word(out,16,2);out.write("data",4);word(out,frames*4,4);
 for(int i=0;i<frames;++i)for(int c=0;c<2;++c)word(out,uint16_t(int16_t(c==channel?9000*sin(2*PI*1000*double(i)/rate):0)),2);
 return bool(out);
}
bool connected(Radio& radio) {
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
 while(std::chrono::steady_clock::now()<deadline) {
  radio.poll();if(radio.state()==Radio::PLAYING)return true;
  if(radio.state()==Radio::FAILED){printf("Radio: %s\n",radio.status().c_str());return false;}
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
 }
 return false;
}
std::pair<double,double> energy(float seconds) {
 double left=0,right=0;float pcm[960];
 for(int i=0;i<int(seconds*100);++i){g_audio.render(pcm,480);for(int j=0;j<480;++j){left+=pcm[j*2]*pcm[j*2];right+=pcm[j*2+1]*pcm[j*2+1];}}
 return {left,right};
}
}
int main() {
 int failures=0;auto check=[&](bool ok,const char* name){printf("Native radio: %s: %s\n",name,ok?"PASS":"FAIL");failures+=!ok;};
 const auto dir=std::filesystem::temp_directory_path()/("somnium_audio_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 std::filesystem::create_directories(dir);const auto left=dir/"left.wav",right=dir/"right.wav";
 if(!fixture(left,44100,0)||!fixture(right,96000,1))return 2;
 g_audio.init(48000);AudioParams p;p.scene=true;p.engines=0;p.engineVol=0;g_audio.setParams(p);energy(.1f);
 Radio radio;check(radio.init(),"worker starts");radio.setVolume(.5f);
 radio.play(left.string());check(connected(radio),"44.1 kHz local PCM decodes through Media Foundation");
 auto e=energy(.1f);check(e.first>.001 && e.first>e.second*1.5,"decoded left channel reaches the spatial mixer");
 radio.stop();energy(.2f);e=energy(.1f);check(e.first+e.second<1e-9,"stop revokes buffered station audio");
 radio.play(left.string());radio.stop();radio.play(right.string());
 check(connected(radio),"rapid replacement accepts the new station");e=energy(.1f);
 check(e.second>.001 && e.second>e.first*1.5,"96 kHz right channel resamples without stale station data");
 radio.setVolume(0);energy(.2f);e=energy(.1f);check(e.first+e.second<1e-9,"native radio mute reaches silence");
 radio.shutdown();energy(.2f);e=energy(.1f);check(e.first+e.second<1e-9,"shutdown silences the worker and ring");
 std::error_code ec;std::filesystem::remove_all(dir,ec);return failures?1:0;
}
