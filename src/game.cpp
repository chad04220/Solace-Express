// Air Xpress - game flow, flight session, cameras, particles, lights, audio feed
#include "game.h"

// ------------------------------------------------------------------ settings / save
static std::string joinPath(const std::string& d, const char* f) { return d.empty() ? std::string(f) : d + "/" + f; }

void Game::loadSettings() {
  FILE* f = fopen(joinPath(saveDir, "settings.cfg").c_str(), "r");
  if (!f) return;
  char k[64]; float v;
  while (fscanf(f, "%63s %f", k, &v) == 2) {
    std::string s = k;
    if (s == "renderScale") set.renderScale = clampf(v, 0.4f, 1.0f);
    else if (s == "quality") set.quality = (int)clampf(v, 0, 2);
    else if (s == "master") set.master = clampf(v, 0, 1);
    else if (s == "engineVol") set.engineVol = clampf(v, 0, 1.5f);
    else if (s == "sfxVol") set.sfxVol = clampf(v, 0, 1.5f);
    else if (s == "radioVol") set.radioVol = clampf(v, 0, 1);
    else if (s == "invertPitch") set.invertPitch = v != 0;
    else if (s == "showHints") set.showHints = v != 0;
    else if (s == "metric") set.metric = v != 0;
    else if (s == "fullscreen") set.fullscreen = v != 0;
    else if (s == "radioStation") set.radioStation = (int)v;
    else if (s == "mouseSens") set.mouseSens = clampf(v, 0.2f, 3.f);
  }
  fclose(f);
}

void Game::saveSettings() {
  FILE* f = fopen(joinPath(saveDir, "settings.cfg").c_str(), "w");
  if (!f) return;
  fprintf(f, "renderScale %f\nquality %d\nmaster %f\nengineVol %f\nsfxVol %f\nradioVol %f\ninvertPitch %d\nshowHints %d\nmetric %d\nfullscreen %d\nradioStation %d\nmouseSens %f\n",
          set.renderScale, set.quality, set.master, set.engineVol, set.sfxVol, set.radioVol, set.invertPitch, set.showHints, set.metric, set.fullscreen, set.radioStation, set.mouseSens);
  fclose(f);
}

void Game::loadStations() {
  stations.clear();
  FILE* f = fopen(joinPath(saveDir, "radio_stations.txt").c_str(), "r");
  if (!f) f = fopen("radio_stations.txt", "r");
  if (f) {
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
      std::string s = line;
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
      if (s.empty() || s[0] == '#') continue;
      size_t bar = s.find('|');
      if (bar == std::string::npos) stations.push_back({s, s});
      else stations.push_back({s.substr(0, bar), s.substr(bar + 1)});
    }
    fclose(f);
  }
  if (stations.empty()) {
    stations = {{"SomaFM Groove Salad (ambient)", "https://ice1.somafm.com/groovesalad-128-mp3"},
                {"SomaFM Drone Zone", "https://ice1.somafm.com/dronezone-128-mp3"},
                {"SomaFM Secret Agent", "https://ice1.somafm.com/secretagent-128-mp3"},
                {"SomaFM Lush", "https://ice1.somafm.com/lush-128-mp3"},
                {"Radio Paradise (eclectic)", "https://stream.radioparadise.com/mp3-128"},
                {"Radio Paradise Mellow", "https://stream.radioparadise.com/mellow-128"},
                {"KEXP Seattle", "https://kexp-mp3-128.streamguys1.com/kexp128.mp3"},
                {"Classic FM style: Venice Classic", "https://uk2.streamingpulse.com/ssl/vcr1"}};
  }
  set.radioStation = std::clamp(set.radioStation, 0, (int)stations.size() - 1);
}

void Game::saveGame() { career.save(joinPath(saveDir, "career.sav")); hasSave = true; }

void Game::init() {
  g_world.build();
  buildStory();
  loadSettings();
  loadStations();
  career.newGame();
  hasSave = career.load(joinPath(saveDir, "career.sav"));
  if (!hasSave) career.newGame();
  radio.init();
  radio.setVolume(set.radioVol);
  camQ = quat();
}

void Game::initHeadless() { headless = true; career.newGame(); }

void Game::shutdown() { saveSettings(); radio.shutdown(); }

void Game::toast(const std::string& s, vec3 col) {
  toasts.push_back({s, 0.f, col});
  if (toasts.size() > 5) toasts.erase(toasts.begin());
}

// ------------------------------------------------------------------ sun & sky
void Game::computeSun(float tod, vec3& dir, vec3& col, float& night) const {
  float a = (tod - 6.f) / 12.f * PI;
  dir = normalize(vec3(cosf(a), sinf(a) * 0.93f, 0.35f + 0.1f * sinf(a)));
  float y = dir.y;
  float od = 1.f / (std::max(y, -0.08f) * 1.4f + 0.08f);
  col = vec3(expf(-0.10f * od * 0.45f), expf(-0.23f * od * 0.45f), expf(-0.56f * od * 0.45f)) * smoothstepf(-0.06f, 0.05f, y);
  col = col * (1.f - 0.75f * wx.cloudCover * wx.cloudCover) * (wx.storm ? 0.6f : 1.f);
  night = smoothstepf(0.06f, -0.14f, y);
}

// ------------------------------------------------------------------ flight session
void Game::startFlight(const Contract& c, int spec, Career::Source src) {
  contract = c; specIdx = spec; source = src;
  wx = c.wx; timeOfDay = wx.timeOfDay;
  const AircraftSpec& s = kAircraft[spec];
  const Airport& a = g_world.airports[c.from];
  // runway into the wind (lessons keep the published runway so the rings line up)
  float h0 = a.heading;
  bool reverse = false;
  if (c.type != CT_LESSON) {
    float hw0 = cosf((wx.windFrom - h0) * DEG), hw1 = cosf((wx.windFrom - h0 - 180.f) * DEG);
    reverse = hw1 > hw0;
  }
  float hdg = reverse ? h0 + 180.f : h0;
  vec3 start = a.threshold(reverse) + (reverse ? -a.dir() : a.dir()) * 30.f;
  float payloadKg = (float)c.cargoKg + c.pax * 85.f + 85.f;
  float fuel = s.maxFuel;
  plane.reset(&s, start, hdg, fuel, payloadKg, c.startAirborne, s.cruise);
  fuelStart = plane.fuel;
  wpIndex = 0; flightClock = 0; crashTimer = 0; endTimer = 0;
  paused = false; showMap = false; landed = completed = crashed = false;
  result = FlightResult();
  timeAccel = 1; camMode = camMode == 1 ? 1 : 0; camYaw = 0; camPitch = 0.12f; camZoom = 1;
  lookYaw = lookPitch = 0;
  camQ = plane.q; camPos = plane.pos + plane.q.rotate(vec3(0, 3, 15));
  flapNotch = 0; phase = 0; lastHintPhase = -1; hint.clear();
  takeoffAnnounced = false; touchedDown = false; touchdownFpm = 0; stillTimer = 0;
  engineAutoStarted = false; startDelay = 1.2f;
  particles.clear();
  lightning = 0; nextLightning = 6; thunderDelay = -1;
  landingLight = true;
  approachMinAgl = 1e9f;
  apWasOn = false;
  licenseBefore = career.license;
  screen = SCR_FLIGHT;
  toast(fmt("%s - %s", a.code, a.name), vec3(0.7f, 0.9f, 1.0f));
  toast(fmt("Runway %02d, %s", a.rwyNumber(reverse), wx.describe().c_str()), vec3(0.8f, 0.8f, 0.8f));
}

