// Solace Express - tower controller voices (see atc.h)
#include "atc.h"
#include "audio.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>

static const char* kVoiceName[AtcVoice::kVoices] = {"tower_north", "tower_coast", "tower_valley"};

bool AtcVoice::load(const std::string& d) {
  dir = d; clips.clear(); lookup.clear(); exact.clear();   // (a reload with removed ids leaves no stale references)
  std::ifstream f(dir + "/voice_index.txt");
  if (!f) return false;
  std::string ln;
  while (std::getline(f, ln)) {
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

// A port of the voice pack's reference resolver (resolve_message.py): the line recorded for a message the game shows,
// or the message assembled from fragments (weather, flap / pod settings, checkpoints, landing ratings, warnings ...).
bool AtcVoice::resolve(const std::string& msg, const std::string& mission, bool /*pad*/, Tx& out) const {
  out = Tx(); out.text = msg;
  auto finish = [&](const std::string& sp, int prio) {
    for (auto& id : out.ids) if (id.empty() || !clips.count(id)) { out.ids.clear(); return false; }   // (a fragment the pack lacks)
    out.prio = prio; out.radio = sp == "tower" || sp == "spectre";
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
  static const std::regex windShiftRe(R"(Autopilot: the wind has shifted - now runway (\d{2}) at ([A-Z]{3}))");
  if (std::regex_match(msg, m, windShiftRe)) {
    out.ids.push_back(atomO("aster", "wind_shift_runway")); digits("aster", m[1]); out.ids.push_back(atomO("aster", "at")); out.ids.push_back(atomO("aster", "airport_" + m[2].str()));
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
    if (!b.compare(o, 4, "fmt ")) { fmt = (int)u16(o + 8); ch = (int)u16(o + 10); rate = (int)u32(o + 12); bits = (int)u16(o + 22); }
    else if (!b.compare(o, 4, "data")) { data = o + 8; len = std::min(n, b.size() - data); }
    o += 8 + n + (n & 1);
  }
  if (!data || ch != 1 || rate <= 0 || !((fmt == 1 && bits == 16) || (fmt == 7 && bits == 8))) return false;
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
  // to the output rate (linear: the radio speech has nothing above ~6 kHz)
  double step = (double)rate / outRate;
  size_t n = (size_t)((in.size() - 1) / step) + 1;
  out.resize(n);
  for (size_t i = 0; i < n; i++) {
    double p = i * step; size_t k = (size_t)p; float t = (float)(p - k);
    out[i] = k + 1 < in.size() ? in[k] * (1 - t) + in[k + 1] * t : in[k];
  }
  return true;
}

bool AtcVoice::decodes(const std::string& id) { const std::vector<float>* p = pcm(id); return p && !p->empty(); }
std::vector<std::string> AtcVoice::indexed() const { std::vector<std::string> v; v.reserve(clips.size()); for (auto& c : clips) v.push_back(c.first); return v; }

const std::vector<float>* AtcVoice::pcm(const std::string& id) {
  auto it = clips.find(id);
  if (it == clips.end()) return nullptr;
  Clip& c = it->second;
  if (!c.loaded) { c.loaded = true; if (!readWav(dir + "/" + c.file, c.pcm, g_audio.sampleRate)) c.pcm.clear(); }
  return c.pcm.empty() ? nullptr : &c.pcm;
}

// squelch click, the clips with 35 ms between fragments (a longer pause between sentences), and a squelch tail
std::shared_ptr<std::vector<float>> AtcVoice::assemble(const Tx& tx) {
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
    if (id.empty()) { out->insert(out->end(), (size_t)(0.22f * sr), 0.f); continue; }
    const std::vector<float>* p = pcm(id);
    if (!p) continue;
    if (any) out->insert(out->end(), (size_t)(0.035f * sr), 0.f);
    out->insert(out->end(), p->begin(), p->end());
    any = true;
  }
  if (sq) { out->insert(out->end(), (size_t)(0.03f * sr), 0.f); squelch(0.14f, 0.22f, true); }
  if (!any) out->clear();
  return out;
}

void AtcVoice::start(const Tx& tx) {
  auto buf = assemble(tx);
  if (buf->empty()) return;
  g_audio.voicePlay(buf);
  playingPrio = tx.prio; idleT = 0;
  started = tx; history.push_back((tx.subtitle ? "TWR " : "") + tx.text);
  if (historyLimit && history.size() > historyLimit) history.erase(history.begin(), history.begin() + (history.size() - historyLimit));
}

bool AtcVoice::busy() const { return g_audio.voiceBusy(); }

void AtcVoice::say(const Tx& tx) {
  if (!ok()) return;
  if (valid && !valid(tx)) { dropped++; return; }   // already overtaken by events: never queued
  // urgent: cut in (a go-around over a routine call; a hazard, 95 and up, over anything routine such as a landing clearance)
  if (busy() && (tx.prio >= playingPrio + 30 || (tx.prio >= kHazard && playingPrio < kHazard))) {
    g_audio.voiceStop();
    queue.clear();
    start(tx);
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
}

// Returns each started transmission exactly once: one started here, or one that cut in from say() since the last call
AtcVoice::Tx AtcVoice::update(float dt) {
  if (!busy()) {
    playingPrio = -1;
    idleT += dt;
    if (started.ids.empty() && !queue.empty() && idleT > 0.6f) {   // the most important waiting call, oldest first
      size_t best = 0;
      for (size_t i = 1; i < queue.size(); i++) if (queue[i].prio > queue[best].prio) best = i;
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

void AtcVoice::cancel() { queue.clear(); started = Tx(); if (busy()) g_audio.voiceStop(); playingPrio = -1; }
