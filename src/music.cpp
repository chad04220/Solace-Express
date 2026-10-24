// Somnium Volandi - the generative score (music.h)
//
// The music is written here as data - each mood's chord progressions, its themes and the sections its arrangement
// steps through - and played by a scheduler that, a quarter of a second before each bar, voices the bar's chords (the
// voicing nearest the last one, so the parts move as little as they can) and lays out every part's notes: pads and
// strings held through the tones two chords share, a bass held, pulsed or walking, arpeggios, comping, a theme, drums
// and percussion, each with a little human timing. The instruments are synthesised 32 samples at a time (envelopes
// and filters at that rate, oscillators at the sample rate), mixed with sends to a ping-pong delay and a hall reverb,
// and mastered: a glue compressor, a look-ahead peak limiter and a soft safety clip.
#include "music.h"
#include "audio.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#if defined(__SSE__) || defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#define MUSIC_FTZ 1
#endif

MusicPlayer g_music;

namespace {
constexpr float kPi = 3.14159265f, kTwoPi = 6.28318531f;
constexpr int kBlock = 32;

// ---------------------------------------------------------------- oscillators, filters, noise
const int kTable = 8192;
float s_sine[kTable + 1];
struct SineInit { SineInit() { for (int i = 0; i <= kTable; i++) s_sine[i] = sinf(kTwoPi * i / kTable); } } s_sineInit;
inline float sine(float ph) {   // ph in cycles, any value
  ph -= floorf(ph);
  const float x = ph * kTable; const int i = (int)x; const float f = x - i;
  return s_sine[i] + (s_sine[i + 1] - s_sine[i]) * f;
}
inline float midiHz(float m) { return 440.f * exp2f((m - 69.f) / 12.f); }
// a band-limited sawtooth (the PolyBLEP correction at its reset): ph 0..1, dt its step a sample
inline float saw(float ph, float dt) {
  float s = 2.f * ph - 1.f;
  if (ph < dt) { const float t = ph / dt; s -= t + t - t * t - 1.f; }
  else if (ph > 1.f - dt) { const float t = (ph - 1.f) / dt; s -= t * t + t + t + 1.f; }
  return s;
}
inline float smooth01(float x) { x = std::clamp(x, 0.f, 1.f); return x * x * (3.f - 2.f * x); }
struct MRng {
  uint32_t s = 1;
  float u() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.f; }
  float n() { return u() * 2.f - 1.f; }
};
// the state-variable filter in its trapezoidal (zero-delay feedback) form: stable however fast it is swept
struct Svf {
  float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;
  void set(float fc, float q, float sr) {
    const float g = tanf(kPi * std::clamp(fc, 16.f, sr * 0.45f) / sr);
    k = 1.f / q; a1 = 1.f / (1.f + g * (g + k)); a2 = g * a1; a3 = g * a2;
  }
  inline void tick(float x, float& lo, float& band) {
    const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.f * v1 - ic1; ic2 = 2.f * v2 - ic2; lo = v2; band = v1;
  }
  inline float lp(float x) { float l, b; tick(x, l, b); return l; }
  inline float bp(float x) { float l, b; tick(x, l, b); return b; }
  inline float hp(float x) { float l, b; tick(x, l, b); return x - k * b - l; }
};

// ---------------------------------------------------------------- the instruments
enum Inst { I_PAD, I_STRINGS, I_BRASS, I_HORN, I_FLUTE, I_EP, I_VIBES, I_CELESTA, I_HARP, I_GUITAR, I_BASS, I_UPRIGHT,
            I_KICK, I_BRUSH, I_RIM, I_HAT, I_SHAKER, I_CLAP, I_TIMPANI, I_CYMBAL, I_COUNT };
// each one's level, its sends to the reverb and the delay, whether a note is held for its length and then released
// (and how fast: seconds to fall by e), or rings out by itself (a held string is damped at its length)
struct InstDef { float level, rev, dly; bool held; float rel; };
const InstDef kInst[I_COUNT] = {
  {0.070f, 0.55f, 0.00f, true, 1.5f},    // pad: detuned saws through a low-pass that opens as it swells
  {0.060f, 0.45f, 0.00f, true, 0.7f},    // strings: an ensemble, a slower vibrato
  {0.075f, 0.32f, 0.00f, true, 0.20f},   // brass: the section's chords, the filter's bite at the attack
  {0.130f, 0.38f, 0.06f, true, 0.25f},   // horn: a mellow solo
  {0.150f, 0.42f, 0.16f, true, 0.16f},   // flute: breath and a vibrato that grows into the note
  {0.200f, 0.30f, 0.12f, true, 0.18f},   // electric piano: FM with its tine and tremolo
  {0.150f, 0.40f, 0.20f, true, 0.40f},   // vibraphone: its bar's partials, the motor's tremolo
  {0.110f, 0.45f, 0.25f, false, 0.20f},  // celesta: struck bars, a bright ting
  {0.300f, 0.40f, 0.10f, false, 0.15f},  // harp: a plucked string
  {0.260f, 0.26f, 0.08f, true, 0.10f},   // nylon guitar: a plucked string, damped at its length
  {0.200f, 0.04f, 0.00f, true, 0.10f},   // bass: sine and a filtered saw
  {0.420f, 0.08f, 0.00f, true, 0.08f},   // upright bass: a dark plucked string, a thump
  {0.420f, 0.05f, 0.00f, false, 0.10f},  // kick (and, p1 > 0.5, an orchestral bass drum)
  {0.110f, 0.16f, 0.00f, false, 0.10f},  // brush on the snare
  {0.060f, 0.18f, 0.00f, false, 0.10f},  // rim click
  {0.050f, 0.12f, 0.00f, false, 0.10f},  // hi-hat (p1 > 0.5: open)
  {0.060f, 0.14f, 0.00f, false, 0.10f},  // shaker
  {0.120f, 0.22f, 0.00f, false, 0.10f},  // a hand clap (p1: its pitch, p2: its Q)
  {0.420f, 0.40f, 0.00f, false, 0.10f},  // timpani
  {0.060f, 0.40f, 0.00f, false, 0.10f},  // cymbal (p1 > 0.5: a swell up to its length)
};
bool isString(int inst) { return inst == I_HARP || inst == I_GUITAR || inst == I_UPRIGHT; }
float g_instGain[I_COUNT] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};   // (each instrument's own fader: 1 in the game; a render tool solos with it)

constexpr int kVoices = 128, kStrings = 40, kStringLen = 4096, kLegato = 10;   // (parts from kLegato on: slurred lines)
struct Voice {
  int inst = -1, part = 0; bool sting = false;
  float midi = 0, f = 0, fTo = 0, vel = 0, dur = 0, gl = 1, gr = 1;   // (fTo: the pitch a slurred line glides to)
  float t = 0, rel = -1, lastEnv = 0, peak = 0, gain = 1;
  float ph[4] = {}, det[3] = {}, lfo = 0, lfoRate = 0, p1 = 0, p2 = 0;
  float z[4] = {};
  Svf fa, fb;
  MRng rng;
  int ks = -1, ksN = 0, ksW = 0; float ksRho = 0, ksS = 0, ksA = 0, ksX1 = 0, ksApX = 0, ksApY = 0, ksHp = 0, ksHpX = 0;
};
struct Event { double at; int inst; float midi, vel, dur, pan, p1, p2; int part; bool sting; float gain; };

// ---------------------------------------------------------------- the music, written out
// a chord: its root (semitones over the key), its kind, and the bass note under it (-99: the root)
struct ChordType { const char* name; int n; int tones[6]; };
const ChordType kChords[] = {
  {"maj", 3, {0, 4, 7}}, {"min", 3, {0, 3, 7}}, {"maj7", 4, {0, 4, 7, 11}}, {"maj9", 5, {0, 4, 7, 11, 14}},
  {"m7", 4, {0, 3, 7, 10}}, {"m9", 5, {0, 3, 7, 10, 14}}, {"m11", 6, {0, 3, 7, 10, 14, 17}}, {"dom7", 4, {0, 4, 7, 10}},
  {"dom9", 5, {0, 4, 7, 10, 14}}, {"sus4", 3, {0, 5, 7}}, {"sus47", 4, {0, 5, 7, 10}}, {"13sus", 5, {0, 5, 10, 14, 21}},
  {"add9", 4, {0, 4, 7, 14}}, {"6sus", 4, {0, 5, 7, 9}}, {"m6", 4, {0, 3, 7, 9}}, {"sus2", 3, {0, 2, 7}},
  {"69", 5, {0, 4, 7, 9, 14}}, {"maj7s11", 5, {0, 4, 7, 11, 18}},
};
const int kNumChords = (int)(sizeof(kChords) / sizeof(kChords[0]));
struct ChordSeg { int root = 0, type = 0, bass = -99; float beat = 0, len = 4; };
struct Note { float beat, len; int pitch; float accent; };   // pitch: semitones over the theme's tonic (-99: a rest)
struct Prog { std::vector<std::vector<ChordSeg>> bars; };
struct Phrase { std::vector<Note> notes; };
// the parts a section plays
enum : uint32_t {
  F_PAD = 1u << 0, F_STRINGS = 1u << 1, F_BRASS = 1u << 2, F_SHIMMER = 1u << 3, F_DRONE = 1u << 4, F_BASS = 1u << 5,
  F_PULSE = 1u << 6, F_WALK = 1u << 7, F_HARP = 1u << 8, F_HARPSLOW = 1u << 9, F_EPARP = 1u << 10, F_GUITAR = 1u << 11,
  F_COMP = 1u << 12, F_EPWHOLE = 1u << 13, F_STARS = 1u << 14, F_BRUSHES = 1u << 15, F_ORCH = 1u << 16,
  F_TIMP = 1u << 17, F_SWELL = 1u << 18, F_FLUTE = 1u << 19, F_HORN = 1u << 20, F_VIBES = 1u << 21, F_DOUBLE = 1u << 22,
  F_PADSOFT = 1u << 23,
};
struct SectionDef { int prog, mel; uint32_t flags; float intensity; };
struct SongDef {
  float bpm, swing; int key, padLo, padHi; float level, rt60, dampHz, wet;
  std::vector<const char*> progs, mels; std::vector<SectionDef> sections; int loopTo;
  float mix[I_COUNT] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};   // the mood's balance: each instrument's gain
};
// The themes are in the key's major scale (semitones over the tonic, 0 = the tonic in the theme's octave); the
// progressions' roots too. The menu and the finale share the main theme ("Somnium": a rising sixth that settles,
// answered a third higher), the finale with a hymn-like second strain
const char* kMenuA = "0:maj9 | 9:m7 | 5:maj7 | 7:sus47 7:dom7 | 0:maj9 | 4:m7 | 5:maj9 | 7:sus4 7:maj";
const char* kMenuB = "2:m9 | 7:13sus | 4:m7 | 9:m9 | 5:maj7 | 4:m7 | 2:m9 | 7:sus47";
const char* kThemeA = "4:1.5 2:.5 4:1 7:1 | 12:2! 11:1 9:1 | 9:3 7:1 | 5:2 4:1 2:1 | 4:1.5 2:.5 4:1 7:1 | 14:2! 11:1 9:1 | 9:1 7:1 5:1 4:1 | 2:3 r:1";
const char* kThemeB = "9:1 12:1 16:2! | 14:3 12:1 | 11:2 7:2 | 9:2 12:1 11:1 | 11:1.5 9:.5 7:2 | 7:2 4:2 | 2:1 4:1 7:1 9:1 | 7:2 r:2";
const char* kHubA = "0:maj7 | 9:m7 | 2:m7 | 7:sus47 7:dom7 | 0:maj7 | 4:m7 9:dom7 | 2:m7 | 7:sus47 7:dom9";
const char* kHubB = "5:maj7 | 5:m6 | 4:m7 | 9:dom7 | 2:m7 | 7:dom7 | 0:maj7 | 2:m7 7:dom7";
const char* kHubTuneA = "r:.5 11:.5 14:1 11:.5 7:1.5 | 7:1 4:.5 7:.5 9:2 | 12:1.5! 9:.5 5:1 2:1 | 5:2 4:1 r:1 | r:.5 11:.5 14:1 16:1! 14:1 | 14:1 9:1 13:1 11:1 | 12:1 9:1 7:1 5:1 | 2:3 r:1";
const char* kHubTuneB = "4:1 7:1 11:2! | 2:1 7:1 8:2 | 7:1.5 4:.5 2:2 | 1:1 4:1 9:1 13:1! | 12:2 9:1 5:1 | 11:1 9:1 6:1 2:1 | 7:3 r:1 | r:4";
const char* kCruise = "0:maj9 | 0:maj9 | 9:m9 | 9:m9 | 5:maj9 | 5:maj9 | 7:6sus | 7:6sus | 4:m7 | 4:m7 | 2:m11 | 2:m11 | 5:add9 | 5:add9 | 7:sus4 | 7:sus47";
const char* kFinaleB = "5:maj | 7:maj | 4:m7 | 9:m7 | 2:m7 | 7:sus4 7:maj | 0:add9 | 0:maj";
const char* kFinaleTuneB = "9:2 7:1 9:1 | 11:3! 9:1 | 7:2 11:2 | 12:2! 11:1 9:1 | 9:2 7:1 4:1 | 4:1 7:1 11:1 14:1 | 12:4! | r:4";

