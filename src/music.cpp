// Somnium Volandi - the generative score (music.h)
#include "music.h"
#include "audio.h"
#include <algorithm>
#include <chrono>
#include <cmath>

MusicPlayer g_music;

namespace {
enum Inst { I_PAD = 0, I_BELL, I_PLUCK, I_BASS, I_CHIME, I_BRASS, I_SHAKER, I_CLAP };
const float kInstLevel[] = {0.055f, 0.11f, 0.10f, 0.16f, 0.09f, 0.045f, 0.035f, 0.07f};

const int kTable = 4096;
float s_sine[kTable + 1];
struct SineInit { SineInit() { for (int i = 0; i <= kTable; i++) s_sine[i] = sinf(6.2831853f * i / kTable); } } s_sineInit;
inline float sine(float ph) {   // ph in cycles (any value)
  ph -= floorf(ph);
  const float x = ph * kTable; const int i = (int)x; const float f = x - i;
  return s_sine[i] + (s_sine[i + 1] - s_sine[i]) * f;
}
inline float midiHz(int m) { return 440.f * powf(2.f, (m - 69) / 12.f); }

// A mood: its tempo and key, four chords (semitones over the key, low to high), the arpeggio's instrument, rate and
// octave, the layers it has, the share of the reverb and its level
struct MoodDef {
  float bpm; int key; int beatsPerChord; int chords[4][4];
  int arpInst, arpPerBeat; float arpVel, arpChance; int arpOct;
  float padVel; bool bass, shaker, brass; float melody, wet, level;
};
const MoodDef kMoods[MOOD_COUNT] = {
  {60, 50, 4, {{0, 7, 11, 16}, {0, 7, 11, 16}, {0, 7, 11, 16}, {0, 7, 11, 16}}, I_BELL, 1, 0, 0, 0, 0, false, false, false, 0, 0.3f, 0},   // (none)
  // the menu: D major, slow and open - Dmaj9, Bm7, Gmaj7, A7sus4, two bars each; bells, a sparse melody
  {68, 50, 8, {{0, 7, 11, 16}, {-3, 4, 7, 12}, {-7, 0, 4, 9}, {-5, 2, 5, 12}}, I_BELL, 2, 0.5f, 0.8f, 12, 1.f, true, false, false, 0.3f, 0.36f, 0.9f},
  // the hub: G major, lighter - Gmaj7, Dsus2, Em9, Cmaj7#11, a bar each; plucks, a shaker on the off-beats
  {86, 55, 4, {{0, 4, 7, 11}, {-5, 2, 7, 9}, {-3, 4, 7, 11}, {-7, 4, 7, 11}}, I_PLUCK, 2, 0.55f, 0.9f, 12, 0.8f, true, true, false, 0.2f, 0.26f, 0.8f},
  // the cruise: E major, a slow wash under the engines - Emaj9, C#m9, Amaj9, B6sus, four bars each; a few high bells
  {56, 52, 16, {{0, 7, 11, 14}, {-3, 4, 7, 11}, {-7, 4, 9, 12}, {-5, 2, 4, 9}}, I_BELL, 1, 0.35f, 0.3f, 24, 0.9f, true, false, false, 0.1f, 0.45f, 0.6f},
  // the finale: D major, brighter - D, A/C#, Bm7, Gmaj9, a bar each; bells, brass, a fuller melody
  {76, 50, 4, {{0, 7, 12, 16}, {-1, 7, 11, 14}, {-3, 4, 9, 12}, {-7, 4, 9, 12}}, I_BELL, 2, 0.6f, 0.9f, 12, 0.9f, true, false, true, 0.45f, 0.32f, 0.9f},
};
const int kPatterns[5][8] = {
  {0, 1, 2, 3, 4, 3, 2, 1}, {0, 2, 1, 3, 2, 4, 3, -1}, {4, 3, 2, 1, 2, 3, 4, 6}, {0, -1, 2, 3, -1, 4, 3, -1}, {2, 3, 4, -1, 5, 4, 3, 2}};
const int kPenta[5] = {0, 2, 4, 7, 9};
}

