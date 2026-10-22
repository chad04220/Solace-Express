// Somnium Volandi - tower controller voices (see atc.h)
#include "atc.h"
#include "audio.h"
#include "audio_stream.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <thread>

static const char* kVoiceName[AtcVoice::kVoices] = {"tower_north", "tower_coast", "tower_valley"};

// the clips of transmissions waiting their turn, decoded on a thread of its own, one at a time, nearest first
struct AtcVoice::Decoder {
  std::mutex m; std::condition_variable cv;
  std::deque<std::string> todo;
  bool stop = false;
  std::thread worker;
  ~Decoder() {
    { std::lock_guard<std::mutex> lk(m); stop = true; }
    cv.notify_all();
    if (worker.joinable()) worker.join();
  }
};
AtcVoice::AtcVoice() = default;
AtcVoice::~AtcVoice() { decoder.reset(); }

bool AtcVoice::load(const std::string& d) {
  decoder.reset();   // (stopped before the clips it reads are replaced)
  std::lock_guard<std::mutex> lock(clipLock);
  dir = d; clips.clear(); lookup.clear(); exact.clear(); cues.clear(); cacheBytes = 0;   // (a reload with removed ids leaves no stale references)
  std::ifstream f(dir + "/voice_index.txt");
  if (!f) return false;
  // Supplemental cues are separate so importing the legacy packs cannot silently erase them.
  std::string index((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::ifstream extra(dir + "/voice_index_extra.txt");
  if(extra) index += "\n" + std::string((std::istreambuf_iterator<char>(extra)), std::istreambuf_iterator<char>());
  std::istringstream input(index);
  std::string ln;
  while (std::getline(input, ln)) {
    if(!ln.empty() && ln.back()=='\r')ln.pop_back(); // Windows editors/checkouts must not change cue patterns or mission IDs
    if (ln.empty() || ln[0] == '#') continue;
    size_t a = ln.find('\t');
    if (a == std::string::npos) continue;
    if (ln[0] == '@') { lookup[ln.substr(1, a - 1)] = ln.substr(a + 1); continue; }
    std::vector<std::string> f;   // id, file, subtitle [, speaker, priority, kind, keyboard text, mission]
    for (size_t p = 0;;) { size_t q = ln.find('\t', p); f.push_back(ln.substr(p, q == std::string::npos ? std::string::npos : q - p)); if (q == std::string::npos) break; p = q + 1; }
    Clip c; c.file = f[1];
    if (f.size() > 2) c.words = f[2];
    if (f.size() >= 6) { c.speaker = f[3]; c.prio = atoi(f[4].c_str()); c.kind = f[5]; }
    else c.kind = f[0].compare(0, 6, "tower.") == 0 ? "line" : "fragment";   // (tower pack: full calls are tower.*)
    if (f.size() > 7) c.mission = f[7];
    // Legacy lesson lines name fixed keys/buttons. Never put them in the general resolver: only the explicitly
    // binding-independent lesson rows may be spoken, including when an older asset directory is loaded.
    bool legacyLesson = f[0].compare(0, 7, "lesson.") == 0 && c.kind != "lesson";
    if ((c.kind == "line" || c.kind == "lesson") && !c.speaker.empty() && !legacyLesson) {
      exact[c.words].push_back(f[0]);
    }
    if (c.speaker.empty()) { // three-column tower rows still have a stable character identity
      size_t slash=c.file.find('/'); if(slash!=std::string::npos) c.speaker=c.file.substr(0,slash);
    }
    if(c.kind=="cue_regex" && f.size()>6) {
      try {cues.push_back({std::regex(f[6]),f[0]});} catch(const std::regex_error&) {continue;}
    }
    clips[f[0]] = c;
  }
  return !clips.empty();
}

bool AtcVoice::resolveLesson(const std::string& mission, int phase, const std::string& displayedHint, Tx& out) const {
  out = Tx(); out.text = displayedHint;
  if (phase < 0 || mission.empty()) return false;
  const std::string id = "lesson." + mission + ".phase" + std::to_string(phase);
  auto it = clips.find(id);
  if (it == clips.end() || it->second.kind != "lesson" || it->second.mission != mission) return false;
  const Clip& c = it->second;
  out.ids = {id}; out.prio = c.prio; out.radio = false; out.group = "lesson";
  return true;
}

std::string AtcVoice::line(int v, const char* key) const { return std::string("tower.") + kVoiceName[v] + "." + key; }
std::string AtcVoice::atom(int v, const std::string& key) const {
  auto it = lookup.find(std::string(kVoiceName[v]) + ".atom." + key); return it == lookup.end() ? std::string() : it->second;
}
std::string AtcVoice::digit(int v, int d) const {
  auto it = lookup.find(std::string(kVoiceName[v]) + ".digit." + std::to_string(d)); return it == lookup.end() ? std::string() : it->second;
}
std::string AtcVoice::alpha(int v, char c) const {
  auto it = lookup.find(std::string(kVoiceName[v]) + ".alpha." + std::string(1, c)); return it == lookup.end() ? std::string() : it->second;
}
std::string AtcVoice::text(const std::string& id) const { auto it = clips.find(id); return it == clips.end() ? std::string() : it->second.words; }
std::string AtcVoice::atomO(const std::string& sp, const std::string& key) const {
  auto it = lookup.find("atom." + sp + "." + key); return it == lookup.end() ? std::string() : it->second;
}
void AtcVoice::cardinal(const std::string& sp, long n, std::vector<std::string>& ids) const {   // number words
  if (n < 0) { ids.push_back(atomO(sp, "minus")); n = -n; }
  if (n < 20) { ids.push_back(atomO(sp, "n" + std::to_string(n))); return; }
  if (n < 100) { ids.push_back(atomO(sp, "n" + std::to_string(n / 10 * 10))); if (n % 10) cardinal(sp, n % 10, ids); return; }
  if (n < 1000) { cardinal(sp, n / 100, ids); ids.push_back(atomO(sp, "hundred")); if (n % 100) cardinal(sp, n % 100, ids); return; }
  static const std::pair<long, const char*> scales[] = {{1000000000L, "billion"}, {1000000L, "million"}, {1000L, "thousand"}};
  for (auto& sc : scales) if (n >= sc.first) { cardinal(sp, n / sc.first, ids); ids.push_back(atomO(sp, sc.second)); if (n % sc.first) cardinal(sp, n % sc.first, ids); return; }
}

bool AtcVoice::resolve(const std::string& shown, const std::string& mission, bool pad, Tx& out) const {
  const std::string message = glyphText(shown);   // (a pad's glyphs matched as their names)
  if(resolveRecorded(message,mission,pad,out)) { out.text=shown; return true; }
  for(const auto& cue:cues) if(std::regex_match(message,cue.first)) {
    const auto& c=clips.at(cue.second);
    out=Tx();out.text=shown;out.ids={cue.second};out.prio=c.prio;out.radio=c.speaker=="command";
    out.group=c.speaker=="command"?"command."+cue.second:"ops."+cue.second;
    return true;
  }
  return false;
}

// A port of the voice pack's reference resolver (resolve_message.py): the line recorded for a message the game shows,
// or the message assembled from fragments (weather, flap / pod settings, checkpoints, landing ratings, warnings ...).
bool AtcVoice::resolveRecorded(const std::string& msg, const std::string& mission, bool /*pad*/, Tx& out) const {
  out = Tx(); out.text = msg;
  auto finish = [&](const std::string& sp, int prio) {
    for (auto& id : out.ids) if (id.empty() || !clips.count(id)) { out.ids.clear(); return false; }   // (a fragment the pack lacks)
    out.prio = prio; out.radio = sp == "tower" || sp == "nocturne" || sp.rfind("tower_",0)==0;
    return !out.ids.empty();
  };
  auto line = [&](const std::string& id) {
    const Clip& c = clips.at(id);
    out.ids = {id};
    if (c.kind == "line" && (msg.rfind("Gear ", 0) == 0 || msg.rfind("Flaps ", 0) == 0 || msg.rfind("Pods ", 0) == 0 || msg.rfind("Thrust vector ", 0) == 0)) out.group = "lever";   // settings: the newest replaces a waiting one
    return finish(c.speaker, c.prio);
  };
  // the HUD's compact annunciators are spoken as their fuller recorded lines
  static const std::unordered_map<std::string, std::string> hudAlias = {
    {"BATTERY FLAT  no autopilot, no GPS", "BATTERY FLAT - no autopilot, no GPS"},
    {"PITOT BLOCKED  airspeed unreliable", "PITOT BLOCKED - airspeed unreliable, fly attitude and power"},
    {"GEAR STUCK UP  belly landing: paved, level, slow", "GEAR STUCK UP - belly landing: paved runway, wings level, slow"},
    {"GEAR STUCK DOWN  slower, more fuel", "GEAR STUCK DOWN - slower, more fuel"},
    {"FLAP ASYMMETRY  hold the wing up", "FLAP ASYMMETRY - hold the wing up with aileron"}};
  auto ha = hudAlias.find(msg);
  auto ex = exact.find(ha != hudAlias.end() ? ha->second : msg);
  if (ex != exact.end()) {
    const char* pref = mission == "L4" ? "rosa" : (mission == "L1" || mission == "L2" || mission == "L3") ? "instructor" : "aster";
    std::string id = ex->second[0];
    for (auto& c : ex->second) if (clips.at(c).speaker == pref) { id = c; break; }
    if (id.compare(0, 10, "clearance.") == 0) return false;   // the takeoff clearance comes from the tower controllers
    return line(id);
  }
  std::smatch m;
  // Generated/real-world fields have no prerecorded proper name. Announce their
  // actual identifier with one existing controller's phonetics, never another
  // airport's name. The full name remains in the displayed message.
  static const std::regex airportRe(R"(([A-Z0-9]{3,8}) - .+)");
  if (std::regex_match(msg, m, airportRe)) {
    for (char c : m[1].str()) out.ids.push_back(c >= '0' && c <= '9' ? digit(0,c-'0') : alpha(0,c));
    out.group="airport"; return finish(kVoiceName[0],45);
  }
  auto digits = [&](const std::string& sp, const std::string& t) { for (char c : t) out.ids.push_back(atomO(sp, std::string("n") + c)); };
  static const std::regex wxRe(R"(Runway (\d{2}), Wind (\d{3})@(\d+)kt(?: G(\d+))?, (clear|scattered|broken|overcast)(?: (\d+)ft)?, vis (10\+|\d+(?:\.\d+)?)km(?:, (rain|thunderstorms|snow))?, (\d{2}):(\d{2}))");
  if (std::regex_match(msg, m, wxRe)) {
    const std::string sp = "tower";
    out.ids.push_back(atomO(sp, "runway")); digits(sp, m[1]); out.ids.push_back(atomO(sp, "wind")); digits(sp, m[2]);
    out.ids.push_back(atomO(sp, "at")); cardinal(sp, std::stol(m[3]), out.ids); out.ids.push_back(atomO(sp, "knots"));
    if (m[4].matched) { out.ids.push_back(atomO(sp, "gusting")); cardinal(sp, std::stol(m[4]), out.ids); }
    out.ids.push_back(atomO(sp, m[5]));
    if (m[6].matched) { cardinal(sp, std::stol(m[6]), out.ids); out.ids.push_back(atomO(sp, "feet")); }
    out.ids.push_back(atomO(sp, "visibility"));
    if (m[7] == "10+") out.ids.push_back(atomO(sp, "ten_plus_km"));
    else {
      std::string v = m[7]; size_t dp = v.find('.');
      cardinal(sp, std::stol(v.substr(0, dp)), out.ids);
      if (dp != std::string::npos) { out.ids.push_back(atomO(sp, "point")); digits(sp, v.substr(dp + 1)); }
      out.ids.push_back(atomO(sp, "kilometres"));
    }
    if (m[8].matched) out.ids.push_back(atomO(sp, m[8]));
    out.ids.push_back(atomO(sp, "time")); digits(sp, m[9]); digits(sp, m[10]); out.ids.push_back(atomO(sp, "hours"));
    return finish(sp, 50);
  }
  static const std::regex flapRe(R"(Flaps (\d+)%)");
  if (std::regex_match(msg, m, flapRe)) {
    out.ids.push_back(atomO("aster", "flaps")); cardinal("aster", std::stol(m[1]), out.ids); out.ids.push_back(atomO("aster", "percent"));
    out.group = "lever"; return finish("aster", 40);
  }
  static const std::regex podRe(R"((Pods|Thrust vector) (\d+) deg)");
  if (std::regex_match(msg, m, podRe)) {
    out.ids.push_back(atomO("nyx", m[1] == "Pods" ? "pods" : "thrust_vector")); cardinal("nyx", std::stol(m[2]), out.ids); out.ids.push_back(atomO("nyx", "degrees"));
    out.group = "lever"; return finish("nyx", 40);
  }
  static const std::regex cpRe(R"(Checkpoint (\d+) of (\d+))");
  if (std::regex_match(msg, m, cpRe)) {
    out.ids.push_back(atomO("aster", "checkpoint")); cardinal("aster", std::stol(m[1]), out.ids); out.ids.push_back(atomO("aster", "of")); cardinal("aster", std::stol(m[2]), out.ids);
    return finish("aster", 50);
  }
  static const std::regex ldgRe(R"((BUTTER!|Smooth landing|Good landing|Firm landing|HARD landing!)  ([+-]?\d+) fpm)");
  if (std::regex_match(msg, m, ldgRe)) {
    auto b = exact.find(m[1]);
    if (b == exact.end()) return false;
    out.ids.push_back(b->second[0]); cardinal("aster", std::stol(m[2]), out.ids); out.ids.push_back(atomO("aster", "feet_per_minute"));
    return finish("aster", 35);
  }
  static const std::regex gearRe(R"(Gear collapsed - hit at ([+-]?\d+) fpm)");
  if (std::regex_match(msg, m, gearRe)) {
    out.ids.push_back(atomO("aster", "gear_collapsed_hit_at")); cardinal("aster", std::stol(m[1]), out.ids); out.ids.push_back(atomO("aster", "feet_per_minute"));
    return finish("aster", 80);
  }
  static const std::regex windShiftRe(R"(Autopilot: the wind has shifted - now runway (\d{2}) at ([A-Z0-9]{3,8}))");
  if (std::regex_match(msg, m, windShiftRe)) {
    out.ids.push_back(atomO("aster", "wind_shift_runway")); digits("aster", m[1]);
    const auto airport=atomO("aster", "airport_" + m[2].str());
    if(!airport.empty()) {out.ids.push_back(atomO("aster", "at"));out.ids.push_back(airport);}
    return finish("aster", 60);
  }
  static const std::regex bellyRe(R"(Belly landing too hard - hit at ([+-]?\d+) fpm)");
  if (std::regex_match(msg, m, bellyRe)) {
    out.ids.push_back(atomO("aster", "belly_landing_too_hard_hit_at")); cardinal("aster", std::stol(m[1]), out.ids); out.ids.push_back(atomO("aster", "feet_per_minute"));
    return finish("aster", 60);
  }
  static const std::regex glideRe(R"(ENGINE FAILURE(?: - |  )glide (\d+):1, best(?: glide)? (\d+) kt)");
  if (std::regex_match(msg, m, glideRe)) {
    out.ids.push_back(atomO("aster", "engine_failure_glide")); cardinal("aster", std::stol(m[1]), out.ids); out.ids.push_back(atomO("aster", "to_one_best_glide")); cardinal("aster", std::stol(m[2]), out.ids); out.ids.push_back(atomO("aster", "knots"));
    return finish("aster", 95);
  }
  static const std::regex altRe(R"(ALTERNATOR(?: - |  )battery (\d+)%)");
  if (std::regex_match(msg, m, altRe)) {
    out.ids.push_back(atomO("aster", "alternator_battery")); cardinal("aster", std::stol(m[1]), out.ids); out.ids.push_back(atomO("aster", "percent"));
    out.group = "alternator"; return finish("aster", 80);
  }
  static const std::regex iceRe(R"(ICING (\d+)%(?: - |  )leave the cloud, keep (?:the speed up|speed))");
  if (std::regex_match(msg, m, iceRe)) {
    out.ids.push_back(atomO("aster", "icing")); cardinal("aster", std::stol(m[1]), out.ids); out.ids.push_back(atomO("aster", "percent")); out.ids.push_back(atomO("aster", "leave_cloud_keep_speed"));
    out.group = "icing"; return finish("aster", 80);
  }
  static const std::regex splashRe(R"(SPLASH (\d+) - (.+) down)");
  if (std::regex_match(msg, m, splashRe)) {
    auto c = lookup.find("craft." + m[2].str());
    out.ids.push_back(atomO("nyx", "splash")); cardinal("nyx", std::stol(m[1]), out.ids);
    // Aircraft added after a voice pack may have no recorded name. Keep the complete name on screen,
    // but speak the accurate existing "Splash <count> down" fragments rather than mute the event or
    // substitute another aircraft's recording. Indexed names still require their actual clip in finish().
    if (c != lookup.end()) out.ids.push_back(atomO("nyx", "craft_" + c->second));
    out.ids.push_back(atomO("nyx", "down"));
    return finish("nyx", 45);
  }
  static const std::regex blastRe(R"((\d+) aircraft caught in the blast)");
  if (std::regex_match(msg, m, blastRe)) { cardinal("nyx", std::stol(m[1]), out.ids); out.ids.push_back(atomO("nyx", "aircraft_in_blast")); return finish("nyx", 45); }
  static const std::regex engRe(R"(ENGINE OFF - press (.+) to restart)");
  if (std::regex_match(msg, m, engRe)) {
    std::string key = m[1];
    out.ids.push_back(atomO("aster", "engine_off_press"));
    auto k = lookup.find("key." + key);
    if (k != lookup.end()) out.ids.push_back(k->second);
    else if (key.compare(0, 4, "KEY ") == 0 && lookup.count("key.KEY")) {
      out.ids.push_back(lookup.at("key.KEY"));
      for (char c : key.substr(4)) out.ids.push_back(c >= 'A' && c <= 'F' ? atomO("aster", std::string("hex_") + c) : atomO("aster", std::string("n") + c));
    } else return false;
    out.ids.push_back(atomO("aster", "to_restart"));
    return finish("aster", 90);
  }
  return false;
}

// WAV: 16-bit PCM or 8-bit mu-law, mono, any rate; decoded to floats at the audio engine's rate
static bool readWav(const std::string& path, std::vector<float>& out, int outRate) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::string b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (b.size() < 12 || b.compare(0, 4, "RIFF") || b.compare(8, 4, "WAVE")) return false;
  auto u16 = [&](size_t o) { return (unsigned)(uint8_t)b[o] | (unsigned)(uint8_t)b[o + 1] << 8; };
  auto u32 = [&](size_t o) { return u16(o) | u16(o + 2) << 16; };
  int fmt = 0, ch = 1, rate = 0, bits = 0; size_t data = 0, len = 0;
  for (size_t o = 12; o + 8 <= b.size();) {
    size_t n = u32(o + 4);
    if (!b.compare(o, 4, "fmt ")) { if (n < 16 || o + 24 > b.size()) return false; fmt = (int)u16(o + 8); ch = (int)u16(o + 10); rate = (int)u32(o + 12); bits = (int)u16(o + 22); }   // (a truncated header: no clip - the review of v3.44.0, AUD-4)
    else if (!b.compare(o, 4, "data")) { data = o + 8; if(n>b.size()-data) return false; len=n; }
    o += 8 + n + (n & 1);
  }
  if (!data || ch != 1 || rate < 8000 || rate > 192000 || outRate < 8000 || outRate > 192000 || !((fmt == 1 && bits == 16) || (fmt == 7 && bits == 8))) return false;
  std::vector<float> in;
  if (fmt == 1) { in.resize(len / 2); for (size_t i = 0; i < in.size(); i++) in[i] = (int16_t)u16(data + i * 2) / 32768.f; }
  else {
    in.resize(len);
    for (size_t i = 0; i < len; i++) {   // G.711 mu-law
      int u = ~(uint8_t)b[data + i] & 0xFF, sign = u & 0x80, e = (u >> 4) & 7, m = u & 0x0F;
      int v = (((m << 3) + 0x84) << e) - 0x84;
      in[i] = (sign ? -v : v) / 32768.f;
    }
  }
  if (in.empty()) return false;   // (no samples: an empty or cut-off data chunk)
  if(in.size()>size_t(rate)*120) return false;
  // Windowed-sinc sample-rate conversion: avoid imaging/sibilance from the old linear upsampler.
  const double step=double(rate)/outRate;
  const size_t n=size_t(in.size()/step);
  out.resize(n); AudioSincKernel kernel;kernel.prepare(rate,outRate);const int taps=kernel.radius;
  for(size_t i=0;i<n;++i) {
    uint64_t position=uint64_t(i)*uint64_t(rate); int center=int(position/outRate); double sum=0,weight=0;
    const auto& coefficients=kernel.at(double(position%outRate)/outRate,center);
    for(int j=-taps+1;j<=taps;++j) {
      int at=center+j;if(at<0 || at>=int(in.size())) continue;
      double w=coefficients[j+taps-1];sum+=in[size_t(at)]*w;weight+=w;
    }
    out[i]=fabs(weight)>1e-9?float(sum/weight):0;
  }
  // Modest level matching; do not normalize breaths/quiet fragments into loud noise.
  double energy=0;size_t active=0;float peak=0;
  for(float x:out) {peak=std::max(peak,fabsf(x));if(fabsf(x)>.015f){energy+=x*x;++active;}}
  float rms=active?float(sqrt(energy/active)):0;
  float gain=rms>.025f?clampf(.14f/rms,.7f,1.8f):1.f;
  if(peak>0) gain=std::min(gain,.85f/peak);
  for(float& x:out) x*=gain;
  return true;
}

bool AtcVoice::decodes(const std::string& id) {
  std::unique_lock<std::mutex> lk(clipLock);
  const std::vector<float>* p = pcm(id, lk); return p && !p->empty();
}
std::vector<std::string> AtcVoice::indexed() const { std::vector<std::string> v; v.reserve(clips.size()); for (auto& c : clips) v.push_back(c.first); return v; }

void AtcVoice::install(Clip& c, std::vector<float>&& samples) {
  c.loaded = true;   // (a file that doesn't decode stays loaded, empty: it isn't read again)
  c.pcm = std::move(samples); c.used = ++useTick;
  cacheBytes += c.pcm.capacity() * sizeof(float);
  // over the budget: the least recently used clips go, never this one (the callers copy a clip out before asking
  // for the next, so a clip let go mid-transmission has already been used)
  while (cacheBytes > cacheLimit) {
    Clip* old = nullptr;
    for (auto& e : clips) if (&e.second != &c && !e.second.pcm.empty() && (!old || e.second.used < old->used)) old = &e.second;
    if (!old) break;
    cacheBytes -= old->pcm.capacity() * sizeof(float);
    std::vector<float>().swap(old->pcm); old->loaded = false;
  }
}

const std::vector<float>* AtcVoice::pcm(const std::string& id, std::unique_lock<std::mutex>& held) {
  auto it = clips.find(id);
  if (it == clips.end()) return nullptr;
  Clip& c = it->second;
  c.used = ++useTick;
  if (!c.loaded && c.decoding) decoded.wait(held, [&] { return !c.decoding; });   // (the decoder has it in hand: its result)
  if (!c.loaded) {   // (not asked for ahead, or let go since: decoded here)
    decodedInPlace++;
    std::vector<float> samples;
    if (!readWav(dir + "/" + c.file, samples, g_audio.sampleRate)) samples.clear();
    install(c, std::move(samples));
  }
  return c.pcm.empty() ? nullptr : &c.pcm;
}

void AtcVoice::prefetch(const Tx& tx) {
  std::vector<std::string> want;
  {
    std::lock_guard<std::mutex> lk(clipLock);
    for (const std::string& id : tx.ids) {
      auto it = clips.find(id);
      if (it != clips.end() && !it->second.loaded && !it->second.decoding) want.push_back(id);
    }
  }
  if (want.empty()) return;
  if (!decoder) { decoder = std::make_unique<Decoder>(); decoder->worker = std::thread([this, d = decoder.get()] { decodeLoop(*d); }); }
  {
    std::lock_guard<std::mutex> lk(decoder->m);
    for (auto& id : want) if (std::find(decoder->todo.begin(), decoder->todo.end(), id) == decoder->todo.end()) decoder->todo.push_back(std::move(id));
  }
  decoder->cv.notify_one();
}

void AtcVoice::decodeLoop(Decoder& d) {
  for (;;) {
    std::string id;
    {
      std::unique_lock<std::mutex> lk(d.m);
      d.cv.wait(lk, [&] { return d.stop || !d.todo.empty(); });
      if (d.stop) return;
      id = std::move(d.todo.front()); d.todo.pop_front();
    }
    Clip* c = nullptr; std::string path;
    {
      std::lock_guard<std::mutex> lk(clipLock);
      auto it = clips.find(id);
      if (it == clips.end() || it->second.loaded || it->second.decoding) continue;
      c = &it->second; c->decoding = true; path = dir + "/" + c->file;
    }
    std::vector<float> samples;   // (read and resampled outside the lock: the game's thread goes on meanwhile)
    if (!readWav(path, samples, g_audio.sampleRate)) samples.clear();
    {
      std::lock_guard<std::mutex> lk(clipLock);
      c->decoding = false;
      if (!c->loaded) install(*c, std::move(samples));
    }
    decoded.notify_all();
  }
}

// squelch click, the clips with 35 ms between fragments (a longer pause between sentences), and a squelch tail
std::shared_ptr<std::vector<float>> AtcVoice::assemble(const Tx& tx, std::unique_lock<std::mutex>& held) {
  auto out = std::make_shared<std::vector<float>>();
  const float sr = (float)g_audio.sampleRate;
  uint32_t seed = 0x9e3779b9u;
  auto noise = [&]() { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 9) / 4194304.f - 1.f; };
  auto squelch = [&](float secs, float level, bool tail) {   // band-limited hiss with a fast edge
    float lp = 0, hp = 0, prev = 0;
    int n = (int)(secs * sr);
    for (int i = 0; i < n; i++) {
      float t = i / (float)n, w = noise();
      lp += (w - lp) * 0.35f; hp = lp - prev; prev = lp;
      float env = tail ? (1.f - t) * (1.f - t) : (t < 0.15f ? t / 0.15f : 1.f - (t - 0.15f) / 0.85f);
      out->push_back(hp * level * env);
    }
  };
  // radio calls built from fragments get one squelch around the whole message (full radio lines carry their own)
  bool sq = tx.radio;
  if (sq) { sq = false; for (auto& id : tx.ids) { auto c = clips.find(id); if (c != clips.end() && c->second.kind != "line") sq = true; } }
  if (sq) { squelch(0.06f, 0.18f, false); out->insert(out->end(), (size_t)(0.04f * sr), 0.f); }
  bool any = false;
  for (const std::string& id : tx.ids) {
    if (id == kPause) { out->insert(out->end(), (size_t)(0.22f * sr), 0.f); continue; }
    const std::vector<float>* p = pcm(id, held);
    if (!p || p->empty()) { ++missingAudio; out->clear(); return out; } // never speak a partial clearance with a missing digit/word
    if (any) out->insert(out->end(), (size_t)(0.035f * sr), 0.f);
    size_t first=0,last=p->size();
    const auto& clip=clips.at(id);
    if(clip.kind=="fragment" || clip.kind=="symbol") {
      const size_t pad=size_t(.012f*sr);
      while(first<last && fabsf((*p)[first])<.003f) ++first;
      first=first>pad?first-pad:0;
      while(last>first && fabsf((*p)[last-1])<.003f) --last;
      last=std::min(p->size(),last+pad);
    }
    size_t start=out->size();out->insert(out->end(),p->begin()+first,p->begin()+last);
    size_t fade=std::min(size_t(.003f*sr),(last-first)/2);
    for(size_t i=0;i<fade;++i) {float gain=float(i)/std::max(size_t(1),fade);(*out)[start+i]*=gain;(*out)[out->size()-1-i]*=gain;}
    any = true;
  }
  if (sq) { out->insert(out->end(), (size_t)(0.03f * sr), 0.f); squelch(0.14f, 0.22f, true); }
  if (!any) out->clear();
  return out;
}

