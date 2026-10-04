// Solace Express - tower controller voices (see atc.h)
#include "atc.h"
#include "audio.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

static const char* kVoiceName[AtcVoice::kVoices] = {"tower_north", "tower_coast", "tower_valley"};

bool AtcVoice::load(const std::string& d) {
  dir = d; clips.clear(); lookup.clear();
  std::ifstream f(dir + "/voice_index.txt");
  if (!f) return false;
  std::string ln;
  while (std::getline(f, ln)) {
    if (ln.empty() || ln[0] == '#') continue;
    size_t a = ln.find('\t');
    if (a == std::string::npos) continue;
    if (ln[0] == '@') { lookup[ln.substr(1, a - 1)] = ln.substr(a + 1); continue; }
    size_t b = ln.find('\t', a + 1);
    Clip c; c.file = ln.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
    if (b != std::string::npos) c.words = ln.substr(b + 1);
    clips[ln.substr(0, a)] = c;
  }
  return !clips.empty();
}

std::string AtcVoice::line(int v, const char* key) const { return std::string("tower.") + kVoiceName[v] + "." + key; }
std::string AtcVoice::atom(int v, const std::string& key) const {
  auto it = lookup.find(std::string(kVoiceName[v]) + ".atom." + key); return it == lookup.end() ? std::string() : it->second;
}
std::string AtcVoice::digit(int v, int d) const {
  auto it = lookup.find(std::string(kVoiceName[v]) + ".digit." + std::to_string(d)); return it == lookup.end() ? std::string() : it->second;
}
std::string AtcVoice::text(const std::string& id) const { auto it = clips.find(id); return it == clips.end() ? std::string() : it->second.words; }

// WAV: 16-bit PCM or 8-bit mu-law, mono, any rate; decoded to floats at the audio engine's rate
static bool readWav(const std::string& path, std::vector<float>& out, int outRate) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::string b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (b.size() < 12 || b.compare(0, 4, "RIFF") || b.compare(8, 4, "WAVE")) return false;
  auto u16 = [&](size_t o) { return (unsigned)(uint8_t)b[o] | (unsigned)(uint8_t)b[o + 1] << 8; };
  auto u32 = [&](size_t o) { return u16(o) | u16(o + 2) << 16; };
  int fmt = 0, ch = 1, rate = 0, bits = 0; size_t data = 0, len = 0;
  for (size_t o = 12; o + 8 <= b.size();) {
    size_t n = u32(o + 4);
    if (!b.compare(o, 4, "fmt ")) { fmt = (int)u16(o + 8); ch = (int)u16(o + 10); rate = (int)u32(o + 12); bits = (int)u16(o + 22); }
    else if (!b.compare(o, 4, "data")) { data = o + 8; len = std::min(n, b.size() - data); }
    o += 8 + n + (n & 1);
  }
  if (!data || ch != 1 || rate <= 0 || !((fmt == 1 && bits == 16) || (fmt == 7 && bits == 8))) return false;
  std::vector<float> in;
  if (fmt == 1) { in.resize(len / 2); for (size_t i = 0; i < in.size(); i++) in[i] = (int16_t)u16(data + i * 2) / 32768.f; }
  else {
    in.resize(len);
    for (size_t i = 0; i < len; i++) {   // G.711 mu-law
      int u = ~(uint8_t)b[data + i] & 0xFF, sign = u & 0x80, e = (u >> 4) & 7, m = u & 0x0F;
      int v = (((m << 3) + 0x84) << e) - 0x84;
      in[i] = (sign ? -v : v) / 32768.f;
    }
  }
  // to the output rate (linear: the radio speech has nothing above ~6 kHz)
  double step = (double)rate / outRate;
  size_t n = (size_t)((in.size() - 1) / step) + 1;
  out.resize(n);
  for (size_t i = 0; i < n; i++) {
    double p = i * step; size_t k = (size_t)p; float t = (float)(p - k);
    out[i] = k + 1 < in.size() ? in[k] * (1 - t) + in[k + 1] * t : in[k];
  }
  return true;
}

