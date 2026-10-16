// Somnium Volandi - procedural audio synthesis
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
  float phase = 0, propPhase = 0, starterPh = 0; int lastK = -1;
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
  void tick(float sr, float rpm, float load, int cyl, int blades, float propRpm, bool running, bool cranking, float& eng, float& prop, int blockPos, float health = 1.f, float timbre = 1.f) {
    float rps = rpm / 60.f;
    phase += rps / sr;
    if (phase >= 2.f) phase -= 2.f;  // full four-stroke cycle = 2 revolutions
    float interval = 2.f / cyl;
    float c = phase;
    int k = (int)(c / interval);
    float exc = 0;
    if (k != lastK) {
      lastK = k;
      int ci = k % std::min(cyl, 8);
      if (running) {
        float irregular = 1.f + (nz.w() * 0.5f) * (0.25f * smoothstepf(1400.f, 650.f, rpm));
        float amp = (0.48f + 0.52f * load) * cylBias[ci] * irregular;
        // a sick engine drops cylinders: a firing event skipped at random (more the worse it is), the next one louder
        if (health < 0.999f && nz.w() * 0.5f + 0.5f < (1.f - health) * 0.7f) { amp *= 0.08f; popEnv = 1.f; }
        else if (popEnv > 0.5f) { amp *= 1.35f; popEnv = 0; }
        exc = amp;
        noiseEnv = amp * (0.6f + 0.4f * load);
      } else if (cranking) {
        exc = 0.18f * cylBias[ci];  // compression pulses
        noiseEnv = 0.05f;
      }
    }
    if (blockPos == 0) {
      // retune resonators with rpm/load (pipe gets "angrier" with power)
      float f1 = (85.f + rpm * 0.012f)*timbre;
      res[0].set(BP, f1, 1.6f, sr);
      res[1].set(BP, (210.f + 40.f * load)*timbre, 3.0f, sr);
      res[2].set(BP, (560.f + 160.f * load)*timbre, 2.5f, sr);
      res[3].set(BP, 1900.f + 600.f * load, 1.2f, sr);
      mech.set(BP, 3800.f, 2.f, sr);
      whoosh.set(BP, 420.f + propRpm * 0.25f, 0.8f, sr);
      buzzLP.set(LP, 900.f + propRpm * 0.8f, 0.7f, sr);
    }
    float n = nz.w();
    float x = exc * 30.f + n * noiseEnv * 0.9f;
    noiseEnv *= expf(-168.f / sr);
    float y = res[0].p(x) * 1.0f + res[1].p(x) * 0.75f + res[2].p(x) * 0.42f + res[3].p(x) * (0.10f + 0.12f * load);
    y += mech.p(n) * 0.012f * (rpm / 2600.f) * (running ? 1.f : 0.3f);
    eng = tanhf(y * 1.6f) * 0.8f;
    if (cranking && !running) {
      // starter motor: geared whine that sags on each compression stroke
      starterPh += (340.f + 60.f * sinf(phase * PI * cyl)) / sr; starterPh -= floorf(starterPh);
      float saw = starterPh * 2.f - 1.f;
      eng += (saw * 0.6f + sinf(2 * PI * starterPh * 2.f) * 0.3f) * 0.09f * (0.7f + 0.3f * cosf(phase * PI * cyl));
    }
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
  void tick(float sr, bool jet, float n1, float spool, float propRpm, int blades, float& eng, float& prop, int blockPos, float timbre = 1.f) {
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
    float f1 = (jet ? 300.f + 2900.f * nn : 600.f + 3600.f * nn)*timbre;
    ph1 += f1 / sr; ph2 += f1 * 1.47f / sr; ph3 += (jet ? f1 * 0.5f : f1 * 2.03f) / sr;
    ph1 -= floorf(ph1); ph2 -= floorf(ph2); ph3 -= floorf(ph3);
    float whine = sinf(2 * PI * ph1) * 0.6f + sinf(2 * PI * ph2) * 0.25f + sinf(2 * PI * ph3) * 0.2f;
    float whineAmp = (jet ? 0.075f : 0.028f) * smoothstepf(5.f, 60.f, n1) * (1.f - 0.4f * spool * (jet ? 1.f : 0.f));
    float roar = roarLP2.p(roarLP.p(n)) * (jet ? (0.35f + 0.75f * powf(spool, 1.5f)) : (0.12f + 0.25f * spool));
    float rum = rumble.p(n) * (jet ? 1.6f * spool : 0.6f * spool);
    float hs = hiss.p(n) * 0.06f * nn;
    float buzzsaw = 0;
    if (jet && n1 > 82.f) buzzsaw = buzzBP.p(n) * 0.5f * smoothstepf(82.f, 98.f, n1);
    eng = (whine * whineAmp + roar * 1.2f + rum + hs + buzzsaw) * smoothstepf(0.f, 18.f, n1);
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

// XR-30 / XR-40 research craft: a tuned, layered power voice instead of a raw jet roar.
//  - core: a warm harmonic stack (detuned left/right for width) whose pitch rises with spool
//  - fan: soft high whine with two slowly beating partials
//  - sub: deep sine + filtered rumble you feel more than hear
//  - exhaust: band-shaped roar that opens up with thrust; reheat adds a broad, crackling, pulsing roar
//  - hover: rhythmic blade-pass thrum as the XR-40's pods tilt towards the hover
struct ResearchVoice {
  float ph[6] = {}, sub = 0, fanPh[2] = {}, liftPh = 0, vib = 0, crackEnv = 0, pulse = 0, pulseT = 0;
  Noise nz;
  Biquad roarA, roarB, roarHP, abLP, abBP, crackBP, subLP, liftBP, liftLP, airBP, warmL, warmR;
  void init(uint32_t seed = 9119) { nz.s = seed; }
  void tick(float sr, float spool, float ab, float nozzle, float mach, int bp, float& outL, float& outR) {
    if (bp == 0) {
      roarA.set(LP, 300.f + 2400.f * spool * spool, 0.6f, sr);
      roarB.set(LP, 420.f + 2000.f * spool, 0.6f, sr);
      roarHP.set(HP, 45.f, 0.7f, sr);
      abLP.set(LP, 900.f + 900.f * ab, 0.6f, sr);
      abBP.set(BP, 140.f, 0.9f, sr);
      crackBP.set(BP, 2600.f, 1.4f, sr);
      subLP.set(LP, 75.f, 0.8f, sr);
      liftBP.set(BP, 160.f + 90.f * spool, 1.1f, sr);
      liftLP.set(LP, 700.f, 0.7f, sr);
      airBP.set(BP, 600.f + 900.f * std::min(mach, 2.f), 0.5f, sr);
      warmL.set(PK, 220.f, 0.9f, sr, 3.f); warmR.set(PK, 220.f, 0.9f, sr, 3.f);
    }
    float n = nz.w(), n2 = nz.w();
    vib += 4.7f / sr; vib -= floorf(vib);
    float f0 = (72.f + 230.f * spool) * (1.f + 0.003f * sinf(2 * PI * vib));
    // harmonic stack: 1, 2, 3, 4.5 (a gentle inharmonic partial gives it a synthetic "drive" character)
    const float mult[6] = {1.f, 1.003f, 2.f, 2.996f, 4.5f, 6.02f};
    const float amp[6] = {0.32f, 0.32f, 0.16f, 0.1f, 0.05f, 0.025f};
    float coreL = 0, coreR = 0;
    for (int i = 0; i < 6; i++) {
      ph[i] += f0 * mult[i] / sr; ph[i] -= floorf(ph[i]);
      float s = sinf(2 * PI * ph[i]) * amp[i];
      if (i & 1) coreR += s; else coreL += s;
      if (i == 0 || i == 2) coreR += s * 0.6f; else coreL += s * 0.6f;
    }
    float coreAmp = 0.18f + 0.32f * spool;
    // fan whine
    float ff = 900.f + 3200.f * spool;
    fanPh[0] += ff / sr; fanPh[1] += ff * 1.5013f / sr; fanPh[0] -= floorf(fanPh[0]); fanPh[1] -= floorf(fanPh[1]);
    float fan = (sinf(2 * PI * fanPh[0]) * 0.6f + sinf(2 * PI * fanPh[1]) * 0.4f) * (0.025f + 0.03f * spool) * (1.f - 0.5f * ab);
    // sub
    sub += (34.f + 18.f * spool) / sr; sub -= floorf(sub);
    float subV = sinf(2 * PI * sub) * (0.12f + 0.3f * spool) + subLP.p(n) * (0.6f + 1.4f * spool);
    // exhaust roar
    float roar = roarHP.p(roarB.p(roarA.p(n))) * (0.15f + 0.9f * powf(spool, 1.5f));
    // reheat: broad roar with slow random pulsing and crackle
    float abV = 0;
    if (ab > 0.01f) {
      pulseT -= 1.f / sr;
      if (pulseT <= 0) { pulseT = 0.05f + 0.1f * (0.5f + 0.5f * nz.w()); pulse = 0.75f + 0.25f * nz.w(); }
      if ((nz.s & 0x7FF) < 3) crackEnv = 1.f;
      crackEnv *= expf(-240.f/sr);
      abV = (abLP.p(n2) * 1.3f * pulse + abBP.p(n2) * 1.8f + crackBP.p(n) * crackEnv * 0.35f) * ab;
    }
    // the XR-40's pods in the hover
    float lift = 0;
    if (nozzle > 0.05f) {
      liftPh += (55.f + 30.f * spool) / sr; liftPh -= floorf(liftPh);
      float blade = 0.55f + 0.45f * powf(0.5f + 0.5f * cosf(2 * PI * liftPh), 3.f);
      lift = (liftBP.p(n2) * 1.2f + liftLP.p(n) * 0.25f) * blade * nozzle * (0.3f + 0.7f * spool);
    }
    float air = airBP.p(n) * clampf(mach, 0.f, 2.5f) * 0.12f;
    float common = subV * 0.9f + roar * 0.75f + abV + lift + air + fan;
    outL = tanhf(warmL.p(coreL * coreAmp + common) * 1.3f) * 0.85f;
    outR = tanhf(warmR.p(coreR * coreAmp + common * 0.97f) * 1.3f) * 0.85f;
  }
};

struct OneShot {
  int type = 0; float t = 0, intensity = 1, ph = 0, ph2 = 0;
  Biquad f1, f2, f3, f4; Noise nz; bool active = false, arrived = false;
  AudioSource source; AudioBinaural spatial;
  float age = 0, pitch = 1, variant = 1, size = 1, waveRadius = 0;
};
struct AmbientVoice {
  uint32_t id = 0; AudioEmitter emitter; float fade = 0;
  PistonVoice piston; TurbineVoice turbine; Noise noise; Biquad fireLP, crackBP;
  AudioBinaural spatial; float pitch = 1, flame = 0; bool seen = false;
};
} // namespace

struct AudioEngine::Impl {
  float sr = 48000;
  AudioParams P;
  PistonVoice pv[4]; TurbineVoice tv[4]; ResearchVoice rv[4];
  AudioBinaural engines[4], wind[2], roll[2], rain[2], mechanisms[2], stall, voiceSpatial, music[2];
  float enginePitch[4] = {1,1,1,1};
  Smooth sRpm, sN1, sSpool, sAir, sGs, sLoad, sRain, sMuffle, sInterior, sVol, sStall, sGear, sFlap, sDuck;
  Noise nz[2]; Biquad windBP[2], windLP[2], buffLP[2], rollLP[2], rollGrit[2], rainLP[2], engineLP[4];
  float stallPh = 0, motorPh = 0, gustPhase = 0, gearPh = 0, flapPh = 0;
  unsigned block = 0;
  OneShot shots[64]; AmbientVoice ambient[20]; Rng variants{0x7b13a279};
  std::shared_ptr<const std::vector<float>> voice; size_t voicePos = 0;
  vec3 voiceSeat = vec3(-.22f,.05f,-.65f); float voiceFade = 0, voiceTail = 0, voiceLast = 0;
  float limiter = 1, dcIn[2] = {}, dcOut[2] = {}, musicGain = 0;
  Smooth sMusic;
};

AudioEngine::~AudioEngine() { delete impl; }
void AudioEngine::init(int sr) {
  // Called before the output thread starts (or by the offline harness).
  delete impl; impl = new Impl(); sampleRate = std::clamp(sr, 8000, 192000); impl->sr = float(sampleRate);
  for (int i = 0; i < 4; ++i) { impl->pv[i].init(1234 + i * 97531); impl->tv[i].init(555 + i * 31337); impl->rv[i].init(9119 + i * 8191); }
  impl->nz[0].s = 7471; impl->nz[1].s = 87319;
  acquire(); trigN = 0; hasPending = false; voiceNew = voiceCut = false; voicePending.reset(); voiceActive.store(false);
  musicRead = musicWrite = 0; ++musicToken; worldReset = false; worldShift = vec3(); release();
}
void AudioEngine::setParams(const AudioParams& p) { acquire(); pending = p; hasPending = true; release(); }
void AudioEngine::setView(vec3 position, vec3 right, vec3 up, vec3 forward) {
  acquire(); pending.listener.position=position; pending.listener.right=right;
  pending.listener.up=up; pending.listener.forward=forward; hasPending=true; release();
}
void AudioEngine::voicePlay(std::shared_ptr<const std::vector<float>> pcm, vec3 seat) {
  acquire(); voicePending = std::move(pcm); voiceSeat = seat; voiceNew = true; voiceCut = false;
  voiceActive.store(voicePending && !voicePending->empty()); release();
}
void AudioEngine::voiceStop() { acquire(); voicePending.reset(); voiceNew = false; voiceCut = true; voiceActive.store(false); release(); }
void AudioEngine::shiftWorld(vec3 d) { acquire(); worldShift += d; release(); }
void AudioEngine::resetWorld() { acquire(); worldReset = true; release(); }
uint32_t AudioEngine::musicBegin() { acquire(); ++musicToken; musicRead = musicWrite = 0; uint32_t t = musicToken; release(); return t; }
void AudioEngine::musicStop() { musicBegin(); }
void AudioEngine::musicVolume(float v) { acquire(); musicLevel = clampf(v, 0, 1); release(); }
int AudioEngine::musicBuffered(uint32_t token) { acquire(); int n = token == musicToken ? int(musicWrite - musicRead) : -1; release(); return n; }
int AudioEngine::musicPush(uint32_t token, const float* data, int frames) {
  if (!data || frames <= 0) return 0;
  acquire(); int n = token == musicToken ? std::min(frames, kMusicFrames - int(musicWrite - musicRead)) : 0;
  for (int i = 0; i < n; ++i) {
    unsigned k = (musicWrite++ % kMusicFrames) * 2;
    for(int c=0;c<2;++c) { // reject non-finite decoder output even in /fp:fast builds
      float v=data[i*2+c]; uint32_t bits; memcpy(&bits,&v,sizeof bits);
      musicRing[k+c]=(bits&0x7f800000u)==0x7f800000u?0.f:clampf(v,-4.f,4.f);
    }
  }
  release(); return n;
}

static AudioSource effectSource(int type, vec3 position, vec3 velocity) {
  AudioSource s; s.position = position; s.velocity = velocity;
  switch (type) {
    case SFX_CRASH: s.radius = 70; s.range = 6000; break;
    case SFX_BOOM: s.radius = 160; s.range = 9000; s.propagation = false; break; // already at observer's Mach-cone crossing
    case SFX_THUNDER: s.radius = 350; s.range = 15000; break;
    case SFX_PLASMA: s.radius = 140; s.range = 8000; break;
    case SFX_SPLASH: s.radius = 20; s.range = 1800; break;
    case SFX_METAL: case SFX_IMPACT: s.radius = 12; s.range = 1600; break;
    case SFX_FLYBY: s.radius = 35; s.range = 3200; break;
    case SFX_UFO_ARRIVE: case SFX_UFO_ZOOM: case SFX_UFO_LAUGH: s.radius = 40; s.range = 4000; break;
    case SFX_LASER: case SFX_CANNON: case SFX_EMP: case SFX_HEAVY: s.radius = 20; s.range = 2500; break;
    default: s.radius = 5; s.range = 650; break;
  }
  return s;
}
void AudioEngine::triggerSource(int type, const AudioSource& source, float intensity) {
  if (type < 0 || type >= SFX_COUNT || !std::isfinite(intensity) || intensity <= 0) return;
  acquire();
  if (trigN < 128) trigQ[trigN++] = {type, clampf(intensity, 0, 2), source};
  release();
}
void AudioEngine::trigger(int type, float intensity) {
  // UI and headset cues have near-field virtual emitters, never a mono bypass.
  AudioSource source; source.space = AudioSpace::Listener; source.position = vec3(-.16f, .06f, -.8f);
  source.radius = 1; source.range = 10; source.propagation = false;
  if (type == SFX_BEEP || type == SFX_AP_DISC) { source.space = AudioSpace::Cabin; source.position = vec3(-.28f, -.18f, -.65f); }
  else if(type>=SFX_TOUCHDOWN) {source=effectSource(type,vec3(),vec3());source.space=AudioSpace::Airframe;source.propagation=false;}
  triggerSource(type, source, intensity);
}
void AudioEngine::triggerAt(int type, vec3 p, float intensity, vec3 v, uint32_t follow) {
  auto s = effectSource(type, p, v); s.follow = follow; triggerSource(type, s, intensity);
}
void AudioEngine::touchdown(vec3 p, float intensity, float speed, AudioSurface surface) {
  auto s=effectSource(SFX_TOUCHDOWN,p,vec3());s.radius=8;s.range=1000;
  s.surface=surface;s.contactSpeed=clampf(speed,0,200);triggerSource(SFX_TOUCHDOWN,s,intensity);
}
void AudioEngine::triggerLocal(int type, vec3 p, float intensity) {
  auto s = effectSource(type, p, vec3()); s.space = AudioSpace::Airframe; s.propagation = false; triggerSource(type, s, intensity);
}
void AudioEngine::triggerUI(int type, float x, float y, float intensity) {
  AudioSource s; s.space = AudioSpace::Listener; s.position = vec3(clampf(x,-1,1)*.7f, clampf(y,-1,1)*.4f, -.9f);
  s.radius = 2; s.range = 10; s.propagation = false; triggerSource(type, s, intensity);
}

static void startShot(OneShot& s, int type, float inten, float sr, const AudioSource& source, Rng& rng) {
  s = OneShot(); s.type = type; s.intensity = inten; s.active = true; s.nz.s = rng.next(); s.source = source;
  s.variant = rng.range(.86f, 1.15f); s.size = rng.range(.8f, 1.25f);
  s.arrived = !source.propagation || source.space != AudioSpace::World;
  // Independent noise, resonances, pitch and envelope for each physical event.
  if (type >= SFX_TOUCHDOWN && type != SFX_BEEP && type != SFX_AP_DISC) sr /= s.variant;
  switch (type) {
    case SFX_TOUCHDOWN: {
      const float band=source.surface==AudioSurface::Gravel?3000.f:source.surface==AudioSurface::Grass?750.f:
        source.surface==AudioSurface::Snow?420.f:source.surface==AudioSurface::Sand?1100.f:1700.f;
      s.f1.set(BP,band,source.surface==AudioSurface::Paved?1.4f:.65f,sr);s.f2.set(LP,90,.8f,sr);break; }
    case SFX_CRASH: s.f1.set(LP, 900, 0.7f, sr); s.f2.set(BP, 2500, 1.0f, sr); break;
    case SFX_THUNDER: s.f1.set(LP, 160, 0.7f, sr); s.f2.set(LP, 60, 0.7f, sr); break;
    case SFX_BOOM: s.f1.set(LP, 240, 0.7f, sr); s.f2.set(LP, 75, 0.8f, sr); s.f3.set(HP, 26, 0.7f, sr); s.f4.set(HP, 26, 0.7f, sr); break;
    case SFX_CASH: s.f1.set(BP, 5000, 2.0f, sr); break;
    case SFX_UFO_ZOOM: s.f1.set(BP, 1800, 0.8f, sr); break;
    case SFX_FLYBY: s.f1.set(BP, 2200, 0.7f, sr); s.f2.set(LP, 320, 0.7f, sr); s.f3.set(BP, 900, 0.9f, sr); break;
    case SFX_LASER: s.f1.set(BP, 3200, 1.2f, sr); break;
    case SFX_PLASMA: s.f1.set(LP, 180, 0.7f, sr); s.f2.set(LP, 60, 0.8f, sr); s.f3.set(HP, 26, 0.7f, sr); s.f4.set(BP, 1400, 0.8f, sr); break;
    case SFX_CLOAK: s.f1.set(BP, 2500, 0.6f, sr); break;
    case SFX_GEAR_CLUNK: s.f1.set(LP, 300, 1.0f, sr); break;
    case SFX_HEAVY: s.f1.set(LP,350,.7f,sr); s.f2.set(BP,1500,.8f,sr); break;
    case SFX_METAL: s.f1.set(BP, 780, 3.5f, sr); s.f2.set(BP, 1930, 5.f, sr); s.f3.set(LP, 5000, .6f, sr); break;
    case SFX_IMPACT: s.f1.set(LP, 270, .7f, sr); s.f2.set(BP, 1100, 1.6f, sr); break;
    case SFX_SPLASH: s.f1.set(LP, 2300, .6f, sr); s.f2.set(BP, 430, 1.3f, sr); break;
    case SFX_REHEAT: s.f1.set(LP, 650, .7f, sr); s.f2.set(BP, 160, .8f, sr); break;
    case SFX_TRANSONIC: s.f1.set(LP, 120, .7f, sr); break;
    case SFX_CANNON: s.f1.set(LP, 2000, .7f, sr); s.f2.set(BP, 200, 1.8f, sr); break;
    case SFX_EMP: s.f1.set(BP, 1700, .8f, sr); s.f2.set(LP, 240, .7f, sr); break;
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
      // Only a paved surface squeals. Loose surfaces brush/crunch, and a
      // stationary vertical landing still has strut impact without tyre chirp.
      const bool paved=s.source.surface==AudioSurface::Paved;
      float speed=smoothstepf(1.f,30.f,s.source.contactSpeed);
      float chirp = s.f1.p(s.nz.w()) * expf(-t * (paved?9.f:15.f)) * smoothstepf(0,0.01f,t) * speed * (paved?1.2f:.55f);
      const float body=s.source.surface==AudioSurface::Gravel?68.f:s.source.surface==AudioSurface::Grass?46.f:
        s.source.surface==AudioSurface::Snow?36.f:s.source.surface==AudioSurface::Sand?41.f:55.f;
      const float soft=s.source.surface==AudioSurface::Snow?.6f:s.source.surface==AudioSurface::Sand?.7f:
        s.source.surface==AudioSurface::Grass?.8f:1.f;
      float thump = (s.f2.p(s.nz.w()) * 3.f + sinf(2 * PI * body * t) * 0.5f) * expf(-t * (paved?14.f:10.f)) * soft;
      v = chirp + thump; alive = t < 0.8f; break; }
    case SFX_CRASH: {
      float env = expf(-t * (1.1f / s.size)) * smoothstepf(0, 0.01f, t);
      v = (s.f1.p(s.nz.w()) * 2.5f + s.f2.p(s.nz.w()) * 0.6f * expf(-t * 4.f)) * env + sinf(2 * PI * (48.f * s.variant - 9.f * t) * t) * expf(-t * 3) * 0.6f;
      alive = t < 4.f; break; }
    case SFX_THUNDER: {
      float env = smoothstepf(0, 0.15f, t) * expf(-t * 0.7f) * (0.6f + 0.4f * sinf(t * 7.f) * sinf(t * 2.3f));
      v = (s.f1.p(s.nz.w()) * 3.f + s.f2.p(s.nz.w()) * 4.f) * env; alive = t < 6.f; break; }
    case SFX_BOOM: {  // sonic boom: a deep double "ba-boom" (long bow and tail shock N-waves, low-passed) and a rolling rumble
      auto nwave = [&](float t0, float len) { float tt = t - t0; if (tt < 0 || tt > len) return 0.f; return (1.f - 2.f * tt / len) * smoothstepf(0, 0.012f, tt) * smoothstepf(len, len - 0.01f, tt); };
      float separation = .12f * s.size;
      float shock = nwave(0.0f, .055f * s.size) + nwave(separation, .055f * s.size) * .85f;
      float body = s.f1.p(shock * 3.5f + s.nz.w() * expf(-t * 8.f) * 0.12f);
      auto thump = [&](float t0, float f, float a) { float tt = t - t0; if (tt < 0) return 0.f;
        float ph = 2 * PI * (f - 6.f * tt) * tt;
        return a * (sinf(ph) + 0.45f * sinf(2.f * ph) * expf(-tt * 4.f)) * expf(-tt * 2.4f) * smoothstepf(0, 0.025f, tt); };
      float sub = thump(0.f, 36.f, 0.9f) + thump(separation, 31.f * s.variant, 0.75f);
      float rumble = s.f2.p(s.nz.w()) * 7.f * expf(-t * 0.9f) * smoothstepf(0.05f, 0.4f, t);
      v = s.f4.p(s.f3.p(body * 2.6f + sub * 0.8f + rumble));   // 24 dB/oct high-pass: no wasted infrasound
      alive = t < 5.f; break; }
    case SFX_UFO_ARRIVE: {   // theremin warble over a low hum
      float env = smoothstepf(0.f, 0.4f, t) * smoothstepf(3.2f, 2.2f, t);
      float f = 520.f + 180.f * sinf(2 * PI * 5.5f * t) + 120.f * sinf(2 * PI * 0.6f * t);
      s.ph += 2 * PI * f * dt; s.ph2 += 2 * PI * 68.f * dt;
      v = (sinf(s.ph) * 0.16f + (sinf(s.ph2) + 0.4f * sinf(s.ph2 * 2.01f)) * 0.12f) * env; alive = t < 3.3f; break; }
    case SFX_UFO_LAUGH: {    // two squeaky voices giggling "hee-hee-hee"
      float syl = 0.15f; int k = (int)(t / syl); float u = fmodf(t, syl) / syl;
      float voice = (k % 2) ? 1.22f : 1.f;
      float f = (820.f + 650.f * u) * voice * (1.f + 0.04f * sinf(2 * PI * 30.f * t));
      float env = sinf(PI * clampf(u * 1.25f, 0.f, 1.f)) * (k < 12 ? 1.f : 0.f) * (1.f - 0.04f * k);
      s.ph += 2 * PI * f * dt;
      float saw = fmodf(s.ph / (2 * PI), 1.f) * 2.f - 1.f;
      v = (sinf(s.ph) * 0.6f + saw * 0.25f) * env * 0.16f; alive = t < 1.9f; break; }
    case SFX_UFO_ZOOM: {     // rising whistle and a whoosh as it shoots away
      float f = 300.f * powf(14.f, clampf(t / 1.6f, 0.f, 1.f)) * (1.f + 0.03f * sinf(2 * PI * 9.f * t));
      s.ph += 2 * PI * f * dt;
      float env = smoothstepf(0.f, 0.08f, t) * smoothstepf(2.4f, 1.0f, t);
      v = (sinf(s.ph) * 0.18f + s.f1.p(s.nz.w()) * 0.5f * smoothstepf(1.8f, 0.2f, t)) * env; alive = t < 2.5f; break; }
    case SFX_FLYBY: {        // jet fly-by: rising tearing roar, a doppler drop in the turbine whine as it passes, rumbling away
      float tp = 0.9f, x = t - tp;
      float env = expf(-x * x * (x < 0 ? 5.f : 1.6f)) * smoothstepf(0.f, 0.15f, t);
      float dop = 1.f; // Doppler comes from the source/listener velocities in the spatial bus.          // pitch ratio: high on approach, low going away
      s.ph += 2 * PI * 1900.f * dop * dt; s.ph2 += 2 * PI * 3100.f * dop * dt;
      float hiss = s.f1.p(s.nz.w()) * (x < 0 ? 1.4f : 0.6f), roar = s.f2.p(s.nz.w()) * 3.2f, mid = s.f3.p(s.nz.w()) * 1.1f;
      float whine = (sinf(s.ph) * 0.05f + sinf(s.ph2) * 0.03f) * (x < 0.2f ? 1.f : 0.5f);
      v = (hiss + roar + mid + whine) * env * 0.55f; alive = t < 4.5f; break; }
    case SFX_LASER: {        // pulse laser: a bright descending zap over a crackle of discharge
      float f = 2600.f * expf(-t * 18.f) + 380.f;
      s.ph += 2 * PI * f * dt; s.ph2 += 2 * PI * f * 1.51f * dt;
      float env = smoothstepf(0.f, 0.004f, t) * expf(-t * 22.f);
      float sq = sinf(s.ph) + .24f * sinf(3*s.ph); // band-limited pulse, no raw square-wave aliasing
      v = (sq * 0.35f + sinf(s.ph2) * 0.4f + s.f1.p(s.nz.w()) * 0.6f * expf(-t * 40.f)) * env * 0.32f; alive = t < 0.3f; break; }
    case SFX_PLASMA: {       // dark-energy detonation: an inhaled rising whine, then a deep implosion-thump and a long rolling roar
      float suck = smoothstepf(0.f, 0.35f, t) * smoothstepf(0.45f, 0.38f, t);
      float fw = 200.f + 2400.f * smoothstepf(0.f, 0.42f, t);
      s.ph += 2 * PI * fw * dt;
      float whine = sinf(s.ph) * suck * 0.12f + s.f4.p(s.nz.w()) * suck * 0.5f;
      float tt = t - 0.42f;
      float thump = tt > 0 ? sinf(2 * PI * (42.f - 10.f * tt) * tt) * expf(-tt * 3.f) * smoothstepf(0.f, 0.02f, tt) : 0.f;
      float roar = tt > 0 ? (s.f1.p(s.nz.w()) * 5.f + s.f2.p(s.nz.w()) * 7.f) * expf(-tt * 0.7f) * smoothstepf(0.f, 0.08f, tt) : 0.f;
      v = (whine + s.f3.p(thump * 1.4f + roar)); alive = t < 6.f; break; }
    case SFX_CLOAK: {        // cloak: a shimmering, phasing sweep
      float env = smoothstepf(0.f, 0.15f, t) * smoothstepf(1.4f, 0.6f, t);
      float f = (s.intensity > 0.85f ? 300.f + 900.f * t : 1200.f - 700.f * t);
      s.ph += 2 * PI * f * dt; s.ph2 += 2 * PI * f * 1.007f * dt;
      v = ((sinf(s.ph) + sinf(s.ph2)) * 0.08f * (0.6f + 0.4f * sinf(t * 40.f)) + s.f1.p(s.nz.w()) * 0.25f) * env; alive = t < 1.5f; break; }
    case SFX_METAL: {
      float n = s.nz.w(), scrape = s.f3.p(n) * expf(-t * 12.f) * .22f;
      float ring = (s.f1.p(n) * 1.7f + s.f2.p(n) * .8f) * expf(-t * 5.f / s.size);
      v = (ring + scrape) * smoothstepf(0,.002f,t); alive = t < 1.5f; break; }
    case SFX_IMPACT: {
      v = (s.f1.p(s.nz.w()) * 2.2f + s.f2.p(s.nz.w()) * .4f * expf(-t*14)) * expf(-t*9.f/s.size)
          + sinf(2*PI*(83*s.variant-24*t)*t)*.28f*expf(-t*18);
      alive = t < 1.1f; break; }
    case SFX_SPLASH: {
      float env = smoothstepf(0,.012f,t)*expf(-t*2.8f/s.size);
      float spray = s.f1.p(s.nz.w())*(.35f+.14f*sinf(t*51));
      float water = s.f2.p(s.nz.w())*.65f + sinf(2*PI*(130*expf(-t*3)+45)*t)*.1f;
      v = (spray+water)*env; alive = t < 2.6f; break; }
    case SFX_REHEAT:
      v = (s.f1.p(s.nz.w())*.9f+s.f2.p(s.nz.w())*.7f)*smoothstepf(0,.025f,t)*expf(-t*5);
      alive=t<1.2f; break;
    case SFX_TRANSONIC:
      v = s.f1.p(s.nz.w())*.8f*smoothstepf(0,.08f,t)*expf(-t*4)*(1+.25f*sinf(t*90)); alive=t<1.1f; break;
    case SFX_CANNON:
      v = (s.f1.p(s.nz.w())*.45f+s.f2.p(s.nz.w())*1.8f)*expf(-t*32)+sinf(2*PI*110*t)*.22f*expf(-t*22);
      alive=t<.4f; break;
    case SFX_EMP:
      s.ph += 2*PI*(600+2200*expf(-t*8))*dt;
      v = (s.f1.p(s.nz.w())*.3f+s.f2.p(s.nz.w())*.8f+sinf(s.ph)*.15f)*smoothstepf(0,.003f,t)*expf(-t*5);
      alive=t<1.6f; break;
    case SFX_HEAVY:
      s.ph += 2*PI*(75+1900*expf(-t*28))*dt;
      v=(sinf(s.ph)*.25f+s.f1.p(s.nz.w())*.9f+s.f2.p(s.nz.w())*.18f)*expf(-t*6.f);
      alive=t<1.4f; break;
    default: alive = false;
  }
  s.t += dt;
  out = v * smoothstepf(0.f, .0015f, t);
  return alive;
}

