// Air Xpress - procedural audio synthesis
//
// Piston engines are modelled per cylinder: every firing event excites a bank of exhaust/body
// resonators with an impulse plus a short noise burst. Each cylinder has a slightly different
// strength and timing, which produces the characteristic uneven "lope" and the sub-harmonic at
// crank rate. The propeller adds a peaky blade-passing buzz and an amplitude-modulated whoosh.
// Twins run two detuned voices so they beat against each other like real unsynchronised props.
#include "audio.h"

AudioEngine g_audio;

namespace {
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void set(int type, float f, float q, float sr, float gainDb = 0) {
    f = clampf(f, 10.f, sr * 0.45f);
    float w = 2 * PI * f / sr, cw = cosf(w), sw = sinf(w), al = sw / (2 * q);
    float A = powf(10.f, gainDb / 40.f);
    float B0, B1, B2, A0, A1, A2;
    switch (type) {
      case 0: B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;        // LP
      case 1: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;     // HP
      case 2: B0 = al; B1 = 0; B2 = -al; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;                               // BP (0dB peak)
      default: B0 = 1 + al * A; B1 = -2 * cw; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * cw; A2 = 1 - al / A; break;  // peaking
    }
    b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
  }
  inline float p(float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
enum { LP = 0, HP = 1, BP = 2, PK = 3 };

struct Noise {
  uint32_t s = 22222;
  inline float w() { s = s * 1664525u + 1013904223u; return ((s >> 9) & 0x7FFFFF) / 4194304.0f - 1.0f; }
};

// One-pole smoother
struct Smooth { float v = 0; inline float p(float target, float k) { v += (target - v) * k; return v; } };

struct PistonVoice {
  float phase = 0, propPhase = 0; int lastK = -1;
  float cylBias[8], cylTime[8];
  Biquad res[4], crackLP, mech;
  float noiseEnv = 0, popEnv = 0;
  Noise nz;
  Biquad whoosh, buzzLP;
  void init(uint32_t seed) {
    Rng r(seed);
    for (int i = 0; i < 8; i++) { cylBias[i] = 0.82f + 0.36f * r.uni(); cylTime[i] = (r.uni() - 0.5f) * 0.035f; }
    nz.s = seed * 7 + 1;
  }
  // returns {engine, prop} contributions
  void tick(float sr, float rpm, float load, int cyl, int blades, float propRpm, bool running, bool cranking, float& eng, float& prop, int blockPos) {
    float rps = rpm / 60.f;
    phase += rps / sr;
    if (phase >= 2.f) phase -= 2.f;  // full four-stroke cycle = 2 revolutions
    float interval = 2.f / cyl;
    float c = phase;
    int k = (int)(c / interval);
    float exc = 0;
    if (k != lastK) {
      lastK = k;
      int ci = k % cyl;
      if (running) {
        float irregular = 1.f + (nz.w() * 0.5f) * (0.25f * smoothstepf(1400.f, 650.f, rpm));
        float amp = (0.30f + 0.70f * load) * cylBias[ci] * irregular;
        exc = amp;
        noiseEnv = amp * (0.6f + 0.4f * load);
      } else if (cranking) {
        exc = 0.18f * cylBias[ci];  // compression pulses
        noiseEnv = 0.05f;
      }
    }
    if (blockPos == 0) {
      // retune resonators with rpm/load (pipe gets "angrier" with power)
      float f1 = 85.f + rpm * 0.012f;
      res[0].set(BP, f1, 1.6f, sr);
      res[1].set(BP, 210.f + 40.f * load, 3.0f, sr);
      res[2].set(BP, 560.f + 160.f * load, 2.5f, sr);
      res[3].set(BP, 1900.f + 600.f * load, 1.2f, sr);
      mech.set(BP, 3800.f, 2.f, sr);
      whoosh.set(BP, 420.f + propRpm * 0.25f, 0.8f, sr);
      buzzLP.set(LP, 900.f + propRpm * 0.8f, 0.7f, sr);
    }
    float n = nz.w();
    float x = exc * 30.f + n * noiseEnv * 0.9f;
    noiseEnv *= 0.9965f;
    float y = res[0].p(x) * 1.0f + res[1].p(x) * 0.75f + res[2].p(x) * 0.42f + res[3].p(x) * (0.10f + 0.12f * load);
    y += mech.p(n) * 0.012f * (rpm / 2600.f) * (running ? 1.f : 0.3f);
    eng = tanhf(y * 1.6f) * 0.8f;
    // propeller
    float bps = propRpm / 60.f * blades;
    propPhase += bps / sr; if (propPhase > 1.f) propPhase -= 1.f;
    float cs = 0.5f + 0.5f * cosf(2 * PI * propPhase);
    float peak = powf(cs, 6.f + 10.f * load) - 0.2f;
    float tip = clampf(propRpm / 2700.f, 0, 1.2f);
    float buzz = buzzLP.p(peak) * (0.15f + 0.85f * load) * tip * tip;
    float wh = whoosh.p(n) * (0.55f + 0.45f * cs) * tip * tip * (0.3f + 0.7f * load);
    prop = buzz * 0.55f + wh * 0.5f;
  }
};

struct TurbineVoice {
  float ph1 = 0, ph2 = 0, ph3 = 0, propPh = 0;
  Noise nz; Biquad hiss, roarLP, roarLP2, rumble, propLP, propWhoosh, buzzBP;
  void init(uint32_t s) { nz.s = s; }
  void tick(float sr, bool jet, float n1, float spool, float propRpm, int blades, float& eng, float& prop, int blockPos) {
    float nn = clampf(n1 / 100.f, 0, 1.1f);
    if (blockPos == 0) {
      hiss.set(BP, 2500.f + 3000.f * nn, 1.2f, sr);
      roarLP.set(LP, jet ? 250.f + 1700.f * spool * spool : 200.f + 400.f * spool, 0.7f, sr);
      roarLP2.set(LP, jet ? 300.f + 1500.f * spool : 300.f, 0.7f, sr);
      rumble.set(LP, 70.f, 0.8f, sr);
      propLP.set(LP, 700.f + propRpm * 0.6f, 0.7f, sr);
      propWhoosh.set(BP, 350.f + propRpm * 0.2f, 0.9f, sr);
      buzzBP.set(BP, n1 * 0.9f * 6.f, 6.f, sr);
    }
    float n = nz.w();
    // compressor / fan whine
    float f1 = jet ? 300.f + 2900.f * nn : 600.f + 3600.f * nn;
    ph1 += f1 / sr; ph2 += f1 * 1.47f / sr; ph3 += (jet ? f1 * 0.5f : f1 * 2.03f) / sr;
    ph1 -= floorf(ph1); ph2 -= floorf(ph2); ph3 -= floorf(ph3);
    float whine = sinf(2 * PI * ph1) * 0.6f + sinf(2 * PI * ph2) * 0.25f + sinf(2 * PI * ph3) * 0.2f;
    float whineAmp = (jet ? 0.10f : 0.07f) * smoothstepf(5.f, 60.f, n1) * (1.f - 0.4f * spool * (jet ? 1.f : 0.f));
    float roar = roarLP2.p(roarLP.p(n)) * (jet ? (0.15f + 1.2f * powf(spool, 1.5f)) : (0.08f + 0.25f * spool));
    float rum = rumble.p(n) * (jet ? 1.6f * spool : 0.6f * spool);
    float hs = hiss.p(n) * 0.06f * nn;
    float buzzsaw = 0;
    if (jet && n1 > 82.f) buzzsaw = buzzBP.p(n) * 0.5f * smoothstepf(82.f, 98.f, n1);
    eng = whine * whineAmp + roar * 1.2f + rum + hs + buzzsaw;
    prop = 0;
    if (!jet) {
      float bps = propRpm / 60.f * blades;
      propPh += bps / sr; propPh -= floorf(propPh);
      float cs = 0.5f + 0.5f * cosf(2 * PI * propPh);
      float tip = clampf(propRpm / 1900.f, 0, 1.2f);
      float load = 0.25f + 0.75f * spool;
      float buzz = propLP.p(powf(cs, 7.f + 9.f * spool) - 0.18f) * load * tip * tip;
      prop = buzz * 0.75f + propWhoosh.p(n) * (0.5f + 0.5f * cs) * tip * tip * 0.6f * load;
    }
  }
};

struct OneShot { int type; float t; float intensity; float ph; float ph2; Biquad f1, f2; Noise nz; bool active; };
}  // namespace

struct AudioEngine::Impl {
  float sr = 48000;
  AudioParams P;
  PistonVoice pv[2];
  TurbineVoice tv[2];
  Smooth sRpm, sN1, sSpool, sAir, sGs, sLoad, sRain, sMuffle, sInterior, sVol, sStall, sCrank;
  Noise nz;
  Biquad windBP, windLP, rollLP, rollBP, gearRumble, rainHP, rainLP;
  Biquad engL[2], engR[2], cabinBoom[2], muffleLP[2][2];
  float delayL[2048] = {}, delayR[2048] = {}; int dpos = 0;
  float stallPh = 0, motorPh = 0, seamDist = 0, bumpEnv = 0;
  int block = 0;
  OneShot shots[24];
  float thunderEnv = 0;
  Biquad thunderLP;
};

void AudioEngine::init(int sr) {
  sampleRate = sr;
  impl = new Impl();
  impl->sr = (float)sr;
  impl->pv[0].init(1234); impl->pv[1].init(98765);
  impl->tv[0].init(555); impl->tv[1].init(777);
  for (auto& s : impl->shots) s.active = false;
  impl->thunderLP.set(LP, 120, 0.7f, (float)sr);
}

void AudioEngine::setParams(const AudioParams& p) { acquire(); pending = p; hasPending = true; release(); }

void AudioEngine::trigger(int sfx, float intensity) {
  acquire();
  if (trigN < 32) { trigQ[trigN] = sfx; trigI[trigN] = intensity; trigN++; }
  release();
}

static void startShot(OneShot& s, int type, float inten, float sr) {
  s = OneShot(); s.type = type; s.t = 0; s.intensity = inten; s.active = true; s.nz.s = 4321 + type * 17;
  switch (type) {
    case SFX_TOUCHDOWN: s.f1.set(BP, 1700, 1.4f, sr); s.f2.set(LP, 90, 0.8f, sr); break;
    case SFX_CRASH: s.f1.set(LP, 900, 0.7f, sr); s.f2.set(BP, 2500, 1.0f, sr); break;
    case SFX_THUNDER: s.f1.set(LP, 160, 0.7f, sr); s.f2.set(LP, 60, 0.7f, sr); break;
    case SFX_CASH: s.f1.set(BP, 5000, 2.0f, sr); break;
    case SFX_GEAR_CLUNK: s.f1.set(LP, 300, 1.0f, sr); break;
    default: break;
  }
}

// returns sample (mono) and false when finished
static bool runShot(OneShot& s, float sr, float& out) {
  float t = s.t, dt = 1.f / sr;
  float v = 0; bool alive = true;
  auto bell = [&](float f, float start, float dur, float a) {
    float tt = t - start; if (tt < 0 || tt > dur) return 0.f;
    float env = expf(-tt * 5.f / dur) * smoothstepf(0, 0.004f, tt);
    return a * env * (sinf(2 * PI * f * tt) + 0.3f * sinf(2 * PI * f * 2.01f * tt) + 0.12f * sinf(2 * PI * f * 3.02f * tt));
  };
  switch (s.type) {
    case SFX_CLICK: v = sinf(2 * PI * 1800 * t) * expf(-t * 120) * 0.25f; alive = t < 0.06f; break;
    case SFX_HOVER: v = sinf(2 * PI * 2600 * t) * expf(-t * 200) * 0.06f; alive = t < 0.03f; break;
    case SFX_CHIME: v = bell(1318.5f, 0, 0.5f, 0.18f) + bell(1975.5f, 0.07f, 0.6f, 0.15f); alive = t < 0.8f; break;
    case SFX_SUCCESS: v = bell(523.25f, 0, 1.2f, 0.16f) + bell(659.25f, 0.12f, 1.2f, 0.14f) + bell(783.99f, 0.24f, 1.2f, 0.14f) + bell(1046.5f, 0.38f, 1.6f, 0.14f); alive = t < 2.2f; break;
    case SFX_FAIL: v = bell(392.f, 0, 0.9f, 0.16f) + bell(311.1f, 0.2f, 1.0f, 0.16f) + bell(261.6f, 0.42f, 1.4f, 0.16f); alive = t < 2.0f; break;
    case SFX_CASH: v = bell(2093.f, 0, 0.35f, 0.12f) + bell(2637.f, 0.08f, 0.5f, 0.12f) + s.f1.p(s.nz.w()) * expf(-t * 30) * 0.3f; alive = t < 0.7f; break;
    case SFX_BEEP: v = (fmodf(t, 0.25f) < 0.12f ? sinf(2 * PI * 1000 * t) : 0.f) * 0.12f; alive = t < 0.75f; break;
    case SFX_AP_DISC: v = sinf(2 * PI * (880 + 220 * (fmodf(t, 0.2f) > 0.1f)) * t) * 0.1f * (t < 0.9f); alive = t < 0.9f; break;
    case SFX_GEAR_CLUNK: v = s.f1.p(s.nz.w()) * expf(-t * 25) * 1.5f + sinf(2 * PI * 70 * t) * expf(-t * 18) * 0.3f; alive = t < 0.4f; break;
    case SFX_TOUCHDOWN: {
      float chirp = s.f1.p(s.nz.w()) * expf(-t * 9.f) * smoothstepf(0, 0.01f, t) * (0.4f + 0.8f * s.intensity);
      float thump = (s.f2.p(s.nz.w()) * 3.f + sinf(2 * PI * 55 * t) * 0.5f) * expf(-t * 14.f) * s.intensity;
      v = chirp + thump; alive = t < 0.8f; break; }
    case SFX_CRASH: {
      float env = expf(-t * 1.2f) * smoothstepf(0, 0.01f, t);
      v = (s.f1.p(s.nz.w()) * 2.5f + s.f2.p(s.nz.w()) * 0.6f * expf(-t * 4.f)) * env + sinf(2 * PI * 40 * t) * expf(-t * 3) * 0.6f;
      alive = t < 4.f; break; }
    case SFX_THUNDER: {
      float env = smoothstepf(0, 0.15f, t) * expf(-t * 0.7f) * (0.6f + 0.4f * sinf(t * 7.f) * sinf(t * 2.3f));
      v = (s.f1.p(s.nz.w()) * 3.f + s.f2.p(s.nz.w()) * 4.f) * env * s.intensity; alive = t < 6.f; break; }
    default: alive = false;
  }
  s.t += dt;
  out = v;
  return alive;
}

void AudioEngine::render(float* out, int frames) {
  if (!impl) { memset(out, 0, sizeof(float) * frames * 2); return; }
  Impl& I = *impl;
  acquire();
  if (hasPending) { I.P = pending; hasPending = false; }
  for (int i = 0; i < trigN; i++) {
    for (auto& s : I.shots) if (!s.active) { startShot(s, trigQ[i], trigI[i], I.sr); break; }
  }
  trigN = 0;
  release();
  const AudioParams& P = I.P;
  float sr = I.sr;
  const float k = 1.f - expf(-1.f / (0.04f * sr));  // ~40ms smoothing
  const float kSlow = 1.f - expf(-1.f / (0.25f * sr));
  for (int f = 0; f < frames; f++) {
    int bp = I.block++ & 63;
    float rpm = I.sRpm.p(P.rpm, k), n1 = I.sN1.p(P.n1, k), spool = I.sSpool.p(P.spool, k);
    float load = I.sLoad.p(clampf(P.throttle * 0.6f + P.spool * 0.4f, 0, 1), k);
    float air = I.sAir.p(P.airspeed, k), gs = I.sGs.p(P.onGround ? P.groundSpeed : 0.f, k);
    float muff = I.sMuffle.p(P.muffled ? 1.f : 0.f, kSlow), inter = I.sInterior.p(P.interior ? 1.f : 0.f, kSlow);
    float vol = I.sVol.p(P.inFlight && !P.paused ? 1.f : 0.f, kSlow);
    float stall = I.sStall.p(P.stallWarn, k);
    float L = 0, R = 0;
    if (vol > 0.001f) {
      float eng[2] = {0, 0}, prop[2] = {0, 0};
      int ne = std::min(P.engines, 2);
      for (int e = 0; e < ne; e++) {
        float det = e == 0 ? 1.f : 1.0065f;  // unsynchronised twins beat slowly
        if (P.engineType == 0)
          I.pv[e].tick(sr, rpm * det, load, std::max(P.cylinders, 2), std::max(P.blades, 2), rpm * det, P.running, P.cranking, eng[e], prop[e], bp);
        else {
          float propRpm = P.engineType == 1 ? rpm * det : 0.f;
          I.tv[e].tick(sr, P.engineType == 2, n1 * det, spool, propRpm, std::max(P.blades, 2), eng[e], prop[e], bp);
        }
      }
      // cabin/exterior tone shaping
      if (bp == 0) {
        for (int c = 0; c < 2; c++) {
          I.engL[c].set(LP, lerpf(9000.f, 2200.f, inter), 0.7f, sr);
          I.engR[c].set(LP, lerpf(9000.f, 2200.f, inter), 0.7f, sr);
          I.cabinBoom[c].set(PK, 115.f, 1.2f, sr, 6.f * inter);
          I.muffleLP[c][0].set(LP, lerpf(16000.f, 380.f, muff), 0.7f, sr);
          I.muffleLP[c][1].set(LP, lerpf(16000.f, 380.f, muff), 0.7f, sr);
        }
        I.windBP.set(BP, 300.f + air * 9.f, 0.6f, sr);
        I.windLP.set(LP, 1500.f + air * 25.f, 0.7f, sr);
        I.rollLP.set(LP, 120.f + gs * 6.f, 0.8f, sr);
        I.rollBP.set(BP, 900.f + gs * 15.f, 1.0f, sr);
        I.gearRumble.set(LP, 140.f, 0.8f, sr);
        I.rainHP.set(HP, 2500.f, 0.7f, sr);
        I.rainLP.set(LP, 9000.f, 0.7f, sr);
      }
      float engVol = P.engineVol * (P.engineType == 0 ? 0.55f : 0.6f);
      float propW = lerpf(1.0f, 0.65f, inter);
      float e0 = (eng[0] + prop[0] * propW), e1 = (eng[1] + prop[1] * propW);
      float mono = (ne == 2) ? 0.f : e0;
      float el = ne == 2 ? e0 * 0.8f + e1 * 0.45f : mono;
      float er = ne == 2 ? e1 * 0.8f + e0 * 0.45f : mono;
      // Haas widening for single engines
      I.delayL[I.dpos & 2047] = el; I.delayR[I.dpos & 2047] = er;
      float wl = I.delayR[(I.dpos - 331) & 2047], wr = I.delayL[(I.dpos - 547) & 2047];
      I.dpos++;
      el = el + wl * 0.18f; er = er + wr * 0.18f;
      el = I.cabinBoom[0].p(I.engL[0].p(el)) * engVol; er = I.cabinBoom[1].p(I.engR[0].p(er)) * engVol;
      // wind & airframe
      float n = I.nz.w();
      float windAmp = clampf(air / 70.f, 0, 1.6f); windAmp *= windAmp;
      float gust = 1.f + P.turbulence * 0.6f * sinf(I.motorPh * 0.0007f);
      float wind = (I.windBP.p(n) * 0.5f + I.windLP.p(n) * 0.12f) * windAmp * gust * lerpf(1.0f, 0.55f, inter);
      float gearR = I.gearRumble.p(I.nz.w()) * (P.gearDown * 0.4f + P.flaps * 0.5f) * windAmp * 0.6f;
      // rolling
      float roll = 0;
      if (P.onGround && gs > 0.5f) {
        I.seamDist += gs / sr;
        if (P.rough < 0.3f && I.seamDist > 18.f) { I.seamDist = 0; I.bumpEnv = 0.6f; }
        if (P.rough > 0.3f && (I.nz.s & 0xFFF) < (uint32_t)(gs * P.rough * 0.25f)) I.bumpEnv = 0.4f + 0.6f * P.rough;
        float sp = clampf(gs / 30.f, 0, 1.5f);
        roll = I.rollLP.p(I.nz.w()) * sp * (0.6f + 1.2f * P.rough) + I.rollBP.p(I.nz.w()) * 0.05f * sp * P.rough;
        roll += sinf(I.motorPh * 2 * PI * 45.f / sr) * I.bumpEnv * 0.5f;
        I.bumpEnv *= 0.9985f;
      }
      I.motorPh += 1.f;
      float body = (wind + gearR + roll) * 0.9f;
      el += body; er += body;
      // muffle (headset ANR) on the engine/airframe bus
      float gm = lerpf(1.f, 0.42f, muff);
      el = I.muffleLP[0][0].p(I.muffleLP[0][1].p(el)) * gm;
      er = I.muffleLP[1][0].p(I.muffleLP[1][1].p(er)) * gm;
      L += el * vol; R += er * vol;
      // cockpit cues (not muffled: they come through the headset)
      if (stall > 0.05f) {
        I.stallPh += (P.stallIsShaker ? 28.f : 1650.f) / sr; I.stallPh -= floorf(I.stallPh);
        float s = P.stallIsShaker ? (I.stallPh < 0.5f ? 1.f : -1.f) * 0.08f * (0.5f + 0.5f * I.nz.w())
                                   : (sinf(2 * PI * I.stallPh) + 0.35f * sinf(6 * PI * I.stallPh)) * 0.07f * (0.75f + 0.25f * sinf(I.motorPh * 2 * PI * 23.f / sr));
        L += s * stall * vol; R += s * stall * vol;
      }
      if (P.gearMoving || P.flapsMoving) {
        float mf = P.gearMoving ? 380.f : 620.f;
        float m = sinf(I.motorPh * 2 * PI * mf / sr) * 0.02f + sinf(I.motorPh * 2 * PI * mf * 2.01f / sr) * 0.01f;
        L += m * vol; R += m * vol;
      }
      // precipitation
      float rain = I.sRain.p(P.rain, kSlow);
      if (rain > 0.01f) {
        float drops = I.rainLP.p(I.rainHP.p(I.nz.w())) * 0.10f * rain * lerpf(0.6f, 1.0f, inter);
        if ((I.nz.s & 0x3FF) < (uint32_t)(rain * 6)) drops += I.nz.w() * 0.25f * rain;
        L += drops * vol; R += drops * vol * 0.9f;
      }
    }
    // one-shots
    float sfx = 0;
    for (auto& s : I.shots) {
      if (!s.active) continue;
      float v; s.active = runShot(s, sr, v);
      bool muffles = s.type == SFX_TOUCHDOWN || s.type == SFX_CRASH || s.type == SFX_THUNDER;
      sfx += v * (muffles ? lerpf(1.f, 0.5f, muff) : 1.f);
    }
    L += sfx * P.sfxVol; R += sfx * P.sfxVol;
    L = tanhf(L * P.master * 1.2f); R = tanhf(R * P.master * 1.2f);
    out[f * 2] = L; out[f * 2 + 1] = R;
  }
}