void Game::endFlight(bool success, const std::string& reason) {
  result.success = success;
  result.failReason = reason;
  result.flightMin = flightClock / 60.f;
  result.maxG = plane.maxG; result.minG = plane.minG;
  result.fuelUsedKg = std::max(0.f, fuelStart - plane.fuel);
  result.touchdownFpm = touchdownFpm;
  result.late = contract.timeLimitMin > 0 && flightClock / 60.f > contract.timeLimitMin;
  if (contract.type == CT_LESSON && success && contract.wps.size() && contract.to == contract.from && !touchedDown) result.touchdownFpm = 0;
  Contract c = contract;
  if (c.type == CT_FERRY) { c.story = false; }
  payout = career.settle(c, specIdx, source, result, &stars);
  lastSuccess = success;
  debriefTitle = success ? (c.type == CT_FERRY ? "Flight complete" : "Contract complete!") : (reason.empty() ? "Flight failed" : reason);
  g_audio.trigger(success ? SFX_SUCCESS : SFX_FAIL);
  if (success && c.payout > 0) g_audio.trigger(SFX_CASH);
  if (!headless) saveGame();
  screen = SCR_DEBRIEF;
  paused = false; showMap = false;
}

int Game::computePhase() const {
  if (!plane.spec) return 0;
  float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
  if (touchedDown && plane.onGround) return 6;
  if (plane.onGround && !takeoffAnnounced) return gs < 3.f ? 0 : 1;
  bool finalLeg = contract.wps.empty() || wpIndex >= (int)contract.wps.size() - 1;
  const Airport& d = dest();
  float dd = length(vec3(plane.pos.x - d.x, 0, plane.pos.z - d.z));
  if (finalLeg && !contract.wps.empty() && wpIndex >= (int)contract.wps.size()) finalLeg = true;
  if (finalLeg && (contract.to != contract.from || wpIndex >= (int)contract.wps.size())) {
    if (dd < d.length * 0.5f + 1800.f && plane.agl() < 180.f) return 5;
    if (dd < 6000.f) return 4;
  }
  if (plane.agl() < 300.f && length(vec3(plane.pos.x - g_world.airports[contract.from].x, 0, plane.pos.z - g_world.airports[contract.from].z)) < 4000.f) return 2;
  return 3;
}

void Game::flightControls(float dt) {
  Controls& c = plane.ctl;
  auto key = [&](int k) { return in.down[k]; };
  float pitchIn = ((key('S') || key(K_DOWN)) ? 1.f : 0.f) - ((key('W') || key(K_UP)) ? 1.f : 0.f);
  float rollIn = ((key('D') || key(K_RIGHT)) ? 1.f : 0.f) - ((key('A') || key(K_LEFT)) ? 1.f : 0.f);
  float yawIn = (key('E') ? 1.f : 0.f) - (key('Q') ? 1.f : 0.f);
  if (set.invertPitch) pitchIn = -pitchIn;
  float padP = 0, padR = 0, padY = 0;
  if (in.pad) {
    auto dz = [](float v) { return fabsf(v) < 0.12f ? 0.f : (v - (v > 0 ? 0.12f : -0.12f)) / 0.88f; };
    padP = -dz(in.ly) * (set.invertPitch ? -1.f : 1.f); padR = dz(in.lx);
    padY = ((in.buttons & PAD_RB) ? 1.f : 0.f) - ((in.buttons & PAD_LB) ? 1.f : 0.f) + dz(in.rx) * 0.0f;
    padP = padP * fabsf(padP) * 0.4f + padP * 0.6f; padR = padR * fabsf(padR) * 0.4f + padR * 0.6f;
  }
  bool manual = fabsf(pitchIn) + fabsf(rollIn) > 0 || fabsf(padP) + fabsf(padR) > 0.3f;
  if (plane.apOn) {
    plane.apHeading = wrapDeg360(plane.apHeading + (rollIn + padR) * 25.f * dt);
    plane.apAlt += -(pitchIn + padP) * (set.invertPitch ? -1.f : 1.f) * 0.f + ((key('S') || key(K_DOWN)) ? -6.f : 0.f) * 0 * dt;
    if (fabsf(pitchIn) > 0 || fabsf(padP) > 0.5f) { plane.apOn = false; g_audio.trigger(SFX_AP_DISC); toast("Autopilot disconnected", vec3(1, 0.7f, 0.3f)); }
  } else {
    float tp = clampf(pitchIn + padP, -1, 1), tr = clampf(rollIn + padR, -1, 1);
    float rate = manual ? 2.8f : 5.f;
    c.pitch = approach(c.pitch, tp, rate, dt);
    c.roll = approach(c.roll, tr, rate, dt);
  }
  (void)manual;
  c.yaw = approach(c.yaw, clampf(yawIn + padY, -1, 1), 4.f, dt);
  // throttle
  float thr = 0;
  if (key(K_SHIFT) || key(K_PGUP) || key(K_PLUS)) thr += 0.55f;
  if (key(K_CTRL) || key(K_PGDN) || key(K_MINUS)) thr -= 0.55f;
  if (in.pad) thr += (in.rt - in.lt) * 0.6f;
  c.throttle = clampf(c.throttle + thr * dt, 0, 1);
  for (int k = 1; k <= 9; k++) if (in.pressed['0' + k] && !in.down[K_CTRL]) c.throttle = k / 9.f;
  if (in.pressed['0']) c.throttle = 0;
  // trim
  float tr = (key(K_RBRACKET) || key(K_HOME) ? 1.f : 0.f) - (key(K_LBRACKET) || key(K_END) ? 1.f : 0.f);
  if (in.pad) tr += ((in.buttons & PAD_UP) ? 1.f : 0.f) - ((in.buttons & PAD_DOWN) ? 1.f : 0.f);
  c.trim = clampf(c.trim + tr * 0.35f * dt, -1, 1);
  // flaps
  if (in.pressed['F'] || (in.buttonsPressed & PAD_B)) { flapNotch = std::min(1.f, flapNotch + 1.f / 3.f); toast(fmt("Flaps %d%%", (int)lroundf(flapNotch * 100)), vec3(0.8f, 0.9f, 1)); }
  if (in.pressed['V'] || (in.buttonsPressed & PAD_X && false)) { flapNotch = std::max(0.f, flapNotch - 1.f / 3.f); toast(fmt("Flaps %d%%", (int)lroundf(flapNotch * 100)), vec3(0.8f, 0.9f, 1)); }
  c.flaps = flapNotch;
  // gear
  if ((in.pressed['G'] || (in.buttonsPressed & PAD_Y)) && plane.spec->retract) {
    if (plane.onGround && c.gearDown) toast("Gear lever is locked on the ground", vec3(1, 0.6f, 0.4f));
    else { c.gearDown = !c.gearDown; toast(c.gearDown ? "Gear down" : "Gear up", vec3(0.8f, 1, 0.8f)); }
  }
  // brakes: B = parking brake toggle, Space = wheel brakes
  static bool parking = true;
  if (flightClock < 0.05f) parking = true;
  if (in.pressed['B']) { parking = !parking; toast(parking ? "Parking brake SET" : "Parking brake released", vec3(1, 0.85f, 0.5f)); }
  float wb = key(K_SPACE) ? 1.f : 0.f;
  if (in.pad && (in.buttons & PAD_A)) wb = 1.f;
  if (wb > 0 && parking && plane.onGround && length(plane.vel) > 2.f) parking = false;
  c.brake = parking ? 1.f : wb;
  // autopilot
  if (in.pressed['Z'] || (in.buttonsPressed & PAD_RS)) {
    if (plane.onGround) toast("Autopilot needs to be airborne", vec3(1, 0.6f, 0.4f));
    else {
      plane.apOn = !plane.apOn;
      if (plane.apOn) { plane.apHeading = plane.heading(); plane.apAlt = plane.pos.y; plane.apPitchI = plane.pitchDeg(); toast("Autopilot ON: holding heading and altitude (A/D adjusts heading)", vec3(0.6f, 1, 0.6f)); }
      else { g_audio.trigger(SFX_AP_DISC); toast("Autopilot OFF", vec3(1, 0.7f, 0.3f)); }
    }
  }
  if (plane.apOn && (key('W') || key(K_UP))) plane.apAlt += 0;
  if (in.pressed['L']) { landingLight = !landingLight; toast(landingLight ? "Landing lights ON" : "Landing lights OFF"); }
  if (in.pressed['I'] && !plane.engineRunning && plane.fuel > 0) { plane.starterTime = 0.01f; toast("Engine start"); }
  // time acceleration
  if (in.pressed['T']) {
    float dd = length(vec3(plane.pos.x - dest().x, 0, plane.pos.z - dest().z));
    if (plane.onGround || plane.agl() < 250.f || dd < 3500.f) { timeAccel = 1; toast("Time acceleration only available in cruise", vec3(1, 0.7f, 0.4f)); }
    else { timeAccel = timeAccel >= 4 ? 1 : timeAccel * 2; toast(fmt("Time x%.0f", timeAccel)); }
  }
}

