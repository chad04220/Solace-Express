// Solace Express - tower controller voices
#pragma once
#include "common.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// The three tower controller voices of the alphabet-and-tower voice pack (assets/voice, imported by
// tools/voice_import.py): full calls, the fragments the clearance templates are built from, and radio digits. This
// class only assembles and plays radio transmissions; what is said, and when, comes from the flight's real state
// (Game::updateAtc). One transmission plays at a time with a squelch click before it and a squelch tail after it;
// a more urgent call (a go-around) cuts in, and cancel() drops everything (pause, crash, flight end, retry).
class AtcVoice {
public:
  static const int kVoices = 3;
  bool load(const std::string& dir);   // voice_index.txt; the clips are decoded on first use
  bool ok() const { return !clips.empty(); }
  struct Tx { std::vector<std::string> ids; std::string text; int prio = 0; };   // "" in ids: a pause between sentences
  std::string line(int voice, const char* key) const;          // a full call, e.g. "greeting_morning"
  std::string atom(int voice, const std::string& key) const;   // a template fragment, e.g. "cleared_land"
  std::string digit(int voice, int d) const;                   // radio digit (tree, fife, niner)
  std::string text(const std::string& id) const;               // its words, for the subtitles
  void say(const Tx& tx);
  // starts the next queued transmission when the channel is free; returns its text the frame it starts ("" if none)
  std::string update(float dt);
  void cancel();
  bool busy() const;
  std::vector<std::string> history;   // every transmission started, in order (tests)
private:
  struct Clip { std::string file, words; std::vector<float> pcm; bool loaded = false; };
  std::unordered_map<std::string, Clip> clips;
  std::unordered_map<std::string, std::string> lookup;
  std::string dir;
  std::vector<Tx> queue;
  int playingPrio = -1;
  float idleT = 10.f;
  const std::vector<float>* pcm(const std::string& id);
  std::shared_ptr<std::vector<float>> assemble(const Tx& tx);
  void start(const Tx& tx);
  std::string started;
};