const std::vector<float>* AtcVoice::pcm(const std::string& id) {
  auto it = clips.find(id);
  if (it == clips.end()) return nullptr;
  Clip& c = it->second;
  if (!c.loaded) { c.loaded = true; if (!readWav(dir + "/" + c.file, c.pcm, g_audio.sampleRate)) c.pcm.clear(); }
  return c.pcm.empty() ? nullptr : &c.pcm;
}

// squelch click, the clips with 35 ms between fragments (a longer pause between sentences), and a squelch tail
std::shared_ptr<std::vector<float>> AtcVoice::assemble(const Tx& tx) {
  auto out = std::make_shared<std::vector<float>>();
  const float sr = (float)g_audio.sampleRate;
  uint32_t seed = 0x9e3779b9u;
  auto noise = [&]() { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 9) / 4194304.f - 1.f; };
  auto squelch = [&](float secs, float level, bool tail) {   // band-limited hiss with a fast edge
    float lp = 0, hp = 0, prev = 0;
    int n = (int)(secs * sr);
    for (int i = 0; i < n; i++) {
      float t = i / (float)n, w = noise();
      lp += (w - lp) * 0.35f; hp = lp - prev; prev = lp;
      float env = tail ? (1.f - t) * (1.f - t) : (t < 0.15f ? t / 0.15f : 1.f - (t - 0.15f) / 0.85f);
      out->push_back(hp * level * env);
    }
  };
  squelch(0.06f, 0.18f, false);
  out->insert(out->end(), (size_t)(0.04f * sr), 0.f);
  bool any = false;
  for (const std::string& id : tx.ids) {
    if (id.empty()) { out->insert(out->end(), (size_t)(0.22f * sr), 0.f); continue; }
    const std::vector<float>* p = pcm(id);
    if (!p) continue;
    if (any) out->insert(out->end(), (size_t)(0.035f * sr), 0.f);
    out->insert(out->end(), p->begin(), p->end());
    any = true;
  }
  out->insert(out->end(), (size_t)(0.03f * sr), 0.f);
  squelch(0.14f, 0.22f, true);
  if (!any) out->clear();
  return out;
}

void AtcVoice::start(const Tx& tx) {
  auto buf = assemble(tx);
  if (buf->empty()) return;
  g_audio.voicePlay(buf);
  playingPrio = tx.prio; idleT = 0;
  started = tx.text; history.push_back(tx.text);
}

bool AtcVoice::busy() const { return g_audio.voiceBusy(); }

void AtcVoice::say(const Tx& tx) {
  if (!ok()) return;
  if (busy() && tx.prio >= playingPrio + 30) {   // urgent: cut in (a go-around over a routine call)
    g_audio.voiceStop();
    queue.clear();
    start(tx);
    return;
  }
  queue.push_back(tx);
  if (queue.size() > 4) {   // never a long backlog of stale calls: drop the least important
    size_t worst = 0;
    for (size_t i = 1; i < queue.size(); i++) if (queue[i].prio < queue[worst].prio) worst = i;
    queue.erase(queue.begin() + worst);
  }
}

std::string AtcVoice::update(float dt) {
  started.clear();
  if (busy()) return started;
  playingPrio = -1;
  idleT += dt;
  if (!queue.empty() && idleT > 0.6f) {   // the most important waiting call, oldest first
    size_t best = 0;
    for (size_t i = 1; i < queue.size(); i++) if (queue[i].prio > queue[best].prio) best = i;
    Tx tx = queue[best];
    queue.erase(queue.begin() + best);
    start(tx);
  }
  return started;
}

void AtcVoice::cancel() { queue.clear(); if (busy()) g_audio.voiceStop(); playingPrio = -1; }