std::vector<SongDef> makeSongs() {
  std::vector<SongDef> s(MOOD_COUNT);
  s[MOOD_NONE] = {72, 0.5f, 62, 50, 69, 0.f, 2.2f, 6500.f, 0.9f, {kMenuA}, {}, {{0, -1, 0, 0.f}}, 0};
  // the menu: D major, slow and open - the theme on the flute, then the horn with the flute an octave over it
  s[MOOD_MENU] = {72, 0.5f, 62, 50, 69, 0.883f, 3.2f, 8000.f, 1.0f, {kMenuA, kMenuB}, {kThemeA, kThemeB},
    {{0, -1, F_PAD | F_BASS | F_HARPSLOW | F_SWELL, 0.45f},
     {0, 0, F_PAD | F_STRINGS | F_BASS | F_EPARP | F_FLUTE, 0.6f},
     {1, 1, F_PAD | F_STRINGS | F_BASS | F_HARP | F_FLUTE | F_SWELL, 0.7f},
     {0, 0, F_PAD | F_STRINGS | F_BRASS | F_PULSE | F_HARP | F_HORN | F_DOUBLE | F_TIMP, 0.9f},
     {1, -1, F_PAD | F_STARS | F_BASS | F_SHIMMER | F_SWELL, 0.45f}}, 1};
  // the hub: G major, swung - electric piano comping, a walking upright, brushes; the vibraphone has the tune
  s[MOOD_HUB] = {88, 0.62f, 55, 52, 70, 0.946f, 1.6f, 7000.f, 0.65f, {kHubA, kHubB}, {kHubTuneA, kHubTuneB},
    {{0, -1, F_COMP | F_WALK | F_BRUSHES, 0.55f},
     {0, 0, F_COMP | F_WALK | F_BRUSHES | F_VIBES, 0.65f},
     {1, 1, F_COMP | F_WALK | F_BRUSHES | F_GUITAR | F_VIBES, 0.75f},
     {0, -1, F_PAD | F_PADSOFT | F_GUITAR | F_WALK | F_BRUSHES | F_STARS, 0.6f},
     {1, 1, F_COMP | F_WALK | F_BRUSHES | F_FLUTE, 0.7f}}, 1};
  // the cruise: E major, a slow wash under the engines - two bars a chord, a few high bells, no drums
  s[MOOD_CRUISE] = {58, 0.5f, 52, 52, 71, 1.11f, 4.8f, 6500.f, 1.25f, {kCruise}, {},
    {{0, -1, F_PAD | F_DRONE | F_STARS, 0.4f},
     {0, -1, F_PAD | F_DRONE | F_STARS | F_EPWHOLE, 0.5f},
     {0, -1, F_PAD | F_DRONE | F_HARPSLOW | F_SHIMMER, 0.5f}}, 0};
  // the finale: D major, the theme's reprise - horns and flute, strings, brass, harp, timpani
  s[MOOD_FINALE] = {80, 0.5f, 62, 50, 69, 0.936f, 2.6f, 8500.f, 0.9f, {kMenuA, kFinaleB}, {kThemeA, kFinaleTuneB},
    {{0, -1, F_PAD | F_STRINGS | F_BRASS | F_PULSE | F_HARP | F_TIMP | F_SWELL, 0.75f},
     {0, 0, F_PAD | F_STRINGS | F_BRASS | F_PULSE | F_HARP | F_HORN | F_DOUBLE | F_ORCH | F_TIMP, 0.9f},
     {1, 1, F_PAD | F_STRINGS | F_BRASS | F_PULSE | F_EPARP | F_FLUTE | F_ORCH | F_SWELL, 0.85f},
     {0, 0, F_PAD | F_STRINGS | F_BRASS | F_PULSE | F_HARP | F_HORN | F_DOUBLE | F_ORCH | F_TIMP | F_STARS, 1.0f}}, 1};
  // each mood's balance, measured: every instrument's level under the mix, while it plays, set to the arrangement's
  // intent (the theme 6 dB under the mix, the pads 9, the bass 9, the brushes 16, the hi-hat 22, ...)
  { const float g[I_COUNT] = {2.61f, 2.78f, 1.88f, 2.9f, 1.38f, 0.439f, 1.f, 0.868f, 0.779f, 1.f, 0.609f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.434f, 3.61f}; std::copy(g, g + I_COUNT, s[MOOD_MENU].mix); }
  { const float g[I_COUNT] = {3.72f, 1.f, 1.f, 1.f, 0.93f, 0.371f, 1.47f, 1.f, 1.f, 0.793f, 1.f, 0.847f, 0.495f, 4.95f, 1.f, 4.15f, 3.28f, 1.f, 1.f, 1.f}; std::copy(g, g + I_COUNT, s[MOOD_HUB].mix); }
  { const float g[I_COUNT] = {2.37f, 3.87f, 1.f, 1.f, 1.f, 0.572f, 1.f, 1.11f, 0.826f, 1.f, 0.404f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f}; std::copy(g, g + I_COUNT, s[MOOD_CRUISE].mix); }
  { const float g[I_COUNT] = {1.95f, 2.74f, 1.74f, 2.23f, 1.32f, 0.377f, 1.f, 1.52f, 0.532f, 1.f, 0.524f, 1.f, 0.371f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.221f, 4.29f}; std::copy(g, g + I_COUNT, s[MOOD_FINALE].mix); }
  return s;
}

Prog parseProg(const char* text) {
  Prog p; std::vector<ChordSeg> bar;
  auto flush = [&] {
    if (bar.empty()) return;
    const float len = 4.f / bar.size();
    for (size_t i = 0; i < bar.size(); i++) { bar[i].beat = i * len; bar[i].len = len; }
    p.bars.push_back(bar); bar.clear();
  };
  for (const char* c = text; *c;) {
    while (*c == ' ') c++;
    if (!*c) break;
    if (*c == '|') { flush(); c++; continue; }
    ChordSeg seg; seg.root = (int)strtol(c, (char**)&c, 10);
    if (*c == ':') c++;
    std::string name; while (*c && *c != ' ' && *c != '|' && *c != '/') name += *c++;
    for (int k = 0; k < kNumChords; k++) if (name == kChords[k].name) seg.type = k;
    if (*c == '/') { c++; seg.bass = (int)strtol(c, (char**)&c, 10); }
    bar.push_back(seg);
  }
  flush();
  return p;
}
Phrase parsePhrase(const char* text) {
  Phrase ph; float beat = 0;
  for (const char* c = text; *c;) {
    while (*c == ' ' || *c == '|') c++;
    if (!*c) break;
    Note n{beat, 1.f, -99, 0.f};
    if (*c == 'r') c++; else n.pitch = (int)strtol(c, (char**)&c, 10);
    if (*c == ':') c++;
    n.len = strtof(c, (char**)&c);
    if (*c == '!') { n.accent = 0.15f; c++; }
    if (n.pitch != -99) ph.notes.push_back(n);
    beat += n.len;
  }
  return ph;
}

