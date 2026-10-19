// Somnium Volandi - the game's own music: a small generative score played through the audio engine's music ring
// (the internet radio's), so the radio and the score never play at once. Moods: the main menu's theme, the hub's
// lighter groove, a quiet ambience for the cruise (Settings), the finale; and short stingers over them (a job done,
// a flight failed, a licence, an achievement, a medal). Everything is synthesised: sine-table pads and bass, FM bells
// and plucks, a soft shaker, a stereo reverb and a gentle limiter
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

enum MusicMood { MOOD_NONE = 0, MOOD_MENU, MOOD_HUB, MOOD_CRUISE, MOOD_FINALE, MOOD_COUNT };
enum MusicSting { STING_SUCCESS = 0, STING_FAIL, STING_LICENCE, STING_ACHIEVEMENT, STING_MEDAL, STING_APPLAUSE, STING_COUNT };

// The generator: the same notes for the same seed, rendered a block at a time (the player's thread, or a test)
class MusicGen {
public:
  explicit MusicGen(float sampleRate = 48000.f, uint32_t seed = 1);
  void setMood(int mood);   // the old mood's notes ring out, the new one starts at once (its pads swell in)
  int mood() const { return moodNow; }
  void sting(int kind);     // a short phrase over whatever plays, in its key
  void note(int step);      // one chime, step notes up the key's major pentatonic (a gate run climbs through them)
  void render(float* stereo, int frames);   // interleaved, about -6 dBFS at its loudest

  struct Voice { int inst = -1; float f = 0, t = 0, vel = 0, dur = 0, pan = 0, ph1 = 0, ph2 = 0, ph3 = 0, rel = -1; };
  struct Event { double at; int inst; int midi; float vel, dur, pan; };
private:
  float sr;
  uint32_t rng;
  int moodNow = MOOD_NONE;
  double now = 0;           // samples since the start
  double beatAt = 0;        // when the next beat falls (samples)
  int beat = 0, chord = 0, pattern = 0, melDeg = 2;
  float gain = 0, gainTo = 0;
  std::vector<Voice> voices;
  std::vector<Event> events;
  // the reverb: four combs and two allpasses a side
  std::vector<float> comb[2][4], ap[2][2];
  int combI[2][4] = {}, apI[2][2] = {};
  float combLp[2][4] = {};
  float urand();
  void play(int inst, int midi, float vel, float dur, float pan);
  void releasePads();
  void onBeat();
  void schedule(double at, int inst, int midi, float vel, float dur, float pan) { events.push_back({at, inst, midi, vel, dur, pan}); }
};

// The player: a thread keeps the engine's music ring topped up while the score has it. own(true) takes the ring
// (the radio idle), own(false) gives it back; the radio taking it by itself (Radio::play) is noticed and respected
class MusicPlayer {
public:
  ~MusicPlayer() { stop(); }
  void start(float sampleRate);
  void stop();
  void own(bool on);
  bool owns() const { return token.load() != 0; }
  void setMood(int mood);
  void sting(int kind);
  void note(int step);
  int mood() const { return wantMood.load(); }
private:
  std::thread th;
  std::atomic<bool> quit{false};
  std::atomic<uint32_t> token{0};
  std::atomic<int> wantMood{MOOD_NONE};
  std::mutex m;
  std::vector<int> stings;
  float rate = 48000.f;
  void run();
};
extern MusicPlayer g_music;