MusicGen::MusicGen(float sampleRate, uint32_t seed) : sr(sampleRate), rng(seed * 2654435761u + 1u) {
  voices.reserve(64);
  const int combD[4] = {1116, 1188, 1277, 1356}, apD[2] = {556, 441};
  for (int c = 0; c < 2; c++) {
    for (int k = 0; k < 4; k++) comb[c][k].assign((size_t)std::max(8.f, (combD[k] + c * 23) * sr / 44100.f), 0.f);
    for (int k = 0; k < 2; k++) ap[c][k].assign((size_t)std::max(8.f, (apD[k] + c * 23) * sr / 44100.f), 0.f);
  }
}

float MusicGen::urand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.f; }

void MusicGen::play(int inst, int midi, float vel, float dur, float pan) {
  Voice v; v.inst = inst; v.f = midiHz(midi); v.vel = vel; v.dur = dur; v.pan = pan;
  v.ph1 = urand(); v.ph2 = urand(); v.ph3 = 0;
  if (voices.size() >= 64) {   // the pool is full: the one sounding longest gives way
    auto it = std::max_element(voices.begin(), voices.end(), [](const Voice& a, const Voice& b) { return a.t < b.t; });
    *it = v;
  } else voices.push_back(v);
}

void MusicGen::releasePads() {
  for (Voice& v : voices)
    if ((v.inst == I_PAD || v.inst == I_BASS || v.inst == I_BRASS) && v.rel < 0 && v.pan < 2.f) v.rel = v.t;
}

void MusicGen::setMood(int m) {
  m = std::clamp(m, 0, MOOD_COUNT - 1);
  if (m == moodNow) return;
  releasePads();
  moodNow = m; beat = 0; chord = 0; pattern = 0; beatAt = now + 0.05 * sr;
  gainTo = kMoods[m].level;
}

void MusicGen::sting(int kind) {
  const int key = moodNow != MOOD_NONE ? kMoods[moodNow].key : 50;
  const double t0 = now + 0.02 * sr;
  // (pan 2..3: a stinger's voices, on their own level: they play whatever the mood's is, even over silence)
  auto at = [&](float sec, int inst, int midi, float vel, float dur) { schedule(t0 + sec * sr, inst, midi, vel, dur, 2.5f + (urand() - 0.5f) * 0.6f); };
  switch (kind) {
    case STING_SUCCESS:
      { const int up[4] = {0, 4, 7, 12}; for (int i = 0; i < 4; i++) at(i * 0.09f + (i == 3 ? 0.03f : 0.f), I_CHIME, key + 24 + up[i], 0.7f, 1.2f); }
      for (int n : {0, 7, 12, 16}) at(0.f, I_PAD, key + n, 0.7f, 1.4f);
      break;
    case STING_FAIL:
      at(0.f, I_BELL, key + 19, 0.5f, 0.6f); at(0.32f, I_BELL, key + 15, 0.45f, 0.6f); at(0.64f, I_BELL, key + 12, 0.45f, 1.2f);
      for (int n : {0, 3, 7}) at(0.f, I_PAD, key + n, 0.55f, 1.6f);
      break;
    case STING_LICENCE:
      for (int i = 0; i < 3; i++) at(i * 0.12f, I_BRASS, key + 19, 0.7f, 0.09f);
      for (int n : {12, 16, 19, 24}) at(0.4f, I_BRASS, key + n, 0.75f, 1.4f);
      at(0.4f, I_CHIME, key + 36, 0.7f, 2.f); at(0.55f, I_CHIME, key + 31, 0.5f, 2.f);
      for (int n : {0, 7, 12, 16}) at(0.4f, I_PAD, key + n, 0.8f, 2.2f);
      at(0.4f, I_BASS, key - 12, 0.8f, 2.f);
      break;
    case STING_ACHIEVEMENT:
      { const int up[5] = {7, 9, 12, 16, 19}; for (int i = 0; i < 5; i++) at(i * 0.065f, I_CHIME, key + 24 + up[i], 0.55f, 1.f); }
      break;
    case STING_APPLAUSE:   // a cabin's applause: claps scattered over three seconds, thick at first, thinning out
      for (int i = 0; i < 90; i++) { const float u = urand(); at(0.8f + u * u * 3.f, I_CLAP, 0, 0.5f + 0.5f * urand(), 0.05f); }   // (as the wheels settle)
      break;
    case STING_MEDAL:
      { const int up[6] = {0, 7, 12, 16, 19, 24}; for (int i = 0; i < 6; i++) at(i * 0.07f, I_CHIME, key + 24 + up[i], 0.6f, 1.2f); }
      for (int n : {12, 16, 19}) at(0.42f, I_BRASS, key + n, 0.6f, 1.f);
      break;
  }
}