// ---------------------------------------------------------------- effects
// a hall: a pre-delay, four diffusing allpasses, then eight delay lines mixed through a Hadamard matrix, each damped
// a little more at the top, two of them gently modulated so the tail never rings metallic
struct Reverb {
  float sr = 48000;
  std::vector<float> pre; int preW = 0;
  std::vector<float> dif[4]; int difW[4] = {}; float difG[4] = {0.72f, 0.72f, 0.62f, 0.62f};
  std::vector<float> line[8]; int w[8] = {}; float len[8] = {}, g[8] = {}, lp[8] = {};
  float damp = 0.3f, rt = 2.f, dampHz = 6000.f, modPh = 0, hyL = 0, hyR = 0, hxL = 0, hxR = 0, hpA = 0.99f;
  void init(float sampleRate) {
    sr = sampleRate; const float k = sr / 48000.f;
    pre.assign((size_t)(0.022f * sr) + 1, 0.f);
    const int dl[4] = {151, 113, 397, 293};
    for (int i = 0; i < 4; i++) dif[i].assign((size_t)std::max(4.f, dl[i] * k), 0.f);
    const int L[8] = {1433, 1601, 1867, 2053, 2251, 2399, 2617, 2797};
    for (int i = 0; i < 8; i++) { len[i] = L[i] * k; line[i].assign((size_t)len[i] + 40, 0.f); }
    hpA = expf(-kTwoPi * 140.f / sr);
    setDecay(2.f, 6000.f);
  }
  void setDecay(float rt60, float hz) {
    rt = rt60; dampHz = hz;
    for (int i = 0; i < 8; i++) g[i] = powf(10.f, -3.f * len[i] / (rt60 * sr));
    damp = 1.f - expf(-kTwoPi * hz / sr);
  }
  inline float readLine(int i, float delay) const {
    const std::vector<float>& b = line[i]; const int n = (int)b.size();
    float pos = (float)w[i] - delay; while (pos < 0) pos += n;
    const int i0 = (int)pos; const float fr = pos - i0;
    return b[i0 % n] * (1.f - fr) + b[(i0 + 1) % n] * fr;
  }
  void process(float inL, float inR, float& outL, float& outR) {
    float x = (inL + inR) * 0.5f;
    const int pn = (int)pre.size();
    const float pd = pre[preW]; pre[preW] = x; preW = (preW + 1) % pn; x = pd;
    for (int k = 0; k < 4; k++) {
      std::vector<float>& b = dif[k]; int& ix = difW[k];
      const float bv = b[ix], y = bv - difG[k] * x;
      b[ix] = x + difG[k] * y; ix = (ix + 1) % (int)b.size(); x = y;
    }
    modPh += 0.23f / sr; if (modPh > 1.f) modPh -= 1.f;
    float o[8];
    for (int i = 0; i < 8; i++) {
      if (i == 1 || i == 6) o[i] = readLine(i, len[i] + 6.f * sine(modPh + (i == 6 ? 0.37f : 0.f)));
      else { const int n = (int)line[i].size(); o[i] = line[i][((int)(w[i] - len[i]) % n + n) % n]; }
    }
    float h[8];
    for (int i = 0; i < 8; i++) { lp[i] += (o[i] - lp[i]) * damp; h[i] = lp[i] * g[i]; }
    for (int s = 1; s < 8; s <<= 1)   // the Hadamard mix, as the fast Walsh-Hadamard transform
      for (int i = 0; i < 8; i += s << 1)
        for (int j = i; j < i + s; j++) { const float a = h[j], b = h[j + s]; h[j] = a + b; h[j + s] = a - b; }
    const float side = (inL - inR) * 0.25f;
    for (int i = 0; i < 8; i++) {
      std::vector<float>& b = line[i]; const int n = (int)b.size();
      b[w[i]] = h[i] * 0.35355339f + x * ((i & 1) ? -0.5f : 0.5f) + ((i & 2) ? side : -side);
      w[i] = (w[i] + 1) % n;
    }
    const float l = o[0] - o[2] + o[4] - o[6] + 0.5f * (o[1] + o[5]);
    const float r = o[1] - o[3] + o[5] - o[7] + 0.5f * (o[2] + o[6]);
    hyL = hpA * (hyL + l - hxL); hxL = l; hyR = hpA * (hyR + r - hxR); hxR = r;   // (a gentle cut under 140 Hz: no mud)
    outL = hyL * 0.5f; outR = hyR * 0.5f;
  }
};
// a ping-pong delay at the mood's dotted eighth, darkened and thinned each time round
struct PingPong {
  std::vector<float> l, r; int w = 0; float d = 0.5f, dTo = 0.5f, lpL = 0, lpR = 0, hpL = 0, hpR = 0, sr = 48000;
  void init(float sampleRate) { sr = sampleRate; l.assign((size_t)(2.2f * sr), 0.f); r.assign(l.size(), 0.f); }
  inline float read(const std::vector<float>& b, float delay) const {
    const int n = (int)b.size(); float pos = (float)w - delay; while (pos < 0) pos += n;
    const int i0 = (int)pos; const float fr = pos - i0;
    return b[i0 % n] * (1.f - fr) + b[(i0 + 1) % n] * fr;
  }
  void process(float inL, float inR, float& outL, float& outR) {
    d += (dTo - d) * 0.0002f;
    const float yl = read(l, d * sr), yr = read(r, d * sr);
    const float a = 1.f - expf(-kTwoPi * 3800.f / sr), b = 1.f - expf(-kTwoPi * 260.f / sr);
    lpL += (yl - lpL) * a; lpR += (yr - lpR) * a; hpL += (lpL - hpL) * b; hpR += (lpR - hpR) * b;
    const float fl = lpL - hpL, fr = lpR - hpR;
    l[w] = (inL + inR) * 0.5f + fr * 0.42f; r[w] = fl * 0.42f;
    w = (w + 1) % (int)l.size();
    outL = yl; outR = yr;
  }
};
// the master: a 22 Hz high-pass, an air shelf, a glue compressor (2.2:1 over -17 dBFS RMS), a 2 ms look-ahead limiter at -1.9 dBFS,
// then a soft clip that holds every peak under 0.85
struct Master {
  float sr = 48000, hpA = 0, hxL = 0, hxR = 0, hyL = 0, hyR = 0, shA = 0, shL = 0, shR = 0;
  float ms = 0, gc = 1, aRms = 0, aAtt = 0, aRel = 0;
  std::vector<float> la[2]; int laW = 0, hold = 0; float held = 1, gl = 1, aLimA = 0, aLimR = 0;
  void init(float sampleRate) {
    sr = sampleRate; hpA = expf(-kTwoPi * 22.f / sr); shA = 1.f - expf(-kTwoPi * 5000.f / sr);
    aRms = 1.f - expf(-1.f / (0.05f * sr)); aAtt = 1.f - expf(-1.f / (0.012f * sr)); aRel = 1.f - expf(-1.f / (0.25f * sr));
    const int n = std::max(8, (int)(0.002f * sr)); la[0].assign(n, 0.f); la[1].assign(n, 0.f);
    aLimA = 1.f - expf(-1.f / (n * 0.25f)); aLimR = 1.f - expf(-1.f / (0.08f * sr));
  }
  void process(float& L, float& R) {
    hyL = hpA * (hyL + L - hxL); hxL = L; hyR = hpA * (hyR + R - hxR); hxR = R; L = hyL; R = hyR;
    shL += (L - shL) * shA; shR += (R - shR) * shA; L += 0.6f * (L - shL); R += 0.6f * (R - shR);   // (air: +4 dB over 5 kHz)
    ms += ((L * L + R * R) * 0.5f - ms) * aRms;
    const float lvl = sqrtf(ms) + 1e-9f, thr = 0.14f;
    const float want = lvl > thr ? powf(lvl / thr, 1.f / 2.2f - 1.f) : 1.f;
    gc += (want - gc) * (want < gc ? aAtt : aRel);
    L *= gc; R *= gc;
    const float pk = std::max(fabsf(L), fabsf(R)), T = 0.80f, req = pk > T ? T / pk : 1.f;
    const int n = (int)la[0].size();
    if (req <= held) { held = req; hold = n; } else if (hold > 0) hold--; else held += (1.f - held) * aLimR;
    gl += (held - gl) * (held < gl ? aLimA : aLimR);
    const float dl = la[0][laW], dr = la[1][laW]; la[0][laW] = L; la[1][laW] = R; laW = (laW + 1) % n;
    L = dl * gl; R = dr * gl;
    auto clip = [](float x) { const float a = fabsf(x); return a < 0.8f ? x : copysignf(0.8f + 0.05f * tanhf((a - 0.8f) / 0.05f), x); };
    L = clip(L); R = clip(R);
  }
};
}   // namespace

// ---------------------------------------------------------------- the generator
struct MusicGen::Impl {
  float sr, inv;
  MRng rng;
  std::vector<SongDef> songs;
  std::vector<std::vector<Prog>> progs;
  std::vector<std::vector<Phrase>> mels;
  int mood = MOOD_NONE;
  double now = 0;
  // where the song is: its section, the bar in it, the times round; when the next bar starts (samples)
  int section = 0, barIn = 0, loops = 0; double barAt = 0;
  std::vector<int> padPrev, strPrev, compPrev, brassPrev, epPrev;
  int arpPat = 0, compPat = 0, lastWalk = -99, legatoFlip[3] = {};
  float cueGain = 1.f;
  float gain = 0, gainTo = 0, duck = 1;
  std::vector<Voice> voices; std::vector<Event> events;
  std::vector<float> strings; bool stringUsed[kStrings] = {};
  Reverb verb; PingPong delay; Master master;
  float wet = 0.9f, wetTo = 0.9f;
  float dryL[2][kBlock], dryR[2][kBlock], revL[2][kBlock], revR[2][kBlock], dlyL[2][kBlock], dlyR[2][kBlock];

  Impl(float sampleRate, uint32_t seed) : sr(sampleRate), inv(1.f / sampleRate) {
    rng.s = seed * 2654435761u + 0x9E3779B9u; if (!rng.s) rng.s = 1;
    songs = makeSongs();
    for (const SongDef& s : songs) {
      progs.emplace_back(); mels.emplace_back();
      for (const char* p : s.progs) progs.back().push_back(parseProg(p));
      for (const char* m : s.mels) mels.back().push_back(parsePhrase(m));
    }
    voices.reserve(kVoices); events.reserve(4096);
    strings.assign((size_t)kStrings * kStringLen, 0.f);
    verb.init(sr); delay.init(sr); master.init(sr);
    verb.setDecay(songs[0].rt60, songs[0].dampHz);
  }

  // ---- voicing: the chord's tones, the most telling first (thirds and sevenths, then the colours, the fifth, the
  // root), placed in a register as the inversion nearest the last chord's voicing
  static int priority(int iv) {
    switch (((iv % 12) + 12) % 12) { case 3: case 4: return 0; case 10: case 11: return 1; case 5: return 1; case 2: case 9: return 2; case 6: return 2; case 7: return 4; default: return 5; }
  }
  std::vector<int> voice(const ChordSeg& c, int key, int lo, int hi, int n, const std::vector<int>& prev) const {
    const ChordType& ct = kChords[c.type];
    std::vector<int> iv(ct.tones, ct.tones + ct.n);
    std::stable_sort(iv.begin(), iv.end(), [](int a, int b) { return priority(a) < priority(b); });
    std::vector<int> pcs;
    for (int x : iv) { const int pc = ((key + c.root + x) % 12 + 12) % 12; if (std::find(pcs.begin(), pcs.end(), pc) == pcs.end() && (int)pcs.size() < n) pcs.push_back(pc); }
    std::sort(pcs.begin(), pcs.end());
    std::vector<int> best; float bestCost = 1e9f;
    const float mid = (lo + hi) * 0.5f;
    for (size_t inv = 0; inv < pcs.size(); inv++)
      for (int drop = 0; drop < 2; drop++)
        for (int oct = -1; oct <= 1; oct++) {
          std::vector<int> v; int p = lo + ((pcs[inv] - lo) % 12 + 12) % 12 + oct * 12;
          for (size_t k = 0; k < pcs.size(); k++) {
            const int pc = pcs[(inv + k) % pcs.size()];
            if (k) { p++; while (((p % 12) + 12) % 12 != pc) p++; }
            v.push_back(p);
          }
          if (drop && v.size() >= 4) { v[v.size() - 2] -= 12; std::sort(v.begin(), v.end()); }
          if (v.front() < lo - 2 || v.back() > hi + 2) continue;
          float cost = 0, mean = 0;
          for (int x : v) mean += x;
          mean /= v.size();
          if (prev.size() == v.size()) for (size_t k = 0; k < v.size(); k++) cost += fabsf((float)(v[k] - prev[k]));
          else if (!prev.empty()) { float pm = 0; for (int x : prev) pm += x; cost += fabsf(mean - pm / prev.size()) * v.size(); }
          cost += 0.35f * fabsf(mean - mid) + (drop ? 0.8f : 0.f);
          if (cost < bestCost) { bestCost = cost; best = v; }
        }
    if (best.empty()) for (int pc : pcs) best.push_back(lo + ((pc - lo) % 12 + 12) % 12);
    return best;
  }
  int fit(int pitch, int lo) const { return lo + ((pitch - lo) % 12 + 12) % 12; }   // a pitch class's note at or over lo

