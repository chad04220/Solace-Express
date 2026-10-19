// Internet radio decoded to PCM on one background worker. Playback is owned by
// AudioEngine: it shares the spatial speakers and peak limiter with all other audio.
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
#include "radio.h"
#include "audio.h"
#include "audio_stream.h"

namespace {
template<class T> void release(T*& p) {if(p){p->Release();p=nullptr;}}
struct State {
  std::mutex mutex;std::condition_variable wake;
  bool closed=false, request=false, exited=false;
  unsigned serial=0;uint32_t audioToken=0;std::string url;
  std::atomic<int> status{Radio::IDLE};std::atomic<long> error{0};
  IMFMediaSource* source=nullptr; // reference owned by worker; access under mutex
};
struct Impl {std::shared_ptr<State> state=std::make_shared<State>();std::thread worker;ULONGLONG started=0;};
bool cancelled(const std::shared_ptr<State>& s,unsigned serial) {
  std::lock_guard<std::mutex> guard(s->mutex);return s->closed || s->serial!=serial;
}
void setStatus(const std::shared_ptr<State>& s,unsigned serial,int status,HRESULT error=S_OK) {
  std::lock_guard<std::mutex> guard(s->mutex);
  if(s->closed || s->serial!=serial)return;
  s->error.store(error);s->status.store(status);
}
void interruptSource(const std::shared_ptr<State>& s) {
  IMFMediaSource* source=nullptr;
  {std::lock_guard<std::mutex> guard(s->mutex);source=s->source;if(source)source->AddRef();}
  if(source){source->Shutdown();source->Release();} // wakes a pending network ReadSample
}
bool publish(const std::shared_ptr<State>& s,unsigned serial,uint32_t token,const float* pcm,int count) {
  int sent=0;
  while(sent<count && !cancelled(s,serial)) {
    int n=g_audio.musicPush(token,pcm+sent*2,count-sent);sent+=n;
    if(!n) {if(g_audio.musicBuffered(token)<0)return false;std::unique_lock<std::mutex> guard(s->mutex);s->wake.wait_for(guard,std::chrono::milliseconds(8));}
  }
  return sent==count;
}
// Streaming windowed-sinc converter. Its lookahead crosses decoder packet
// boundaries, so 44.1 kHz music does not click or accumulate timing drift at 48 kHz.
HRESULT decode(const std::shared_ptr<State>& s,unsigned serial,uint32_t token,const std::string& url) {
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
    if(s->closed || s->serial!=serial)hr=E_ABORT;else s->source=source;
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
  while(SUCCEEDED(hr) && !cancelled(s,serial)) {
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
        if(!publish(s,serial,token,pcm.data(),int(pcm.size()/2)))break;
        setStatus(s,serial,Radio::PLAYING);
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
  HRESULT mf=SUCCEEDED(com)?MFStartup(MF_VERSION,MFSTARTUP_FULL):com;
  for(;;) {
    unsigned serial;uint32_t token;std::string url;
    {std::unique_lock<std::mutex> guard(s->mutex);s->wake.wait(guard,[&]{return s->request || s->closed;});
      if(s->closed)break;
      s->request=false;serial=s->serial;token=s->audioToken;url=s->url;}
    HRESULT hr=FAILED(mf)?mf:decode(s,serial,token,url);
    setStatus(s,serial,Radio::FAILED,hr);
  }
  if(SUCCEEDED(mf))MFShutdown();
  if(SUCCEEDED(com))CoUninitialize();
  {std::lock_guard<std::mutex> guard(s->mutex);s->exited=true;}s->wake.notify_all();
}
}
bool Radio::init() {
  if(impl)return true;
  auto i=new Impl();impl=i;i->worker=std::thread(worker,i->state);return true;
}
void Radio::stop() {
  auto i=static_cast<Impl*>(impl);if(!i)return;
  {std::lock_guard<std::mutex> guard(i->state->mutex);++i->state->serial;i->state->request=false;i->state->status.store(IDLE);}
  g_audio.musicStop();interruptSource(i->state);i->state->wake.notify_all();st=IDLE;msg="Radio off";
}
void Radio::play(const std::string& url) {
  if(!impl && !init())return;
  stop();auto i=static_cast<Impl*>(impl);i->started=GetTickCount64();
  {std::lock_guard<std::mutex> guard(i->state->mutex);i->state->url=url;i->state->audioToken=g_audio.musicBegin();i->state->request=true;i->state->status.store(CONNECTING);}
  g_audio.musicVolume(vol);i->state->wake.notify_all();st=CONNECTING;msg="Connecting... "+url;
}
void Radio::setVolume(float v){vol=std::clamp(v,0.f,1.f);if(st!=IDLE)g_audio.musicVolume(vol);}   // (off, the music ring is the game's own score's: music.h)
void Radio::poll() {
  auto i=static_cast<Impl*>(impl);if(!i)return;
  auto next=static_cast<Radio::State>(i->state->status.load());
  if(next==CONNECTING && GetTickCount64()-i->started>20000) {
    stop();i->state->error.store(HRESULT_FROM_WIN32(ERROR_TIMEOUT));i->state->status.store(FAILED);next=FAILED;
  }
  if(next==st)return;
  st=next;
  if(st==PLAYING)msg="Playing (live, spatial cockpit speakers)";
  else if(st==FAILED){char b[160];snprintf(b,sizeof(b),"Stream error 0x%08lX - check the connection or station URL",i->state->error.load());msg=b;}
}
void Radio::shutdown() {
  auto i=static_cast<Impl*>(impl);if(!i)return;stop();
  {std::lock_guard<std::mutex> guard(i->state->mutex);i->state->closed=true;}i->state->wake.notify_all();
  // A URL resolver may still be inside the OS network stack. State stays owned by
  // the sole worker; no Radio/Game pointers are captured, and its token is revoked.
  bool done;{std::unique_lock<std::mutex> guard(i->state->mutex);done=i->state->wake.wait_for(guard,std::chrono::milliseconds(100),[&]{return i->state->exited;});}
  if(i->worker.joinable()){if(done)i->worker.join();else i->worker.detach();}delete i;impl=nullptr;
}