void AtcVoice::start(const Tx& tx) {
  std::shared_ptr<std::vector<float>> buf;
  { std::unique_lock<std::mutex> lk(clipLock); buf = assemble(tx, lk); }   // (the decoder installs nothing while its clips are copied)
  if (buf->empty()) return;
  std::string speaker;
  for(const auto& id:tx.ids) {auto c=clips.find(id);if(c!=clips.end()) {speaker=c->second.speaker;break;}}
  vec3 seat(-.22f,.05f,-.65f); // Aster: left glareshield
  if(speaker=="instructor" || speaker=="rosa") seat=vec3(.65f,.02f,-.12f); // right seat
  else if(speaker=="nyx") seat=vec3(.2f,.12f,-.75f);
  else if(speaker=="ops") seat=vec3(-.35f,.08f,-.8f);
  else if(speaker=="command") seat=vec3(.32f,-.1f,-.7f);
  else if(tx.radio) seat=vec3(-.18f,-.22f,-.7f); // radio stack
  g_audio.voicePlay(buf,seat);
  playingText=tx.text;
  playingPrio = tx.prio; idleT = 0;
  started = tx; history.push_back((tx.subtitle ? "TWR " : "") + tx.text);
  if (historyLimit && history.size() > historyLimit) history.erase(history.begin(), history.begin() + (history.size() - historyLimit));
}