  // ---- events
  void ev(double at, int inst, float midi, float vel, float durSec, float pan, int part = 0, float p1 = 0, float p2 = 0, bool sting = false) {
    if (events.size() >= 4000) return;
    const float gain = sting ? cueGain : songs[mood].mix[inst];   // (a cue's own level; a mood's balance of its instruments)
    events.push_back({std::max(at, now), inst, midi, std::clamp(vel, 0.f, 1.2f), durSec, std::clamp(pan, -1.f, 1.f), p1, p2, part, sting, gain});
  }
  float hum(float ms) { return rng.n() * ms * 0.001f * sr; }

  // a voice starts: its oscillators, filters, string and pan
  void start(const Event& e, double blockStart) {
    // a held part's note that the chord before already sounds is held on, not struck again
    if (e.part > 0 && !e.sting)
      for (Voice& v : voices)
        if (v.part == e.part && !v.sting && v.inst == e.inst && fabsf(v.midi - e.midi) < 0.01f && v.rel < 0 && v.t >= 0) {
          v.dur = std::max(v.dur, v.t + e.dur); v.vel = std::max(v.vel, e.vel * 0.9f); return;
        }
    // a theme's line slurred: the note before, just ending, glides on to this one (the flute and horn's legato)
    if (e.part >= kLegato && (e.inst == I_FLUTE || e.inst == I_HORN))
      for (Voice& v : voices)
        if (v.part == e.part && v.inst == e.inst && v.sting == e.sting && v.t >= v.dur - 0.09f && (v.rel < 0 || v.t - v.rel < 0.03f)) {
          v.fTo = midiHz(e.midi); v.midi = e.midi; v.rel = -1; v.dur = v.t + e.dur; v.vel = e.vel; return;
        }
    if ((int)voices.size() >= kVoices) {   // the pool full: the quietest gives way
      auto it = std::min_element(voices.begin(), voices.end(), [](const Voice& a, const Voice& b) { return a.lastEnv < b.lastEnv; });
      freeVoice(*it); voices.erase(it);
    }
    Voice v; v.inst = e.inst; v.part = e.part; v.sting = e.sting; v.midi = e.midi; v.f = v.fTo = midiHz(e.midi);
    v.vel = e.vel; v.dur = std::max(e.dur, 0.01f); v.p1 = e.p1; v.p2 = e.p2; v.gain = e.gain;
    v.t = (float)((blockStart - e.at) * inv);
    const float a = (e.pan + 1.f) * kPi * 0.25f; v.gl = cosf(a) * 1.41421356f; v.gr = sinf(a) * 1.41421356f;
    v.rng.s = (uint32_t)(rng.u() * 4294967000.f) | 1u;
    for (int k = 0; k < 4; k++) v.ph[k] = rng.u();
    v.lfo = rng.u();
    switch (v.inst) {
      case I_PAD: v.det[0] = 0.9938f; v.det[1] = 1.0007f; v.det[2] = 1.0061f; v.lfoRate = 0.11f + 0.08f * rng.u(); break;
      case I_STRINGS: v.det[0] = 0.9962f; v.det[1] = 1.0f; v.det[2] = 1.0041f; v.lfoRate = 5.0f + 0.5f * rng.u(); break;
      case I_BRASS: case I_HORN: v.det[0] = 0.9985f; v.det[1] = 1.0015f; v.lfoRate = 4.8f + 0.6f * rng.u(); break;
      case I_FLUTE: v.lfoRate = 4.9f + 0.4f * rng.u(); v.fa.set(v.f * 2.f, 1.2f, sr); break;
      case I_EP: v.lfoRate = 4.2f; v.p2 = 1.9f * powf(261.6f / v.f, 0.35f); break;
      case I_VIBES: v.lfoRate = 5.4f; v.p2 = 2.6f * powf(261.6f / v.f, 0.3f); break;
      case I_CELESTA: v.p2 = 1.5f * powf(523.3f / v.f, 0.3f); break;
      case I_BASS: break;
      case I_KICK: if (v.p1 > 0.5f) v.fa.set(110.f, 0.7f, sr); v.z[0] = 1.f; break;
      case I_BRUSH: v.fa.set(3400.f, 0.55f, sr); break;
      case I_RIM: v.fa.set(2800.f, 1.2f, sr); break;
      case I_HAT: v.fa.set(7600.f, 0.7f, sr); v.fb.set(11000.f, 1.4f, sr); break;
      case I_SHAKER: v.fa.set(6500.f, 1.4f, sr); break;
      case I_CLAP: v.fa.set(v.p1 > 0 ? v.p1 : 1300.f, v.p2 > 0 ? v.p2 : 1.2f, sr); break;
      case I_TIMPANI: v.fa.set(480.f, 0.7f, sr); break;
      case I_CYMBAL: v.fa.set(4200.f, 0.7f, sr); v.fb.set(4200.f, 0.7f, sr); break;
      default: break;
    }
    if (isString(v.inst)) startString(v);
    voices.push_back(v);
  }
  // a plucked string (Karplus-Strong): a delay line a period long, round which the pluck's noise burst runs, losing
  // its top each time through a two-point average and tuned by an allpass for the fraction of a sample
  void startString(Voice& v) {
    int slot = -1;
    for (int i = 0; i < kStrings; i++) if (!stringUsed[i]) { slot = i; break; }
    if (slot < 0) {   // all ringing: the oldest string gives way
      Voice* oldest = nullptr;
      for (Voice& o : voices) if (o.ks >= 0 && (!oldest || o.t > oldest->t)) oldest = &o;
      if (!oldest) { v.inst = -1; return; }
      slot = oldest->ks; oldest->ks = -1; oldest->rel = oldest->t; oldest->inst = -2;
    }
    stringUsed[slot] = true; v.ks = slot;
    const float D = sr / v.f;
    float S, T60, hz, noise, pos;   // (hz: how hard the pluck is, the excitation's roll-off; noise: the finger's)
    if (v.inst == I_HARP) { S = 0.2f; T60 = std::clamp(3.4f * sqrtf(196.f / v.f), 0.9f, 6.f); hz = 1800.f + 2600.f * v.vel; noise = 0.12f; pos = 0.13f; }
    else if (v.inst == I_GUITAR) { S = 0.3f; T60 = std::clamp(2.4f * sqrtf(147.f / v.f), 0.7f, 4.f); hz = 2200.f + 3000.f * v.vel; noise = 0.2f; pos = 0.17f; }
    else { S = 0.48f; T60 = std::clamp(1.5f * sqrtf(55.f / v.f), 0.5f, 2.2f); hz = 450.f + 600.f * v.vel; noise = 0.15f; pos = 0.12f; }
    hz = std::max(hz, 2.5f * v.f);   // (never under the note's own first harmonics)
    int N = (int)floorf(D - S - 0.15f); N = std::clamp(N, 4, kStringLen - 1);
    const float frac = D - S - N;
    v.ksN = N; v.ksS = S; v.ksA = (1.f - frac) / (1.f + frac);
    v.ksRho = powf(0.001f, 1.f / (v.f * T60));
    v.ksW = 0; v.ksX1 = v.ksApX = v.ksApY = v.ksHp = v.ksHpX = 0;
    // the string pulled aside at one point and let go: a triangle peaking there (its harmonics falling as 1/n^2, those
    // with a node at that point missing), with a little of the finger's noise in it; the fingertip rounds it (two
    // one-pole lowpasses run round the period, so the shape stays periodic and the first cycles never click)
    float* b = &strings[(size_t)slot * kStringLen];
    const int P = std::clamp((int)(pos * N), 1, N - 1);
    for (int i = 0; i < N; i++) b[i] = (i < P ? (float)i / P : (float)(N - i) / (N - P)) + noise * v.rng.n();
    const float a = 1.f - expf(-kTwoPi * std::min(hz, 0.4f * sr) / sr);
    for (int pass = 0; pass < 2; pass++) {
      float lp = b[N - 1];
      for (int k = 0; k < 2 * N; k++) { lp += (b[k % N] - lp) * a; if (k >= N) b[k - N] = lp; }
    }
    float mean = 0;
    for (int i = 0; i < N; i++) mean += b[i];
    mean /= N; float pk = 1e-6f;
    for (int i = 0; i < N; i++) { b[i] -= mean; pk = std::max(pk, fabsf(b[i])); }
    for (int i = 0; i < N; i++) b[i] *= 0.9f / pk;
  }
  void freeVoice(Voice& v) { if (v.ks >= 0) { stringUsed[v.ks] = false; v.ks = -1; } }

  // the envelope at a time (velocity and level aside): its attack and decay, and the release once it has begun
  float env(const Voice& v, float t) const {
    if (t <= 0) return 0.f;
    float e = 1.f;
    switch (v.inst) {
      case I_PAD: e = smooth01(t / (v.p2 > 0 ? v.p2 : 1.5f)); break;
      case I_STRINGS: e = smooth01(t / (v.p2 > 0 ? v.p2 : 0.42f)); break;
      case I_BRASS: e = std::min(t / 0.045f, 1.f) * (0.78f + 0.22f * expf(-t / 0.3f)); break;
      case I_HORN: e = smooth01(t / 0.09f) * (0.9f + 0.1f * expf(-t / 0.5f)); break;
      case I_FLUTE: e = smooth01(t / 0.075f) * (0.94f + 0.06f * expf(-t / 0.4f)); break;
      case I_EP: e = std::min(t / 0.002f, 1.f) * expf(-t / v.p2); break;
      case I_VIBES: e = std::min(t / 0.0015f, 1.f) * expf(-t / v.p2); break;
      case I_CELESTA: e = std::min(t / 0.001f, 1.f) * expf(-t / v.p2); break;
      case I_BASS: e = std::min(t / 0.008f, 1.f) * (0.72f + 0.28f * expf(-t / 0.45f)); break;
      case I_HARP: case I_GUITAR: case I_UPRIGHT: e = std::min(t / 0.0015f, 1.f); break;
      case I_KICK: e = std::min(t / 0.001f, 1.f) * expf(-t / (v.p1 > 0.5f ? 0.9f : 0.3f)); break;
      case I_BRUSH: e = std::min(t / (v.p1 > 0.5f ? 0.025f : 0.002f), 1.f) * expf(-t / (v.p1 > 0.5f ? 0.2f : 0.085f)); break;
      case I_RIM: e = std::min(t / 0.0007f, 1.f) * expf(-t / 0.016f); break;
      case I_HAT: e = std::min(t / 0.0007f, 1.f) * expf(-t / (v.p1 > 0.5f ? 0.32f : 0.038f)); break;
      case I_SHAKER: e = std::min(t / 0.006f, 1.f) * expf(-t / 0.048f); break;
      case I_CLAP: {   // three quick slaps of the hands, a tail
        float s = 0; const float tk[3] = {0.f, 0.009f, 0.019f};
        for (float k : tk) if (t >= k) s += expf(-(t - k) / 0.0045f);
        e = std::min(t / 0.0005f, 1.f) * (0.55f * s + 0.45f * expf(-t / 0.065f));
        break;
      }
      case I_TIMPANI: e = std::min(t / 0.002f, 1.f); break;
      case I_CYMBAL:
        if (v.p1 > 0.5f) e = t < v.dur ? powf(t / v.dur, 2.6f) : expf(-(t - v.dur) / 0.07f);
        else e = std::min(t / 0.0015f, 1.f) * expf(-t / 1.5f);
        break;
      default: break;
    }
    if (v.rel >= 0 && t > v.rel) e *= expf(-(t - v.rel) / kInst[v.inst].rel);
    return e;
  }

