// Somnium Volandi - the pilot's profile in the game: achievements as they are earned, the airport stamps, the
// landmarks found, the types' softest touchdowns (profile.h); and the score's mood for what is on screen (music.h)
#include "game.h"
#include <cmath>

static std::string profilePath(const std::string& dir) { return dir.empty() ? std::string("profile.cfg") : dir + "/profile.cfg"; }

void Game::loadProfile() {
  if (diskless || (headless && saveDir.empty())) return;   // (a tool's or a test's session: a fresh profile, in memory)
  profile.load(profilePath(saveDir));
}

void Game::saveProfile() {
  if (diskless || (headless && saveDir.empty())) return;
  profile.save(profilePath(saveDir));
}

// An achievement earned: kept, listed in the next debrief, and shown (a banner and a chime, one at a time)
void Game::award(const std::string& id) {
  if (botControl || dbgCam) return;
  if (!profile.earn(id)) return;
  flightAch.push_back(id);
  achShow.push_back({id, 0.f});
  saveProfile();
}

void Game::profileTouchdown(float fpm, int ap) {
  if (botControl || researchFlight || militaryFlight || !plane.spec || ap < 0 || g_world.procedural) return;   // (on a runway of the islands)
  fpm = fabsf(fpm);
  const Airport& A = g_world.airports[ap];
  if (profile.airports.insert(A.code).second) {
    const int n = (int)profile.airports.size(), N = (int)g_world.airports.size();
    toast(fmt("Airport stamp: %s %s (%d of %d)", A.code, A.name, n, N), vec3(1.f, 0.85f, 0.45f), false);
    if (n >= 10) award("airports10");
    if (n >= N) award("airports_all");
  }
  const int grade = touchdownGrade(fpm);
  if (grade == TD_BUTTER) { award("butter"); if (profile.add("butters") >= 25.f) award("butter25"); }
  if (fpm < 60.f) award("greaser");
  {   // the type's softest touchdown
    auto it = profile.best.find(plane.spec->id);
    const float before = it != profile.best.end() ? it->second : -1.f;
    if (pbOld < 0.f) pbOld = before;   // (the best before this flight: a bounce doesn't move it)
    if (before < 0.f || fpm < before) { profile.best[plane.spec->id] = fpm; pbNew = pbOld >= 0.f; }
  }
  // the conditions it was flown in: the crosswind on this runway, the night, low cloud in rain or snow, no engines
  const float rel = (wx.windFrom - A.heading) * DEG;
  if (fabsf(wx.windSpeed * sinf(rel)) * MS_TO_KT > 15.f) award("crosswind");
  if (timeOfDay < 6.5f || timeOfDay > 19.5f) award("night");
  if (wx.precip > 0 && wx.cloudCover > 0.5f && wx.cloudBase - A.elev < 300.f) award("weather");
  if (plane.spec->engines > 0 && (!plane.engineRunning || plane.fail.enginesOut(plane.spec->engines) >= plane.spec->engines)) award("deadstick");
  if (result.goArounds > 0) award("go_around");
  saveProfile();
}

void Game::profileTick(float dt) {
  if (botControl || dbgCam || !plane.spec || crashed) return;
  const float gsKm = length(vec3(plane.vel.x, 0, plane.vel.z)) * dt * std::max(timeAccel, 1.f) / 1000.f;
  flightKm += gsKm;
  if (timeAccel > 1.f && profile.add("warpKm", gsKm) >= 500.f) award("warp");
  profileT += dt;
  if (profileT < 1.f) return;
  profileT = 0.f;
  if (plane.mach > 1.f) award("supersonic");
  if (ufo.on) { profile.seen.insert("ufo"); award("ufo"); }
  if (traffic.escortActive()) { profile.seen.insert("display"); award("display"); }
  if (g_world.procedural || researchFlight) return;
  // a landmark found: within sight of it, low enough to make it out
  const auto& sites = profileSites();
  for (const Site& st : sites) {
    if (profile.sites.count(st.id)) continue;
    const float dx = g_world.wrap(plane.pos.x - st.pos.x), dz = g_world.wrap(plane.pos.z - st.pos.z);
    if (dx * dx + dz * dz > (st.radius + 1500.f) * (st.radius + 1500.f) || plane.pos.y - st.pos.y > 1500.f) continue;
    profile.sites.insert(st.id);
    const int n = (int)profile.sites.size();
    toast(fmt("Landmark found: %s (%d of %d)", st.name.c_str(), n, (int)sites.size()), vec3(1.f, 0.85f, 0.45f), false);
    if (n >= 5) award("sites5");
    if (n >= (int)sites.size()) award("sites_all");
    saveProfile();
  }
}

void Game::profileFlightEnd(bool success) {
  if (botControl) return;
  if (militaryFlight) { if (success) award("sortie"); saveProfile(); return; }
  if (researchFlight) { saveProfile(); return; }
  if (success) {
    award("first_flight");
    if (flightKm >= 150.f) award("long_haul");
  }
  saveProfile();
}

void Game::profileCareerCheck() {
  if (botControl) return;
  if (career.license >= LIC_PPL) award("lic_ppl");
  if (career.license >= LIC_CPL) award("lic_cpl");
  if (career.license >= LIC_ATP) award("lic_atp");
  if (!career.fleet.empty()) award("owner");
  if (career.fleet.size() >= 3) award("fleet3");
  if (!career.airline.routes.empty()) award("airline");
  if (career.money >= 1000000) award("millionaire");
  if (career.finished) award("campaign");
}

// ---- the score
int Game::musicMood() const {
  switch (screen) {
    case SCR_MENU: return MOOD_MENU;
    case SCR_HUB: case SCR_FREE_FLIGHT: case SCR_DEBRIEF: case SCR_LOADING: case SCR_REGION_LOADING:
      return finaleShown && screen == SCR_HUB && career.finished && licenceShown < 0 ? MOOD_FINALE : MOOD_HUB;
    case SCR_FLIGHT:
      if (!set.musicFlight || paused || militaryFlight || researchFlight || crashed || !plane.spec) return MOOD_NONE;
      return computePhase() == 3 && plane.agl() > 300.f ? MOOD_CRUISE : MOOD_NONE;
    default: return MOOD_NONE;
  }
}

void Game::updateMusic() {
  if (headless || diskless) return;
  // the score has the music ring while the radio is off (Radio::play takes it, and the score notices)
  const bool want = set.musicVol > 0.01f && set.master > 0.01f && radio.state() == Radio::IDLE;
  g_music.own(want);
  g_music.setMood(musicMood());
  if (g_music.owns()) {
    const float v = set.master * set.musicVol * (1.f - 0.5f * voiceDuck);
    if (fabsf(v - musicSentVol) > 1e-3f) { g_audio.musicVolume(v); musicSentVol = v; }
  } else musicSentVol = -1.f;
}