bool AtcVoice::busy() const { return g_audio.voiceBusy(); }

void AtcVoice::say(const Tx& tx) {
  if (!ok()) return;
  if(tx.ids.empty()) return;
  if((busy() && playingText==tx.text) || std::any_of(queue.begin(),queue.end(),[&](const Tx& q){return q.text==tx.text;})) return;
  if (valid && !valid(tx)) { dropped++; return; }   // already overtaken by events: never queued
  // urgent: cut in (a go-around over a routine call; a hazard, 95 and up, over anything routine such as a landing clearance)
  if (busy() && (tx.prio >= playingPrio + 30 || (tx.prio >= kHazard && playingPrio < kHazard))) {
    g_audio.voiceStop();
    queue.clear();
    // next, without the gap between calls: update() starts it as it would any other, its clips decoded meanwhile
    queue.push_back(tx); idleT = 10.f;
    prefetch(tx);
    return;
  }
  if (!tx.group.empty())   // a newer lever setting replaces the one still waiting to be said
    queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const Tx& q) { return q.group == tx.group; }), queue.end());
  queue.push_back(tx);
  if (queue.size() > 4) {   // never a long backlog of stale calls: drop the least important
    size_t worst = 0;
    for (size_t i = 1; i < queue.size(); i++) if (queue[i].prio < queue[worst].prio) worst = i;
    queue.erase(queue.begin() + worst);
  }
  prefetch(tx);   // (its clips decoded while it waits: the channel's 0.6 s gap at the least)
}