  // ---- one voice's block, mixed into its bus with its sends
  void renderVoice(Voice& v, int n) {
    const float t0 = v.t, t1 = v.t + n * inv;
    if (t1 <= 0.f) { v.t = t1; return; }
    const InstDef& D = kInst[v.inst];
    // a held note at its length: released (a held string damped)
    if (v.rel < 0 && t0 >= v.dur && (D.held || v.inst == I_CYMBAL)) {
      if (v.inst != I_CYMBAL) v.rel = std::max(t0, 0.f);
      if (isString(v.inst)) v.ksRho = std::min(v.ksRho, powf(0.001f, 1.f / (v.f * 0.18f)));
    }
    const int i0 = t0 < 0 ? std::min(n, (int)ceilf(-t0 * sr)) : 0;
    if (v.f != v.fTo) { v.f += (v.fTo - v.f) * (1.f - expf(-n * inv / 0.035f)); if (fabsf(v.f - v.fTo) < 0.01f) v.f = v.fTo; }
    const float amp = v.vel * D.level * v.gain * g_instGain[v.inst];
    const float e0 = env(v, std::max(t0, i0 * inv + t0)) * amp, e1 = env(v, t1) * amp;
    const float de = (e1 - e0) / std::max(1, n - i0);
    float L[kBlock], R[kBlock];
    const float tm = std::max(t0, 0.f);   // (the block's controls at its start)
    bool stereo = false;
    switch (v.inst) {
      case I_PAD: case I_STRINGS: {
        const bool pad = v.inst == I_PAD;
        float vib = 1.f;
        if (pad) { v.lfo += v.lfoRate * n * inv; }
        else { v.lfo += v.lfoRate * n * inv; vib = 1.f + 0.0028f * smooth01((tm - 0.25f) / 0.5f) * sine(v.lfo); }
        const float bright = std::clamp(v.p1, 0.f, 1.f);
        const float open = smooth01(tm / (pad ? 2.2f : 0.5f)) * (pad ? (1.f + 0.12f * sine(v.lfo)) : 1.f);
        const float fc = pad ? v.f * (1.8f + (3.f + 8.f * bright) * open) + 1000.f * bright : v.f * (3.f + 8.f * bright) + 2500.f * bright * open;
        v.fa.set(fc, pad ? 0.8f : 0.62f, sr); v.fb.set(fc * 1.03f, pad ? 0.8f : 0.62f, sr);
        const float dt0 = v.f * v.det[0] * vib * inv, dt1 = v.f * v.det[1] * vib * inv, dt2 = v.f * v.det[2] * vib * inv;
        float p0 = v.ph[0], p1 = v.ph[1], p2 = v.ph[2];
        for (int i = i0; i < n; i++) {
          p0 += dt0; if (p0 >= 1.f) p0 -= 1.f;
          p1 += dt1; if (p1 >= 1.f) p1 -= 1.f;
          p2 += dt2; if (p2 >= 1.f) p2 -= 1.f;
          const float s0 = saw(p0, dt0), s1 = saw(p1, dt1), s2 = saw(p2, dt2);
          L[i] = v.fa.lp(s0 + 0.6f * s1) * 0.32f; R[i] = v.fb.lp(s2 + 0.6f * s1) * 0.32f;
        }
        v.ph[0] = p0; v.ph[1] = p1; v.ph[2] = p2; stereo = true;
        break;
      }
      case I_BRASS: case I_HORN: {
        const bool horn = v.inst == I_HORN;
        v.lfo += v.lfoRate * n * inv;
        const float vib = 1.f + 0.0034f * smooth01((tm - 0.3f) / 0.4f) * sine(v.lfo);
        const float bite = std::min(tm / (horn ? 0.11f : 0.065f), 1.f) * (0.62f + 0.38f * expf(-tm / 0.35f));
        const float bright = std::clamp(v.vel, 0.f, 1.f);
        const float fc = v.f * (1.05f + (horn ? 2.2f : 5.0f) * bright * bite) + (horn ? 150.f : 250.f);
        v.fa.set(fc, horn ? 0.7f : 1.05f, sr);
        const float dt0 = v.f * v.det[0] * vib * inv, dt1 = v.f * v.det[1] * vib * inv, dt2 = v.f * vib * inv;
        float p0 = v.ph[0], p1 = v.ph[1], p2 = v.ph[2];
        for (int i = i0; i < n; i++) {
          p0 += dt0; if (p0 >= 1.f) p0 -= 1.f;
          p1 += dt1; if (p1 >= 1.f) p1 -= 1.f;
          p2 += dt2; if (p2 >= 1.f) p2 -= 1.f;
          L[i] = v.fa.lp(saw(p0, dt0) + saw(p1, dt1)) * 0.42f + (horn ? 0.35f * sine(p2) : 0.f);
        }
        v.ph[0] = p0; v.ph[1] = p1; v.ph[2] = p2;
        break;
      }
      case I_FLUTE: {
        v.lfo += v.lfoRate * n * inv;
        const float depth = 0.0055f * smooth01((tm - 0.22f) / 0.5f), lfo = sine(v.lfo);
        const float dt = v.f * (1.f + depth * lfo) * inv, trem = 1.f + 0.05f * depth / 0.0055f * lfo;
        const float breath = 0.045f + 0.3f * expf(-tm / 0.035f);
        float p = v.ph[0];
        for (int i = i0; i < n; i++) {
          p += dt; if (p >= 1.f) p -= 1.f;
          const float tone = sine(p) + 0.13f * sine(2.f * p) + 0.035f * sine(3.f * p);
          L[i] = (tone + v.fa.bp(v.rng.n()) * breath * 3.f) * trem;
        }
        v.ph[0] = p;
        break;
      }
      case I_EP: {
        v.lfo += v.lfoRate * n * inv;
        const float dc = v.f * inv, dm = v.f * 1.0007f * inv, dtn = v.f * 14.f * inv;
        const float idx = ((0.45f + 1.35f * v.vel) * expf(-tm * 2.8f) + 0.12f) * 0.159f, tine = dtn < 0.45f ? 0.28f * v.vel * expf(-tm * 26.f) : 0.f;
        const float tr = 0.28f * sine(v.lfo);
        float pc = v.ph[0], pm = v.ph[1], pt = v.ph[2];
        for (int i = i0; i < n; i++) {
          pc += dc; if (pc >= 1.f) pc -= 1.f;
          pm += dm; if (pm >= 1.f) pm -= 1.f;
          pt += dtn; pt -= floorf(pt);
          const float s = sine(pc + idx * sine(pm)) + tine * sine(pt);
          L[i] = s * (1.f + tr); R[i] = s * (1.f - tr);
        }
        v.ph[0] = pc; v.ph[1] = pm; v.ph[2] = pt; stereo = true;
        break;
      }
      case I_VIBES: {
        v.lfo += v.lfoRate * n * inv;
        const float a1 = 0.26f * expf(-tm / 0.5f) * (0.6f + 0.6f * v.vel), a2 = 0.06f * expf(-tm / 0.08f) * v.vel;
        const float trL = 1.f + 0.24f * sine(v.lfo), trR = 1.f + 0.24f * sine(v.lfo + 0.18f);
        const float d0 = v.f * inv, d1 = 4.f * d0, d2 = d0 * 10.f > 0.45f ? 0.f : d0 * 10.f;
        float p0 = v.ph[0], p1 = v.ph[1], p2 = v.ph[2];
        for (int i = i0; i < n; i++) {
          p0 += d0; if (p0 >= 1.f) p0 -= 1.f; p1 += d1; p1 -= floorf(p1); p2 += d2; p2 -= floorf(p2);
          const float s = sine(p0) + a1 * sine(p1) + a2 * sine(p2);
          L[i] = s * trL; R[i] = s * trR;
        }
        v.ph[0] = p0; v.ph[1] = p1; v.ph[2] = p2; stereo = true;
        break;
      }
      case I_CELESTA: {   // a struck bar: its partials at 1, 2.76, 5.40 and 8.93 times the note, the higher ones short
        const float a1 = 0.34f * expf(-tm / 0.42f), a2 = 0.15f * expf(-tm / 0.15f), a3 = 0.07f * expf(-tm / 0.05f);
        const float d0 = v.f * inv, d1 = d0 * 2.76f, d2 = d0 * 5.4f > 0.45f ? 0.f : d0 * 5.4f, d3 = d0 * 8.93f > 0.45f ? 0.f : d0 * 8.93f;
        float p0 = v.ph[0], p1 = v.ph[1], p2 = v.ph[2], p3 = v.ph[3];
        for (int i = i0; i < n; i++) {
          p0 += d0; p0 -= floorf(p0); p1 += d1; p1 -= floorf(p1); p2 += d2; p2 -= floorf(p2); p3 += d3; p3 -= floorf(p3);
          L[i] = sine(p0) + a1 * sine(p1) + a2 * sine(p2) + a3 * sine(p3);
        }
        v.ph[0] = p0; v.ph[1] = p1; v.ph[2] = p2; v.ph[3] = p3;
        break;
      }
      case I_HARP: case I_GUITAR: case I_UPRIGHT: {
        if (v.ks < 0) { for (int i = i0; i < n; i++) L[i] = 0.f; break; }
        float* b = &strings[(size_t)v.ks * kStringLen];
        const float rho = v.ksRho, S = v.ksS, A = v.ksA, hpk = 1.f - 30.f * kTwoPi * inv;
        const float thump = v.inst == I_UPRIGHT ? 0.8f * expf(-tm / 0.035f) : 0.f, dth = v.f * inv;
        int w = v.ksW; float x1 = v.ksX1, apx = v.ksApX, apy = v.ksApY, hp = v.ksHp, hpx = v.ksHpX, pth = v.ph[0];
        const int N = v.ksN;
        for (int i = i0; i < n; i++) {
          const float x = b[w];
          const float lpv = rho * ((1.f - S) * x + S * x1); x1 = x;
          const float ap = A * lpv + apx - A * apy; apx = lpv; apy = ap;
          b[w] = ap; if (++w >= N) w = 0;
          hp = hpk * (hp + x - hpx); hpx = x;   // (no DC)
          float s = hp;
          if (thump > 1e-4f) { pth += dth; if (pth >= 1.f) pth -= 1.f; s += thump * sine(pth); }
          L[i] = s;
        }
        v.ksW = w; v.ksX1 = x1; v.ksApX = apx; v.ksApY = apy; v.ksHp = hp; v.ksHpX = hpx; v.ph[0] = pth;
        break;
      }
      case I_BASS: {
        const float fc = v.f * (2.4f + 3.f * expf(-tm / 0.12f)) + 60.f;
        v.fa.set(fc, 0.75f, sr);
        const float d0 = v.f * inv, d1 = v.f * 1.0025f * inv;
        float p0 = v.ph[0], p1 = v.ph[1];
        for (int i = i0; i < n; i++) {
          p0 += d0; if (p0 >= 1.f) p0 -= 1.f; p1 += d1; if (p1 >= 1.f) p1 -= 1.f;
          L[i] = sine(p0) + 0.22f * sine(2.f * p0) + 0.3f * v.fa.lp(saw(p1, d1));
        }
        v.ph[0] = p0; v.ph[1] = p1;
        break;
      }
      case I_KICK: {
        const bool orch = v.p1 > 0.5f;
        const float fa0 = orch ? 42.f + 50.f * expf(-tm * 16.f) : 47.f + 95.f * expf(-tm * 34.f);
        const float fa1 = orch ? 42.f + 50.f * expf(-(tm + n * inv) * 16.f) : 47.f + 95.f * expf(-(tm + n * inv) * 34.f);
        const float clickK = expf(-inv / 0.0016f), rumble = orch ? 0.7f * expf(-tm / 0.35f) : 0.f;
        float p = v.ph[0], click = v.z[0];
        for (int i = i0; i < n; i++) {
          const float fr = fa0 + (fa1 - fa0) * (i - i0) / std::max(1, n - i0);
          p += fr * inv; if (p >= 1.f) p -= 1.f;
          float s = sine(p) + v.rng.n() * click * 0.12f; click *= clickK;
          if (orch) s += v.fa.lp(v.rng.n()) * rumble;
          L[i] = s;
        }
        v.ph[0] = p; v.z[0] = click;
        break;
      }
      case I_BRUSH: case I_RIM: case I_SHAKER: case I_CLAP: {
        const float body = v.inst == I_BRUSH ? 0.25f * expf(-tm / 0.03f) : 0.f, d0 = (v.inst == I_RIM ? 1750.f : 190.f) * inv, d1 = 820.f * inv;
        const float wood = v.inst == I_RIM ? 1.f : 0.f;
        float p0 = v.ph[0], p1 = v.ph[1];
        for (int i = i0; i < n; i++) {
          float s = v.fa.bp(v.rng.n());
          if (v.inst == I_BRUSH) { p0 += d0; p0 -= floorf(p0); s = s * 1.3f + body * sine(p0); }
          else if (wood > 0) { p0 += d0; p0 -= floorf(p0); p1 += d1; p1 -= floorf(p1); s = s * 0.5f + 0.45f * sine(p0) + 0.6f * sine(p1); }
          else s *= 1.6f;
          L[i] = s;
        }
        v.ph[0] = p0; v.ph[1] = p1;
        break;
      }
      case I_HAT: {
        for (int i = i0; i < n; i++) { const float nz = v.rng.n(); L[i] = v.fa.hp(nz) + 0.5f * v.fb.bp(nz); }
        break;
      }
      case I_TIMPANI: {   // the drum's head: its modes at 1, 1.50, 1.98 and 2.44 times the note, a soft mallet
        const float glide = 1.f + 0.014f * expf(-tm / 0.06f);
        const float a0 = expf(-tm / 1.7f), a1 = 0.5f * expf(-tm / 1.0f), a2 = 0.28f * expf(-tm / 0.65f), a3 = 0.14f * expf(-tm / 0.4f);
        const float mallet = 0.55f * expf(-tm / 0.025f);
        const float d0 = v.f * glide * inv;
        float p0 = v.ph[0], p1 = v.ph[1], p2 = v.ph[2], p3 = v.ph[3];
        for (int i = i0; i < n; i++) {
          p0 += d0; p0 -= floorf(p0); p1 += d0 * 1.5f; p1 -= floorf(p1); p2 += d0 * 1.98f; p2 -= floorf(p2); p3 += d0 * 2.44f; p3 -= floorf(p3);
          L[i] = a0 * sine(p0) + a1 * sine(p1) + a2 * sine(p2) + a3 * sine(p3) + mallet * v.fa.lp(v.rng.n());
        }
        v.ph[0] = p0; v.ph[1] = p1; v.ph[2] = p2; v.ph[3] = p3;
        break;
      }
      case I_CYMBAL: {
        for (int i = i0; i < n; i++) { L[i] = v.fa.hp(v.rng.n()); R[i] = v.fb.hp(v.rng.n()); }
        stereo = true;
        break;
      }
      default: for (int i = i0; i < n; i++) L[i] = 0.f; break;
    }
    // into the bus: level, pan, sends
    const int bus = v.sting ? 1 : 0;
    float e = e0, pk = 0;
    if (stereo)
      for (int i = i0; i < n; i++) {
        const float l = L[i] * e * v.gl, r = R[i] * e * v.gr; e += de;
        dryL[bus][i] += l; dryR[bus][i] += r; revL[bus][i] += l * D.rev; revR[bus][i] += r * D.rev; dlyL[bus][i] += l * D.dly; dlyR[bus][i] += r * D.dly;
        pk = std::max(pk, fabsf(l) + fabsf(r));
      }
    else
      for (int i = i0; i < n; i++) {
        const float s = L[i] * e, l = s * v.gl, r = s * v.gr; e += de;
        dryL[bus][i] += l; dryR[bus][i] += r; revL[bus][i] += l * D.rev; revR[bus][i] += r * D.rev; dlyL[bus][i] += l * D.dly; dlyR[bus][i] += r * D.dly;
        pk = std::max(pk, fabsf(s));
      }
    v.t = t1; v.lastEnv = e1; v.peak = v.peak * 0.9f + pk * 0.1f;
    // spent: released or ringing out, and silent
    const bool decays = v.rel >= 0 || !D.held || isString(v.inst);
    const bool swelling = v.inst == I_CYMBAL && v.p1 > 0.5f && t1 < v.dur;   // (a swell starts from silence)
    if (decays && !swelling && t1 > 0.06f && ((isString(v.inst) ? v.peak < 2e-5f && t1 > 0.15f : e1 < 2e-5f) || t1 > v.dur + 12.f)) v.inst = -1 - v.inst;
  }

