// Solace Express - tower controller voices
#pragma once
#include "common.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// The game's voices (assets/voice, imported by tools/voice_import.py from the two voice packs):
//  - three tower controllers (the alphabet-and-tower pack): full calls, the fragments the clearance templates are
//    built from, and radio digits; what they say, and when, comes from the flight's real state (Game::updateAtc)
//  - six characters (the original pack): the instructor, the checkride examiner, airport information, the display
//    pilot, the cockpit assistant and the research computer, each line keyed by the text the game shows (resolve())
// All of them share one comms channel: one transmission at a time (radio calls with a squelch click before and a
// squelch tail after), the most important waiting call next, an urgent one (a go-around, a warning) cutting in,
// and cancel() dropping everything (pause, crash, flight end, retry).
class AtcVoice {
public:
  static const int kVoices = 3;
  bool load(const std::string& dir);   // voice_index.txt; the clips are decoded on first use
  bool ok() const { return !clips.empty(); }
  // a transmission; "" in ids: a pause between sentences. radio: squelch around it; subtitle: show its text (the
  // calls the game didn't already show); group: a newer call of the same group replaces a waiting one (levers)
  struct Tx { std::vector<std::string> ids; std::string text; int prio = 0; bool radio = true, subtitle = false; std::string group; };
  // the voice line (or the line assembled from fragments) for a message the game shows; mission: the lesson it
  // belongs to (picks the instructor or the examiner); pad: a gamepad is in use (lines that name its buttons)
  bool resolve(const std::string& message, const std::string& mission, bool pad, Tx& out) const;
  std::string line(int voice, const char* key) const;          // a full call, e.g. "greeting_morning"
  std::string atom(int voice, const std::string& key) const;   // a template fragment, e.g. "cleared_land"
  std::string digit(int voice, int d) const;                   // radio digit (tree, fife, niner)
  std::string text(const std::string& id) const;               // its words, for the subtitles
  void say(const Tx& tx);
  // starts the next queued transmission when the channel is free; returns it the frame it starts (empty ids if none)
  Tx update(float dt);
  void cancel();
  bool busy() const;
  std::vector<std::string> history;   // every transmission started, in order; the towers' prefixed "TWR " (tests)
private:
  struct Clip { std::string file, words, speaker, kind, mission; int prio = 40; std::vector<float> pcm; bool loaded = false; };
  std::unordered_map<std::string, Clip> clips;
  std::unordered_map<std::string, std::vector<std::string>> exact;   // shown text -> lines
  std::unordered_map<std::string, std::string> padAlias;            // keyboard text -> the gamepad-worded line
  std::string atomO(const std::string& speaker, const std::string& key) const;   // original pack fragments
  void cardinal(const std::string& sp, long n, std::vector<std::string>& ids) const;
  std::unordered_map<std::string, std::string> lookup;
  std::string dir;
  std::vector<Tx> queue;
  int playingPrio = -1;
  float idleT = 10.f;
  const std::vector<float>* pcm(const std::string& id);
  std::shared_ptr<std::vector<float>> assemble(const Tx& tx);
  void start(const Tx& tx);
  Tx started;
};