void Game::spawn(vec3 p, vec3 v, float life, float size, float grow, vec3 col, float alpha, int kind, float drag, float buoy) {
  if (particles.size() > 3000) return;
  particles.push_back({p, v, life, life, size, grow, col, alpha, kind, drag, buoy});
}

void Game::updateFlight(float dt) {
  if (paused) return;
  flightControls(dt);
  // automatic engine start sequence after the scene fades in
  if (!engineAutoStarted && !contract.startAirborne) {
    startDelay -= dt;
    if (startDelay <= 0) { engineAutoStarted = true; plane.starterTime = 0.01f; toast("Starting engine..."); }
  }
  bool wasRunning = plane.engineRunning;
  float simDt = dt * timeAccel;
  if (timeAccel > 1) {
    float dd = length(vec3(plane.pos.x - dest().x, 0, plane.pos.z - dest().z));
    if (plane.agl() < 250.f || dd < 3500.f || plane.onGround || crashed) { timeAccel = 1; toast("Time acceleration off"); }
  }
  vec3 prevPos = plane.pos;
  if (!crashed) {
    plane.step(simDt, wx, gameTime);
    flightClock += simDt;
    timeOfDay += simDt / 3600.f;
  }
  gameTime += simDt;
  if (plane.engineRunning && !wasRunning) {
    // starter catches: puff of smoke from the exhausts
    vec3 ex = plane.pos + plane.q.rotate(vec3(0.4f, -plane.spec->fusRad * 0.6f, -plane.spec->fusLen * 0.35f));
    for (int i = 0; i < 14; i++) spawn(ex, plane.q.rotate(vec3(0.8f + i * 0.05f, -0.5f, 2.f)) + vec3(0, 0.6f, 0), 2.5f, 0.6f, 1.6f, vec3(0.55f, 0.58f, 0.62f), 0.55f, SPR_SMOKE, 1.5f, 0.3f);
    if (!takeoffAnnounced && contract.type == CT_LESSON) toast("Engine running. Release the parking brake with B.", vec3(0.7f, 1, 0.7f));
    else if (!takeoffAnnounced) toast(fmt("Engine running. Cleared for takeoff runway %02d.", g_world.airports[contract.from].rwyNumber(cosf((plane.heading() - g_world.airports[contract.from].heading) * DEG) < 0)), vec3(0.7f, 1, 0.7f));
  }
  // gear / flap motor cues
  if (prevGear > 0.99f && plane.gear < 0.99f) g_audio.trigger(SFX_GEAR_CLUNK, 0.6f);
  if (prevGear < 0.99f && plane.gear >= 0.99f) g_audio.trigger(SFX_GEAR_CLUNK, 1.0f);
  prevGear = plane.gear; prevFlaps = plane.flaps;
  // weather effects
  if (wx.storm) {
    nextLightning -= dt;
    if (nextLightning <= 0) { lightning = 1.f; nextLightning = 6.f + (rand() % 1000) * 0.015f; thunderDelay = 0.8f + (rand() % 100) * 0.03f; }
    if (thunderDelay > 0) { thunderDelay -= dt; if (thunderDelay <= 0) g_audio.trigger(SFX_THUNDER, 0.7f + (rand() % 30) * 0.01f); }
  }
  lightning = std::max(0.f, lightning - dt * 4.f) * (lightning > 0.5f ? 1.f : (rand() % 3 ? 1.f : 0.3f));
  cloudOff = cloudOff + vec2(-sinf(wx.windFrom * DEG), cosf(wx.windFrom * DEG)) * (wx.windSpeed * 2.f * simDt);

  if (crashed) {
    crashTimer += dt;
    if (crashTimer < 6.f) {
      for (int i = 0; i < 3; i++) spawn(plane.pos + vec3((rand() % 100 - 50) * 0.05f, 0.5f, (rand() % 100 - 50) * 0.05f), vec3(0, 3.f + (rand() % 100) * 0.03f, 0), 1.0f, 2.f, 1.5f, vec3(1.f, 0.45f, 0.12f), 1.f, SPR_FIRE, 0.5f, 1.f);
      spawn(plane.pos + vec3(0, 2, 0), vec3((rand() % 100 - 50) * 0.02f, 4.f, (rand() % 100 - 50) * 0.02f) + plane.windVel, 6.f, 3.f, 3.f, vec3(0.12f, 0.11f, 0.1f), 0.7f, SPR_SMOKE, 0.3f, 1.5f);
    }
    if (crashTimer > 4.0f && screen == SCR_FLIGHT) endFlight(false, plane.ev.crashReason);
    return;
  }
  if (plane.ev.crashed) {
    crashed = true; plane.vel = vec3(); plane.w = vec3();
    g_audio.trigger(SFX_CRASH);
    toast(plane.ev.crashReason, vec3(1, 0.4f, 0.3f));
    bool water = g_world.height(plane.pos.x, plane.pos.z) < 0.5f;
    for (int i = 0; i < 60; i++) {
      vec3 v((rand() % 200 - 100) * 0.12f, (rand() % 100) * 0.15f, (rand() % 200 - 100) * 0.12f);
      if (water) spawn(plane.pos, v, 2.5f, 1.5f, 2.f, vec3(0.9f, 0.95f, 1.f), 0.8f, SPR_SMOKE, 1.f, -6.f);
      else spawn(plane.pos, v, 1.2f, 2.f, 3.f, vec3(1.f, 0.6f, 0.2f), 1.f, SPR_FIRE, 1.f, 0.f);
    }
    return;
  }
  // takeoff
  if (!takeoffAnnounced && !plane.onGround && plane.agl() > 8.f) {
    takeoffAnnounced = true;
    toast("Positive climb!", vec3(0.7f, 1, 0.7f));
  }
  // touchdown
  if (plane.ev.touchdown && takeoffAnnounced) {
    float fpm = -plane.ev.touchdownVs * 196.85f;
    touchdownFpm = fpm; touchedDown = true;
    g_audio.trigger(SFX_TOUCHDOWN, clampf(fpm / 600.f, 0.15f, 1.f));
    std::string r = fpm < 120 ? "BUTTER!" : fpm < 250 ? "Smooth landing" : fpm < 450 ? "Good landing" : fpm < 650 ? "Firm landing" : "HARD landing!";
    toast(fmt("%s  %.0f fpm", r.c_str(), fpm), fpm < 450 ? vec3(0.6f, 1, 0.6f) : vec3(1, 0.6f, 0.3f));
    float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
    for (int side = -1; side <= 1; side += 2) {
      vec3 wp = plane.pos + plane.q.rotate(vec3(side * std::max(1.2f, plane.spec->span * 0.13f), -plane.gearHeight(), 0));
      int n = (int)clampf(gs * 0.3f, 4, 18);
      for (int i = 0; i < n; i++) spawn(wp, plane.vel * 0.3f + vec3((rand() % 100 - 50) * 0.02f, 0.5f, (rand() % 100 - 50) * 0.02f), 2.5f + (rand() % 100) * 0.01f, 0.6f, 1.8f, vec3(0.75f, 0.75f, 0.76f), 0.45f, SPR_SMOKE, 2.f, 0.2f);
    }
  }
  if (!plane.onGround) result.maxBank = std::max(result.maxBank, fabsf(plane.bankDeg()));
  // rolling dust / spray
  float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
  if (plane.onGround && plane.groundRough > 0.3f && gs > 4.f) {
    dustAccum += dt * gs * 0.6f;
    int rw = g_world.onRunway(plane.pos.x, plane.pos.z, 3);
    int surf = rw >= 0 ? g_world.airports[rw].surface : SURF_GRASS;
    vec3 col = surf == SURF_SAND ? vec3(0.8f, 0.72f, 0.55f) : surf == SURF_SNOW ? vec3(0.95f, 0.96f, 1.f) : surf == SURF_GRAVEL ? vec3(0.55f, 0.5f, 0.45f) : vec3(0.45f, 0.42f, 0.32f);
    while (dustAccum > 1) {
      dustAccum -= 1;
      int side = rand() & 1 ? 1 : -1;
      vec3 wp = plane.pos + plane.q.rotate(vec3(side * std::max(1.2f, plane.spec->span * 0.13f), -plane.gearHeight(), 0.5f));
      spawn(wp, plane.vel * 0.15f + vec3(0, 0.8f, 0), 2.5f, 0.8f, 2.2f, col, surf == SURF_GRASS ? 0.15f : 0.35f, SPR_SMOKE, 1.5f, 0.1f);
    }
  }
  // prop wash dust on rough ground
  if (plane.onGround && plane.groundRough > 0.3f && plane.engineSpool > 0.5f && gs < 15.f && (rand() % 3 == 0)) {
    vec3 bp = plane.pos + plane.q.rotate(vec3((rand() % 100 - 50) * 0.04f, -plane.gearHeight(), 3.f));
    spawn(bp, plane.q.rotate(vec3(0, 0.3f, 8.f)), 1.5f, 0.6f, 2.f, vec3(0.6f, 0.55f, 0.45f), 0.18f, SPR_SMOKE, 2.f, 0.f);
  }
  // wingtip vapour in humid air under g
  if (!plane.onGround && plane.gLoad > 1.7f && (wx.precip > 0 || plane.pos.y > wx.cloudBase - 300.f)) {
    for (int s = -1; s <= 1; s += 2) spawn(plane.pos + plane.q.rotate(vec3(s * plane.spec->span * 0.5f, plane.spec->wingY * plane.spec->fusRad, plane.spec->wingZ + 0.6f)), plane.vel * 0.9f, 0.5f, 0.25f, 0.4f, vec3(1, 1, 1), 0.35f, SPR_SMOKE, 3.f, 0.f);
  }
  // waypoints
  if (wpIndex < (int)contract.wps.size()) {
    const Waypoint& w = contract.wps[wpIndex];
    vec3 wp(w.x, w.alt, w.z);
    if (length(plane.pos - wp) < 110.f) {
      wpIndex++;
      g_audio.trigger(SFX_CHIME);
      toast(wpIndex < (int)contract.wps.size() ? fmt("Checkpoint %d of %d", wpIndex, (int)contract.wps.size()) : "All checkpoints passed!", vec3(0.5f, 1, 0.7f));
      if (contract.wps.size() && wpIndex >= (int)contract.wps.size() && contract.id == "L1") { endTimer = 2.f; }
    }
  }
  if (endTimer > 0) { endTimer -= dt; if (endTimer <= 0) { endFlight(true, ""); return; } }
  // fuel warnings
  static bool lowFuelWarned = false;
  if (flightClock < 0.1f) lowFuelWarned = false;
  if (!lowFuelWarned && plane.fuel < plane.spec->maxFuel * 0.15f) { lowFuelWarned = true; g_audio.trigger(SFX_BEEP); toast("LOW FUEL", vec3(1, 0.5f, 0.2f)); }
  // completion
  if (plane.onGround && takeoffAnnounced && gs < 2.5f) {
    stillTimer += dt;
    if (stillTimer > 1.2f) {
      float dA; int ap = g_world.nearestAirport(plane.pos.x, plane.pos.z, &dA);
      bool atField = ap >= 0 && dA < g_world.airports[ap].length * 0.5f + 600.f;
      if (wpIndex < (int)contract.wps.size()) {
        if (stillTimer > 1.25f && stillTimer < 1.3f) toast("Checkpoints remaining - take off again to continue", vec3(1, 0.8f, 0.4f));
      } else if (atField && ap == contract.to) { endFlight(true, ""); return; }
      else if (atField) { endFlight(false, fmt("Diverted to %s", g_world.airports[ap].name)); return; }
      else if (stillTimer > 3.f) { endFlight(false, "Landed off-airport"); return; }
    }
  } else stillTimer = 0;
  if (plane.fuel <= 0 && plane.onGround && gs < 1.f && !takeoffAnnounced) { endFlight(false, "Out of fuel"); return; }
  if (plane.fuel <= 0 && plane.onGround && gs < 1.f && takeoffAnnounced) {
    float dA; int ap = g_world.nearestAirport(plane.pos.x, plane.pos.z, &dA);
    if (!(ap == contract.to && dA < 2000)) { endFlight(false, "Out of fuel"); return; }
  }
  (void)prevPos;
  // tutorial hints
  phase = computePhase();
  if (contract.hints.size() > (size_t)phase && phase != lastHintPhase) {
    lastHintPhase = phase;
    if (!contract.hints[phase].empty()) hint = contract.hints[phase];
  }
}