  // ---- the score: one bar laid out, a quarter of a second before it starts
  float swingBeat(float beat, float swing) const {
    const float b = floorf(beat), fr = beat - b;
    return fabsf(fr - 0.5f) < 1e-3f ? b + swing : b + fr;
  }
  const ChordSeg& chordAt(int sec, int bar) const {
    const SongDef& S = songs[mood];
    const Prog& P = progs[mood][S.sections[sec].prog];
    return P.bars[bar % P.bars.size()][0];
  }
  void scheduleBar() {
    const SongDef& S = songs[mood];
    const SectionDef& sec = S.sections[section];
    const Prog& P = progs[mood][sec.prog];
    const int nBars = (int)P.bars.size();
    const double bl = 60.0 / S.bpm * sr, t0 = barAt;
    const float I = sec.intensity, sw = S.swing;
    const uint32_t F = sec.flags;
    const bool first = barIn == 0, last = barIn == nBars - 1;
    auto at = [&](float beat) { return t0 + swingBeat(beat, sw) * bl; };
    auto secs = [&](float beats) { return (float)(beats * bl / sr); };
    // the next bar's root (a walking bass leads into it)
    int nextSec = section, nextBar = barIn + 1;
    if (nextBar >= nBars) { nextBar = 0; nextSec = section + 1 < (int)S.sections.size() ? section + 1 : S.loopTo; }
    const ChordSeg& nextChord = chordAt(nextSec, nextBar);
    if (barIn % 2 == 0) { arpPat = (int)(rng.u() * 6) % 6; compPat = (int)(rng.u() * 5) % 5; }
    for (const ChordSeg& c : P.bars[barIn]) {
      const double cs = at(c.beat); const float clen = secs(c.len);
      const int bassPc = S.key + (c.bass != -99 ? c.bass : c.root);
      const std::vector<int> pv = voice(c, S.key, S.padLo, S.padHi, 4, padPrev); padPrev = pv;
      if (F & F_PAD) {
        const float vel = (F & F_PADSOFT ? 0.32f : 0.5f + 0.25f * I);
        for (size_t k = 0; k < pv.size(); k++) ev(cs, I_PAD, (float)pv[k], vel, clen + 0.1f, -0.6f + 1.2f * k / std::max<size_t>(1, pv.size() - 1), 1, 0.3f + 0.55f * I);
      }
      if (F & F_STRINGS) {
        const std::vector<int> v = voice(c, S.key, S.padLo + 7, S.padHi + 7, 4, strPrev); strPrev = v;
        for (size_t k = 0; k < v.size(); k++) ev(cs, I_STRINGS, (float)v[k], 0.42f + 0.38f * I, clen + 0.05f, 0.5f - 1.f * k / std::max<size_t>(1, v.size() - 1), 2, 0.35f + 0.5f * I);
      }
      if (F & F_SHIMMER)
        for (size_t k = pv.size() >= 2 ? pv.size() - 2 : 0; k < pv.size(); k++) ev(cs, I_STRINGS, (float)pv[k] + 12.f, 0.3f, clen + 0.05f, k % 2 ? 0.7f : -0.7f, 3, 0.25f);
      if (F & F_BRASS) {
        const std::vector<int> v = voice(c, S.key, 53, 72, 3, brassPrev); brassPrev = v;
        for (size_t k = 0; k < v.size(); k++) ev(cs + hum(4), I_BRASS, (float)v[k], 0.38f + 0.42f * I, clen * 0.94f, -0.35f + 0.35f * k, 4);
      }
      if (F & F_DRONE) ev(cs, I_BASS, (float)fit(bassPc, 28), 0.55f, clen + 0.2f, 0.f, 5);
      if (F & F_BASS) ev(cs, I_BASS, (float)fit(bassPc, 33), 0.62f + 0.2f * I, clen * 0.97f, 0.f, 6);
      if (F & F_PULSE)
        for (float b = 0; b < c.len - 0.01f; b += 2.f) {
          const bool fifth = b > 0 && rng.u() < 0.35f;
          ev(at(c.beat + b) + hum(3), I_BASS, (float)fit(bassPc + (fifth ? 7 : 0), 33), (b == 0 ? 0.78f : 0.6f) + 0.15f * I, secs(std::min(1.8f, c.len - b)), 0.f);
        }
      if (F & F_WALK) {   // a walking bass: the root, chord tones and steps, then a half step into the next root
        const ChordType& ct = kChords[c.type];
        const int root = fit(bassPc, 40);
        int prevNote = lastWalk;
        for (float b = 0; b < c.len - 0.01f; b += 1.f) {
          int note;
          const bool lastBeat = c.beat + b >= 3.f - 0.01f;
          if (b == 0) note = root;
          else if (lastBeat) {
            const int target = fit(S.key + (nextChord.bass != -99 ? nextChord.bass : nextChord.root), 40);
            const float u = rng.u();
            note = u < 0.45f ? target - 1 : u < 0.8f ? target + 1 : target - 5;
          } else {
            const int tone = ct.tones[1 + (int)(rng.u() * std::min(3, ct.n - 1))] % 12;
            note = root + tone; if (rng.u() < 0.3f) note -= 12;
            if (prevNote > 0 && abs(note - prevNote) > 7) note += note > prevNote ? -12 : 12;
          }
          note = std::clamp(note, 36, 55);
          ev(at(c.beat + b) + hum(5), I_UPRIGHT, (float)note, (b == 0 ? 0.85f : 0.7f) + 0.1f * rng.u(), secs(0.95f), 0.05f);
          prevNote = note;
        }
        lastWalk = prevNote;
      }
      if (F & F_EPWHOLE) {
        const std::vector<int> v = voice(c, S.key, 55, 72, 4, epPrev); epPrev = v;
        for (size_t k = 0; k < v.size(); k++) ev(cs + hum(12) + k * 0.012 * sr, I_EP, (float)v[k], 0.32f, clen, -0.3f + 0.2f * k);
      }
      if (F & F_COMP) {   // electric-piano comping on the off-beats, the rhythm changing every two bars
        static const float pats[5][4] = {{0.f, 1.5f, 2.5f, -1}, {0.5f, 2.f, 3.5f, -1}, {0.f, 2.5f, -1, -1}, {1.f, 2.5f, 3.5f, -1}, {0.f, 1.5f, 3.f, -1}};
        const std::vector<int> v = voice(c, S.key, 52, 70, 4, compPrev); compPrev = v;
        for (int h = 0; h < 4 && pats[compPat][h] >= 0; h++) {
          const float b = pats[compPat][h];
          if (b < c.beat - 0.01f || b >= c.beat + c.len - 0.01f) continue;
          const float len = (h + 1 < 4 && pats[compPat][h + 1] >= 0) ? std::min(0.9f, pats[compPat][h + 1] - b - 0.1f) : std::min(1.4f, 4.f - b);
          const float vel = 0.42f + 0.18f * rng.u() + 0.12f * I;
          for (size_t k = 0; k < v.size(); k++) ev(at(b) + hum(6) + k * 0.004 * sr, I_EP, (float)v[k], vel * (k + 1 == v.size() ? 1.08f : 0.95f), secs(len), -0.1f + 0.2f * k);   // (the piano right of the vibes)
        }
      }
      // the arpeggios: the chord's tones over two octaves, in a pattern that changes every two bars
      const bool harp = (F & (F_HARP | F_HARPSLOW)) != 0, ep = (F & F_EPARP) != 0, gtr = (F & F_GUITAR) != 0;
      if (harp || ep || gtr) {
        static const int pats[6][8] = {{0, 1, 2, 3, 4, 3, 2, 1}, {0, 2, 1, 3, 2, 4, 3, 5}, {0, 1, 2, 4, 3, 2, 1, 2},
                                       {4, 3, 2, 1, 2, 3, 4, 5}, {0, 2, 4, 5, 4, 2, 3, 1}, {0, 1, 3, 2, 4, 3, 5, 4}};
        const int inst = harp ? I_HARP : ep ? I_EP : I_GUITAR;
        std::vector<int> notes;   // (the harp an octave over the pads, the piano from the D over middle C, the guitar with them)
        for (int x : pv) notes.push_back(harp ? x + 12 : ep ? fit(x, 62) : x);
        std::sort(notes.begin(), notes.end());
        const size_t nn = notes.size();
        for (size_t k = 0; k < nn; k++) notes.push_back(notes[k] + 12);
        const float step = (F & F_HARPSLOW) ? 1.f : 0.5f;
        for (float b = c.beat; b < c.beat + c.len - 0.01f; b += step) {
          const int si = (int)(b / step) % 8;
          const int idx = pats[arpPat][si] % (int)notes.size();
          const float vel = (inst == I_HARP ? 0.5f : inst == I_EP ? 0.38f : 0.5f) + 0.2f * I + 0.08f * rng.n() + (si % 2 == 0 ? 0.05f : 0.f);
          ev(at(b) + hum(inst == I_GUITAR ? 7 : 5), inst, (float)notes[idx], vel, secs(inst == I_EP ? 1.2f : 2.5f), (inst == I_GUITAR ? -0.75f : -0.5f) + 1.f * idx / notes.size());
        }
      }
      // stars: a few high bells on the chord's colours, left ringing in the delay
      if (F & F_STARS) {
        const ChordType& ct = kChords[c.type];
        for (float b = c.beat; b < c.beat + c.len - 0.01f; b += 1.f) {
          if (rng.u() > 0.22f + 0.28f * I) continue;
          const int tone = ct.tones[(int)(rng.u() * ct.n) % ct.n];
          const int pitch = fit(S.key + c.root + tone, mood == MOOD_HUB ? 72 : 79);
          const int inst = mood == MOOD_HUB ? I_VIBES : I_CELESTA;
          ev(at(b + (rng.u() < 0.4f ? 0.5f : 0.f)) + hum(8), inst, (float)pitch, 0.32f + 0.18f * rng.u(), secs(3.f), rng.n() * 0.7f);
        }
      }
    }
    // the theme
    const int mel = sec.mel;
    if (mel >= 0 && (F & (F_FLUTE | F_HORN | F_VIBES))) {
      const Phrase& ph = mels[mood][mel];
      const float b0 = barIn * 4.f;
      for (size_t ni = 0; ni < ph.notes.size(); ni++) {
        const Note& nt = ph.notes[ni];
        if (nt.beat < b0 - 0.01f || nt.beat >= b0 + 4.f - 0.01f) continue;
        const float b = nt.beat - b0;
        const bool joined = ni > 0 && fabsf(ph.notes[ni - 1].beat + ph.notes[ni - 1].len - nt.beat) < 0.01f;
        const int slur = joined && rng.u() < 0.72f ? 0 : 1;   // (tongued: a new breath, a new part number)
        legatoFlip[0] ^= slur; legatoFlip[1] ^= slur; legatoFlip[2] ^= slur;
        const float vel = 0.55f + 0.2f * I + nt.accent + 0.05f * rng.n();
        const double tt = at(b) + hum(7);
        if (F & F_FLUTE) {
          if (loops > 0 && nt.len >= 2.f && rng.u() < 0.3f) ev(tt - 0.07 * sr, I_FLUTE, (float)(S.key + 12 + nt.pitch + 2), vel * 0.7f, 0.075f, 0.1f);   // (a grace note from above)
          ev(tt, I_FLUTE, (float)(S.key + 12 + nt.pitch), vel, secs(nt.len) * 0.98f, 0.12f, kLegato + legatoFlip[0]);
        }
        if (F & F_HORN) ev(tt, I_HORN, (float)(S.key + nt.pitch), vel, secs(nt.len) * 0.97f, -0.1f, kLegato + 2 + legatoFlip[1]);
        if (F & F_DOUBLE) ev(tt + 0.008 * sr, I_FLUTE, (float)(S.key + 24 + nt.pitch), vel * 0.5f, secs(nt.len) * 0.97f, 0.25f, kLegato + 4 + legatoFlip[2]);
        if (F & F_VIBES) ev(tt, I_VIBES, (float)(S.key + 12 + nt.pitch), vel * 0.95f, secs(nt.len) + 0.25f, -0.2f);
      }
    }
    // the drums
    if (F & F_BRUSHES) {
      for (int b = 0; b < 4; b++) {
        if (b == 0 || (b == 2 && rng.u() < 0.8f)) ev(at((float)b) + hum(3), I_KICK, 0, b == 0 ? 0.62f : 0.48f, 0.4f, 0.f);
        if (b == 1 || b == 3) ev(at((float)b) + hum(4), I_BRUSH, 0, 0.58f + 0.1f * rng.u(), 0.2f, -0.25f);
        else if (rng.u() < 0.25f) ev(at(b + 0.5f) + hum(5), I_BRUSH, 0, 0.22f, 0.2f, -0.25f);
        ev(at((float)b) + hum(3), I_HAT, 0, 0.30f + 0.06f * rng.u(), 0.1f, 0.5f);
        const bool open = last && b == 3 && rng.u() < 0.35f;
        ev(at(b + 0.5f) + hum(3), I_HAT, 0, 0.42f + 0.08f * rng.u(), 0.1f, 0.5f, 0, open ? 1.f : 0.f);
        if (I > 0.62f) ev(at(b + 0.5f) + hum(4), I_SHAKER, 0, 0.32f + 0.1f * rng.u(), 0.1f, -0.6f);
      }
      if (last) for (int k = 0; k < 3; k++) ev(t0 + (3.f + k / 3.f) * bl + hum(3), I_BRUSH, 0, 0.3f + 0.15f * k, 0.2f, -0.1f + 0.1f * k);   // (a fill into the next section)
      if (first && rng.u() < 0.5f) ev(t0 + hum(3), I_BRUSH, 0, 0.7f, 0.4f, 0.2f, 0, 1.f);   // (a swish on the downbeat)
    }
    if (F & F_ORCH) {
      ev(at(0), I_KICK, 0, 0.55f, 1.f, 0.f, 0, 1.f);
      ev(at(2), I_KICK, 0, 0.32f, 1.f, 0.f, 0, 1.f);
      if (first) ev(at(0), I_CYMBAL, 0, 0.5f, 2.f, 0.3f);
    }
    if (F & F_TIMP) {
      const ChordSeg& c = P.bars[barIn][0];
      const int tim = fit(S.key + c.root, 38);
      if (first || barIn == 4) ev(at(0), I_TIMPANI, (float)tim, first ? 0.9f : 0.65f, 2.f, -0.2f);
      if (last) for (int k = 0; k < 8; k++) ev(t0 + (2.f + k * 0.25f) * bl, I_TIMPANI, (float)fit(S.key + 7, 38), 0.18f + 0.07f * k, 0.5f, -0.2f);
    }
    if ((F & F_SWELL) && last) ev(t0 + 1.f * bl, I_CYMBAL, 0, 0.55f, secs(3.f), 0.f, 0, 1.f);
    // on to the next bar
    barAt += 4.0 * bl;
    if (++barIn >= nBars) {
      barIn = 0;
      if (++section >= (int)S.sections.size()) { section = S.loopTo; loops++; }
    }
  }

