// Solace Express - the pre-rendered main-menu montage (see menu_video_win.h)
#include "menu_video_win.h"
#include "gl.h"
#ifndef NOMINMAX
#define NOMINMAX   // (windows.h would otherwise define min / max macros that break std::min / std::max)
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
#include <cmath>

#ifndef GL_RGB8
#define GL_RGB8 0x8051
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

template <class T> static void release(T*& p) { if (p) { p->Release(); p = nullptr; } }
static void mfStart() { static bool once = false; if (!once) { once = true; MFStartup(MF_VERSION, MFSTARTUP_FULL); } }

// ------------------------------------------------------------------ writer
struct MenuVideoWriter::Impl {
  IMFSinkWriter* w = nullptr; DWORD stream = 0;
  int W = 0, H = 0, fps = 30; LONGLONG frame = 0;
  std::vector<uint8_t> nv12;
};

MenuVideoWriter::MenuVideoWriter() = default;
MenuVideoWriter::~MenuVideoWriter() { if (d) release(d->w); }

bool MenuVideoWriter::open(const std::string& path, int w, int h, int fps, int kbps) {
  mfStart();
  d.reset(new Impl()); d->W = w & ~1; d->H = h & ~1; d->fps = fps;
  std::wstring wp(path.begin(), path.end());
  IMFAttributes* at = nullptr; MFCreateAttributes(&at, 1);
  at->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
  HRESULT hr = MFCreateSinkWriterFromURL(wp.c_str(), nullptr, at, &d->w);
  release(at);
  if (FAILED(hr)) { error = "could not create the video file"; return false; }
  auto common = [&](IMFMediaType* t) {
    t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(t, MF_MT_FRAME_SIZE, d->W, d->H);
    MFSetAttributeRatio(t, MF_MT_FRAME_RATE, fps, 1);
    MFSetAttributeRatio(t, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    t->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
    t->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
  };
  IMFMediaType* out = nullptr; MFCreateMediaType(&out); common(out);
  out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
  out->SetUINT32(MF_MT_AVG_BITRATE, (UINT32)kbps * 1000);
  hr = d->w->AddStream(out, &d->stream);
  release(out);
  if (FAILED(hr)) { error = "no H.264 encoder"; return false; }
  // the frames go in as NV12 (converted here, rows top first): no colour converter, no question of orientation
  IMFMediaType* in = nullptr; MFCreateMediaType(&in); common(in);
  in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
  in->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)d->W);
  hr = d->w->SetInputMediaType(d->stream, in, nullptr);
  release(in);
  if (FAILED(hr)) { error = "the encoder rejected NV12 input"; return false; }
  if (FAILED(d->w->BeginWriting())) { error = "could not start writing"; return false; }
  d->nv12.resize((size_t)d->W * d->H * 3 / 2);
  return true;
}

bool MenuVideoWriter::write(const uint8_t* rgb) {
  if (!d || !d->w) return false;
  const int W = d->W, H = d->H;
  uint8_t* Y = d->nv12.data(); uint8_t* UV = Y + (size_t)W * H;
  auto px = [&](int x, int y) { return rgb + ((size_t)(H - 1 - y) * W + x) * 3; };   // (bottom-up source)
  for (int y = 0; y < H; y++)   // BT.709, studio range
    for (int x = 0; x < W; x++) {
      const uint8_t* p = px(x, y);
      Y[(size_t)y * W + x] = (uint8_t)std::lround(16.f + (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) * (219.f / 255.f));
    }
  for (int y = 0; y < H; y += 2)
    for (int x = 0; x < W; x += 2) {
      float r = 0, g = 0, b = 0;
      for (int k = 0; k < 4; k++) { const uint8_t* p = px(x + (k & 1), y + (k >> 1)); r += p[0]; g += p[1]; b += p[2]; }
      r *= 0.25f / 255.f; g *= 0.25f / 255.f; b *= 0.25f / 255.f;
      uint8_t* c = UV + (size_t)(y / 2) * W + x;
      c[0] = (uint8_t)std::clamp(std::lround(128.f + 224.f * (-0.1146f * r - 0.3854f * g + 0.5f * b)), 16L, 240L);
      c[1] = (uint8_t)std::clamp(std::lround(128.f + 224.f * (0.5f * r - 0.4542f * g - 0.0458f * b)), 16L, 240L);
    }
  IMFMediaBuffer* buf = nullptr;
  if (FAILED(MFCreateMemoryBuffer((DWORD)d->nv12.size(), &buf))) return false;
  BYTE* dst = nullptr;
  buf->Lock(&dst, nullptr, nullptr); memcpy(dst, d->nv12.data(), d->nv12.size()); buf->Unlock();
  buf->SetCurrentLength((DWORD)d->nv12.size());
  IMFSample* s = nullptr; MFCreateSample(&s); s->AddBuffer(buf);
  LONGLONG dur = 10000000LL / d->fps;
  s->SetSampleTime(d->frame * dur); s->SetSampleDuration(dur);
  HRESULT hr = d->w->WriteSample(d->stream, s);
  release(s); release(buf);
  d->frame++;
  return SUCCEEDED(hr);
}

bool MenuVideoWriter::finish() {
  if (!d || !d->w) return false;
  HRESULT hr = d->w->Finalize();
  release(d->w);
  return SUCCEEDED(hr);
}

