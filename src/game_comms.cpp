// Somnium Volandi - the voices: the tower, comms, toasts and the audio engine's parameters
#include "road_traffic.h"
#include "volcano_effects.h"
#include "game.h"
#include "earth_charts.h"
#include "sky.h"
#include "load_pacer.h"
#include "world_load_progress.h"
#include <chrono>
#include <ctime>
#include <thread>
#include <stdexcept>
#include "airport_layout.h"
#include "entities.h"
#include "entity_lod.h"
#include "hive_world_collision.h"
#include "scenery.h"
#include "regional_surface.h"
#include "models.h"
#include "menu_layout.h"
#include "hangar_catalog.h"
#include "military_missions.h"
#include "game_internal.h"

void Game::callout(const std::string& s) { if (screen == SCR_FLIGHT || screen == SCR_LOADING) commsPending.push_back({s, contract.id}); }
void Game::toast(const std::string& s, vec3 col, bool voiced) {
  toasts.push_back({s, 0.f, col});
  if (voiced && (screen == SCR_FLIGHT || screen == SCR_LOADING)) commsPending.push_back({s, contract.id});
  if (toasts.size() > 5) toasts.erase(toasts.begin());
}

// ------------------------------------------------------------------ audio
// ------------------------------------------------------------------ tower controller
// The towers speak only from what is actually happening on this flight: a greeting at the departure airport, the
// takeoff clearance for the runway the aircraft is on (with the real wind), a handoff once it is clear of the field
// (or "remain in the pattern" for circuits), pattern-entry or straight-in instructions on the way into the
// destination, the landing clearance once the aircraft is established on final for a runway, and "exit the runway"
// on the landing roll. A go-around (climbing away after the clearance) brings a fresh approach call and clearance.
// Only fields with a tower (size 1 and up) talk; the strips are uncontrolled.
// They call the aircraft by its registration (the one painted on it): in full on first contact with each tower, then
// abbreviated (SX-ABC: "Sierra X-ray Alfa Bravo Charlie", then "Sierra Bravo Charlie"). The AI traffic at the field
// is in the exchange too: a departure holds for traffic on final or on the runway, an arrival is told it is number two
// behind an aircraft ahead of it on final (with a wake caution behind a much heavier one), the landing clearance waits
// while the runway is occupied, and an aircraft still on the runway on short final means a go-around.
void Game::updateAtc(float dt) {
  if (!atc.ok()) return;
  AtcFlight& F = atcF;
  F.t += dt; F.waitT += dt;
  if (F.dep < 0 || F.arr < 0) return;
  const float kt = MS_TO_KT;
  float agl = plane.agl(), gs = length(vec3(plane.vel.x, 0, plane.vel.z));
  if (!plane.onGround && agl > 15.f) F.airborne = true;
  // (the hold counts from where the aircraft is once the call has been heard - the channel free again - not from where
  // it was when the call was queued: it tripped while still being spoken - the review of v3.44.0, FLT-6)
  if (F.holding && !F.holdHeard && !atc.busy()) { F.holdHeard = true; F.holdPos = plane.pos; }
  if (F.holding && F.holdHeard && F.phase == 1 && (length(plane.pos - F.holdPos) > 40.f || F.airborne)) { result.holdViolated = true; F.holding = false; }   // (moved off the hold, or took off)
  const std::string reg = registrationOf(*plane.spec);   // "SX-ABC"
  auto callsign = [&](int v, bool arrival, std::vector<std::string>& ids, std::string& txt) {
    int k = arrival && F.dep != F.arr;
    bool full = !F.called[k];
    std::string say = full ? reg.substr(0, 2) + reg.substr(3) : reg.substr(0, 1) + reg.substr(4);
    for (char ch : say) ids.push_back(atc.alpha(v, ch));
    ids.push_back(AtcVoice::kPause);
    txt = (full ? reg : reg.substr(0, 1) + "-" + reg.substr(4)) + ", ";
    return full ? k + 1 : 0;   // the Tx tag: updateComms marks the full form heard when it starts
  };
  // the AI traffic at an airport as its tower sees it: anything on the runway (lining up, on its takeoff run or its
  // landing roll), and the nearest aircraft on final or turning onto it, with its distance from the field
  struct RwyTraffic { bool onRunway = false, departing = false; const TrafficCraft* fin = nullptr; float finD = 1e9f; };
  auto rwyTraffic = [&](int ai) {
    RwyTraffic r; const Airport& ap = g_world.airports[ai];
    for (auto& o : traffic.craft) {
      if (!o.alive || o.role != TrafficCraft::AIRPORT || o.airport != ai) continue;
      if (o.state == TrafficCraft::LINEUP || o.state == TrafficCraft::TAKEOFF) r.onRunway = r.departing = true;
      if (o.state == TrafficCraft::ROLLOUT) r.onRunway = true;
      vec3 d = o.pos - ap.pos(); d.y = 0;
      bool fin = o.state == TrafficCraft::FINAL || (o.state == TrafficCraft::CIRCUIT && o.wp >= 3);
      if (fin && length(d) < 7000.f && length(d) < r.finD) { r.fin = &o; r.finD = length(d); }
    }
    return r;
  };
  auto runway = [&](int v, int n, std::vector<std::string>& ids) { ids.push_back(atc.digit(v, n / 10)); ids.push_back(atc.digit(v, n % 10)); };
  auto wind = [&](int v, std::vector<std::string>& ids, std::string& txt) {   // the actual wind, or nothing when calm
    int spd = (int)lroundf(wx.windSpeed * kt), gst = (int)lroundf((wx.windSpeed + wx.gust) * kt);
    if (spd < 3) return;
    int hdg = (int)lroundf(wrapDeg360(wx.windFrom) / 10.f) * 10; if (hdg == 0) hdg = 360;
    ids.push_back(atc.atom(v, "wind"));
    for (int d : {hdg / 100, (hdg / 10) % 10, 0}) ids.push_back(atc.digit(v, d));
    ids.push_back(atc.atom(v, "at"));
    auto num = [&](int k) { if (k >= 10) ids.push_back(atc.digit(v, k / 10)); ids.push_back(atc.digit(v, k % 10)); };
    num(spd);
    txt += fmt("wind %03d at %d", hdg, spd);
    if (gst >= spd + 5) { ids.push_back(atc.atom(v, "gusting")); num(gst); txt += fmt(" gusting %d", gst); }
    ids.push_back(atc.atom(v, "knots")); txt += " knots. ";
    ids.push_back(AtcVoice::kPause);
  };
  // the arrival tower is the operational destination's: the field the autopilot is landing at when that isn't the
  // contract's (a GPS alternate, a glide to the nearest field) - a fresh inbound call to it; the contract's own
  // destination is untouched (the review of v3.44.0, FLT-5)
  if (plane.apOn && (plane.apMode == Plane::AP_NAV || plane.apMode == Plane::AP_APPR) && plane.apAirport >= 0 && plane.apAirport != F.arr && F.airborne && !plane.onGround) {
    F.arr = plane.apAirport;
    if (F.phase >= 4 && F.phase <= 5) { F.phase = 3; F.waitT = 0; F.trafficSaid = false; F.goAround = F.goAroundUnable = false; }
  }
  const Airport& D = g_world.airports[F.dep];
  const Airport& A = g_world.airports[F.arr];
  int vd = atcStation(F.dep), va = atcStation(F.arr);
  // the smallest fields (farm strips, island and mountain strips: size 0) have no tower: nothing is said there, and a
  // departure from one goes straight to the arrival exchange (if the destination has a tower)
  if (F.phase <= 2 && D.size == 0) F.phase = 3;
  if (F.phase >= 3 && A.size == 0) return;
  switch (F.phase) {
    case 0:   // on the ground at the departure airport: the greeting
      if (F.t > 2.5f) {
        const char* key = timeOfDay < 12.f ? "greeting_morning" : timeOfDay < 18.f ? "greeting_afternoon" : "greeting_evening";
        AtcVoice::Tx tx; tx.prio = 10; tx.subtitle = true; tx.group = "tower";
        tx.tag = callsign(vd, false, tx.ids, tx.text);
        tx.ids.push_back(atc.line(vd, key)); tx.text += atc.text(tx.ids.back());
        tx.apt = F.dep; F.phase = 1; tx.key = atcKey(); atc.say(tx); F.waitT = 0;
      }
      break;
    case 1:   // lined up on the runway: cleared for takeoff (straight away if the aircraft is already rolling)
      if ((F.spoken >= 1 && F.waitT > 4.f && !atc.busy()) || gs > 4.f) {   // (after the greeting has been heard)
        // traffic landing or departing first: hold (the clearance comes once it's clear; after 90 s the tower clears the
        // traffic out of the way first - never the player onto it: the review of v3.44.0, FLT-4)
        if (F.trafficT >= 90.f) { traffic.clearRunway(F.dep); F.trafficT = 0; }
        RwyTraffic rt = rwyTraffic(F.dep);
        if (gs <= 4.f && (rt.onRunway || (rt.fin && rt.finD < 4500.f))) {
          F.trafficT += dt;
          if (!F.trafficSaid) {
            AtcVoice::Tx tx; tx.prio = 85; tx.subtitle = true; tx.group = "tower";
            tx.tag = callsign(vd, false, tx.ids, tx.text);
            tx.ids.push_back(atc.line(vd, rt.departing ? "hold_departure" : rt.onRunway ? "hold_position" : "hold_arrival"));
            tx.text += atc.text(tx.ids.back());
            tx.apt = F.dep; tx.key = atcKey(); atc.say(tx); F.trafficSaid = true; F.waitT = 0;
            F.holding = true; F.holdHeard = false; F.holdPos = plane.pos;
          }
          break;
        }
        F.holding = false;
        AtcVoice::Tx tx; tx.prio = 90; tx.subtitle = true; tx.group = "tower";
        tx.tag = callsign(vd, false, tx.ids, tx.text);
        wind(vd, tx.ids, tx.text);
        tx.ids.push_back(atc.atom(vd, "runway")); runway(vd, D.rwyNumber(F.depRev), tx.ids); tx.ids.push_back(atc.atom(vd, "cleared_takeoff"));
        tx.text += fmt("Runway %02d, cleared for takeoff.", D.rwyNumber(F.depRev));
        tx.apt = F.dep; F.phase = 2; tx.key = atcKey(); atc.say(tx); F.trafficSaid = false; F.trafficT = 0;
      }
      break;
    case 2:   // climbing out, clear of the field: handed on (circuits stay with the tower)
      if (F.airborne && agl > 120.f && length(plane.pos - D.pos()) > 1500.f) {
        AtcVoice::Tx tx; tx.prio = 40; tx.subtitle = true; tx.group = "tower";
        if (F.dep == F.arr) { tx.ids.push_back(atc.line(vd, "remain_pattern")); tx.text = atc.text(tx.ids[0]); }
        else { tx.ids = {atc.line(vd, "contact_departure"), AtcVoice::kPause, atc.line(vd, "good_day")}; tx.text = atc.text(tx.ids[0]) + " " + atc.text(tx.ids[2]); }
        tx.apt = F.dep; F.phase = F.dep == F.arr ? 4 : 3; tx.key = atcKey(); atc.say(tx); F.waitT = 0;
        if (F.dep == F.arr) F.arrRev = F.depRev;
      }
      break;
    case 3: {   // inbound: within 9 km and below 1500 m, heading for the field: the arrival runway and how to join
      vec3 toA = A.pos() - plane.pos; toA.y = 0;
      float dist = length(toA);
      if (F.airborne && dist < 9000.f && agl < 1500.f && dot(plane.vel, toA) > 0.f) {
        // the runway end: the one the aircraft is already lined up on (unless that means a tailwind of 10 kt or
        // more), else the one into the wind, or in light winds the one facing the aircraft's arrival
        auto head = [&](bool rev) { return A.heading + (rev ? 180.f : 0.f); };
        float wkt = wx.windSpeed * kt;
        int lined = -1;
        for (int r = 0; r < 2; r++) {
          vec3 d = r ? -A.dir() : A.dir(), q = plane.pos - A.threshold(r != 0); q.y = 0;
          float al = dot(q, d), la = fabsf(q.x * d.z - q.z * d.x);
          float tail = -cosf((wx.windFrom - head(r != 0)) * DEG) * wkt;
          if (al < -500.f && al > -12000.f && la < 0.25f * -al + 300.f && cosf((plane.heading() - head(r != 0)) * DEG) > 0.9f && tail < 10.f) lined = r;
        }
        if (lined >= 0) F.arrRev = lined == 1;
        else if (wkt >= 6.f) F.arrRev = cosf((wx.windFrom - head(true)) * DEG) > cosf((wx.windFrom - head(false)) * DEG);
        else F.arrRev = dot(plane.pos - A.pos(), A.dir()) > 0.f;
        vec3 ld = F.arrRev ? -A.dir() : A.dir(), thr = A.threshold(F.arrRev);
        vec3 rel = plane.pos - thr; rel.y = 0;
        float along = dot(rel, ld), lat = fabsf(rel.x * ld.z - rel.z * ld.x);
        float hdgAl = cosf((plane.heading() - head(F.arrRev)) * DEG);
        bool straight = along < -2000.f && lat < 0.4f * -along && hdgAl > 0.7f;
        int n = A.rwyNumber(F.arrRev);
        AtcVoice::Tx tx; tx.prio = 60; tx.subtitle = true; tx.group = "tower";
        tx.tag = callsign(va, true, tx.ids, tx.text);
        tx.ids.push_back(atc.atom(va, straight ? "make_straight_in" : "enter_left_downwind")); runway(va, n, tx.ids);
        tx.ids.push_back(atc.atom(va, "report_final"));
        tx.text += straight ? fmt("Make straight-in runway %02d, report final.", n) : fmt("Enter left downwind runway %02d, report final.", n);
        // the sequence: an aircraft on final closer in than this one lands first
        RwyTraffic rt = rwyTraffic(F.arr);
        if (rt.fin && rt.finD < dist) {
          tx.ids.push_back(AtcVoice::kPause); tx.ids.push_back(atc.line(va, "number_two")); tx.text += " " + atc.text(tx.ids.back());
          if (kAircraft[rt.fin->spec].emptyMass > 2.5f * plane.spec->emptyMass) {
            tx.ids.push_back(AtcVoice::kPause); tx.ids.push_back(atc.line(va, "wake_caution")); tx.text += " " + atc.text(tx.ids.back());
          }
        }
        tx.apt = F.arr; F.phase = 4; tx.key = atcKey(); atc.say(tx); F.waitT = 0; F.trafficSaid = false;
      }
      break;
    }
    case 4: {   // established on final for that runway: cleared to land
      vec3 ld = F.arrRev ? -A.dir() : A.dir(), thr = A.threshold(F.arrRev);
      vec3 rel = plane.pos - thr; rel.y = 0;
      float along = dot(rel, ld), lat = fabsf(rel.x * ld.z - rel.z * ld.x);
      float hdgAl = cosf((plane.heading() - (A.heading + (F.arrRev ? 180.f : 0.f))) * DEG);
      if (F.airborne && !plane.onGround && along < 0.f && along > -6000.f && lat < 500.f && agl < 500.f && hdgAl > 0.82f) {
        int n = A.rwyNumber(F.arrRev);
        // the runway isn't free: an aircraft on it, or one ahead on final. Continue the approach until it is; still
        // occupied on short final: go around
        RwyTraffic rt = rwyTraffic(F.arr);
        vec3 toA = A.pos() - plane.pos; toA.y = 0;
        if (rt.onRunway || (rt.fin && rt.finD < length(toA) - 300.f)) {
          AtcVoice::Tx tx; tx.subtitle = true; tx.group = "tower";
          if (rt.onRunway && along > -700.f && agl < 90.f) {
            tx.prio = 99; tx.tag = callsign(va, true, tx.ids, tx.text);
            tx.ids.push_back(atc.line(va, "go_around_aircraft")); tx.text += atc.text(tx.ids.back());
            tx.apt = F.arr; F.phase = 5; tx.key = atcKey(); atc.say(tx); F.waitT = 0; F.trafficSaid = false; F.goAround = true;
            // (on the autopilot's approach it goes around itself - or, unable to climb away, lands: not the player's doing)
            F.goAroundUnable = plane.apOn && plane.apAirport >= 0 && !plane.apTowerGoAround();
          } else if (!F.trafficSaid) {
            tx.prio = 80; tx.tag = callsign(va, true, tx.ids, tx.text);
            tx.ids.push_back(atc.line(va, rt.onRunway ? "runway_occupied" : "clearance_follows")); tx.text += atc.text(tx.ids.back());
            tx.apt = F.arr; tx.key = atcKey(); atc.say(tx); F.trafficSaid = true;
          }
          break;
        }
        AtcVoice::Tx tx; tx.prio = 90; tx.subtitle = true; tx.group = "tower";
        tx.tag = callsign(va, true, tx.ids, tx.text);
        wind(va, tx.ids, tx.text);
        tx.ids.push_back(atc.atom(va, "runway")); runway(va, n, tx.ids); tx.ids.push_back(atc.atom(va, "cleared_land"));
        tx.text += fmt("Runway %02d, cleared to land.", n);
        tx.apt = F.arr; F.phase = 5; tx.key = atcKey(); atc.say(tx); F.waitT = 0; F.trafficSaid = false;
      } else if (F.airborne && plane.onGround && length(plane.pos - A.pos()) < 3000.f) F.phase = 5;   // landed without the clearance
      break;
    }
    case 5:   // cleared: the landing roll, or a go-around (climbing away again: a fresh approach call)
      // (until the wheels are on, the runway is watched: traffic on it takes the clearance back - continue, or go
      // around on short final, as before it was given: the review of v3.44.0, FLT-4)
      if (!F.goAround && F.airborne && !plane.onGround && !touchedDown && rwyTraffic(F.arr).onRunway) { F.phase = 4; F.trafficSaid = false; break; }
      if (plane.onGround && length(plane.pos - A.pos()) < 3000.f) {
        if (gs < 18.f) {
          AtcVoice::Tx tx; tx.prio = 55; tx.subtitle = true; tx.group = "tower"; tx.ids.push_back(atc.line(va, "exit_when_able")); tx.text = atc.text(tx.ids[0]);
          tx.apt = F.arr; F.phase = 6; tx.key = atcKey(); atc.say(tx);
        }
      } else if (!plane.onGround && agl > 180.f && plane.vel.y > 2.f && F.waitT > 20.f) { F.phase = F.dep == F.arr ? 4 : 3; F.waitT = 0; F.lastValid = false; F.goAround = F.goAroundUnable = false; }   // (went around: the clearance no longer stands)
      if (F.goAround && !plane.onGround && agl > 150.f && plane.vel.y > 1.f) F.goAround = F.goAroundUnable = false;   // (complied: climbing away)
      break;
    default: break;
  }
}