// ------------------------------------------------------------------ camera
void Game::updateCamera(float dt) {
  if (!plane.spec) return;
  if (in.pressed['C'] || (in.buttonsPressed & PAD_BACK)) {
    camMode = (camMode + 1) % 4; camYaw = 0; camPitch = 0.12f; lookYaw = lookPitch = 0;
    static const char* names[] = {"Chase camera", "Cockpit view", "Orbit camera", "Flyby camera"};
    toast(names[camMode]);
    if (camMode == 3) camPos = plane.pos + normalize(vec3(plane.vel.x, 0, plane.vel.z) + vec3(0.01f, 0, 0)) * 350.f + plane.right() * 40.f + vec3(0, 12, 0);
  }
  if (in.wheel != 0) camZoom = clampf(camZoom * powf(0.88f, in.wheel), 0.35f, 4.f);
  bool drag = in.mDown[1] || (camMode == 2 && in.mDown[0]);
  if (drag) { camYaw -= in.mdx * 0.005f * set.mouseSens; camPitch = clampf(camPitch + in.mdy * 0.004f * set.mouseSens, -1.3f, 1.4f); }
  if (in.pad) { float rx = fabsf(in.rx) > 0.2f ? in.rx : 0, ry = fabsf(in.ry) > 0.2f ? in.ry : 0; camYaw -= rx * 2.f * dt; camPitch = clampf(camPitch + ry * 1.5f * dt, -1.3f, 1.4f); }
  const AircraftSpec& s = *plane.spec;
  float size = std::max(s.fusLen, s.span);
  if (camMode == 0) {
    if (!drag && !(in.pad && (fabsf(in.rx) > 0.2f || fabsf(in.ry) > 0.2f))) { camYaw = approach(camYaw, 0, 1.5f, dt); camPitch = approach(camPitch, 0.12f, 1.5f, dt); }
    // follow orientation with lag; reduce roll so the horizon isn't nauseating
    vec3 f = plane.forward();
    if (length(plane.vel) > 15.f && !plane.onGround) f = normalize(lerp(f, normalize(plane.vel), 0.5f));
    float yaw = atan2f(f.x, -f.z), pitch = asinf(clampf(f.y, -1, 1));
    quat target = quat::axisAngle(vec3(0, 1, 0), -yaw) * quat::axisAngle(vec3(1, 0, 0), pitch * 0.85f);
    camQ = slerp(camQ, target, 1.f - expf(-4.f * dt));
    float dist = (size * 0.85f + 5.f) * camZoom;
    quat orbit = camQ * quat::axisAngle(vec3(0, 1, 0), camYaw) * quat::axisAngle(vec3(1, 0, 0), -camPitch);
    camPos = plane.pos + orbit.rotate(vec3(0, 0, dist)) + vec3(0, s.fusRad * 0.6f, 0);
  } else if (camMode == 1) {
    if (drag || in.pad) { lookYaw = camYaw; lookPitch = camPitch - 0.12f; }
    else { lookYaw = approach(lookYaw, 0, 2.f, dt); lookPitch = approach(lookPitch, 0, 2.f, dt); camYaw = lookYaw; camPitch = lookPitch + 0.12f; }
    camPos = plane.pos + plane.q.rotate(vec3(s.engines == 2 && s.pax > 10 ? -0.45f : -0.28f * s.fusRad, 0.78f * s.fusRad, -0.27f * s.fusLen));
  } else if (camMode == 2) {
    float dist = (size * 1.4f + 8.f) * camZoom;
    quat orbit = quat::axisAngle(vec3(0, 1, 0), camYaw) * quat::axisAngle(vec3(1, 0, 0), -camPitch);
    camPos = plane.pos + orbit.rotate(vec3(0, 0, dist));
  } else {
    if (length(camPos - plane.pos) > 700.f) camPos = plane.pos + normalize(vec3(plane.vel.x, 0, plane.vel.z) + vec3(0.01f, 0, 0)) * 400.f + plane.right() * 45.f + vec3(0, 8, 0);
  }
  float gh = std::max(g_world.height(camPos.x, camPos.z, 6), 0.f) + 1.5f;
  if (camPos.y < gh) camPos.y = gh;
}