namespace {
AudioSpatialCue sourceCue(const AudioParams& p, const AudioSource& s, float age = 0) {
  vec3 pos = s.position, vel = s.velocity;
  AudioListener listener = p.listener;
  if (s.space == AudioSpace::Listener || (s.space == AudioSpace::Cabin && !p.interior)) {
    listener = AudioListener(); // source is already in head coordinates, +z behind
  } else if (s.space == AudioSpace::Airframe || s.space == AudioSpace::Cabin) {
    if(s.space == AudioSpace::Cabin) pos+=p.cabinEye;
    pos = p.aircraftPosition + p.aircraftRotation.rotate(pos); vel = p.aircraftVelocity;
  } else {
    bool tracked = false;
    if (s.follow) for (int i = 0; i < std::clamp(p.emitterCount,0,20); ++i) if (p.emitters[i].id == s.follow) {
      pos = p.emitters[i].position; vel = p.emitters[i].velocity; tracked = true; break;
    }
    if (!tracked) pos += vel * std::min(age, 3.f);
  }
  return audioSpatialCue(listener, pos, vel, s.radius, s.range,
                          s.space == AudioSpace::Listener ? 0 : p.wrapPeriod, p.soundSpeed);
}
AudioSource attached(vec3 p, float radius = 5, float range = 2500) {
  AudioSource s; s.space = AudioSpace::Airframe; s.position = p; s.radius = radius; s.range = range; s.propagation = false; return s;
}
AudioSource headset(const AudioParams& p, vec3 seat) {
  auto s = attached(p.cabinEye + seat, 2, 20);
  if (!p.interior) { s.space = AudioSpace::Listener; s.position = seat; }
  return s;
}
}