// The comms channel, every frame: what the game showed this frame is resolved to voice lines (AtcVoice::resolve)
// and queued; the warnings speak on their rising edge (and again if they persist); the towers run their exchange.
// Nothing stale is ever said: the queue is dropped on pause, at the end of the flight and when the aircraft
// crashes (then only the crash itself is announced).
void Game::announceFlightEnd(const std::string& message, const std::string& followup) {
  // Explicit results and their landing feedback survive the screen transition.
  atc.cancel(); commsPending.clear(); commsEndScreen=-1;
  AtcVoice::Tx tx;
  if(atc.resolve(message,contract.id,padPrompts(),tx)) {
    tx.prio=90; atc.say(tx); commsEndScreen=int(screen); commsEndTime=0;
    if(!followup.empty() && atc.resolve(followup,contract.id,padPrompts(),tx)) {tx.prio=35;atc.say(tx);}
  }
}

void Game::updateComms(float dt) {
  bool live = (screen == SCR_FLIGHT || screen == SCR_LOADING) && !paused && !launchCardPending();
  if(commsEndScreen>=0) {
    if(int(screen)!=commsEndScreen || paused) {commsEndScreen=-1;atc.cancel();}
    else {
      atc.update(dt); commsEndTime+=dt;
      voiceDuck=approach(voiceDuck,atc.busy()?1.f:0.f,atc.busy()?6.f:1.5f,dt);
      if(commsEndTime>1.f && !atc.pending()) commsEndScreen=-1;
      return;
    }
  }
  // (paused, or in the menus, nobody is talking: the music comes back up - it stayed at the duck when a pause cut a
  // call short: the review of v3.44.0, AUD-2)
  if (!live) { atc.cancel(); commsPending.clear(); commsCrashSeen = false; voiceDuck = approach(voiceDuck, 0.f, 1.5f, dt); return; }
  auto lessonUnavailable = [&]() {
    if (lessonVoiceWarned || voicesOff) return;
    lessonVoiceWarned = true;
    toast("Lesson recording unavailable. Use the on-screen controls; check the voice assets.", vec3(1.f, 0.8f, 0.4f), false);
  };
  if (!atc.ok()) {
    for (const auto& m : commsPending) if (m.lessonPhase >= 0) { lessonUnavailable(); break; }
    commsPending.clear(); voiceDuck = 0.f; return;
  }
  if (crashed && !commsCrashSeen) { atc.cancel(); commsCrashSeen = true; }
  if (!crashed) commsCrashSeen = false;
  if (screen == SCR_FLIGHT && !crashed && flightClock > 2.f) {   // the HUD's warnings (game_ui.cpp), spoken as they come on
    bool w[4] = {plane.stallWarn > 0.8f && !plane.onGround && plane.ias > 10.f,
                 !plane.onGround && plane.agl() < 120 && plane.vel.y < -7.f,
                 gearWarning(),
                 !plane.engineRunning && engineAutoStarted && plane.starterTime <= 0 && !plane.glideOnly() && !(engineShutDown && plane.onGround)};   // (a failed engine has no restart to advise; one shut down at the stand, none needed)
    const char* say[4] = {"STALL", "PULL UP", "GEAR!", nullptr};
    for (int i = 0; i < 4; i++) {
      if (w[i] && (!warnWas[i] || realTime - warnLastT[i] > 8.f)) {
        std::string m = say[i] ? say[i] : "ENGINE OFF - press " + actLabel(ACT_ENGINE, padPrompts(), true) + " to restart";
        AtcVoice::Tx tx;
        if (atc.resolve(m, contract.id, padPrompts(), tx)) { tx.prio = std::max(tx.prio, 95); tx.key = kWarnKey + i; atc.say(tx); }   // (said only while it still holds)
        warnLastT[i] = realTime;
      }
      warnWas[i] = w[i];
    }
    // the failure annunciators (hudAnnunciators), spoken when they come on or change severity: each wording once
    for (auto& a : hudAnnunciators()) {
      auto it = failVoiced.find(a.slot);
      if (it != failVoiced.end() && it->second == a.key) continue;
      AtcVoice::Tx tx;
      if (atc.resolve(a.text, contract.id, padPrompts(), tx)) { atc.say(tx); failVoiced[a.slot] = a.key; }
    }
  }
  for (const auto& m : commsPending) {
    AtcVoice::Tx tx;
    bool resolved = m.lessonPhase >= 0 ? atc.resolveLesson(m.mission, m.lessonPhase, m.text, tx) : atc.resolve(m.text, m.mission, padPrompts(), tx);
    if (m.lessonPhase >= 0 && resolved) for (const auto& id : tx.ids) resolved = atc.decodes(id) && resolved;
    if (resolved) atc.say(tx);
    else if (m.lessonPhase >= 0) lessonUnavailable();
  }
  commsPending.clear();
  if (screen == SCR_FLIGHT && !crashed && !researchFlight && !militaryFlight) updateAtc(dt);
  // while someone is talking the music and the engine sit lower (a headset's comms priority): quick down, slow up
  voiceDuck = approach(voiceDuck, atc.busy() && live ? 1.f : 0.f, atc.busy() ? 6.f : 1.5f, dt);
  AtcVoice::Tx st = atc.update(dt);
  if (!st.ids.empty() && st.subtitle) {
    toast("TOWER  " + st.text, vec3(0.55f, 1.f, 0.72f), false); atcF.spoken++;
    atcF.lastCall = st.text; atcF.lastApt = st.apt; atcF.lastT = flightClock; atcF.lastValid = true;
  }
  if (!st.ids.empty() && st.tag > 0) atcF.called[st.tag - 1] = true;
}