// ------------------------------------------------------------------ particles
void Game::updateParticles(float dt) {
  for (size_t i = 0; i < particles.size();) {
    Particle& p = particles[i];
    p.life -= dt;
    if (p.life <= 0) { particles[i] = particles.back(); particles.pop_back(); continue; }
    p.v = p.v * expf(-p.drag * dt);
    p.v.y += p.buoy * dt;
    p.p += p.v * dt;
    p.size += p.grow * dt;
    i++;
  }
}

// ------------------------------------------------------------------ frame assembly
static void fillPlaneVisual(PlaneVisual& pv, const Plane& p, float propAngle, bool inside) {
  const AircraftSpec& s = *p.spec;
  pv.on = true;
  pv.pos = p.pos;
  vec3 r = p.right(), u = p.up(), b = p.q.rotate(vec3(0, 0, 1));
  float m[9] = {r.x, r.y, r.z, u.x, u.y, u.z, b.x, b.y, b.z};
  memcpy(pv.rot, m, sizeof(m));
  pv.A[0] = s.fusLen; pv.A[1] = s.fusRad; pv.A[2] = s.span; pv.A[3] = s.chord;
  pv.B[0] = s.wingY; pv.B[1] = s.wingZ; pv.B[2] = (float)s.engLayout; pv.B[3] = (float)s.tail;
  pv.C[0] = p.gear; pv.C[1] = p.flaps; pv.C[2] = p.ctl.roll; pv.C[3] = clampf(p.ctl.pitch + p.ctl.trim * 0.3f, -1, 1);
  float blur = s.engineType == ENG_JET ? 1.f : smoothstepf(250.f, 700.f, p.rpm);
  pv.D[0] = p.ctl.yaw; pv.D[1] = propAngle; pv.D[2] = blur; pv.D[3] = inside ? 1.f : 0.f;
  pv.E[0] = s.taildragger ? 1.f : 0.f; pv.E[1] = p.gearHeight(); pv.E[2] = (float)std::max(s.blades, 2); pv.E[3] = 0;
  pv.colBase = s.colBase; pv.colStripe = s.colStripe;
  pv.propCount = 0;
  float R = s.fusRad, L = s.fusLen;
  if (s.engLayout == 0) {
    pv.prop[0][0] = 0; pv.prop[0][1] = -0.05f * R; pv.prop[0][2] = -0.5f * L - 0.25f; pv.prop[0][3] = 0.85f + R * 0.55f; pv.propCount = 1;
  } else if (s.engLayout == 1) {
    for (int i = 0; i < 2; i++) {
      pv.prop[i][0] = (i ? 1.f : -1.f) * s.span * 0.5f * 0.32f; pv.prop[i][1] = s.wingY * R - 0.15f * R;
      pv.prop[i][2] = s.wingZ - s.chord * 1.15f - 0.3f; pv.prop[i][3] = 1.0f + R * 0.5f;
    }
    pv.propCount = 2;
  }
}

