// Solace Express - procedural audio engine (engine synthesis, environment, effects, UI)
#pragma once
#include "common.h"
#include <atomic>
#include <memory>
#include <vector>

struct AudioParams {
  bool inFlight = false;
  int engineType = 0, engines = 1, cylinders = 4, blades = 2;
  float rpm = 0, maxRpm = 2600, n1 = 0, spool = 0, throttle = 0;
  bool running = false, cranking = false;
  float engineHealth[4] = {1, 1, 1, 1};   // per engine: 1 sound; below it a piston misfires and a turbine's n1 sags (C7)
  float airspeed = 0, groundSpeed = 0; bool onGround = false; float rough = 0;
  float stallWarn = 0; bool gearMoving = false, flapsMoving = false; float gearDown = 1, flaps = 0;
  bool interior = false, muffled = false;
  float rain = 0, snow = 0, turbulence = 0;
  float master = 0.8f, engineVol = 1.0f, sfxVol = 1.0f;
  bool paused = false;
  bool stallIsShaker = false;
  bool research = false; float nozzle = 0, mach = 0;   // XR-30 research craft voice
  float voiceVol = 0.9f;   // ATC radio voice (headset: not muffled, not faded by the flight / pause mix)
};

enum Sfx { SFX_CLICK = 0, SFX_HOVER, SFX_CHIME, SFX_SUCCESS, SFX_FAIL, SFX_CASH, SFX_TOUCHDOWN, SFX_CRASH, SFX_THUNDER, SFX_BEEP, SFX_GEAR_CLUNK, SFX_AP_DISC, SFX_BOOM, SFX_UFO_ARRIVE, SFX_UFO_LAUGH, SFX_UFO_ZOOM, SFX_FLYBY, SFX_LASER, SFX_PLASMA, SFX_CLOAK, SFX_COUNT };

class AudioEngine {
public:
  int sampleRate = 48000;
  void init(int sr);
  void setParams(const AudioParams& p);
  void trigger(int sfx, float intensity = 1.0f);
  void render(float* out, int frames);  // interleaved stereo, called from the audio thread
  // Radio voice: one assembled transmission at a time at the engine's sample rate (replaces whatever is playing).
  void voicePlay(std::shared_ptr<const std::vector<float>> pcm);
  void voiceStop();
  bool voiceBusy() const { return voiceActive.load(std::memory_order_relaxed); }
private:
  std::shared_ptr<const std::vector<float>> voicePending; bool voiceNew = false, voiceCut = false;
  std::atomic<bool> voiceActive{false};
  struct Impl; Impl* impl = nullptr;
  std::atomic_flag lock = ATOMIC_FLAG_INIT;
  AudioParams pending; bool hasPending = false;
  int trigQ[32]; float trigI[32]; int trigN = 0;
  void acquire() { while (lock.test_and_set(std::memory_order_acquire)) {} }
  void release() { lock.clear(std::memory_order_release); }
};

extern AudioEngine g_audio;