void AudioEngine::render(float* out, int frames) {
  if (!out || frames <= 0) return;
  if (!impl) { memset(out, 0, sizeof(float) * size_t(frames) * 2); return; }
  // Bound stack/ring copies even if an offline caller requests several seconds at once.
  if (frames > 4096) { for (int i = 0; i < frames; i += 4096) render(out + i*2, std::min(4096, frames-i)); return; }
  Impl& I = *impl; float musicBlock[8192] = {}; int musicN = 0;
  // Never spin the realtime thread behind a UI/network producer. Keep the last snapshot for one buffer if busy.
  if (!lock.test_and_set(std::memory_order_acquire)) {
    bool clear = worldReset;
    if (hasPending) { clear |= pending.sceneId != I.P.sceneId || (I.P.scene && !pending.scene) || (!I.P.paused && pending.paused); I.P = pending; hasPending = false; }
    if (clear) { for (auto& s : I.shots) if (s.source.space != AudioSpace::Listener) s.active = false; for (auto& a : I.ambient) a.id = 0; }
    worldReset = false;
    if (length(worldShift) > 0) for (auto& s : I.shots) if (s.source.space == AudioSpace::World) s.source.position += worldShift;
    worldShift = vec3();
    for (int i = 0; i < trigN; ++i) {
      const auto& e = trigQ[i];
      if ((I.P.paused || !I.P.scene) && e.source.space == AudioSpace::World) continue;
      OneShot* slot = nullptr; bool duplicate = false;
      for (auto& s : I.shots) {
        if (!s.active) { if (!slot) slot = &s; continue; }
        // Coalesce only the same event at the same place in a 35ms window; separate parts remain distinct.
        if (s.type == e.type && s.age < .035f && s.source.space == e.source.space && length(s.source.position-e.source.position) < 1.25f) {
          s.intensity = std::max(s.intensity, e.intensity); duplicate = true; break;
        }
      }
      if (duplicate) continue;
      if (!slot) { // Steal an inaudible/quiet tail, not the first (often loudest) impact or a warning.
        float quiet = 1e9f;
        for (auto& s : I.shots) if (s.type != SFX_AP_DISC && s.type != SFX_BEEP) {
          float score = sourceCue(I.P, s.source, s.age).gain * s.intensity / (1+s.t*2);
          if (score < quiet) { quiet=score; slot=&s; }
        }
        if (slot && quiet > sourceCue(I.P,e.source).gain*e.intensity) slot=nullptr;
      }
      if (slot) startShot(*slot, e.type, e.intensity, I.sr, e.source, I.variants);
    }
    trigN = 0;
    if (voiceNew || voiceCut) { I.voiceTail = I.voiceLast; I.voiceFade = 0; }
    if (voiceNew) { I.voice=std::move(voicePending); I.voiceSeat=voiceSeat; I.voicePos=0; voiceNew=false; }
    if (voiceCut) { I.voice.reset(); voiceCut=false; }
    musicN = std::min(frames, int(musicWrite-musicRead));
    for (int i=0; i<musicN; ++i) { unsigned k=(musicRead++ % kMusicFrames)*2; musicBlock[i*2]=musicRing[k]; musicBlock[i*2+1]=musicRing[k+1]; }
    I.musicGain = musicLevel;
    release();
  }
  const AudioParams& P=I.P; float sr=I.sr;
  const float k=1-expf(-1/(.04f*sr)), slow=1-expf(-1/(.18f*sr));
  for (auto& a : I.ambient) a.seen=false;
  for (int n=0; n<std::clamp(P.emitterCount,0,20); ++n) {
    const auto& e=P.emitters[n]; if (!e.id || e.kind==AudioEmitter::Track) continue;
    AmbientVoice* slot=nullptr;
    for (auto& a:I.ambient) if(a.id==e.id) {slot=&a;break;}
    if(!slot) for(auto& a:I.ambient) if(!a.id || a.fade<.0001f) {slot=&a; *slot=AmbientVoice(); slot->id=e.id; slot->piston.init(e.id*7919+3); slot->turbine.init(e.id*373+1); slot->noise.s=e.id*191+17; break;}
    if(slot) {slot->emitter=e;slot->seen=true;}
  }
  for(int f=0; f<frames; ++f) {
    int bp=I.block++ & 63;
    float rpm=I.sRpm.p(P.rpm,k), n1=I.sN1.p(P.n1,k), spool=I.sSpool.p(P.spool,k);
    float load=I.sLoad.p(clampf(P.throttle*.6f+P.spool*.4f,0,1),k);
    float air=I.sAir.p(P.airspeed,k), gs=I.sGs.p(P.onGround?P.groundSpeed:0.f,k);
    float inter=I.sInterior.p(P.interior?1.f:0.f,slow), muff=I.sMuffle.p(P.muffled?1.f:0.f,slow);
    float world=I.sVol.p(P.scene && !P.paused?1.f:0.f,slow);
    float duck=I.sDuck.p(I.voice?1.f:0.f, I.voice?k:slow);
    float L=0,R=0;
    int ne=std::clamp(P.engines,0,4);
    if(bp==0) {
      for(int e=0;e<4;++e) {
        auto cue=sourceCue(P,attached(P.enginePosition[e],P.engineRadius,6500));
        vec3 toListener=audioDisplacement(P.aircraftPosition+P.aircraftRotation.rotate(P.enginePosition[e]),P.listener.position,P.wrapPeriod);
        float aft=dot(normalize(toListener),P.aircraftRotation.rotate(P.engineDirection[e]));
        float direct=P.interior?1.f:lerpf(.65f,1.15f,.5f+.5f*aft);
        // Summed engine power stays comparable across single/twin/quad aircraft; each retains its own source.
        float level=P.inFlight && e<ne ? P.engineVol*.65f/std::sqrt(float(std::max(1,ne)))*direct : 0;
        I.engines[e].target(cue,level*(1-.3f*duck)*(1-.5f*muff),sr,clampf(.45f*inter+.42f*muff,0,.85f));
        I.enginePitch[e]=cue.pitch;
        I.engineLP[e].set(LP,lerpf(15000.f,3200.f,inter),.707f,sr);
      }
      for(int c=0;c<2;++c) {
        float side=c?1.f:-1.f;
        auto windPos=P.cabinEye+vec3(side*.8f,.12f,-.25f);
        I.wind[c].target(sourceCue(P,attached(windPos,4)),P.sfxVol*(1-.35f*duck)*(1-.65f*muff)*lerpf(.65f,.32f,inter),sr,.25f*inter);
        I.roll[c].target(sourceCue(P,attached(P.gearPosition+vec3(side*1.5f,0,0),6)),P.sfxVol*.55f*(1-.45f*muff),sr,.25f*inter);
        I.rain[c].target(sourceCue(P,attached(windPos,4)),P.sfxVol*.35f*(1-.3f*duck),sr,.2f*inter);
        I.windBP[c].set(BP,260+std::min(air,400.f)*4.5f,.55f,sr);
        I.windLP[c].set(LP,1600+std::min(air,400.f)*12,.7f,sr);
        I.buffLP[c].set(BP,46+c*7,.65f,sr);
        const bool gravel=P.groundSurface==AudioSurface::Gravel;
        float band=P.groundSurface==AudioSurface::Snow?220.f:P.groundSurface==AudioSurface::Grass?500.f:gravel?2400.f:850.f;
        I.rollLP[c].set(LP,120+gs*4,.75f,sr);I.rollGrit[c].set(BP,band,.65f,sr);
        I.rainLP[c].set(BP,inter?1800.f:3800.f,.55f,sr);
      }
      I.mechanisms[0].target(sourceCue(P,attached(P.gearPosition,6)),P.sfxVol*.8f,sr,.3f*inter);
      I.mechanisms[1].target(sourceCue(P,attached(vec3(2,0,0),6)),P.sfxVol*.65f,sr,.3f*inter);
      I.stall.target(sourceCue(P,headset(P,vec3(-.35f,-.25f,-.5f))),P.sfxVol,sr);
      I.voiceSpatial.target(sourceCue(P,headset(P,I.voiceSeat)),P.voiceVol*1.15f,sr);
      for(int c=0;c<2;++c) I.music[c].target(sourceCue(P,headset(P,vec3(c?.65f:-.65f,-.12f,-.7f))),.72f,sr);
      for(auto& s:I.shots) if(s.active) {
        auto cue=sourceCue(P,s.source,s.age);
        if(!s.arrived) { // propagation from the emission point, before following any moving tail
          float distance=length(audioDisplacement(P.listener.position,s.source.position,P.wrapPeriod));
          s.arrived=s.waveRadius>=distance;
          if(s.age>45.f || distance>s.source.range) s.active=false;
        }
        bool external=s.source.space!=AudioSpace::Listener;
        s.spatial.target(cue,s.intensity*P.sfxVol*(external?(1-.4f*muff):1),sr,external?.25f*inter+.35f*muff:0);
        s.pitch = s.type==SFX_BOOM || s.type==SFX_THUNDER ? 1.f : cue.pitch;
      }
      for(auto& a:I.ambient) if(a.id) {
        auto& e=a.emitter; auto cue=audioSpatialCue(P.listener,e.position,e.velocity,e.radius,e.kind==AudioEmitter::Fire?1600.f:6500.f,P.wrapPeriod,P.soundSpeed);
        float direct=e.kind==AudioEmitter::Engine ? .8f+.25f*dot(normalize(audioDisplacement(e.position,P.listener.position,P.wrapPeriod)),-e.forward) : 1;
        a.spatial.target(cue,e.level*direct*(e.kind==AudioEmitter::Engine?P.engineVol:P.sfxVol)*(1-.35f*duck),sr,.5f*inter+.25f*muff);
        a.pitch=cue.pitch; a.fireLP.set(LP,580,.65f,sr); a.crackBP.set(BP,2400,.8f,sr);
      }
    }
    if(world>.0001f) {
      for(int e=0;e<ne;++e) {
        float eng=0,prop=0,health=clampf(P.engineHealth[e],0,1), det=1.f+float(e)*.0037f;
        float dsr=sr / I.enginePitch[e], prpm=rpm*det;
        if(P.research) {
          float a,b; float power=clampf(spool*P.enginePower[e]*health,0,1.2f);
          I.rv[e].tick(dsr,power,smoothstepf(.85f,1.f,power),P.nozzle,P.mach,bp,a,b);
          eng=(a+b)*.5f*smoothstepf(0,.12f,power); // no immortal core hum after shutdown/failure
        } else if(P.engineType==0) {
          I.pv[e].tick(dsr,prpm*(health>0?1:0),load,std::clamp(P.cylinders,2,8),std::clamp(P.blades,2,8),prpm,P.running&&health>0,P.cranking,eng,prop,bp,health,P.signature);
          eng*=smoothstepf(0,120,rpm); prop*=smoothstepf(0,120,rpm);
        } else {
          I.tv[e].tick(dsr,P.engineType==2,n1*det*health,spool*health,P.engineType==1?prpm*health:0,std::clamp(P.blades,2,8),eng,prop,bp,P.signature);
        }
        I.engines[e].mix(I.engineLP[e].p(eng+prop*.8f)*world,L,R);
      }
      I.gustPhase += .73f/sr; if(I.gustPhase>=1) I.gustPhase-=1;
      float pressure=std::min(2.1f,powf(std::max(0.f,air)/90.f,1.45f));
      float gust=1+P.turbulence*.4f*sinf(2*PI*I.gustPhase);
      float rain=I.sRain.p(P.rain,slow);
      for(int c=0;c<2;++c) {
        float noise=I.nz[c].w();
        float wind=(I.windBP[c].p(noise)*.28f+I.windLP[c].p(noise)*.08f)*pressure*gust;
        float buffet=I.buffLP[c].p(noise)*(P.buffet*.65f+P.gearDown*.08f+P.flaps*.12f)*pressure;
        float slip=1+(c?1.f:-1.f)*clampf(P.slip,-1,1)*.3f;
        I.wind[c].mix((wind+buffet)*slip*world*(P.inFlight?1:0),L,R);
        float grain=I.nz[c].w();
        float texture=P.groundSurface==AudioSurface::Paved?.012f:P.groundSurface==AudioSurface::Gravel?.20f:
          P.groundSurface==AudioSurface::Snow?.07f:.10f;
        float roll=(I.rollLP[c].p(grain)*(.4f+.9f*P.rough)+I.rollGrit[c].p(grain)*texture)*clampf(gs/30,0,1.3f);
        I.roll[c].mix(roll*world*(P.onGround && P.inFlight?1:0),L,R);
        float drops=I.rainLP[c].p(I.nz[c].w())*.18f*rain;
        I.rain[c].mix(drops*world*(P.inFlight?1:0),L,R);
      }
      float stall=I.sStall.p(P.inFlight?P.stallWarn:0,k);
      I.stallPh+=(P.stallIsShaker?28.f:1650.f)/sr; I.stallPh-=floorf(I.stallPh);
      float warning=P.stallIsShaker?sinf(2*PI*I.stallPh)*(.055f+.035f*I.nz[0].w()):sinf(2*PI*I.stallPh)*.065f;
      I.stall.mix(warning*stall*world,L,R);
      float gear=I.sGear.p(P.gearMoving&&P.inFlight?1.f:0.f,k), flap=I.sFlap.p(P.flapsMoving&&P.inFlight?1.f:0.f,k);
      I.gearPh+=380.f/sr;I.gearPh-=floorf(I.gearPh);I.flapPh+=620.f/sr;I.flapPh-=floorf(I.flapPh);
      I.mechanisms[0].mix((sinf(2*PI*I.gearPh)*.025f+I.nz[0].w()*.006f)*gear*world,L,R);
      I.mechanisms[1].mix(sinf(2*PI*I.flapPh)*.02f*flap*world,L,R);
    }
    for(auto& a:I.ambient) if(a.id) {
      a.fade=approach(a.fade,a.seen?1.f:0.f,12.f,1/sr);
      if(!a.seen && a.fade<.0001f) {a.id=0;continue;}
      float eng=0,prop=0;auto& e=a.emitter;
      if(e.kind==AudioEmitter::Fire) {
        float n=a.noise.w(); if((a.noise.s&8191)<3) a.flame=1; a.flame*=expf(-120/sr);
        eng=a.fireLP.p(n)*.25f+a.crackBP.p(n)*a.flame*.2f;
      } else if(e.engineType==0) a.piston.tick(sr/a.pitch,e.rpm,e.spool,std::clamp(e.cylinders,2,8),std::clamp(e.blades,2,8),e.rpm,true,false,eng,prop,bp);
      else a.turbine.tick(sr/a.pitch,e.engineType==2,e.n1,e.spool,e.engineType==1?e.rpm:0,std::clamp(e.blades,2,8),eng,prop,bp);
      a.spatial.mix((eng+prop*.7f)*a.fade*world*.45f,L,R);
    }
    for(auto& s:I.shots) if(s.active) {
      s.age+=1/sr; s.waveRadius+=P.soundSpeed/sr;
      if(!s.arrived) continue;
      float v=0; s.active=runShot(s,sr/s.pitch,v);
      float gain=(s.source.space==AudioSpace::Listener || (s.source.space==AudioSpace::Cabin && !P.scene))?1.f:world;
      s.spatial.mix(v*gain,L,R);
    }
    float speech=0;
    if(I.voice) {
      if(I.voicePos<I.voice->size()) {
        I.voiceFade=std::min(1.f,I.voiceFade+1/(.006f*sr));
        float remaining=float(I.voice->size()-I.voicePos)/(.006f*sr);
        speech=(*I.voice)[I.voicePos++]*I.voiceFade*std::min(1.f,remaining);
      } else {
        I.voice.reset();
        if (!lock.test_and_set(std::memory_order_acquire)) {if(!voiceNew)voiceActive.store(false);release();}
      }
    } else if(voiceActive.load() && !lock.test_and_set(std::memory_order_acquire)) {if(!voiceNew)voiceActive.store(false);release();}
    I.voiceLast=speech; speech+=I.voiceTail; I.voiceTail*=expf(-1/(.003f*sr));
    I.voiceSpatial.mix(speech,L,R);
    L*=P.master;R*=P.master;
    // Radio::setVolume already includes master/duck. It now goes through the same near-field speakers and limiter.
    float musicGain=I.sMusic.p(I.musicGain,1-expf(-1/(.006f*sr)));
    for(int c=0;c<2;++c) I.music[c].mix(musicBlock[f*2+c]*musicGain,L,R);
    float v[2]={L,R};
    for(int c=0;c<2;++c) {float y=v[c]-I.dcIn[c]+expf(-2*PI*22/sr)*I.dcOut[c];I.dcIn[c]=v[c];I.dcOut[c]=y;v[c]=y;}
    // Stereo-linked peak protection preserves location; ordinary signals remain linear (no constant tanh distortion).
    float peak=std::max(fabsf(v[0]),fabsf(v[1])); float target=peak>.92f?.92f/peak:1;
    I.limiter=target<I.limiter?target:approach(I.limiter,target,5.f,1/sr);
    out[f*2]=v[0]*I.limiter;out[f*2+1]=v[1]*I.limiter;
  }
}
