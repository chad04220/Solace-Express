// Somnium Volandi - tower controller voices
#pragma once
#include "common.h"
#include <condition_variable>
#include <functional>
#include <regex>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// A pad button drawn as its glyph (game.cpp padGlyph: '\x01' + the name, spaces as '_', + '\x02') back to its name, for
// text matched or written out as words: the voice lines, the problem report
inline std::string glyphText(const std::string& s) {
  std::string out; out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    const size_t e = s[i] == '\x01' ? s.find('\x02', i) : std::string::npos;
    if (e == std::string::npos) { out += s[i]; continue; }
    for (size_t k = i + 1; k < e; k++) out += s[k] == '_' ? ' ' : s[k];
    i = e;
  }
  return out;
}

// The game's voices (assets/voice, imported by tools/voice_import.py from the two voice packs, with the verified
// binding-independent lesson overrides in lesson_controls.json):
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
  static const int kHazard = 95;   // priority from which a transmission interrupts any routine one
  static constexpr const char* kPause = "@pause";
  static constexpr float kDecodeLead = 0.25f;   // (s) a queued transmission waits this long before it starts: its clips' decode (a flight's first calls, some 16 clips, take about 0.1 s)
  AtcVoice();
  ~AtcVoice();   // (the decoder stopped)
  AtcVoice(const AtcVoice&) = delete;
  AtcVoice& operator=(const AtcVoice&) = delete;
  // voice_index.txt; the clips are decoded on first use - those of a queued transmission ahead of it, on a thread of
  // their own (a call's few clips, decoded and resampled the frame it started, took 10-30 ms of the game's thread)
  bool load(const std::string& dir);
  bool ok() const { return !clips.empty(); }
  // a transmission; kPause in ids: a pause between sentences. Empty/missing IDs refuse the entire call.
  // radio: squelch around it; subtitle: show its text (the
  // calls the game didn't already show); group: a newer call of the same group replaces a waiting one (levers);
  // tag: the caller's own marker, handed back when the transmission starts
  struct Tx { std::vector<std::string> ids; std::string text; int prio = 0; bool radio = true, subtitle = false; std::string group; int tag = 0, apt = -1;
              float age = 0; int key = -1; };   // validity: the flight state the call was made for (Game::atcKey); stale when it starts -> dropped
  std::function<bool(const Tx&)> valid;   // the game's check, run when a queued transmission is about to start
  // the voice line (or the line assembled from fragments) for a message the game shows; mission picks the
  // instructor or examiner. Lesson hints use resolveLesson, independently of their displayed key/button names.
  // A pad's button glyphs in the message (padGlyph) are matched by their names; the text kept is the one shown
  bool resolve(const std::string& message, const std::string& mission, bool pad, Tx& out) const;
  // Only indexed, binding-independent lesson recordings are accepted. The displayed hint may name any current
  // bindings; it is retained as transmission text, while the actual spoken words remain available through text().
  bool resolveLesson(const std::string& mission, int phase, const std::string& displayedHint, Tx& out) const;
  std::string line(int voice, const char* key) const;          // a full call, e.g. "greeting_morning"
  std::string atom(int voice, const std::string& key) const;   // a template fragment, e.g. "cleared_land"
  std::string digit(int voice, int d) const;                   // radio digit (tree, fife, niner)
  std::string alpha(int voice, char c) const;                  // phonetic letter (Alfa .. Zulu)
  std::string text(const std::string& id) const;               // its words, for the subtitles
  bool has(const std::string& id) const { return clips.count(id) > 0; }   // an indexed clip
  bool decodes(const std::string& id);                         // its file loads (tests: every indexed clip)
  std::vector<std::string> indexed() const;                    // every clip id
  void say(const Tx& tx);
  // starts the next queued transmission when the channel is free; returns it the frame it starts (empty ids if none)
  Tx update(float dt);
  void cancel();
  bool busy() const;
  bool pending() const { return busy() || !queue.empty() || !started.ids.empty(); }
  std::vector<std::string> history;   // every transmission started, in order; the towers' prefixed "TWR " (tests)
  int missingAudio = 0;
  int decodedInPlace = 0;             // clips decoded where they were wanted, not ahead on the decoder (perf_check)
  int dropped = 0;                    // stale transmissions never started
  size_t historyLimit = 64;           // the newest this many are kept (0: all, for tests)
  // the decoded clips kept, at most this many bytes (the least recently used go first, and decode again when next
  // wanted): the whole cast decodes to over 400 MB at the engine's rate, a session's working set to a few tens
  size_t cacheLimit = size_t(64) << 20;
  size_t cachedBytes() const { std::lock_guard<std::mutex> lk(clipLock); return cacheBytes; }
private:
  bool resolveRecorded(const std::string&, const std::string&, bool, Tx&) const;
  std::vector<std::pair<std::regex,std::string>> cues;
  std::string playingText;
  struct Clip { std::string file, words, speaker, kind, mission; int prio = 40; std::vector<float> pcm; bool loaded = false, decoding = false; uint64_t used = 0; };
  std::unordered_map<std::string, Clip> clips;
  size_t cacheBytes = 0; uint64_t useTick = 0;
  std::unordered_map<std::string, std::vector<std::string>> exact;   // shown text -> lines
  std::string atomO(const std::string& speaker, const std::string& key) const;   // original pack fragments
  void cardinal(const std::string& sp, long n, std::vector<std::string>& ids) const;
  std::unordered_map<std::string, std::string> lookup;
  std::string dir;
  std::vector<Tx> queue;
  int playingPrio = -1;
  float idleT = 10.f;
  // the clips' samples, loaded and decoding flags, last uses and the cache's bytes: the decoder writes them too. The
  // map itself changes only in load(), with the decoder stopped
  mutable std::mutex clipLock;
  std::condition_variable decoded;   // (a clip the decoder had in hand is in)
  struct Decoder;
  std::unique_ptr<Decoder> decoder;
  void prefetch(const Tx& tx);   // a queued transmission's clips handed to the decoder
  void decodeLoop(Decoder& d);
  void install(Clip& c, std::vector<float>&& samples);   // (clipLock held) its samples in, the least recently used out past the budget
  const std::vector<float>* pcm(const std::string& id, std::unique_lock<std::mutex>& held);   // (clipLock held)
  std::shared_ptr<std::vector<float>> assemble(const Tx& tx, std::unique_lock<std::mutex>& held);
  void start(const Tx& tx);
  Tx started;
};