  void setMood(int m) {
    m = std::clamp(m, 0, MOOD_COUNT - 1);
    if (m == mood) return;
    for (Voice& v : voices) if (!v.sting && v.rel < 0) { v.rel = std::max(v.t, 0.f); if (isString(v.inst)) v.ksRho = std::min(v.ksRho, powf(0.001f, 1.f / (v.f * 0.6f))); }
    events.erase(std::remove_if(events.begin(), events.end(), [](const Event& e) { return !e.sting; }), events.end());
    mood = m; section = 0; barIn = 0; loops = 0; barAt = now + 0.05 * sr; lastWalk = -99;
    padPrev.clear(); strPrev.clear(); compPrev.clear(); brassPrev.clear(); epPrev.clear();
    gainTo = songs[m].level;
    if (m != MOOD_NONE) { verb.setDecay(songs[m].rt60, songs[m].dampHz); wetTo = songs[m].wet; delay.dTo = 0.75f * 60.f / songs[m].bpm; }
  }

  // ---- the cues
  int cueTonic() const { const int key = mood != MOOD_NONE ? songs[mood].key : 62; return 60 + ((key - 60) % 12 + 12) % 12; }
  void sting(int kind) {
    const int K = cueTonic();
    const double t0 = now + 0.02 * sr;
    static const float kCueGain[STING_COUNT] = {1.f, 0.89f, 0.35f, 1.f, 0.61f, 2.6f};   // (each cue's level over silence: about -24 dBFS, the licence's -21)
    cueGain = kind >= 0 && kind < STING_COUNT ? kCueGain[kind] : 1.f;
    auto at = [&](float sec, int inst, float midi, float vel, float dur, float pan = 0.f, float p1 = 0.f, float p2 = 0.f) {
      ev(t0 + sec * sr, inst, midi, vel, dur, pan, 0, p1, p2, true);
    };
    switch (kind) {
      case STING_SUCCESS: {   // a celesta and harp run up the major chord, the brass's "ta-daa" over strings
        const int run[5] = {12, 16, 19, 24, 28};
        for (int i = 0; i < 5; i++) at(i * 0.065f, I_CELESTA, K + run[i], 0.55f + 0.04f * i, 1.4f, -0.3f + 0.15f * i);
        const int gl[8] = {-12, -8, -5, 0, 4, 7, 12, 16};
        for (int i = 0; i < 8; i++) at(i * 0.028f, I_HARP, K + gl[i], 0.42f, 2.f, -0.5f + 0.12f * i);
        at(0.2f, I_BRASS, K - 5, 0.55f, 0.13f, -0.1f);
        for (int x : {0, 4, 7}) at(0.34f, I_BRASS, K + x, 0.62f, 1.1f, -0.25f + 0.08f * x);
        for (int x : {-12, -5, 2, 4}) at(0.34f, I_STRINGS, K + x, 0.55f, 1.7f, -0.4f + 0.1f * (x + 12) / 4.f, 0.6f, 0.25f);
        at(0.34f, I_BASS, K - 24, 0.7f, 1.4f);
        at(0.34f, I_CYMBAL, 0, 0.32f, 2.f, 0.2f);
        at(0.62f, I_VIBES, K + 14, 0.42f, 1.6f, 0.4f);
        break;
      }
      case STING_FAIL:   // a soft fall to the minor tonic: nothing harsh
        { const int fall[4] = {7, 3, 2, 0};
          for (int i = 0; i < 4; i++) at(i * 0.3f, I_EP, K + fall[i], 0.42f, i == 3 ? 1.5f : 0.55f, 0.1f); }
        for (int x : {-12, -5, 3}) at(0.f, I_STRINGS, K + x, 0.42f, 2.2f, -0.3f + 0.2f * (x + 12) / 7.5f, 0.12f, 0.35f);
        at(0.f, I_BASS, K - 24, 0.55f, 2.f);
        at(0.9f, I_VIBES, K - 12, 0.32f, 1.6f, -0.2f);
        break;
      case STING_LICENCE:   // a fanfare: a timpani roll and a cymbal swell into the brass's major chord, strings, bells
        for (int i = 0; i < 10; i++) at(i * 0.055f, I_TIMPANI, K - 24 + 7, 0.22f + 0.05f * i, 0.5f, -0.25f);
        at(0.58f, I_TIMPANI, K - 24, 1.f, 2.f, -0.25f);
        at(0.f, I_CYMBAL, 0, 0.5f, 0.58f, 0.f, 1.f);
        at(0.58f, I_CYMBAL, 0, 0.62f, 2.f, 0.2f);
        for (int i = 0; i < 3; i++) at(i * 0.12f, I_HORN, K + 7, 0.6f, 0.1f, 0.05f);
        at(0.36f, I_HORN, K + 7, 0.65f, 0.16f, 0.05f);
        for (int x : {0, 4, 7, 12}) at(0.58f, I_BRASS, K + x, 0.78f, 1.7f, -0.3f + 0.05f * x);
        at(0.58f, I_HORN, K + 12, 0.8f, 1.8f, 0.05f);
        for (int x : {-12, -5, 0, 4, 7}) at(0.58f, I_STRINGS, K + x, 0.62f, 2.5f, -0.5f + 0.05f * (x + 12), 0.7f, 0.3f);
        { const int run[4] = {24, 28, 31, 36};
          for (int i = 0; i < 4; i++) at(0.62f + 0.06f * i, I_CELESTA, K + run[i], 0.5f, 1.5f, 0.3f); }
        at(0.58f, I_BASS, K - 24, 0.8f, 2.2f);
        at(0.58f, I_KICK, 0, 0.75f, 1.f, 0.f, 1.f);
        break;
      case STING_ACHIEVEMENT: {   // a celesta's sparkle up the pentatonic, a harp's sweep, a vibraphone chord
        const int run[6] = {7, 9, 12, 14, 16, 19};
        for (int i = 0; i < 6; i++) at(i * 0.055f, I_CELESTA, K + 12 + run[i], 0.48f + 0.03f * i, 1.2f, -0.4f + 0.15f * i);
        const int gl[6] = {0, 4, 7, 12, 16, 19};
        for (int i = 0; i < 6; i++) at(i * 0.026f, I_HARP, K + gl[i], 0.36f, 1.8f, -0.3f + 0.1f * i);
        for (int x : {4, 7, 11, 14}) at(0.33f, I_VIBES, K + x, 0.36f, 1.4f, -0.2f + 0.05f * x);
        break;
      }
      case STING_MEDAL:   // a horn call (tonic, fifth, octave) over brass, a run of chimes, a timpani stroke
        at(0.f, I_HORN, K, 0.62f, 0.18f, 0.f); at(0.2f, I_HORN, K + 7, 0.65f, 0.18f, 0.f); at(0.4f, I_HORN, K + 12, 0.72f, 1.25f, 0.f);
        for (int x : {0, 4, 7}) at(0.4f, I_BRASS, K + x - 12, 0.5f, 1.2f, -0.3f + 0.1f * x);
        { const int run[4] = {24, 28, 31, 36};
          for (int i = 0; i < 4; i++) at(0.4f + 0.06f * i, I_CELESTA, K + run[i], 0.5f, 1.3f, 0.35f); }
        at(0.4f, I_TIMPANI, K - 24, 0.8f, 1.5f, -0.2f);
        at(0.4f, I_CYMBAL, 0, 0.3f, 1.5f, 0.2f);
        break;
      case STING_APPLAUSE: {   // a cabin's applause as the wheels settle: each passenger at their own pace, pitch and seat
        const int people = 14;
        for (int p = 0; p < people; p++) {
          const float rate = 2.6f + 1.8f * rng.u(), startT = 0.7f + 0.35f * rng.u(), stopT = 2.2f + 1.6f * rng.u() * rng.u();
          const float fc = 850.f + 1500.f * rng.u(), q = 0.8f + 0.9f * rng.u(), pan = rng.n() * 0.85f, loud = 0.55f + 0.45f * rng.u();
          for (float t = startT + rng.u() / rate; t < stopT; t += (1.f + 0.12f * rng.n()) / rate) {
            const float swell = std::min((t - 0.7f) / 0.3f, 1.f) * std::min((stopT - t) / 0.6f + 0.3f, 1.f);
            at(t, I_CLAP, 0, loud * swell * (0.75f + 0.25f * rng.u()), 0.08f, pan, fc * (0.95f + 0.1f * rng.u()), q);
          }
        }
        break;
      }
      default: break;
    }
  }
  void note(int step) {
    const int K = cueTonic();
    static const int penta[5] = {0, 2, 4, 7, 9};
    step = std::clamp(step, 0, 14);
    const int p = K + 12 + penta[step % 5] + 12 * (step / 5);
    cueGain = 1.f;
    ev(now + 0.01 * sr, I_CELESTA, (float)p, 0.62f, 1.4f, 0.15f, 0, 0, 0, true);
    ev(now + 0.01 * sr, I_VIBES, (float)(p - 12), 0.32f, 1.2f, -0.15f, 0, 0, 0, true);
  }