FrameParams Game::buildFrame() {
  FrameParams fp;
  fp.time = realTime;
  computeSun(timeOfDay, fp.sunDir, fp.sunCol, fp.night);
  fp.cloudCover = wx.cloudCover; fp.cloudBase = wx.cloudBase;
  fp.fogB = 1.5f / std::max(wx.visibility, 500.f);
  fp.wet = wx.precip == 1 ? 1.f : 0.f; fp.snow = wx.precip == 2 ? 0.8f : 0.f;
  fp.storm = wx.storm ? 1.f : 0.f; fp.lightning = lightning;
  fp.windOff = cloudOff;
  fp.exposure = 1.0f + fp.night * 0.8f;
  if (screen == SCR_FLIGHT && plane.spec) {
    fillPlaneVisual(fp.plane, plane, propAngle, camMode == 1);
    vec3 fwd = camMode == 1 ? plane.q.rotate(quat::axisAngle(vec3(0, 1, 0), lookYaw).rotate(quat::axisAngle(vec3(1, 0, 0), lookPitch).rotate(vec3(0, 0, -1))))
                            : normalize(plane.pos + vec3(0, plane.spec->fusRad * 0.3f, 0) - camPos);
    vec3 upRef = camMode == 1 ? plane.up() : vec3(0, 1, 0);
    if (camMode == 0) upRef = normalize(lerp(vec3(0, 1, 0), camQ.rotate(vec3(0, 1, 0)), 0.3f));
    fp.camPos = camPos;
    fp.camBack = -fwd;
    fp.camRight = normalize(cross(fwd, upRef));
    fp.camUp = cross(fp.camRight, fwd);
    fp.fovY = (camMode == 1 ? 68.f : 55.f) * DEG;
    if (camMode == 3) fp.fovY = clampf(2.f * atanf(std::max(plane.spec->span, plane.spec->fusLen) * 1.5f / length(plane.pos - camPos)), 4.f * DEG, 60.f * DEG);
    if (camMode == 1) fp.fovY /= std::min(camZoom, 1.4f) > 0 ? 1.f : 1.f;
    fp.landLight = landingLight && plane.engineRunning ? (0.3f + 0.7f * fp.night) : 0.f;
    fp.landLightPos = plane.pos + plane.forward() * (plane.spec->fusLen * 0.4f);
    fp.landLightDir = normalize(plane.forward() - plane.up() * 0.1f);
    fp.rainLens = camMode == 1 && wx.precip == 1 ? 1.f : 0.f;
    if (crashed) fp.fade = clampf(1.f - (crashTimer - 3.f), 0, 1);
  } else {
    menuBackgroundCamera(fp);
  }
  return fp;
}

void Game::menuBackgroundCamera(FrameParams& fp) {
  // A Wren circles over the islands while the camera chases it in a slow arc
  static Plane demo;
  static bool initd = false;
  if (!initd) { demo.reset(&kAircraft[screen == SCR_MENU ? 1 : 0], vec3(0, 600, 0), 0, 50, 100, true, 60); initd = true; }
  int ap = career.location;
  vec3 centre = screen == SCR_MENU ? vec3(-6000, 0, 11000) : g_world.airports[ap].pos();
  float R = 2200.f, t = realTime * 0.035f;
  float alt = (screen == SCR_MENU ? 380.f : 320.f) + std::max(centre.y, 0.f);
  vec3 p = centre + vec3(cosf(t) * R, 0, sinf(t) * R); p.y = std::max(alt, g_world.height(p.x, p.z) + 250.f);
  vec3 vdir = normalize(vec3(-sinf(t), 0, cosf(t)));
  float hdg = atan2f(vdir.x, -vdir.z) / DEG;
  demo.pos = p;
  demo.q = quat::axisAngle(vec3(0, 1, 0), -hdg * DEG) * quat::axisAngle(vec3(0, 0, 1), -18.f * DEG) * quat::axisAngle(vec3(1, 0, 0), 2.f * DEG);
  demo.rpm = 2400; demo.gear = 1; demo.flaps = 0; demo.ctl = Controls();
  fillPlaneVisual(fp.plane, demo, realTime * 250.f, false);
  float ca = realTime * 0.05f;
  vec3 off = vec3(cosf(ca) * 16.f, 3.5f + 2.f * sinf(ca * 0.7f), sinf(ca) * 16.f) + vdir * -6.f;
  fp.camPos = p + off;
  vec3 fwd = normalize(p - fp.camPos + vdir * 4.f);
  fp.camBack = -fwd; fp.camRight = normalize(cross(fwd, vec3(0, 1, 0))); fp.camUp = cross(fp.camRight, fwd);
  fp.fovY = 50.f * DEG;
  fp.vignette = 0.9f;
}