void Game::feedAudio() {
  AudioParams ap;
  ap.scene = (screen == SCR_FLIGHT || (screen == SCR_DEBRIEF && crashed)) && plane.spec;
  ap.paused = paused;
  if (ap.scene && !audioSceneWas) { ++audioSceneId; audioCameraValid=false; audioMachArmed=true; audioPlayerCone=-1e9f; }
  audioSceneWas=ap.scene; ap.sceneId=audioSceneId;
  ap.wrapPeriod=g_world.repeating?WRAP_SPAN:0;
  ap.listener.position=dbgCam?dbgCamPos:camPos;
  if (plane.spec) {
    ap.aircraftPosition=plane.pos; ap.aircraftVelocity=plane.vel; ap.aircraftRotation=plane.q;
    ap.cabinEye=kModels[std::max(0, typeOf(plane.spec))].eye; ap.soundSpeed=plane.soundSpeed;
    ap.interior=camMode==1 && !dbgCam && !crashed;
    vec3 forward=ap.interior?plane.q.rotate(quat::axisAngle(vec3(0,1,0),lookYaw).rotate(quat::axisAngle(vec3(1,0,0),lookPitch).rotate(vec3(0,0,-1)))):
      normalize(plane.pos+vec3(0,plane.spec->fusRad*.3f,0)-ap.listener.position);
    vec3 up=ap.interior?plane.up():vec3(0,1,0);
    if(camMode==0 && !dbgCam) up=normalize(lerp(vec3(0,1,0),camQ.rotate(vec3(0,1,0)),.3f));
    if(dbgCam) forward=normalize(dbgCamLook-dbgCamPos);
    ap.listener.forward=forward; ap.listener.right=normalize(cross(forward,up)); ap.listener.up=cross(ap.listener.right,forward);
    vec3 movement=audioDisplacement(audioCameraPrevious,ap.listener.position,ap.wrapPeriod);
    bool continuous=audioCameraValid && audioCameraMode==camMode && lastDt>1e-4f && length(movement)<2000.f;
    ap.listener.velocity=continuous?movement/(lastDt*(dbgCam?1.f:std::max(timeAccel,1.f))):(camMode==3?vec3():plane.vel);
    if(length(ap.listener.velocity)>2000) ap.listener.velocity=plane.vel; // camera cuts are not supersonic ears
    audioCameraPrevious=ap.listener.position;audioCameraValid=ap.scene;audioCameraMode=camMode;
    float cone=audioMachCone(plane.pos,plane.vel,ap.listener.position,plane.soundSpeed,ap.wrapPeriod);
    audioBoomCooldown=std::max(0.f,audioBoomCooldown-lastDt);
    if(ap.scene && !paused && !ap.interior && camMode==3 && audioPlayerCone>-1e8f && audioPlayerCone<0 && cone>=0 && audioBoomCooldown<=0) {
      g_audio.triggerAt(SFX_BOOM,plane.pos,.85f);audioBoomCooldown=6.f;
      shake(0.45f);   // (the shock front's double jolt reaching the flyby camera)
    }
    audioPlayerCone=cone;
  }
  ap.master = set.master; ap.engineVol = set.engineVol * (1.f - 0.35f * voiceDuck); ap.sfxVol = set.sfxVol; ap.voiceVol = set.atcVol;
  {   // the radio music, at the master volume (the review of v3.44.0, AUD-1), lowered under speech
    // (Radio::setVolume reaches the music ring only while the radio plays: idle, the ring is the score's - updateMusic)
    const float rv = radioLevel();
    if (rv != radioSentVol) { radio.setVolume(rv); radioSentVol = rv; }
  }
  static bool muffled = false;
  if (actKeyP(ACT_ANR) || actJoyP(ACT_ANR) || (screen == SCR_FLIGHT && !paused && actPadP(ACT_ANR))) { muffled = !muffled; toast(muffled ? "Engine noise muffled (headset ANR on)" : "Headset ANR off"); }
  ap.muffled = muffled;
  if (screen == SCR_FLIGHT && plane.spec && !crashed) {
    const AircraftSpec& s = *plane.spec;
    ap.inFlight = true;
    ap.signature = clampf(powf((s.engineType==ENG_JET?30000.f:200000.f)/std::max(s.power,1.f),.055f),.82f,1.2f);
    ap.engineType = s.engineType; ap.engines = s.engines; ap.cylinders = s.cylinders; ap.blades = s.blades;
    ap.research = s.special != 0; ap.nozzle = plane.nozzle; ap.mach = plane.mach;
    ap.rpm = s.engineType == ENG_JET ? plane.n1 : plane.rpm; ap.maxRpm = s.maxRpm; ap.n1 = plane.n1;
    ap.spool = plane.engineSpool; ap.throttle = plane.engineRunning ? plane.ctl.throttle : 0.f;
    ap.running = plane.engineRunning; ap.cranking = !plane.engineRunning && plane.starterTime > 0;
    for (int e = 0; e < 4; e++) ap.engineHealth[e] = plane.fail.engineHealth[e];
    ap.airspeed = plane.airspeed; ap.groundSpeed = length(vec3(plane.vel.x, 0, plane.vel.z)); ap.onGround = plane.onGround; ap.rough = plane.groundRough;
    static_assert(SURF_ASPHALT==int(AudioSurface::Paved) && SURF_GRASS==int(AudioSurface::Grass) &&
      SURF_GRAVEL==int(AudioSurface::Gravel) && SURF_SNOW==int(AudioSurface::Snow) && SURF_SAND==int(AudioSurface::Sand) &&
        SURF_WATER==int(AudioSurface::Water),"audio ground materials match the world");
    int groundAirport=plane.onGround?g_world.onRunway(plane.pos.x,plane.pos.z,4.f):-1;
    ap.groundSurface=plane.onWater?AudioSurface::Water:groundAirport>=0?AudioSurface(g_world.airports[groundAirport].surface):AudioSurface::Grass;
    ap.stallWarn = plane.onGround ? 0.f : plane.stallWarn; ap.stallIsShaker = s.engineType == ENG_JET || s.license == LIC_ATP;
    ap.gearMoving = plane.gear > 0.01f && plane.gear < 0.99f; ap.flapsMoving = fabsf(plane.flaps - plane.ctl.flaps) > 0.02f;
    ap.gearDown = plane.gear; ap.flaps = plane.flaps;
    const int model = std::max(0, typeOf(plane.spec));
    const auto& md=kModels[model];
    ap.engineRadius=clampf(sqrtf(std::max(s.power,1.f))*(s.engineType==ENG_PISTON?.025f:.13f),9.f,50.f);
    ap.gearPosition=vec3(0,-plane.gearHeight(),gearStations(s).mainZ);
    ap.slip=dot(plane.vel-plane.windVel,plane.right())/std::max(plane.airspeed,1.f);
    ap.buffet=clampf(plane.stallWarn*.6f+plane.wxl.sigma*.3f+expf(-powf((plane.mach-.98f)*16.f,2))*.25f,0,1);
    if(s.special) {
      vec3 xp[4],xd[4];float strength[4];int count=jetExhausts(plane,xp,xd,strength);
      for(int e=0;e<count;++e) {ap.enginePosition[e]=xp[e];ap.engineDirection[e]=xd[e];ap.enginePower[e]=s.special==2?clampf(plane.podThr[e],0,1.6f):1.f;}
    } else {
      const auto& geometry=aeroGeom(s);
      for(int e=0;e<std::min(s.engines,4);++e) {
        ap.enginePosition[e]=e<geometry.nEng?geometry.eng[e].pos:vec3(s.engines==1?0.f:(e%2?md.nacX:-md.nacX),md.nacY,md.nacZ0);
        ap.engineDirection[e]=vec3(0,0,1);
      }
    }
    // (the rain drumming on the airframe as hard as it rains here, louder the faster it meets it; the wind noise
    // gusting with the eddies and gust bursts the aircraft is flying through)
    ap.rain = wx.precip == 1 ? clampf(rainNow * (0.75f + 0.35f * smoothstepf(10.f, 70.f, plane.airspeed)), 0.f, 1.f) : 0.f;
    ap.turbulence = clampf(0.6f * plane.wxl.sigma + 0.3f * plane.gustBurst, 0.f, 1.f);
    ap.paused = paused;
  }
  if (ap.scene) {
    // Retain IDs across ordering changes: sorting by audibility never swaps an engine's oscillator state.
    std::vector<std::pair<float,AudioEmitter>> candidates;
    auto offer=[&](AudioEmitter e) {
      float distance=length(audioDisplacement(ap.listener.position,e.position,ap.wrapPeriod));
      if(distance<6500) candidates.push_back({e.level*e.radius/std::max(e.radius,distance),e});
    };
    if(set.traffic) for(const auto& c:traffic.craft) if(c.alive && c.state!=TrafficCraft::PARKED) {
      const auto& sp=kAircraft[c.spec]; AudioEmitter e;e.id=10000+c.id;e.position=c.pos;e.velocity=c.vel;e.forward=c.q.rotate(vec3(0,0,-1));
      e.engineType=sp.engineType;e.cylinders=sp.cylinders;e.blades=sp.blades;e.spool=clampf(c.throttle,0,1);
      e.n1=22+e.spool*78;e.rpm=sp.maxRpm*(.3f+.7f*e.spool);e.radius=sp.engineType==ENG_PISTON?22:55;e.level=.7f;offer(e);
    }
    for(const auto& a:hiveCombat.actors) if(a.alive) {
      AudioEmitter e;e.id=100000+a.id;e.position=plane.pos+hiveCombat.displacement(plane.pos,a.position);e.velocity=a.velocity;
      e.forward=normalize(a.velocity);e.spool=clampf(length(a.velocity)/180.f,.25f,1.f);e.n1=30+e.spool*70;e.level=.65f;e.radius=45;offer(e);
    }
    for(size_t i=0;i<wreck.size();++i) if(wreck[i].fire>.2f && wreck[i].c.y>0) {
      AudioEmitter e;e.id=500000+uint32_t(i);e.kind=AudioEmitter::Fire;e.position=wreck[i].c;e.velocity=wreck[i].v;
      e.level=wreck[i].fire*clampf(1.2f-crashTimer*.06f,.15f,1);e.radius=8;offer(e);
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto&a,const auto&b){return a.first>b.first;});
    for(size_t i=0;i<candidates.size() && ap.emitterCount<19;++i) ap.emitters[ap.emitterCount++]=candidates[i].second;
    if(ufo.on) {AudioEmitter e;e.id=900001;e.kind=AudioEmitter::Track;e.position=ufo.pos;e.velocity=plane.vel;ap.emitters[ap.emitterCount++]=e;}
  }
  g_audio.setParams(ap);
}