  // ---- the mix
  void render(float* out, int frames) {
#ifdef MUSIC_FTZ
    const unsigned csr = _mm_getcsr(); _mm_setcsr(csr | 0x8040);   // (flush denormals to zero: the reverb's tail never slows to a crawl)
#endif
    const float gk = 1.f - expf(-(float)kBlock / (0.8f * sr));
    for (int b0 = 0; b0 < frames; b0 += kBlock) {
      const int n = std::min(kBlock, frames - b0);
      const double bs = now, be = now + n;
      if (mood != MOOD_NONE) while (barAt < be + 0.25 * sr) scheduleBar();
      for (size_t e = 0; e < events.size();) {
        if (events[e].at < be) { const Event ev2 = events[e]; events[e] = events.back(); events.pop_back(); start(ev2, bs); }
        else e++;
      }
      for (int bus = 0; bus < 2; bus++) {
        std::fill(dryL[bus], dryL[bus] + n, 0.f); std::fill(dryR[bus], dryR[bus] + n, 0.f);
        std::fill(revL[bus], revL[bus] + n, 0.f); std::fill(revR[bus], revR[bus] + n, 0.f);
        std::fill(dlyL[bus], dlyL[bus] + n, 0.f); std::fill(dlyR[bus], dlyR[bus] + n, 0.f);
      }
      bool stingOn = false;
      for (Voice& v : voices) if (v.inst >= 0) { renderVoice(v, n); stingOn |= v.sting && v.t > 0 && v.lastEnv > 1e-3f; }
      for (Voice& v : voices) if (v.inst < 0) freeVoice(v);
      voices.erase(std::remove_if(voices.begin(), voices.end(), [](const Voice& v) { return v.inst < 0; }), voices.end());
      // the mood's level, and a dip under a cue so it stands out
      gain += (gainTo - gain) * gk;
      duck += ((stingOn ? 0.6f : 1.f) - duck) * (stingOn ? 0.08f : 0.012f);
      wet += (wetTo - wet) * gk;
      const float gm = gain * duck;
      for (int i = 0; i < n; i++) {
        const float dL = dryL[0][i] * gm + dryL[1][i], dR = dryR[0][i] * gm + dryR[1][i];
        float yl, yr; delay.process(dlyL[0][i] * gm + dlyL[1][i], dlyR[0][i] * gm + dlyR[1][i], yl, yr);
        float rl, rr; verb.process(revL[0][i] * gm + revL[1][i] + 0.3f * yl, revR[0][i] * gm + revR[1][i] + 0.3f * yr, rl, rr);
        float L = dL + yl * 0.55f + rl * wet * 0.5f, R = dR + yr * 0.55f + rr * wet * 0.5f;
        master.process(L, R);
        out[(b0 + i) * 2] = L; out[(b0 + i) * 2 + 1] = R;
      }
      now = be;
    }
#ifdef MUSIC_FTZ
    _mm_setcsr(csr);
#endif
  }
};

MusicGen::MusicGen(float sampleRate, uint32_t seed) : d(new Impl(sampleRate, seed)) {}
MusicGen::~MusicGen() = default;
void MusicGen::setMood(int m) { d->setMood(m); }
int MusicGen::mood() const { return d->mood; }
void MusicGen::sting(int kind) { d->sting(kind); }
void MusicGen::note(int step) { d->note(step); }
void MusicGen::render(float* stereo, int frames) { d->render(stereo, frames); }

// ---------------------------------------------------------------- the player
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
