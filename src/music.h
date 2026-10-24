// Somnium Volandi - the game's own music: a generative score played through the audio engine's music ring (the
// internet radio's), so the radio and the score never play at once. Moods: the main menu's theme, the hub's swung
// groove, an ambient wash for the cruise (Settings), the finale's reprise of the theme; and short cues over them (a
// job done, a flight failed, a licence, an achievement, a medal, a cabin's applause) and a gate run's climbing chimes.
// It is all written out and synthesised here (music.cpp): composed themes and progressions, voiced to move smoothly
// and arranged in sections; a small orchestra and band of synthesised instruments; a hall reverb, a delay and a
// mastering compressor and limiter
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

enum MusicMood { MOOD_NONE = 0, MOOD_MENU, MOOD_HUB, MOOD_CRUISE, MOOD_FINALE, MOOD_COUNT };
enum MusicSting { STING_SUCCESS = 0, STING_FAIL, STING_LICENCE, STING_ACHIEVEMENT, STING_MEDAL, STING_APPLAUSE, STING_COUNT };

// The generator: the same notes for the same seed, rendered a block at a time (the player's thread, or a test)
class MusicGen {
public:
  explicit MusicGen(float sampleRate = 48000.f, uint32_t seed = 1);
  ~MusicGen();
  MusicGen(const MusicGen&) = delete;
  MusicGen& operator=(const MusicGen&) = delete;
  void setMood(int mood);   // the old mood's notes ring out, the new one starts at once (its pads swell in)
  int mood() const;
  void sting(int kind);     // a short cue over whatever plays, in its key
  void note(int step);      // one chime, step notes up the key's major pentatonic (a gate run climbs through them)
  void render(float* stereo, int frames);   // interleaved, peaks held under -1.5 dBFS
  struct Impl;
private:
  std::unique_ptr<Impl> d;
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