void Game::buildSprites(const FrameParams& fp, std::vector<SpriteVert>& alpha, std::vector<SpriteVert>& add) {
  vec3 cr = fp.camRight, cu = fp.camUp;
  auto bill = [&](std::vector<SpriteVert>& v, vec3 p, float s, vec3 col, float a, int kind, float soft) {
    vec3 c0 = p - cr * s - cu * s, c1 = p + cr * s - cu * s, c2 = p + cr * s + cu * s, c3 = p - cr * s + cu * s;
    SpriteVert q[4] = {{c0.x, c0.y, c0.z, 0, 0, col.x, col.y, col.z, a, (float)kind, soft}, {c1.x, c1.y, c1.z, 1, 0, col.x, col.y, col.z, a, (float)kind, soft},
                       {c2.x, c2.y, c2.z, 1, 1, col.x, col.y, col.z, a, (float)kind, soft}, {c3.x, c3.y, c3.z, 0, 1, col.x, col.y, col.z, a, (float)kind, soft}};
    v.push_back(q[0]); v.push_back(q[1]); v.push_back(q[2]); v.push_back(q[0]); v.push_back(q[2]); v.push_back(q[3]);
  };
  auto quadAx = [&](std::vector<SpriteVert>& v, vec3 p, vec3 ax, vec3 ay, vec3 col, float a, int kind, float soft) {
    vec3 c0 = p - ax - ay, c1 = p + ax - ay, c2 = p + ax + ay, c3 = p - ax + ay;
    SpriteVert q[4] = {{c0.x, c0.y, c0.z, 0, 0, col.x, col.y, col.z, a, (float)kind, soft}, {c1.x, c1.y, c1.z, 1, 0, col.x, col.y, col.z, a, (float)kind, soft},
                       {c2.x, c2.y, c2.z, 1, 1, col.x, col.y, col.z, a, (float)kind, soft}, {c3.x, c3.y, c3.z, 0, 1, col.x, col.y, col.z, a, (float)kind, soft}};
    v.push_back(q[0]); v.push_back(q[1]); v.push_back(q[2]); v.push_back(q[0]); v.push_back(q[2]); v.push_back(q[3]);
  };
  float night = fp.night;
  float lowVis = smoothstepf(8000.f, 2000.f, wx.visibility) + (wx.cloudCover > 0.8f ? 0.3f : 0.f);
  float lightI = clampf(night + lowVis * 0.6f + 0.12f, 0, 1);
  // runway lighting
  for (size_t ai = 0; ai < g_world.airports.size(); ai++) {
    const Airport& a = g_world.airports[ai];
    float d = length(a.pos() - fp.camPos);
    if (d > 16000.f) continue;
    vec3 dir = a.dir(), rt(-dir.z, 0, dir.x);
    float sz = std::max(0.5f, d * 0.0022f);
    if (lightI > 0.15f) {
      for (float u = -a.length * 0.5f; u <= a.length * 0.5f + 0.1f; u += 60.f)
        for (int sd = -1; sd <= 1; sd += 2) {
          vec3 p = a.pos() + dir * u + rt * (sd * (a.width * 0.5f + 1.5f)) + vec3(0, 0.4f, 0);
          vec3 col = fabsf(u) > a.length * 0.5f - 600.f && a.size > 0 ? vec3(1.f, 0.85f, 0.45f) : vec3(1.f, 0.92f, 0.75f);
          bill(add, p, sz, col * (2.0f * lightI), 1.f, SPR_GLOW, 2.f);
        }
      for (int end = -1; end <= 1; end += 2) {
        for (float v = -a.width * 0.5f; v <= a.width * 0.5f; v += 3.f) {
          vec3 p = a.pos() + dir * (end * (a.length * 0.5f + 1.f)) + rt * v + vec3(0, 0.4f, 0);
          bill(add, p, sz, (end < 0 ? vec3(0.2f, 1.f, 0.3f) : vec3(1.f, 0.15f, 0.1f)) * 2.0f * lightI, 1.f, SPR_GLOW, 2.f);
        }
        if (a.size > 0 && night > 0.3f)
          for (int k = 1; k <= 6; k++)
            for (float v = -8.f; v <= 8.f; v += 4.f) {
              vec3 p = a.pos() + dir * (end * (a.length * 0.5f + 60.f * k)) + rt * v;
              p.y = std::max(g_world.height(p.x, p.z), a.elev) + 1.f;
              bill(add, p, sz * 1.2f, vec3(1.f, 0.95f, 0.85f) * 2.5f * night, 1.f, SPR_GLOW, 3.f);
            }
      }
    }
    // PAPI (always on) on the left of each landing direction, 300 m past the threshold
    if (a.size > 0 || a.surface == SURF_ASPHALT) {
      for (int end = -1; end <= 1; end += 2) {
        vec3 ldir = dir * (float)(-end);              // landing direction
        vec3 thr = a.pos() + dir * (end * a.length * 0.5f);
        vec3 lrt(-ldir.z, 0, ldir.x);
        vec3 base = thr + ldir * 300.f - lrt * (a.width * 0.5f + 15.f) + vec3(0, 0.6f, 0);
        vec3 toCam = fp.camPos - base;
        float horiz = length(vec3(toCam.x, 0, toCam.z));
        if (dot(vec3(toCam.x, 0, toCam.z), ldir) > 0) continue;  // only visible from the approach side
        float ang = atan2f(toCam.y, horiz) / DEG;
        for (int i = 0; i < 4; i++) {
          float th = 2.5f + i * 0.333f;   // outer light has the highest threshold
          bool white = ang > (3.5f - i * 0.333f);
          (void)th;
          vec3 p = base - lrt * (i * 9.f);
          bill(add, p, std::max(0.7f, d * 0.003f), (white ? vec3(1.f, 0.95f, 0.9f) : vec3(1.f, 0.1f, 0.08f)) * 3.0f, 1.f, SPR_GLOW, 3.f);
        }
      }
    }
  }
  if (screen == SCR_FLIGHT && plane.spec && !crashed) {
    const AircraftSpec& s = *plane.spec;
    float t = realTime;
    float dcam = length(plane.pos - fp.camPos);
    float ls = std::max(0.25f, dcam * 0.002f);
    float navI = 0.6f + 2.5f * night;
    vec3 lt = plane.pos + plane.q.rotate(vec3(-s.span * 0.5f, s.wingY * s.fusRad, s.wingZ)), rtp = plane.pos + plane.q.rotate(vec3(s.span * 0.5f, s.wingY * s.fusRad, s.wingZ));
    if (camMode != 1) {
      bill(add, lt, ls, vec3(1.f, 0.1f, 0.05f) * navI, 1, SPR_GLOW, 0.3f);
      bill(add, rtp, ls, vec3(0.1f, 1.f, 0.2f) * navI, 1, SPR_GLOW, 0.3f);
      bill(add, plane.pos + plane.q.rotate(vec3(0, s.fusRad * 0.5f, s.fusLen * 0.5f)), ls, vec3(1.f) * navI, 1, SPR_GLOW, 0.3f);
      if (plane.engineRunning && fmodf(t, 1.0f) < 0.12f) bill(add, plane.pos + plane.q.rotate(vec3(0, s.fusRad * 1.05f, 0.1f * s.fusLen)), ls * 1.6f, vec3(1.f, 0.05f, 0.02f) * (2.f + 3.f * night), 1, SPR_GLOW, 0.3f);
      float st = fmodf(t, 1.3f);
      if (!plane.onGround && (st < 0.05f || (st > 0.12f && st < 0.16f))) { bill(add, lt, ls * 3.f, vec3(4.f), 1, SPR_GLOW, 0.3f); bill(add, rtp, ls * 3.f, vec3(4.f), 1, SPR_GLOW, 0.3f); }
    }
    if (landingLight && plane.engineRunning && camMode != 1)
      bill(add, plane.pos + plane.q.rotate(vec3(s.engLayout == 0 ? -s.span * 0.25f : 0, s.engLayout == 0 ? s.wingY * s.fusRad : -s.fusRad * 0.6f, s.engLayout == 0 ? s.wingZ - s.chord * 0.5f : -0.35f * s.fusLen)), ls * 1.8f,
           vec3(1.f, 0.95f, 0.85f) * (1.f + 4.f * night), 1, SPR_GLOW, 0.3f);
    // waypoint rings
    for (int i = wpIndex; i < (int)contract.wps.size() && i < wpIndex + 3; i++) {
      const Waypoint& w = contract.wps[i];
      vec3 c(w.x, w.alt, w.z);
      vec3 prev = i == 0 ? g_world.airports[contract.from].pos() : vec3(contract.wps[i - 1].x, contract.wps[i - 1].alt, contract.wps[i - 1].z);
      vec3 nd = normalize(vec3(c.x - prev.x, 0, c.z - prev.z) + vec3(0.001f, 0, 0));
      vec3 ax = normalize(cross(nd, vec3(0, 1, 0))) * 90.f, ay = vec3(0, 90.f, 0);
      vec3 col = i == wpIndex ? vec3(0.2f, 1.f, 0.45f) * 2.f : vec3(1.f, 0.4f, 1.f) * 0.9f;
      quadAx(add, c, ax, ay, col, 1.f, SPR_RING, 5.f);
    }
  }
  // particles
  std::vector<std::pair<float, const Particle*>> order;
  for (const Particle& p : particles) order.push_back({-length(p.p - fp.camPos), &p});
  std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first < b.first; });
  for (auto& o : order) {
    const Particle& p = *o.second;
    float fade = clampf(p.life / p.maxLife, 0, 1);
    float a = p.alpha * (p.kind == SPR_FIRE ? fade : fade * smoothstepf(0.f, 0.15f, 1.f - fade + 0.15f));
    if (p.kind == SPR_FIRE) bill(add, p.p, p.size, p.col, a, SPR_FIRE, 1.f);
    else bill(alpha, p.p, p.size, p.col, a, p.kind, 1.f);
  }
  // precipitation around the camera
  if (screen == SCR_FLIGHT && wx.precip > 0) {
    const int N = wx.precip == 1 ? 700 : 1400;
    if (rainDrops.size() != (size_t)N) { rainDrops.resize(N); Rng r(5); for (auto& d : rainDrops) d = vec3(r.range(-30, 30), r.range(-20, 20), r.range(-30, 30)); }
    vec3 camV = screen == SCR_FLIGHT && plane.spec ? plane.vel : vec3();
    bool rain = wx.precip == 1;
    vec3 fall = rain ? vec3(0, -9.f, 0) : vec3(0, -1.2f, 0);
    vec3 wv = plane.windVel;
    vec3 rel = fall + wv - camV;
    for (size_t i = 0; i < rainDrops.size(); i++) {
      vec3& d = rainDrops[i];
      float tt = realTime;
      vec3 p = d + rel * fmodf(tt * 0.6f + (i % 37) * 0.13f, 1.0f) * 0.12f * (rain ? 6.f : 1.f);
      if (!rain) p = p + vec3(sinf(tt * 1.3f + i), 0, cosf(tt * 1.1f + i * 0.7f)) * 0.5f;
      // wrap into the box around the camera
      auto wrap = [](float v, float h) { return v - floorf((v + h) / (2 * h)) * 2 * h; };
      vec3 wpos(wrap(p.x, 30.f), wrap(p.y, 20.f), wrap(p.z, 30.f));
      wpos = wpos + fp.camPos;
      if (rain) {
        vec3 dir = normalize(rel); float len = clampf(length(rel) * 0.035f, 0.3f, 3.f);
        vec3 side = normalize(cross(dir, wpos - fp.camPos)) * 0.012f;
        quadAx(alpha, wpos, side, dir * len, vec3(0.75f, 0.78f, 0.85f), 0.35f, SPR_RAIN, 0.5f);
      } else {
        bill(alpha, wpos, 0.09f, vec3(1, 1, 1), 0.9f, SPR_SNOW, 0.5f);
      }
    }
  }
}