void MusicGen::note(int step) {
  const int key = moodNow != MOOD_NONE ? kMoods[moodNow].key : 50;
  step = std::clamp(step, 0, 14);
  schedule(now + 0.01 * sr, I_CHIME, key + 24 + kPenta[step % 5] + 12 * (step / 5), 0.65f, 1.2f, 2.5f);
  schedule(now + 0.01 * sr, I_BELL, key + 12 + kPenta[step % 5] + 12 * (step / 5), 0.35f, 1.f, 2.5f);
}

void MusicGen::onBeat() {
  const MoodDef& d = kMoods[moodNow];
  const double bl = 60.0 / d.bpm * sr;
  const int inChord = beat % d.beatsPerChord;
  if (inChord == 0) {
    chord = (beat / d.beatsPerChord) % 4;
    if (chord == 0 && beat > 0) pattern = (int)(urand() * 5) % 5;
    releasePads();
    const float len = (float)(d.beatsPerChord * bl / sr);
    for (int i = 0; i < 4; i++) play(I_PAD, d.key + d.chords[chord][i], d.padVel, len + 0.2f, -0.45f + 0.3f * i);
    if (d.bass) play(I_BASS, d.key + d.chords[chord][0] - 12, 0.8f, d.beatsPerChord > 4 ? len : (float)(1.6 * bl / sr), 0.f);
    if (d.brass) for (int i = 1; i < 4; i++) play(I_BRASS, d.key + d.chords[chord][i], 0.55f, len * 0.85f, -0.3f + 0.3f * i);
  }
  if (d.bass && d.beatsPerChord == 4 && inChord == 2)   // the hub's and the finale's bass walks on the third beat
    schedule(now, I_BASS, d.key + d.chords[chord][urand() < 0.5f ? 0 : 1] - 12, 0.65f, (float)(1.4 * bl / sr), 0.f);
  // the arpeggio (a menu chord's second bar sparser)
  const bool secondBar = d.beatsPerChord >= 8 && inChord >= 4;
  for (int k = 0; k < d.arpPerBeat; k++) {
    const int step = (inChord * d.arpPerBeat + k) % 8, idx = kPatterns[pattern][step];
    if (idx < 0 || urand() > d.arpChance * (secondBar && k ? 0.4f : 1.f)) continue;
    const int note = d.key + d.chords[chord][idx % 4] + 12 * (idx / 4) + d.arpOct;
    schedule(now + k * bl / d.arpPerBeat, d.arpInst, note, d.arpVel * (0.85f + 0.3f * urand()), (float)(bl / sr * 1.5f), (urand() - 0.5f) * 0.8f);
  }
  if (d.shaker) schedule(now + bl * 0.5, I_SHAKER, 0, 0.6f + 0.3f * urand(), 0.05f, 0.25f);
  // a melody in the key's major pentatonic, two octaves up, near the last note
  if (d.melody > 0 && urand() < d.melody && inChord % 2 == 0) {
    melDeg = std::clamp(melDeg + (int)(urand() * 5) - 2, 0, 9);
    const int note = d.key + 24 + kPenta[melDeg % 5] + 12 * (melDeg / 5);
    schedule(now + (urand() < 0.3f ? bl * 0.5 : 0.0), urand() < 0.7f ? I_BELL : I_CHIME, note, 0.5f + 0.2f * urand(), (float)(bl / sr * 2), (urand() - 0.5f) * 0.6f);
  }
  beat++;
}