// ------------------------------------------------------------------ player
struct MenuVideoPlayer::Impl {
  struct Frame { double t; std::vector<uint8_t> px; };
  std::thread th; std::mutex m; std::condition_variable cv;
  std::deque<Frame> q;
  std::atomic<bool> stop{false}, failed{false};
  std::atomic<int> W{0}, H{0};
  std::wstring path;
  GLuint tex = 0; int texW = 0, texH = 0;
  double clock = 0; float lastNow = -1; bool shown = false;
  std::vector<uint8_t> cur;

  void run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    mfStart();
    IMFSourceReader* rd = nullptr;
    IMFAttributes* at = nullptr; MFCreateAttributes(&at, 1);
    at->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);   // decoded to RGB32 for us
    HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), at, &rd);
    release(at);
    IMFMediaType* t = nullptr;
    if (SUCCEEDED(hr)) {
      MFCreateMediaType(&t);
      t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); t->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
      hr = rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, t);
      release(t);
    }
    UINT32 w = 0, h = 0; LONG stride = 0;
    if (SUCCEEDED(hr) && SUCCEEDED(rd->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &t))) {
      MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &w, &h);
      UINT32 st = 0; if (SUCCEEDED(t->GetUINT32(MF_MT_DEFAULT_STRIDE, &st))) stride = (LONG)st; else MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, w, &stride);
      release(t);
    } else hr = E_FAIL;
    if (FAILED(hr) || !w || !h) { failed = true; release(rd); CoUninitialize(); return; }
    W = (int)w; H = (int)h;
    double loopBase = 0, lastT = 0;
    while (!stop) {
      DWORD idx = 0, flags = 0; LONGLONG ts = 0; IMFSample* s = nullptr;
      if (FAILED(rd->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &idx, &flags, &ts, &s))) { failed = true; break; }
      if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {   // loop
        release(s);
        loopBase += lastT + 1.0 / 30.0; lastT = 0;
        PROPVARIANT pv; PropVariantInit(&pv); pv.vt = VT_I8; pv.hVal.QuadPart = 0;
        rd->SetCurrentPosition(GUID_NULL, pv); PropVariantClear(&pv);
        continue;
      }
      if (!s) continue;
      Frame f; f.t = loopBase + ts / 1e7; lastT = ts / 1e7;
      f.px.resize((size_t)w * h * 4);
      IMFMediaBuffer* b = nullptr;
      if (SUCCEEDED(s->ConvertToContiguousBuffer(&b))) {
        IMF2DBuffer* b2 = nullptr; BYTE* row0 = nullptr; LONG pitch = 0;
        if (SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&b2))) && SUCCEEDED(b2->Lock2D(&row0, &pitch))) {   // row0: the top row
          for (UINT32 y = 0; y < h; y++) memcpy(&f.px[(size_t)y * w * 4], row0 + (LONG)y * pitch, (size_t)w * 4);
          b2->Unlock2D();
        } else {
          BYTE* p = nullptr; DWORD len = 0;
          if (SUCCEEDED(b->Lock(&p, nullptr, &len))) {
            BYTE* top = stride < 0 ? p + (size_t)(-stride) * (h - 1) : p;
            for (UINT32 y = 0; y < h; y++) memcpy(&f.px[(size_t)y * w * 4], top + (LONG)y * stride, (size_t)w * 4);
            b->Unlock();
          }
        }
        release(b2); release(b);
      }
      release(s);
      std::unique_lock<std::mutex> lk(m);
      cv.wait(lk, [&] { return stop || q.size() < 4; });
      if (stop) break;
      q.push_back(std::move(f));
    }
    release(rd);
    CoUninitialize();
  }
};

MenuVideoPlayer::MenuVideoPlayer() = default;
MenuVideoPlayer::~MenuVideoPlayer() {
  if (!d) return;
  { std::lock_guard<std::mutex> lk(d->m); d->stop = true; }
  d->cv.notify_all();
  if (d->th.joinable()) d->th.join();
  if (d->tex) glDeleteTextures(1, &d->tex);
}

bool MenuVideoPlayer::open(const std::string& path) {
  if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
  d.reset(new Impl());
  d->path = std::wstring(path.begin(), path.end());
  d->th = std::thread([this] { d->run(); });
  return true;
}

int MenuVideoPlayer::width() const { return d ? d->W.load() : 0; }
int MenuVideoPlayer::height() const { return d ? d->H.load() : 0; }

unsigned MenuVideoPlayer::frame(float now) {
  if (!d || d->failed) return 0;
  // the playback clock runs only while the menu is up (a gap means it was away: carry on from where it was)
  float dt = d->lastNow < 0 ? 0.f : now - d->lastNow;
  d->lastNow = now;
  if (dt > 0.f) d->clock += std::min(dt, 0.1f);
  bool got = false;
  {
    std::lock_guard<std::mutex> lk(d->m);
    while (!d->q.empty() && (d->q.front().t <= d->clock || !d->shown)) {   // the newest frame that is due
      d->cur.swap(d->q.front().px); d->q.pop_front(); got = true;
      if (!d->shown) { d->shown = true; d->clock = std::max(d->clock, 0.0); break; }
    }
    if (d->q.empty() && got) {}   // (decoding fell behind: show what's there)
  }
  d->cv.notify_all();
  if (got) {
    int w = d->W, h = d->H;
    if (!d->tex || d->texW != w || d->texH != h) {
      if (!d->tex) glGenTextures(1, &d->tex);
      glBindTexture(GL_TEXTURE_2D, d->tex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, d->cur.data());   // (RGB: the decoder may leave alpha at 0)
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      d->texW = w; d->texH = h;
    } else {
      glBindTexture(GL_TEXTURE_2D, d->tex);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, d->cur.data());
    }
  }
  return d->tex;
}
