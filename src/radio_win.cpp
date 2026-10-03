// Solace Express - internet radio via Media Foundation (MFPlay). Streams MP3/AAC over HTTP/HTTPS.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfplay.h>
#include "radio.h"

namespace {
struct Callback : public IMFPMediaPlayerCallback {
  LONG refs = 1;
  Radio* owner = nullptr;
  volatile int event = 0;      // 1 playing, 2 error
  HRESULT lastHr = S_OK;
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == __uuidof(IMFPMediaPlayerCallback)) { *ppv = static_cast<IMFPMediaPlayerCallback*>(this); AddRef(); return S_OK; }
    *ppv = nullptr; return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs); }
  STDMETHODIMP_(ULONG) Release() override { LONG r = InterlockedDecrement(&refs); if (!r) delete this; return r; }
  void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* ev) override {
    if (!ev) return;
    if (FAILED(ev->hrEvent)) { event = 2; lastHr = ev->hrEvent; return; }
    if (ev->eEventType == MFP_EVENT_TYPE_PLAY) event = 1;
    if (ev->eEventType == MFP_EVENT_TYPE_ERROR) { event = 2; lastHr = ev->hrEvent; }
  }
};
struct Impl { IMFPMediaPlayer* player = nullptr; Callback* cb = nullptr; bool mfOk = false; DWORD started = 0; };
}  // namespace

bool Radio::init() {
  Impl* i = new Impl();
  i->mfOk = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));
  impl = i;
  return i->mfOk;
}

void Radio::stop() {
  Impl* i = (Impl*)impl;
  if (!i) return;
  if (i->player) { i->player->Stop(); i->player->Shutdown(); i->player->Release(); i->player = nullptr; }
  if (i->cb) { i->cb->Release(); i->cb = nullptr; }
  st = IDLE; msg = "Radio off";
}

void Radio::play(const std::string& url) {
  Impl* i = (Impl*)impl;
  if (!i) return;
  stop();
  if (!i->mfOk) { st = FAILED; msg = "Media Foundation unavailable on this system"; return; }
  int n = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &w[0], n);
  i->cb = new Callback();
  HRESULT hr = MFPCreateMediaPlayer(w.c_str(), TRUE, 0, i->cb, nullptr, &i->player);
  if (FAILED(hr) || !i->player) { st = FAILED; char b[128]; snprintf(b, sizeof(b), "Could not open stream (0x%08lX)", (unsigned long)hr); msg = b; return; }
  i->player->SetVolume(vol);
  st = CONNECTING; msg = "Connecting... " + url;
  i->started = GetTickCount();
}

void Radio::setVolume(float v) {
  vol = v < 0 ? 0 : (v > 1 ? 1 : v);
  Impl* i = (Impl*)impl;
  if (i && i->player) i->player->SetVolume(vol);
}

void Radio::poll() {
  Impl* i = (Impl*)impl;
  if (!i || !i->cb) return;
  if (i->cb->event == 1 && st != PLAYING) { st = PLAYING; msg = "Playing (live)"; }
  if (i->cb->event == 2 && st != FAILED) {
    char b[160]; snprintf(b, sizeof(b), "Stream error 0x%08lX - check your connection or the station URL", (unsigned long)i->cb->lastHr);
    stop(); st = FAILED; msg = b;
  }
  if (st == CONNECTING && GetTickCount() - i->started > 20000) { stop(); st = FAILED; msg = "Timed out connecting to station"; }
}

void Radio::shutdown() {
  stop();
  Impl* i = (Impl*)impl;
  if (i) { if (i->mfOk) MFShutdown(); delete i; impl = nullptr; }
}