void MusicGen::render(float* out, int frames) {
  const float inv = 1.f / sr, gk = 1.f - expf(-1.f / (1.2f * sr));
  for (int i = 0; i < frames; i++) {
    if ((i & 31) == 0) {
      if (moodNow != MOOD_NONE) while (now >= beatAt) { onBeat(); beatAt += 60.0 / kMoods[moodNow].bpm * sr; }
      for (size_t e = 0; e < events.size();) {
        if (events[e].at <= now) {
          const Event ev = events[e]; events[e] = events.back(); events.pop_back();
          if (ev.inst == I_SHAKER || ev.inst == I_CLAP) { Voice v; v.inst = ev.inst; v.vel = ev.vel; v.dur = ev.dur; v.pan = ev.pan; v.ph3 = 0; if (voices.size() < 64) voices.push_back(v); }
          else play(ev.inst, ev.midi, ev.vel, ev.dur, ev.pan);
        } else e++;
      }
    }
    gain += (gainTo - gain) * gk;
    float L = 0, R = 0, sL = 0, sR = 0;
    for (Voice& v : voices) {
      float s = 0, env = 0, s2 = 0;
      const float t = v.t;
      if (v.rel < 0 && t >= v.dur && v.inst != I_BELL && v.inst != I_CHIME && v.inst != I_PLUCK && v.inst != I_SHAKER && v.inst != I_CLAP) v.rel = t;
      const float relEnv = v.rel >= 0 ? expf(-(t - v.rel) / (v.inst == I_PAD ? 0.9f : v.inst == I_BRASS ? 0.18f : 0.12f)) : 1.f;
      switch (v.inst) {
        case I_PAD: {
          const float a = std::min(t / 1.6f, 1.f); env = a * a * (3 - 2 * a) * relEnv;
          v.ph1 += v.f * inv; v.ph2 += v.f * 1.004f * inv;
          s = sine(v.ph1) + 0.22f * sine(2 * v.ph1) + 0.07f * sine(3 * v.ph1);
          s2 = sine(v.ph2) + 0.22f * sine(2 * v.ph2);
          break;
        }
        case I_BELL: case I_CHIME: case I_PLUCK: {
          const float ratio = v.inst == I_BELL ? 2.f : v.inst == I_CHIME ? 3.5f : 1.f;
          const float idx = v.inst == I_BELL ? 1.6f * expf(-t * 5.f) + 0.25f : v.inst == I_CHIME ? 2.4f * expf(-t * 3.f) + 0.4f : 1.8f * expf(-t * 14.f) + 0.1f;
          env = std::min(t / 0.004f, 1.f) * expf(-t * (v.inst == I_BELL ? 1.4f : v.inst == I_CHIME ? 0.9f : 3.2f));
          v.ph1 += v.f * inv; v.ph2 += v.f * ratio * 1.002f * inv;
          s = sine(v.ph1 + idx * sine(v.ph2) * 0.159f);
          s2 = s;
          break;
        }
        case I_BASS: {
          env = std::min(t / 0.015f, 1.f) * expf(-t * 0.5f) * relEnv;
          v.ph1 += v.f * inv;
          s = sine(v.ph1) + 0.18f * sine(2 * v.ph1); s2 = s;
          break;
        }
        case I_BRASS: {
          env = std::min(t / 0.08f, 1.f) * relEnv;
          const float vib = t > 0.3f ? 1.f + 0.004f * sine(5.f * t) : 1.f;
          v.ph1 += v.f * vib * inv;
          const float bright = 0.6f + 0.4f * std::min(t / 0.15f, 1.f);
          float w = 1.f;
          for (int k = 1; k <= 6; k++) { w *= bright; s += sine(k * v.ph1) * w / k; }
          s *= 0.6f; s2 = s;
          break;
        }
        case I_SHAKER: {
          env = expf(-t * 45.f);
          const float n = urand() * 2.f - 1.f; s = n - v.ph3; v.ph3 = n; s2 = s;
          break;
        }
        case I_CLAP: {   // a hand clap: a burst of noise, a little body under its hiss (ph2: a low-passed copy)
          env = std::min(t / 0.002f, 1.f) * expf(-t * 32.f);
          const float n = urand() * 2.f - 1.f; v.ph2 += (n - v.ph2) * 0.35f; s = (n - v.ph3) * 0.6f + v.ph2 * 0.8f; v.ph3 = n; s2 = s;
          break;
        }
      }
      v.t += inv;
      const bool stingV = v.pan >= 2.f;
      const float pan = stingV ? v.pan - 2.5f : v.pan;
      const float a = env * v.vel * kInstLevel[v.inst];
      const float l = a * (0.5f - 0.5f * pan) * 2.f, r = a * (0.5f + 0.5f * pan) * 2.f;
      if (stingV) { sL += s * l; sR += s2 * r; } else { L += s * l; R += s2 * r; }
      if (env < 1e-4f && t > 0.05f && (v.rel >= 0 || v.inst == I_BELL || v.inst == I_CHIME || v.inst == I_PLUCK || v.inst == I_SHAKER || v.inst == I_CLAP)) v.inst = -1 - v.inst;   // (spent)
    }
    voices.erase(std::remove_if(voices.begin(), voices.end(), [](const Voice& v) { return v.inst < 0; }), voices.end());
    L = L * gain + sL * 0.85f; R = R * gain + sR * 0.85f;
    // the reverb: the mood's share of a small hall, fed from both sides
    const float wet = moodNow != MOOD_NONE ? kMoods[moodNow].wet : 0.3f;
    const float in = (L + R) * 0.5f;
    float out2[2];
    for (int c = 0; c < 2; c++) {
      float acc = 0;
      for (int k = 0; k < 4; k++) {
        std::vector<float>& b = comb[c][k]; int& ix = combI[c][k];
        const float y = b[ix];
        combLp[c][k] = y * 0.7f + combLp[c][k] * 0.3f;
        b[ix] = in * 0.25f + combLp[c][k] * 0.82f;
        ix = (ix + 1) % (int)b.size();
        acc += y;
      }
      for (int k = 0; k < 2; k++) {
        std::vector<float>& b = ap[c][k]; int& ix = apI[c][k];
        const float bv = b[ix], y = -acc + bv;
        b[ix] = acc + bv * 0.5f;
        ix = (ix + 1) % (int)b.size();
        acc = y;
      }
      out2[c] = acc;
    }
    const float oL = L + out2[0] * wet * 0.5f, oR = R + out2[1] * wet * 0.5f;
    out[i * 2] = tanhf(oL * 1.6f) * 0.85f;
    out[i * 2 + 1] = tanhf(oR * 1.6f) * 0.85f;
    now += 1.0;
  }
}