// Returns each started transmission exactly once: one started here, or one that cut in from say() since the last call
AtcVoice::Tx AtcVoice::update(float dt) {
  for(auto& q:queue) q.age+=std::max(dt,0.f);
  queue.erase(std::remove_if(queue.begin(),queue.end(),[&](const Tx& q){bool stale=q.age>(q.prio>=kHazard?5.f:14.f);if(stale)++dropped;return stale;}),queue.end());
  if (!busy()) {
    playingPrio = -1;
    idleT += dt;
    if (started.ids.empty() && !queue.empty() && idleT > 0.6f) {   // the most important waiting call, oldest first
      size_t best = 0;
      for (size_t i = 1; i < queue.size(); i++) if (queue[i].prio > queue[best].prio) best = i;
      // (a quarter of a second queued at the least, every time: its clips decoded meanwhile, on the decoder - a call on
      // a quiet channel would otherwise start the frame it was made, decoding them on the game's thread)
      if (queue[best].age < kDecodeLead) return Tx();
      Tx tx = queue[best];
      queue.erase(queue.begin() + best);
      if (valid && !valid(tx)) { dropped++; return update(0.f); }   // overtaken by events (a go-around, a runway change, a phase gone by): never said
      start(tx);
    }
  }
  Tx out = std::move(started);
  started = Tx();
  return out;
}

void AtcVoice::cancel() {
  queue.clear(); started = Tx(); if (busy()) g_audio.voiceStop(); playingPrio = -1; playingText.clear();
  if (decoder) { std::lock_guard<std::mutex> lk(decoder->m); decoder->todo.clear(); }   // (nothing waits to be said)
}
