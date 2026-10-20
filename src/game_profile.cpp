// Somnium Volandi - the pilot's profile in the game: achievements as they are earned, the airport stamps, the
// landmarks found, the types' softest touchdowns (profile.h); and the score's mood for what is on screen (music.h)
#include "game.h"
#include <cmath>

static std::string profilePath(const std::string& dir) { return dir.empty() ? std::string("profile.cfg") : dir + "/profile.cfg"; }

void Game::loadProfile() {
  if (diskless || (headless && saveDir.empty())) return;   // (a tool's or a test's session: a fresh profile, in memory)
  profile.load(profilePath(saveDir));
  if (!headless) {   // (Steam has every achievement already earned: those from before the Steam build, or earned offline)
    std::vector<std::string> ids;
    for (const auto& a : profile.ach) ids.push_back(a.first);
    steam::sync(ids);
  }
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
  if (!headless && !diskless) steam::unlock(id);   // (a tool's session in the same process earns nothing on Steam)
}

void Game::profileTouchdown(float fpm, int ap) {
  if (botControl || researchFlight || militaryFlight || !plane.spec || ap < 0 || ap >= (int)g_world.airports.size()) return;
  fpm = fabsf(fpm);
  const Airport& A = g_world.airports[ap];
  if (profile.airports.insert(airportStamp(A)).second) {   // (a region's stamps are its own: Profile::airports)
    const int N = (int)g_world.airports.size();
    if (g_world.procedural && g_world.regionRecipe) {
      const int n = profile.regionAirports(g_world.regionId);
      toast(fmt("Airport stamp: %s %s (%d of %d in the %s)", A.code, A.name, n, N, g_world.regionRecipe->descriptor.name.c_str()), vec3(1.f, 0.85f, 0.45f), false);
      award("region_first");
      if (profile.regionsLanded().size() >= 5) award("regions5");
      if (n >= N) award("region_all");
    } else {
      const int n = profile.islandAirports();
      toast(fmt("Airport stamp: %s %s (%d of %d)", A.code, A.name, n, N), vec3(1.f, 0.85f, 0.45f), false);
      if (n >= N) award("airports_all");
    }
    if (profile.airports.size() >= 10) award("airports10");
  }
  const int grade = touchdownGrade(fpm);
  if (grade == TD_BUTTER) { award("butter"); if (profile.add("butters") >= 25.f) award("butter25"); }
  if (grade == TD_BUTTER && contract.pax >= 4 && contract.type != CT_LESSON && g_music.owns() && !touchdownApplauded) { touchdownApplauded = true; g_music.sting(STING_APPLAUSE); }   // (the cabin applauds a butter landing)
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
  if (researchFlight) return;
  const bool region = g_world.procedural && g_world.regionRecipe;
  if (region && !plane.onGround && profile.seen.insert("region:" + g_world.regionId).second) saveProfile();   // (explored: the passport lists it)
  // a landmark found: within sight of it, low enough to make it out
  const auto& sites = profileSites();
  for (const Site& st : sites) {
    if (profile.sites.count(st.id)) continue;
    const float dx = g_world.wrap(plane.pos.x - st.pos.x), dz = g_world.wrap(plane.pos.z - st.pos.z);
    if (dx * dx + dz * dz > (st.radius + 1500.f) * (st.radius + 1500.f) || plane.pos.y - st.pos.y > 1500.f) continue;
    profile.sites.insert(st.id);
    if (region) {
      toast(fmt("Landmark found: %s (%d of %d in the %s)", st.name.c_str(), profile.regionSites(g_world.regionId), (int)sites.size(),
        g_world.regionRecipe->descriptor.name.c_str()), vec3(1.f, 0.85f, 0.45f), false);
      if (profile.sitesBeyond() >= 20) award("discover20");
    } else {
      const int n = profile.islandSites();
      toast(fmt("Landmark found: %s (%d of %d)", st.name.c_str(), n, (int)sites.size()), vec3(1.f, 0.85f, 0.45f), false);
      if (n >= (int)sites.size()) award("sites_all");
    }
    if (profile.sites.size() >= 5) award("sites5");
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
  if (career.airline.earned >= 1000000) award("tycoon");
  if (career.flights >= kMilitaryFlights && career.military.sorties == 0 && profile.count("militaryInvite") <= 0.f) {   // (once: the board has opened)
    profile.num["militaryInvite"] = 1.f; saveProfile();
    toast("Defense Command has an offer for you: the Military board is open (Contracts > Military)", vec3(1.f, 0.78f, 0.3f), false);
  }
}

// The passengers' request: once airborne and clear of the field they ask to see the landmark nearest the way; flying
// within sight of it (low enough to make it out) earns the tip. Optional: the job pays as it would without it
void Game::updateSightseeing() {
  if (sightReq < 0 || sightDone || crashed || plane.onGround) return;
  const auto& sites = profileSites();
  if (sightReq >= (int)sites.size()) { sightReq = -1; return; }
  const Site& st = sites[sightReq];
  if (!sightAsked && takeoffAnnounced && plane.agl() > 250.f) {
    sightAsked = true;
    std::string nm = st.name; if (nm.rfind("The ", 0) == 0) nm = "the " + nm.substr(4);
    toast("Passengers: \"Could we see " + nm + " on the way?\"  (optional - they tip)", vec3(1.f, 0.85f, 0.45f), false);
    g_audio.trigger(SFX_CHIME, 0.6f);
  }
  const float dx = g_world.wrap(plane.pos.x - st.pos.x), dz = g_world.wrap(plane.pos.z - st.pos.z);
  if (sightAsked && dx * dx + dz * dz < (st.radius + 900.f) * (st.radius + 900.f) && plane.pos.y - st.pos.y < 1500.f) {
    sightDone = true; result.sightseen = true;
    toast("The passengers loved the view of " + st.name, vec3(0.6f, 1.f, 0.7f), false);
  }
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

// ---- Steam: what the player's friends see (steam/rich_presence_english.vdf has every token and the values it uses)
void Game::steamPresence(std::string& token, steam::Pairs& values) const {
  token.clear(); values.clear();
  const int nAp = (int)g_world.airports.size();
  auto apName = [&](int i) { return i >= 0 && i < nAp ? std::string(g_world.airports[i].name) : std::string(); };
  const std::string craft = plane.spec ? std::string(plane.spec->name) : std::string();
  switch (screen) {
    case SCR_MENU: token = "#Status_Menu"; return;
    case SCR_RESEARCH: token = "#Status_Research"; return;
    case SCR_FREE_FLIGHT: token = "#Status_FreeFlightSetup"; return;
    case SCR_HUB: case SCR_DEBRIEF: {
      const std::string at = g_world.procedural ? std::string() : apName(career.location);
      if (at.empty()) { token = "#Status_HubPlain"; return; }
      token = "#Status_Hub"; values.push_back({"airport", at}); return;
    }
    case SCR_FLIGHT: break;
    default: return;   // (a loading screen: the last status stays up)
  }
  if (craft.empty()) return;
  values.push_back({"aircraft", craft});
  if (photo.on) { token = "#Status_Photo"; return; }
  if (militaryFlight) { token = "#Status_Military"; return; }
  if (researchFlight) { token = "#Status_ResearchFlight"; return; }
  if (freeFlight) { token = "#Status_FreeFlight"; return; }
  if (contract.type == CT_TRIAL) {
    std::string name = "a trial";
    for (int k = 0; k < 5; k++) if (contract.id == trialId(k)) name = trialName(k);
    token = "#Status_Trial"; values.push_back({"trial", name}); return;
  }
  if (contract.type == CT_LESSON) { token = "#Status_Lesson"; return; }
  const std::string from = g_world.procedural ? std::string() : apName(contract.from), to = g_world.procedural ? std::string() : apName(contract.to);
  if (isolatedFlight && !to.empty()) { token = "#Status_Practice"; values.push_back({"to", to}); return; }
  if (from.empty() || to.empty()) { token = "#Status_FreeFlight"; return; }
  token = "#Status_Job"; values.push_back({"from", from}); values.push_back({"to", to});
}

void Game::updateSteam(float dt) {
  if (headless || diskless || !steam::active()) return;
  steam::tick(dt);
  if (deckPreset && screen == SCR_MENU) {   // (the Deck's settings, chosen at the first start on it: said once, then kept)
    deckPreset = false; saveSettings();
    toast("Steam Deck: the graphics are set for the Deck (Low, 30 fps, larger text) - change them any time in Settings", vec3(0.6f, 1.f, 0.7f), false);
  }
  if (steam::overlayOpened() && screen == SCR_FLIGHT && !paused && !crashed) { paused = true; settingsFromPause = false; }   // (as Valve asks: the overlay pauses the flight)
  if ((steamPresenceT -= dt) > 0.f) return;
  steamPresenceT = 1.f;
  std::string token; steam::Pairs values;
  steamPresence(token, values);
  if (!token.empty()) steam::presence(token, values);
}