void MusicPlayer::start(float sampleRate) {
  if (th.joinable()) return;
  rate = sampleRate; quit = false;
  th = std::thread([this] { run(); });
}

void MusicPlayer::stop() {
  quit = true;
  if (th.joinable()) th.join();
}

void MusicPlayer::own(bool on) {
  const uint32_t t = token.load();
  if (on && !t) token = g_audio.musicBegin();
  else if (!on && t) {
    if (g_audio.musicBuffered(t) >= 0) g_audio.musicStop();   // (only while it is still ours: the radio may have it)
    token = 0;
  }
}

void MusicPlayer::setMood(int m) { wantMood = m; }
void MusicPlayer::sting(int k) { std::lock_guard<std::mutex> g(m); if (stings.size() < 8) stings.push_back(k); }
void MusicPlayer::note(int step) { std::lock_guard<std::mutex> g(m); if (stings.size() < 8) stings.push_back(1000 + std::clamp(step, 0, 14)); }   // (a note: 1000 + its step)

void MusicPlayer::run() {
  MusicGen gen(rate, (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count());
  std::vector<float> buf(1024 * 2);
  while (!quit) {
    const uint32_t t = token.load();
    const int have = t ? g_audio.musicBuffered(t) : -1;
    if (t && have < 0) { uint32_t was = t; token.compare_exchange_strong(was, 0u); }   // (the radio took the ring)
    if (have < 0 || have > 9000) { std::this_thread::sleep_for(std::chrono::milliseconds(have < 0 ? 20 : 4)); continue; }
    gen.setMood(wantMood.load());
    { std::lock_guard<std::mutex> g(m); for (int k : stings) { if (k >= 1000) gen.note(k - 1000); else gen.sting(k); } stings.clear(); }
    gen.render(buf.data(), 1024);
    g_audio.musicPush(t, buf.data(), 1024);
  }
}
