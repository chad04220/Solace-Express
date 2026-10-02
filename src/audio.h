// Air Xpress - procedural audio engine (engine synthesis, environment, effects, UI)
#pragma once
#include "common.h"
#include <atomic>

struct AudioParams {
  bool inFlight = false;
  int engineType = 0, engines = 1, cylinders = 4, blades = 2;
  float rpm = 0, maxRpm = 2600, n1 = 0, spool = 0, throttle = 0;
  bool running = false, cranking = false;
  float airspeed = 0, groundSpeed = 0; bool onGround = false; float rough = 0;
  float stallWarn = 0; bool gearMoving = false, flapsMoving = false; float gearDown = 1, flaps = 0;
  bool interior = false, muffled = false;
  float rain = 0, snow = 0, turbulence = 0;
  float master = 0.8f, engineVol = 1.0f, sfxVol = 1.0f;
  bool paused = false;
  bool stallIsShaker = false;
  bool research = false; float nozzle = 0, mach = 0;   // XR-9 research craft voice
};

enum Sfx { SFX_CLICK = 0, SFX_HOVER, SFX_CHIME, SFX_SUCCESS, SFX_FAIL, SFX_CASH, SFX_TOUCHDOWN, SFX_CRASH, SFX_THUNDER, SFX_BEEP, SFX_GEAR_CLUNK, SFX_AP_DISC, SFX_BOOM, SFX_UFO_ARRIVE, SFX_UFO_LAUGH, SFX_UFO_ZOOM, SFX_COUNT };

class AudioEngine {
public:
  int sampleRate = 48000;
  void init(int sr);
  void setParams(const AudioParams& p);
  void trigger(int sfx, float intensity = 1.0f);
  void render(float* out, int frames);  // interleaved stereo, called from the audio thread
private:
  struct Impl; Impl* impl = nullptr;
  std::atomic_flag lock = ATOMIC_FLAG_INIT;
  AudioParams pending; bool hasPending = false;
  int trigQ[32]; float trigI[32]; int trigN = 0;
  void acquire() { while (lock.test_and_set(std::memory_order_acquire)) {} }
  void release() { lock.clear(std::memory_order_release); }
};

extern AudioEngine g_audio;