// ------------------------------------------------------------------ audio
void Game::feedAudio() {
  AudioParams ap;
  ap.master = set.master; ap.engineVol = set.engineVol; ap.sfxVol = set.sfxVol;
  static bool muffled = false;
  if (in.pressed['M'] || (in.buttonsPressed & PAD_LS)) { muffled = !muffled; toast(muffled ? "Engine noise muffled (headset ANR on)" : "Headset ANR off"); }
  ap.muffled = muffled;
  if (screen == SCR_FLIGHT && plane.spec && !crashed) {
    const AircraftSpec& s = *plane.spec;
    ap.inFlight = true;
    ap.engineType = s.engineType; ap.engines = s.engines; ap.cylinders = s.cylinders; ap.blades = s.blades;
    ap.rpm = s.engineType == ENG_JET ? plane.n1 : plane.rpm; ap.maxRpm = s.maxRpm; ap.n1 = plane.n1;
    ap.spool = plane.engineSpool; ap.throttle = plane.engineRunning ? plane.ctl.throttle : 0.f;
    ap.running = plane.engineRunning; ap.cranking = !plane.engineRunning && plane.starterTime > 0;
    ap.airspeed = plane.airspeed; ap.groundSpeed = length(vec3(plane.vel.x, 0, plane.vel.z)); ap.onGround = plane.onGround; ap.rough = plane.groundRough;
    ap.stallWarn = plane.onGround ? 0.f : plane.stallWarn; ap.stallIsShaker = s.engineType == ENG_JET || s.license == LIC_ATP;
    ap.gearMoving = plane.gear > 0.01f && plane.gear < 0.99f; ap.flapsMoving = fabsf(plane.flaps - plane.ctl.flaps) > 0.02f;
    ap.gearDown = plane.gear; ap.flaps = plane.flaps;
    ap.interior = camMode == 1;
    ap.rain = wx.precip == 1 ? (wx.storm ? 1.f : 0.6f) : 0.f; ap.turbulence = wx.turbulence;
    ap.paused = paused;
  }
  g_audio.setParams(ap);
}

// ------------------------------------------------------------------ main update / render
void Game::update(float dt) {
  dt = std::min(dt, 0.05f);
  realTime += dt;
  for (auto& t : toasts) t.t += dt;
  while (!toasts.empty() && toasts.front().t > 5.f) toasts.erase(toasts.begin());
  hubMsgTime = std::max(0.f, hubMsgTime - dt);
  if (in.pressed[K_F11]) wantFullscreenToggle = true;
  if (in.pressed['R'] && screen == SCR_FLIGHT) showRadio = !showRadio;
  radio.poll();
  if (screen == SCR_FLIGHT) {
    if (in.pressed[K_ESC] || (in.buttonsPressed & PAD_START)) { if (showMap) showMap = false; else if (showRadio) showRadio = false; else { paused = !paused; settingsFromPause = false; } }
    if (in.pressed['N'] || in.pressed[K_TAB]) showMap = !showMap;
    if (in.pressed['H']) hudOn = !hudOn;
    if (!paused) {
      updateFlight(dt);
      if (screen == SCR_FLIGHT) { updateCamera(dt); updateParticles(dt * timeAccel); }
      if (plane.spec) {
        float rps = plane.spec->engineType == ENG_TURBOPROP ? plane.rpm / 60.f : plane.rpm / 60.f;
        propAngle = fmodf(propAngle + rps * 2 * PI * dt * (plane.rpm < 400 ? 1.f : 0.0f) + (plane.rpm >= 400 ? dt * 3.f : 0.f), 2 * PI * 100);
      }
    }
  } else {
    wx = Weather(); wx.cloudCover = 0.35f; wx.cloudBase = 1500; wx.visibility = 45000; wx.windSpeed = 4;
    timeOfDay = screen == SCR_MENU ? 17.3f : 15.8f;
    cloudOff = cloudOff + vec2(dt * 8.f, dt * 3.f);
  }
  feedAudio();
}

void Game::render() {
  FrameParams fp = buildFrame();
  std::vector<SpriteVert> a, b;
  buildSprites(fp, a, b);
  g_ren.renderScene(fp, a, b);
  g_ren.uiBegin();
  switch (screen) {
    case SCR_MENU: drawMenu(); break;
    case SCR_HUB: drawHub(); break;
    case SCR_FLIGHT: drawHud(fp); if (showMap) drawMapOverlay(); if (paused) drawPause(); break;
    case SCR_DEBRIEF: drawDebrief(); break;
  }
  drawToasts();
  g_ren.uiEnd();
}

// ------------------------------------------------------------------ development scenes for the render harness
void Game::debugScene(const std::string& name) {
  career.newGame(); career.license = LIC_ATP;
  if (name == "menu") { screen = SCR_MENU; realTime = 20; return; }
  if (name == "hub") { screen = SCR_HUB; realTime = 20; return; }
  Contract c = g_story[0];
  int spec = 0;
  if (name == "storm") { c = g_story[g_story.size() - 1]; spec = 5; }
  if (name == "jet") { c = g_story[g_story.size() - 4]; spec = 6; }
  if (name == "night") { c = g_story[11]; c.wx.timeOfDay = 21.5f; spec = 1; }
  if (name == "snow") { c = g_story[20]; spec = 2; }
  startFlight(c, spec, Career::SRC_OWNED);
  realTime = 10;
  plane.starterTime = 0.01f; plane.engineRunning = true; plane.rpm = 1000; engineAutoStarted = true;
  if (name == "air" || name == "sunset" || name == "mountain" || name == "cockpit" || name == "jet" || name == "storm" || name == "snow" || name == "hud") {
    vec3 p(-4000, 600, 9000); float hdg = 40;
    if (name == "sunset") { timeOfDay = 18.2f; p = vec3(-26000, 300, 14000); hdg = 270; }
    if (name == "mountain") { p = vec3(-20000, 1900, -9000); hdg = 60; }
    if (name == "jet") { p = vec3(8000, 900, 6000); hdg = 80; }
    if (name == "storm") { p = vec3(10000, 700, 10000); hdg = 120; }
    if (name == "snow") { p = vec3(20000, 600, -24000); hdg = 80; }
    plane.reset(&kAircraft[spec], p, hdg, kAircraft[spec].maxFuel, 100, true, kAircraft[spec].cruise);
    takeoffAnnounced = true;
    camQ = plane.q;
    if (name == "cockpit") camMode = 1;
  }
  if (name == "top") { camMode = 2; camYaw = 0.3f; camPitch = 1.35f; camZoom = 4.f; }
  if (name == "orbit") { camMode = 2; camYaw = 2.3f; camPitch = 0.25f; camZoom = 0.6f; timeOfDay = 9.0f; }
  for (int i = 0; i < 30; i++) updateCamera(0.1f);
  if (name == "hud") hint = contract.hints.size() ? contract.hints[2] : "";
}
