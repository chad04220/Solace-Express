// Internet radio decoded to PCM, each station on its own background worker. Playback is
// owned by AudioEngine: it shares the spatial speakers and peak limiter with all other audio.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <condition_variable>
#include <chrono>
#include <thread>
#include <vector>
#include "radio.h"
#include "audio.h"
#include "audio_stream.h"

namespace {
template<class T> void release(T*& p) {if(p){p->Release();p=nullptr;}}
// One station: its worker owns it alone (Radio keeps a reference, no Radio/Game pointers
// are captured). Stopping closes it; the worker unwinds on its own and the next station
// starts on a fresh worker at once, never queued behind a cancelled one still inside
// Media Foundation (a URL resolver in the OS network stack, a reader whose source was
// shut down beneath it).
struct State {
  std::mutex mutex;std::condition_variable wake;
  bool closed=false, exited=false;
  uint32_t audioToken=0;std::string url;
  std::atomic<int> status{Radio::IDLE};std::atomic<long> error{0};
  IMFMediaSource* source=nullptr; // reference owned by worker; access under mutex
};
struct Station {std::shared_ptr<State> state;std::thread worker;};
struct Impl {Station current;std::vector<Station> retired;ULONGLONG started=0;long failure=0;};
bool cancelled(const std::shared_ptr<State>& s) {
  std::lock_guard<std::mutex> guard(s->mutex);return s->closed;
}
void setStatus(const std::shared_ptr<State>& s,int status,HRESULT error=S_OK) {
  std::lock_guard<std::mutex> guard(s->mutex);
  if(s->closed)return;
  s->error.store(error);s->status.store(status);
}
void interruptSource(const std::shared_ptr<State>& s) {
  IMFMediaSource* source=nullptr;
  {std::lock_guard<std::mutex> guard(s->mutex);source=s->source;if(source)source->AddRef();}
  if(source){source->Shutdown();source->Release();} // wakes a pending network ReadSample
}
bool publish(const std::shared_ptr<State>& s,uint32_t token,const float* pcm,int count) {
  int sent=0;
  while(sent<count && !cancelled(s)) {
    int n=g_audio.musicPush(token,pcm+sent*2,count-sent);sent+=n;
    if(!n) {if(g_audio.musicBuffered(token)<0)return false;std::unique_lock<std::mutex> guard(s->mutex);s->wake.wait_for(guard,std::chrono::milliseconds(8));}
  }
  return sent==count;
}
// Streaming windowed-sinc converter. Its lookahead crosses decoder packet
// boundaries, so 44.1 kHz music does not click or accumulate timing drift at 48 kHz.
HRESULT decode(const std::shared_ptr<State>& s,uint32_t token,const std::string& url) {
  IMFSourceResolver* resolver=nullptr;IUnknown* object=nullptr;IMFMediaSource* source=nullptr;
  IMFSourceReader* reader=nullptr;IMFMediaType* requested=nullptr;IMFMediaType* actual=nullptr;
  HRESULT hr=MFCreateSourceResolver(&resolver);MF_OBJECT_TYPE objectType=MF_OBJECT_INVALID;
  int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.c_str(),-1,nullptr,0);
  std::wstring wide(size_t(std::max(1,n)),0);
  if(!n)hr=E_INVALIDARG;else MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.c_str(),-1,&wide[0],n);
  if(SUCCEEDED(hr))hr=resolver->CreateObjectFromURL(wide.c_str(),MF_RESOLUTION_MEDIASOURCE,nullptr,&objectType,&object);
  if(SUCCEEDED(hr))hr=object?object->QueryInterface(IID_PPV_ARGS(&source)):E_UNEXPECTED;
  if(source) {
    std::lock_guard<std::mutex> guard(s->mutex);
    if(s->closed)hr=E_ABORT;else s->source=source;
  }
  if(SUCCEEDED(hr))hr=MFCreateSourceReaderFromMediaSource(source,nullptr,&reader);
  if(SUCCEEDED(hr))hr=reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE);
  if(SUCCEEDED(hr))hr=reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM,TRUE);
  if(SUCCEEDED(hr))hr=MFCreateMediaType(&requested);
  if(SUCCEEDED(hr))hr=requested->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);
  if(SUCCEEDED(hr))hr=requested->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_Float);
  // Let Media Foundation respect the source channel mask when folding surround
  // stations to stereo; never silently discard rear channels or mistake quad
  // rear-left for a centre channel. Windows 10/11 support this conversion.
  if(SUCCEEDED(hr))hr=requested->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2);
  if(SUCCEEDED(hr))hr=reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,nullptr,requested);
  if(SUCCEEDED(hr))hr=reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,&actual);
  UINT32 channels=0,rate=0,bits=0;
  if(SUCCEEDED(hr))hr=actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&channels);
  if(SUCCEEDED(hr))hr=actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&rate);
  if(SUCCEEDED(hr))hr=actual->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,&bits);
  if(SUCCEEDED(hr) && (channels!=2 || rate<8000 || rate>192000 || bits!=32))hr=MF_E_INVALIDMEDIATYPE;
  AudioStreamResampler resampler;
  while(SUCCEEDED(hr) && !cancelled(s)) {
    IMFSample* sample=nullptr;DWORD flags=0;LONGLONG stamp=0;
    hr=reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,nullptr,&flags,&stamp,&sample);
    if(FAILED(hr) || (flags&(MF_SOURCE_READERF_ENDOFSTREAM|MF_SOURCE_READERF_ERROR))) {
      release(sample);if(SUCCEEDED(hr))hr=MF_E_INVALID_STREAM_DATA;break;
    }
    if(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
      UINT32 oldRate=rate,oldChannels=channels;release(actual);
      hr=reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,&actual);
      if(SUCCEEDED(hr))hr=actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&channels);
      if(SUCCEEDED(hr))hr=actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&rate);
      if(SUCCEEDED(hr))hr=actual->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,&bits);
      GUID subtype={};if(SUCCEEDED(hr))hr=actual->GetGUID(MF_MT_SUBTYPE,&subtype);
      if(SUCCEEDED(hr) && (channels!=2 || bits!=32 || rate<8000 || rate>192000 || subtype!=MFAudioFormat_Float))hr=MF_E_INVALIDMEDIATYPE;
      if(FAILED(hr)){release(sample);break;}
      if(oldRate!=rate || oldChannels!=channels)resampler=AudioStreamResampler();
    }
    if(!sample)continue;
    IMFMediaBuffer* buffer=nullptr;hr=sample->ConvertToContiguousBuffer(&buffer);
    BYTE* data=nullptr;DWORD bytes=0;
    if(SUCCEEDED(hr))hr=buffer->Lock(&data,nullptr,&bytes);
    if(SUCCEEDED(hr)) {
      resampler.append(reinterpret_cast<const float*>(data),int(bytes/(channels*sizeof(float))),int(channels));buffer->Unlock();
      for(;;) {auto pcm=resampler.take(int(rate),g_audio.sampleRate);if(pcm.empty())break;
        if(!publish(s,token,pcm.data(),int(pcm.size()/2)))break;
        setStatus(s,Radio::PLAYING);
      }
    }
    release(buffer);release(sample);
  }
  {std::lock_guard<std::mutex> guard(s->mutex);if(s->source==source)s->source=nullptr;}
  if(source)source->Shutdown();
  release(actual);release(requested);release(reader);release(source);release(object);release(resolver);
  return hr;
}
void worker(std::shared_ptr<State> s) {
  HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
  HRESULT mf=SUCCEEDED(com)?MFStartup(MF_VERSION,MFSTARTUP_FULL):com;   // (both counted per call: workers overlap safely)
  uint32_t token;std::string url;
  {std::lock_guard<std::mutex> guard(s->mutex);token=s->audioToken;url=s->url;}
  HRESULT hr=FAILED(mf)?mf:decode(s,token,url);
  setStatus(s,Radio::FAILED,hr);
  if(SUCCEEDED(mf))MFShutdown();
  if(SUCCEEDED(com))CoUninitialize();
  {std::lock_guard<std::mutex> guard(s->mutex);s->exited=true;}s->wake.notify_all();
}
bool exited(const std::shared_ptr<State>& s){std::lock_guard<std::mutex> guard(s->mutex);return s->exited;}
void reap(std::vector<Station>& retired) {   // join the stopped stations' workers that have finished unwinding
  for(auto it=retired.begin();it!=retired.end();)
    if(exited(it->state)){it->worker.join();it=retired.erase(it);}else ++it;
}
}
bool Radio::init() {
  if(!impl)impl=new Impl();
  return true;
}
void Radio::stop() {
  auto i=static_cast<Impl*>(impl);if(!i)return;
  if(auto s=i->current.state) {
    {std::lock_guard<std::mutex> guard(s->mutex);s->closed=true;s->status.store(IDLE);}
    g_audio.musicStop();interruptSource(s);s->wake.notify_all();
    i->retired.push_back(std::move(i->current));i->current=Station();
  } else g_audio.musicStop();
  reap(i->retired);i->failure=0;st=IDLE;msg="Radio off";
}
void Radio::play(const std::string& url) {
  if(!impl && !init())return;
  stop();auto i=static_cast<Impl*>(impl);i->started=GetTickCount64();
  auto s=std::make_shared<::State>();   // (the station's, not the Radio::State enum)
  s->url=url;s->audioToken=g_audio.musicBegin();s->status.store(CONNECTING);
  i->current.state=s;i->current.worker=std::thread(worker,s);
  g_audio.musicVolume(vol);st=CONNECTING;msg="Connecting... "+url;
}
void Radio::setVolume(float v){vol=std::clamp(v,0.f,1.f);if(st!=IDLE)g_audio.musicVolume(vol);}   // (off, the music ring is the game's own score's: music.h)
void Radio::poll() {
  auto i=static_cast<Impl*>(impl);if(!i)return;
  if(!i->retired.empty())reap(i->retired);
  const auto& s=i->current.state;
  auto next=s?static_cast<Radio::State>(s->status.load()):i->failure?FAILED:IDLE;
  if(next==CONNECTING && GetTickCount64()-i->started>20000) {
    stop();i->failure=HRESULT_FROM_WIN32(ERROR_TIMEOUT);next=FAILED;
  }
  if(next==st)return;
  st=next;
  if(st==PLAYING)msg="Playing (live, spatial cockpit speakers)";
  else if(st==FAILED){char b[160];snprintf(b,sizeof(b),"Stream error 0x%08lX - check the connection or station URL",s?s->error.load():i->failure);msg=b;}
}
void Radio::shutdown() {
  auto i=static_cast<Impl*>(impl);if(!i)return;stop();
  // A URL resolver may still be inside the OS network stack: a worker that has not
  // unwound within the grace is detached (it owns its State, and its token is revoked).
  const auto grace=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
  for(auto& r:i->retired) {
    bool done;{std::unique_lock<std::mutex> guard(r.state->mutex);done=r.state->wake.wait_until(guard,grace,[&]{return r.state->exited;});}
    if(done)r.worker.join();else r.worker.detach();
  }
  delete i;impl=nullptr;
}
