// Somnium Volandi - procedural audio engine (engine synthesis, environment, effects, UI)
#pragma once
#include "common.h"
#include "audio_spatial.h"
#include <atomic>
#include <memory>
#include <vector>

enum class AudioSpace { Listener, Cabin, Airframe, World };
enum class AudioSurface { Paved, Grass, Gravel, Snow, Sand, Water };   // (Water: a float plane on a seaplane base's lane)
struct AudioSource {
  AudioSpace space = AudioSpace::World;
  vec3 position, velocity;
  float radius = 12, range = 2500;
  bool propagation = true;
  uint32_t follow = 0; // optional persistent emitter ID, e.g. the UFO
  AudioSurface surface = AudioSurface::Paved;
  float contactSpeed = 40; // tyre spin-up/brush speed, independent of vertical impact energy
};
struct AudioEmitter {
  enum Kind { Engine, Fire, Track };
  uint32_t id = 0;
  Kind kind = Engine;
  vec3 position, velocity, forward = vec3(0, 0, -1);
  int engineType = 2, cylinders = 4, blades = 2;
  float rpm = 1800, n1 = 65, spool = .6f, level = 1, radius = 45;
};

struct AudioParams {
  AudioListener listener;
  vec3 aircraftPosition, aircraftVelocity, cabinEye;
  quat aircraftRotation;
  float wrapPeriod = 0, soundSpeed = 343;
  bool scene = false; // flight or the wreck scene, independent of engine state
  uint32_t sceneId = 0;
  vec3 enginePosition[4], engineDirection[4] = {vec3(0,0,1),vec3(0,0,1),vec3(0,0,1),vec3(0,0,1)};
  float enginePower[4] = {1,1,1,1}, engineRadius = 18, signature = 1;
  vec3 gearPosition = vec3(0,-1,0);
  AudioEmitter emitters[20]; int emitterCount = 0;
  float slip = 0, buffet = 0;
  bool inFlight = false;
  int engineType = 0, engines = 1, cylinders = 4, blades = 2;
  float rpm = 0, maxRpm = 2600, n1 = 0, spool = 0, throttle = 0;
  bool running = false, cranking = false;
  float engineHealth[4] = {1, 1, 1, 1};   // per engine: 1 sound; below it a piston misfires and a turbine's n1 sags (C7)
  float airspeed = 0, groundSpeed = 0; bool onGround = false; float rough = 0;
  AudioSurface groundSurface = AudioSurface::Paved;
  float stallWarn = 0; bool gearMoving = false, flapsMoving = false; float gearDown = 1, flaps = 0;
  bool interior = false, muffled = false;
  float rain = 0, snow = 0, turbulence = 0;
  float master = 0.8f, engineVol = 1.0f, sfxVol = 1.0f;
  bool paused = false;
  bool stallIsShaker = false;
  bool research = false; float nozzle = 0, mach = 0;   // XR-30 research craft voice
  float voiceVol = 0.9f;   // ATC radio voice (headset: not muffled, not faded by the flight / pause mix)
};

enum Sfx { SFX_CLICK = 0, SFX_HOVER, SFX_CHIME, SFX_SUCCESS, SFX_FAIL, SFX_CASH, SFX_TOUCHDOWN, SFX_CRASH, SFX_THUNDER, SFX_BEEP, SFX_GEAR_CLUNK, SFX_AP_DISC, SFX_BOOM, SFX_UFO_ARRIVE, SFX_UFO_LAUGH, SFX_UFO_ZOOM, SFX_FLYBY, SFX_LASER, SFX_PLASMA, SFX_CLOAK, SFX_METAL, SFX_IMPACT, SFX_SPLASH, SFX_REHEAT, SFX_TRANSONIC, SFX_CANNON, SFX_EMP, SFX_HEAVY, SFX_COUNT };

class AudioEngine {
public:
  AudioEngine() = default;
  ~AudioEngine();
  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;
  int sampleRate = 48000;
  void init(int sr);
  void setParams(const AudioParams& p);
  void setView(vec3 position, vec3 right, vec3 up, vec3 forward); // final rendered camera, including debrief orbit
  void trigger(int sfx, float intensity = 1.0f);
  void triggerAt(int sfx, vec3 position, float intensity = 1, vec3 velocity = vec3(), uint32_t follow = 0);
  void touchdown(vec3 position, float intensity, float groundSpeed, AudioSurface surface);
  void triggerLocal(int sfx, vec3 bodyPosition, float intensity = 1);
  void triggerUI(int sfx, float x, float y, float intensity = 1); // normalized screen position (-1..1)
  void triggerSource(int sfx, const AudioSource& source, float intensity = 1);
  void shiftWorld(vec3 delta); // seamless island wrap; preserves active tails and propagation
  void resetWorld(); // new local tangent frame; discard world tails rather than move them incorrectly
  void render(float* out, int frames);  // interleaved stereo, called from the audio thread
  // Radio voice: one assembled transmission at a time at the engine's sample rate (replaces whatever is playing).
  void voicePlay(std::shared_ptr<const std::vector<float>> pcm, vec3 seat = vec3(-.22f, .05f, -.65f));
  void voiceStop();
  bool voiceBusy() const { return voiceActive.load(std::memory_order_relaxed); }
  // Streaming music decoded by the platform into a bounded ring, through the same spatial mixer.
  // A token rejects data from a station that was stopped/replaced during a network read.
  uint32_t musicBegin();
  void musicStop();
  int musicPush(uint32_t token, const float* stereo, int frames);
  int musicBuffered(uint32_t token);
  void musicVolume(float volume); // includes master, matching Radio::setVolume's existing contract
private:
  std::shared_ptr<const std::vector<float>> voicePending; bool voiceNew = false, voiceCut = false;
  std::atomic<bool> voiceActive{false};
  vec3 voiceSeat;
  struct Impl; Impl* impl = nullptr;
  std::atomic_flag lock = ATOMIC_FLAG_INIT;
  AudioParams pending; bool hasPending = false;
  struct Trigger { int type = 0; float intensity = 1; AudioSource source; };
  Trigger trigQ[128]; int trigN = 0;
  vec3 worldShift; bool worldReset = false;
  static constexpr int kMusicFrames = 16384;
  float musicRing[kMusicFrames * 2] = {};
  unsigned musicRead = 0, musicWrite = 0; uint32_t musicToken = 0;
  float musicLevel = 0;
  void acquire() { while (lock.test_and_set(std::memory_order_acquire)) {} }
  void release() { lock.clear(std::memory_order_release); }
};

extern AudioEngine g_audio;
