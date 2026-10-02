// Air Xpress - game flow, flight session, cameras, particles, lights, audio feed
#include "game.h"
#include "entities.h"
#include "models.h"

// XR-9 wingtip (body frame) matching mapJet's cranked delta in shaders.h
static const vec3 kJetWingTip(5.62f, -0.38f, 4.4f);
// XR-9 nozzle swivel (0 aft .. 90 deg down) plus pitch vectoring; mirrored by mapJet and the exhaust plumes
static float jetNozzleAngle(const Plane& p) { return p.nozzle * 0.5f * PI - clampf(p.ctl.pitch + p.ctl.trim * 0.3f, -1, 1) * 0.5f; }
static const vec3 kWraithWingTip(6.2f, -0.24f, 2.0f);
// research craft exhaust exits and jet directions (body coords): XR-9 two 2D nozzles, XR-11 four pods.
// strength: that exhaust's share of the thrust (1 = the XR-9's)
static int jetExhausts(const Plane& p, vec3* pos, vec3* dir, float* strength) {
  if (p.spec->special == 2) {
    for (int i = 0; i < 4; i++) {
      float a0 = p.podTilt[i], a = a0 + p.podVane[i], y = p.podYaw[i];
      pos[i] = kWraithPods[i] + vec3(0, -sinf(a0), cosf(a0)) * 1.55f;
      dir[i] = normalize(vec3(-sinf(y), -sinf(a) * cosf(y), cosf(a) * cosf(y)));
      strength[i] = clampf(p.podThr[i] * 1.4f, 0.f, 1.2f);
    }
    return 4;
  }
  float a = jetNozzleAngle(p);
  for (int s = 0; s < 2; s++) { pos[s] = vec3(s ? 0.82f : -0.82f, -0.12f, 7.75f) + vec3(0, -sinf(a), cosf(a)); dir[s] = vec3(0, -sinf(a), cosf(a)); strength[s] = 1.f; }
  return 2;
}

// ------------------------------------------------------------------ settings / save
static std::string joinPath(const std::string& d, const char* f) { return d.empty() ? std::string(f) : d + "/" + f; }

void Game::loadSettings() {
  FILE* f = fopen(joinPath(saveDir, "settings.cfg").c_str(), "r");
  if (!f) return;
  char k[64]; float v;
  while (fscanf(f, "%63s %f", k, &v) == 2) {
    std::string s = k;
    if (s == "renderScale") set.renderScale = 1.0f;   // old setting: rendering is now always at 100%
    else if (s == "quality") set.quality = (int)clampf(v, 0, 2);
    else if (s == "master") set.master = clampf(v, 0, 1);
    else if (s == "engineVol") set.engineVol = clampf(v, 0, 1.5f);
    else if (s == "sfxVol") set.sfxVol = clampf(v, 0, 1.5f);
    else if (s == "radioVol") set.radioVol = clampf(v, 0, 1);
    else if (s == "invertPitch") set.invertPitch = v != 0;
    else if (s == "showHints") set.showHints = v != 0;
    else if (s == "traffic") set.traffic = v != 0;
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
  fprintf(f, "renderScale %f\nquality %d\nmaster %f\nengineVol %f\nsfxVol %f\nradioVol %f\ninvertPitch %d\nshowHints %d\nmetric %d\nfullscreen %d\nradioStation %d\nmouseSens %f\ntraffic %d\n",
          set.renderScale, set.quality, set.master, set.engineVol, set.sfxVol, set.radioVol, set.invertPitch, set.showHints, set.metric, set.fullscreen, set.radioStation, set.mouseSens, set.traffic);
  fclose(f);
}

// Built-in presets. Bump kStationsVersion when adding presets: older station files get the new ones appended.
static const int kStationsVersion = 2;
static const std::pair<const char*, const char*> kDefaultStations[] = {
  {"SomaFM Groove Salad (ambient)", "https://ice1.somafm.com/groovesalad-128-mp3"},
  {"SomaFM Drone Zone", "https://ice1.somafm.com/dronezone-128-mp3"},
  {"SomaFM Secret Agent", "https://ice1.somafm.com/secretagent-128-mp3"},
  {"SomaFM Lush", "https://ice1.somafm.com/lush-128-mp3"},
  {"SomaFM Indie Pop Rocks", "https://ice1.somafm.com/indiepop-128-mp3"},
  {"Radio Paradise (eclectic)", "https://stream.radioparadise.com/mp3-128"},
  {"Radio Paradise Mellow", "https://stream.radioparadise.com/mellow-128"},
  {"KEXP Seattle", "https://kexp-mp3-128.streamguys1.com/kexp128.mp3"},
  // US East Coast public and college stations (free, listener-supported)
  {"WNYC 93.9 New York - NPR news", "https://fm939.wnyc.org/wnycfm"},
  {"WQXR 105.9 New York - classical", "https://stream.wqxr.org/wqxr"},
  {"WFMU 91.1 Jersey City - freeform", "https://stream0.wfmu.org/freeform-128k"},
  {"WBGO 88.3 Newark - jazz", "https://wbgo.streamguys1.com/wbgo128"},
  {"WFUV 90.7 New York - indie & folk", "https://onair.wfuv.org/onair-hi"},
  {"WXPN 88.5 Philadelphia - AAA", "https://wxpnhi.xpn.org/xpnhi"},
  {"WBUR 90.9 Boston - NPR news", "https://icecast-stream.wbur.org/wbur"},
  {"GBH 89.7 Boston - NPR news", "https://wgbh-live.streamguys1.com/wgbh"},
  {"CRB 99.5 Boston - classical", "https://wgbh-live.streamguys1.com/classical-hi"},
  {"WMBR 88.1 MIT Cambridge - college", "https://wmbr.org:8002/hi"},
  {"WUNC 91.5 Chapel Hill - NPR news", "https://wunc-ice.streamguys1.com/wunc-128-mp3"},
  {"WCPE 89.7 Raleigh - classical", "https://audio-mp3.ibiblio.org/wcpe.mp3"},
};

static void writeStations(const std::string& path, const std::vector<std::pair<std::string, std::string>>& st) {
  FILE* f = fopen(path.c_str(), "w");
  if (!f) return;
  fprintf(f, "# Air Xpress internet radio stations\n# stations-version %d\n# One per line:  Name|URL   (MP3 or AAC HTTP/HTTPS streams). Add your own below.\n", kStationsVersion);
  for (auto& s : st) fprintf(f, "%s|%s\n", s.first.c_str(), s.second.c_str());
  fclose(f);
}

void Game::loadStations() {
  stations.clear();
  std::string path = joinPath(saveDir, "radio_stations.txt");
  FILE* f = fopen(path.c_str(), "r");
  bool userFile = f != nullptr;
  if (!f) f = fopen("radio_stations.txt", "r");
  int version = 1;
  if (f) {
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
      std::string s = line;
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
      if (s.compare(0, 19, "# stations-version ") == 0) version = atoi(s.c_str() + 19);
      if (s.empty() || s[0] == '#') continue;
      size_t bar = s.find('|');
      if (bar == std::string::npos) stations.push_back({s, s});
      else stations.push_back({s.substr(0, bar), s.substr(bar + 1)});
    }
    fclose(f);
  }
  if (stations.empty() || version < kStationsVersion || (!userFile && !saveDir.empty())) {
    // add presets the list doesn't have yet (keeps the player's own stations and order)
    for (auto& d : kDefaultStations) {
      bool have = false;
      for (auto& s : stations) if (s.second == d.second) have = true;
      if (!have) stations.push_back({d.first, d.second});
    }
    if (!saveDir.empty() || userFile) writeStations(path, stations);
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
  wpIndex = 0; flightClock = 0; crashTimer = 0; endTimer = 0; airBreak = false; crashEndT = 7.5f; gTunnel = 0;
  traffic.reset();
  ufo = Ufo(); ufo.next = 180.f + (rand() % 1000) * 0.24f;   // first encounter after 3-7 minutes in the air
  paused = false; showMap = false; landed = completed = crashed = false;
  result = FlightResult();
  timeAccel = 1; camMode = camMode == 1 ? 1 : 0; camYaw = 0; camPitch = 0.12f; camZoom = 1;
  lookYaw = 0; lookPitch = -0.13f;
  camQ = plane.q; camPos = plane.pos + plane.q.rotate(vec3(0, 3, 15));
  flapNotch = 0; phase = 0; lastHintPhase = -1; hint.clear();
  takeoffAnnounced = false; touchedDown = false; touchdownFpm = 0; stillTimer = 0;
  engineAutoStarted = false; startDelay = 1.2f;
  particles.clear(); bursts.clear(); trail.clear(); trailT = 0; wreck.clear(); debris.clear(); craterR = 0;
  lightning = 0; nextLightning = 6; thunderDelay = -1;
  landingLight = true;
  approachMinAgl = 1e9f;
  apWasOn = false; apDest = -1; wraith = WraithState();
  licenseBefore = career.license;
  screen = SCR_FLIGHT;
  toast(fmt("%s - %s", a.code, a.name), vec3(0.7f, 0.9f, 1.0f));
  toast(fmt("Runway %02d, %s", a.rwyNumber(reverse), wx.describe().c_str()), vec3(0.8f, 0.8f, 0.8f));
}

void Game::endFlight(bool success, const std::string& reason) {
  if (researchFlight) {  // research flights never touch the career: back to the research menu
    researchFlight = false; paused = false; showMap = false;
    screen = SCR_RESEARCH; resOpened = realTime;
    if (!reason.empty()) toast(reason, success ? vec3(0.6f, 1, 0.7f) : vec3(1, 0.5f, 0.4f));
    return;
  }
  result.success = success;
  result.failReason = reason;
  result.flightMin = flightClock / 60.f;
  result.maxG = plane.maxG; result.minG = plane.minG;
  result.fuelUsedKg = std::max(0.f, fuelStart - plane.fuel);
  result.touchdownFpm = touchdownFpm;
  result.late = contract.timeLimitMin > 0 && flightClock / 60.f > contract.timeLimitMin;
  result.landed = touchedDown && plane.onGround;
  if (!result.landed) result.touchdownFpm = 0;
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

// the autopilot is flying a leg where time acceleration is safe (holds, en route, descent orbit - not the approach)
void Game::cycleApDest(int dir) {
  int n = (int)g_world.airports.size();
  std::vector<int> order(n);
  for (int i = 0; i < n; i++) order[i] = i;
  auto d2 = [&](int i) { float dx = g_world.airports[i].x - plane.pos.x, dz = g_world.airports[i].z - plane.pos.z; return dx * dx + dz * dz; };
  std::sort(order.begin(), order.end(), [&](int a, int b) { return d2(a) < d2(b); });
  if (apDest < 0) apDest = (int)(&dest() - &g_world.airports[0]);   // first pick: the contract's destination
  else {
    int k = (int)(std::find(order.begin(), order.end(), apDest) - order.begin());
    apDest = order[((k + dir) % n + n) % n];
  }
  g_audio.trigger(SFX_CLICK);
}

bool Game::apCruising() const {
  return plane.apOn && (plane.apMode == Plane::AP_HOLD || (plane.apMode == Plane::AP_APPR && plane.apStage == Plane::APS_NAV));
}

void Game::engageAutopilot() {
  if (apDest >= 0) {
    plane.apEngage(Plane::AP_NAV, apDest, wx);
    const Airport& a = g_world.airports[apDest];
    toast(fmt("Autopilot: AUTOLAND %s runway %02d", a.code, a.rwyNumber(plane.apRev)), vec3(0.6f, 1, 0.6f));
    toast("Any stick input hands control back", vec3(0.8f, 0.8f, 0.8f));
  } else {
    plane.apEngage(Plane::AP_HOLD, -1, wx);
    toast("Autopilot ON: A/D heading, W/S altitude, throttle keys speed", vec3(0.6f, 1, 0.6f));
    toast("Pick an AUTOLAND airport on the GPS (N) to fly there", vec3(0.8f, 0.8f, 0.8f));
  }
  g_audio.trigger(SFX_CLICK);
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
  bool apNav = plane.apOn && plane.apMode == Plane::AP_APPR;
  if (plane.apOn) {
    if (apNav) {   // flying a GPS route / autoland: any real stick input hands control back
      if (fabsf(pitchIn + padP) > 0.5f || fabsf(rollIn + padR) > 0.5f) {
        plane.apDisengage(); g_audio.trigger(SFX_AP_DISC); toast("Autopilot disconnected - your aircraft", vec3(1, 0.7f, 0.3f));
      }
    } else {       // hold mode: the stick moves the targets (A/D heading, W/S altitude)
      plane.apHeading = wrapDeg360(plane.apHeading + (rollIn + padR) * 20.f * dt);
      plane.apAlt = std::max(plane.apAlt + (pitchIn + padP) * 45.f * dt, g_world.height(plane.pos.x, plane.pos.z) + 60.f);
    }
  }
  if (!plane.apOn) {
    float tp = clampf(pitchIn + padP, -1, 1), tr = clampf(rollIn + padR, -1, 1);
    float rate = manual ? 2.8f : 5.f;
    c.pitch = approach(c.pitch, tp, rate, dt);
    c.roll = approach(c.roll, tr, rate, dt);
  }
  (void)manual;
  // XR-11 weapons hot: the bumpers fire and bomb, so the rudder is frozen until weapons are safe again
  if (plane.spec->special == 2 && wraith.armed && !plane.onGround) { yawIn = 0; padY = 0; }
  c.yaw = approach(c.yaw, clampf(yawIn + padY, -1, 1), 4.f, dt);
  // throttle
  float thr = 0;
  if (key(K_SHIFT) || key(K_PGUP) || key(K_PLUS)) thr += 0.55f;
  if (key(K_CTRL) || key(K_PGDN) || key(K_MINUS)) thr -= 0.55f;
  if (in.pad) thr += (in.rt - in.lt) * 0.6f;
  bool numKey = in.pressed['0'];
  for (int k = 1; k <= 9; k++) numKey |= in.pressed['0' + k] && !in.down[K_CTRL];
  if (plane.apOn && plane.apMode == Plane::AP_HOLD && plane.apSpeed > 0 && numKey) { plane.apSpeed = 0; toast("Autothrottle off - manual throttle", vec3(1, 0.85f, 0.5f)); }
  if (apNav) {}   // the autopilot has the throttle
  else if (plane.apOn && plane.apSpeed > 0) {   // autothrottle: throttle inputs move the speed target
    const AircraftSpec& sp = *plane.spec;
    plane.apSpeed = clampf(plane.apSpeed + thr * 30.f * dt, sp.vref * 1.2f, sp.cruise * (sp.special ? 1.6f : 1.15f));
  } else {
    c.throttle = clampf(c.throttle + thr * dt, 0, 1);
    for (int k = 1; k <= 9; k++) if (in.pressed['0' + k] && !in.down[K_CTRL]) c.throttle = k / 9.f;
    if (in.pressed['0']) c.throttle = 0;
  }
  // trim
  float tr = (key(K_RBRACKET) || key(K_HOME) ? 1.f : 0.f) - (key(K_LBRACKET) || key(K_END) ? 1.f : 0.f);
  if (in.pad) tr += ((in.buttons & PAD_UP) ? 1.f : 0.f) - ((in.buttons & PAD_DOWN) ? 1.f : 0.f);
  c.trim = clampf(c.trim + tr * 0.35f * dt, -1, 1);
  // flaps
  auto flapToast = [&]() {
    if (plane.spec->special == 2) toast(flapNotch > 0.99f ? "Pods 90 deg - VTOL hover" : fmt("Pods %d deg", (int)lroundf(flapNotch * 90)), vec3(0.8f, 0.55f, 1));
    else if (plane.spec->special) toast(flapNotch > 0.99f ? "Thrust vector 90 deg - VTOL hover" : fmt("Thrust vector %d deg", (int)lroundf(flapNotch * 90)), vec3(0.4f, 0.9f, 1));
    else toast(fmt("Flaps %d%%", (int)lroundf(flapNotch * 100)), vec3(0.8f, 0.9f, 1));
  };
  if (apNav) flapNotch = c.flaps;   // the autopilot runs the flaps on the approach
  else {
    if (in.pressed['F'] || (in.buttonsPressed & PAD_B)) { flapNotch = std::min(1.f, flapNotch + 1.f / 3.f); flapToast(); }
    if (in.pressed['V'] || (in.buttonsPressed & PAD_X)) { flapNotch = std::max(0.f, flapNotch - 1.f / 3.f); flapToast(); }
  }
  c.flaps = flapNotch;
  // gear
  bool wrAir = plane.spec->special == 2 && !plane.onGround;   // XR-11 in the air: gamepad Y is weapons hot / safe instead of the gear
  if ((in.pressed['G'] || (in.buttonsPressed & ((wrAir ? 0u : (unsigned)PAD_Y) | (showMap ? 0u : (unsigned)PAD_RIGHT)))) && plane.spec->retract) {
    if (plane.onGround && c.gearDown) toast("Gear lever is locked on the ground", vec3(1, 0.6f, 0.4f));
    else { c.gearDown = !c.gearDown; toast(c.gearDown ? "Gear down" : "Gear up", vec3(0.8f, 1, 0.8f)); }
  }
  // brakes: B / D-pad left = parking brake toggle, Space = wheel brakes
  static bool parking = true;
  if (flightClock < 0.05f) parking = plane.onGround;
  if (plane.apDone) { plane.apDone = false; parking = true; toast("Autoland complete - parking brake set", vec3(0.5f, 1, 0.6f)); g_audio.trigger(SFX_AP_DISC, 0.7f); }
  if (plane.spec->special == 2) wraithControls(dt);
  if (in.pressed['B'] || (!showMap && (in.buttonsPressed & PAD_LEFT))) { parking = !parking; toast(parking ? "Parking brake SET" : "Parking brake released", vec3(1, 0.85f, 0.5f)); }
  float wb = key(K_SPACE) ? 1.f : 0.f;
  if (in.pad && (in.buttons & PAD_A)) wb = 1.f;
  if (wb > 0 && parking && plane.onGround && length(plane.vel) > 2.f) parking = false;
  c.brake = parking ? 1.f : wb;
  // autopilot: Z / right stick click. With an autoland airport picked on the GPS it flies there and lands;
  // otherwise it holds heading, altitude and speed
  if (in.pressed['Z'] || (in.buttonsPressed & PAD_RS)) {
    if (plane.apOn) { plane.apDisengage(); g_audio.trigger(SFX_AP_DISC); toast("Autopilot OFF", vec3(1, 0.7f, 0.3f)); }
    else if (plane.onGround) toast("Autopilot needs to be airborne", vec3(1, 0.6f, 0.4f));
    else engageAutopilot();
  }
  if (in.pressed['L']) { landingLight = !landingLight; toast(landingLight ? "Landing lights ON" : "Landing lights OFF"); }
  if (in.pressed['I'] && !plane.engineRunning && plane.fuel > 0) { plane.starterTime = 0.01f; toast("Engine start"); }
  // time acceleration
  if (in.pressed['T']) {
    float dd = length(vec3(plane.pos.x - dest().x, 0, plane.pos.z - dest().z));
    if (!apCruising() && (plane.onGround || plane.agl() < 250.f || dd < 3500.f)) { timeAccel = 1; toast("Time acceleration only available in cruise", vec3(1, 0.7f, 0.4f)); }
    else { timeAccel = timeAccel >= 4 ? 1 : timeAccel * 2; toast(fmt("Time x%.0f", timeAccel)); }
  }
}

void Game::spawn(vec3 p, vec3 v, float life, float size, float grow, vec3 col, float alpha, int kind, float drag, float buoy) {
  if (particles.size() > 6000) return;
  particles.push_back({p, v, life, life, size, grow, col, alpha, kind, drag, buoy, false});
}

// checkpoint gate i: centre and half-extent axes (gate faces along the leg leading to it)
bool Game::ringGeom(int i, vec3& c, vec3& ax, vec3& ay) const {
  if (i < 0 || i >= (int)contract.wps.size()) return false;
  const Waypoint& w = contract.wps[i];
  c = vec3(w.x, w.alt, w.z);
  vec3 prev = i == 0 ? g_world.airports[contract.from].pos() : vec3(contract.wps[i - 1].x, contract.wps[i - 1].alt, contract.wps[i - 1].z);
  vec3 nd = normalize(vec3(c.x - prev.x, 0, c.z - prev.z) + vec3(0.001f, 0, 0));
  ax = normalize(cross(nd, vec3(0, 1, 0))) * 90.f; ay = vec3(0, 90.f, 0);
  return true;
}

void Game::updateFlight(float dt) {
  if (paused) return;
  if (!botControl) flightControls(dt);
  // automatic engine start sequence after the scene fades in
  if (!engineAutoStarted && !contract.startAirborne) {
    startDelay -= dt;
    if (startDelay <= 0) { engineAutoStarted = true; plane.starterTime = 0.01f; toast("Starting engine..."); }
  }
  bool wasRunning = plane.engineRunning;
  float simDt = dt * timeAccel;
  if (timeAccel > 1) {
    float dd = length(vec3(plane.pos.x - dest().x, 0, plane.pos.z - dest().z));
    bool approach = plane.apOn && plane.apMode == Plane::AP_APPR && plane.apStage != Plane::APS_NAV;
    if ((!apCruising() && (plane.agl() < 250.f || dd < 3500.f)) || approach || plane.onGround || crashed) { timeAccel = 1; toast("Time acceleration off"); }
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

  updateUfo(simDt);
  // AI traffic
  traffic.enabled = set.traffic;
  if (traffic.update(simDt, plane.pos, plane.vel, plane.onGround || crashed, plane.spec->span) && !crashed && !plane.ev.crashed) {
    plane.ev.crashed = true; plane.ev.crashReason = "Mid-air collision";
  }
  for (auto& f : traffic.puffs) spawn(f.p, f.v, f.life, f.size, f.grow, f.col, f.alpha, f.kind, 1.f, 0.f);
  for (auto& b : traffic.booms) g_audio.trigger(SFX_BOOM, b.second);
  updateWraith(simDt);
  for (float f : traffic.flybys) g_audio.trigger(SFX_FLYBY, f);
  for (auto& m : traffic.radio) toast(m, vec3(1.f, 0.78f, 0.3f));
  // entertainment: O + P held for a second while flying summons the Spectre display pair (again: sends them home).
  // They fly their show around you and bow out when you land or settle onto an approach
  {
    bool flying = !crashed && !plane.onGround;
    if (flying && in.down['O'] && in.down['P']) {
      escortSummon += dt;
      if (escortSummon >= 1.f && !escortLatch) {
        escortLatch = true;
        if (traffic.escortActive()) traffic.dismissEscort();
        else if (traffic.escortStop) toast("Spectre display pair: climb away from the field first", vec3(1.f, 0.78f, 0.3f));
        else { traffic.spawnEscort(plane.pos, plane.vel); g_audio.trigger(SFX_CHIME, 0.8f); }
      }
    } else { escortSummon = 0; escortLatch = false; }
    float dn; int na = g_world.nearestAirport(plane.pos.x, plane.pos.z, &dn);
    (void)na;
    bool onApproach = plane.apOn && plane.apMode == Plane::AP_APPR && plane.apStage != Plane::APS_NAV && plane.apStage != Plane::APS_GOAROUND;
    traffic.escortStop = crashed || plane.onGround || onApproach || (plane.agl() < 150.f && dn < 4000.f);
  }
  {
    // g-force tunnel: a faint red tint from the first noticeable g that slowly closes into the full ring as the load
    // nears the airframe's limit (regular aircraft 1.8 -> 6 g, the XR-9's damped cell 4 -> 50 g; negative g from
    // -0.5 g). It builds over ~1 s and recovers over ~1.5 s
    bool jet = plane.spec->special != 0;
    float gp = smoothstepf(jet ? 4.f : 1.8f, jet ? 50.f : 6.f, plane.gLoad), gn = smoothstepf(jet ? -2.f : -0.5f, jet ? -25.f : -3.f, plane.gLoad);
    float target = crashed ? 0.f : std::max(gp, gn);
    gTunnel = approach(gTunnel, target, target > gTunnel ? 1.2f : 0.7f, dt);
  }
  if (crashed) {
    crashTimer += dt;
    // skip the crash sequence: A on the gamepad (or Enter / Space) goes straight to the results
    if (screen == SCR_FLIGHT && crashTimer > 0.8f && ((in.buttonsPressed & PAD_A) || in.pressed[K_ENTER] || in.pressed[' '])) {
      in.buttonsPressed &= ~PAD_A; in.pressed[K_ENTER] = in.pressed[' '] = false;   // don't let the same press click the results screen
      in.mPressed[0] = false; in.mDown[0] = false; padHoldA = false;
      endFlight(false, plane.ev.crashReason);
      return;
    }
    updateWreck(dt);
    camYaw += dt * 0.12f;   // slow orbit around the crash site
    if (crashTimer > crashEndT && screen == SCR_FLIGHT) endFlight(false, plane.ev.crashReason);
    return;
  }
  if (plane.ev.crashed) {
    vec3 impactVel = plane.vel;
    crashed = true;
    g_audio.trigger(SFX_CRASH);
    toast(plane.ev.crashReason, vec3(1, 0.4f, 0.3f));
    bool water = g_world.height(plane.pos.x, plane.pos.z) < 0.5f;
    bool air = plane.ev.crashReason.find("Structural") != std::string::npos && plane.agl() > 4.f;   // overstressed in flight: it comes apart in the air
    breakUp(impactVel, water, air);
    plane.vel = vec3(); plane.w = vec3();
    if (camMode == 1 || camMode == 3) camMode = 2;
    camZoom = std::max(camZoom, air ? 2.6f : 1.8f); camPitch = 0.3f;
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
    // emitted at the real wingtips and spread back along the path flown this frame; the vapour stays in the air
    // mass (it used to be launched at 90% of the aircraft's speed, which carried it out ahead of the wingtips)
    vec3 tip = plane.spec->special == 2 ? kWraithWingTip : plane.spec->special ? kJetWingTip : modelWingTip(kModels[plane.spec - kAircraft]);
    int n = (int)clampf(length(plane.vel) * simDt / 0.45f, 1.f, 16.f);   // ~0.45 m spacing: a continuous streak
    for (int s = -1; s <= 1; s += 2)
      for (int k = 0; k < n; k++) {
        vec3 tp = plane.pos + plane.q.rotate(vec3(s * tip.x, tip.y, tip.z + 0.3f)) - plane.vel * (simDt * (float)k / n);
        spawn(tp, plane.vel * 0.04f + plane.windVel, 0.5f, 0.42f, 1.4f, vec3(1, 1, 1), 0.2f, SPR_SMOKE, 1.f, 0.f);
        if (!particles.empty()) particles.back().instant = true;
      }
  }
  if (plane.spec->special) jetEffects(simDt);
  // GPS breadcrumb trail
  trailT += dt;
  if (trailT > 2.f && !plane.onGround) { trailT = 0; trail.push_back(vec2(plane.pos.x, plane.pos.z)); if (trail.size() > 500) trail.erase(trail.begin()); }
  // waypoints
  if (wpIndex < (int)contract.wps.size()) {
    const Waypoint& w = contract.wps[wpIndex];
    vec3 wp(w.x, w.alt, w.z);
    vec3 gc, gx, gy;
    if (ringGeom(wpIndex, gc, gx, gy)) {
      // energy sparks drifting around the active gate's rim
      sparkAccum += dt * 70.f;
      Rng& r = sparkRng;
      while (sparkAccum >= 1.f) {
        sparkAccum -= 1.f;
        float a = r.range(0, 6.2832f), rr = r.range(0.8f, 0.92f);
        vec3 radial = gx * (cosf(a) / 90.f) + gy * (sinf(a) / 90.f);
        vec3 tang = gx * (-sinf(a) / 90.f) + gy * (cosf(a) / 90.f);
        vec3 nrm = normalize(cross(gx, gy));
        spawn(gc + radial * (rr * 90.f), tang * r.range(6.f, 16.f) + radial * r.range(-8.f, 2.f) + nrm * r.range(-4.f, 4.f), r.range(0.8f, 1.6f), r.range(2.5f, 5.f), -1.5f,
              vec3(0.3f, 1.f, 0.6f) * r.range(2.f, 4.f), 1.f, SPR_SPARK, 0.6f, 0.f);
      }
    }
    if (length(plane.pos - wp) < 110.f) {
      // gate burst: shockwave + a shower of sparks flung outward
      if (ringGeom(wpIndex, gc, gx, gy)) {
        bursts.push_back({gc, gx, gy, vec3(0.3f, 1.f, 0.6f), 0.f});
        Rng& r = sparkRng;
        for (int k = 0; k < 160; k++) {
          float a = r.range(0, 6.2832f);
          vec3 radial = gx * (cosf(a) / 90.f) + gy * (sinf(a) / 90.f);
          vec3 nrm = normalize(cross(gx, gy));
          spawn(gc + radial * 78.f, radial * r.range(30.f, 90.f) + nrm * r.range(-25.f, 25.f) + plane.vel * 0.3f, r.range(0.8f, 1.8f), r.range(3.f, 6.f), -1.5f,
                (k % 3 ? vec3(0.4f, 1.f, 0.7f) : vec3(1.f, 1.f, 0.8f)) * r.range(2.f, 5.f), 1.f, SPR_SPARK, 1.4f, -3.f);
        }
      }
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
  if (plane.onGround && takeoffAnnounced && gs < 2.5f && !researchFlight) {
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
    camMode = (camMode + 1) % 4; camYaw = 0; camPitch = 0.12f; lookYaw = 0; lookPitch = -0.13f;
    static const char* names[] = {"Chase camera", "Cockpit view", "Orbit camera", "Flyby camera"};
    toast(names[camMode]);
    if (camMode == 3) camPos = plane.pos + normalize(vec3(plane.vel.x, 0, plane.vel.z) + vec3(0.01f, 0, 0)) * 350.f + plane.right() * 40.f + vec3(0, 12, 0);
  }
  if (in.wheel != 0 && showMap) gpsRangeTarget = clampf(gpsRangeTarget * powf(0.8f, in.wheel), 1500.f, 40000.f);
  else if (in.wheel != 0 && !showRadio) camZoom = clampf(camZoom * powf(0.88f, in.wheel), 0.35f, 4.f);
  bool drag = in.mDown[1] || (camMode == 2 && in.mDown[0]);
  // chase and orbit cameras use reversed pitch: stick up / drag up swings the camera down so the view tilts up
  float pitchDir = (camMode == 0 || camMode == 2) ? -1.f : 1.f;
  if (drag) { camYaw -= in.mdx * 0.005f * set.mouseSens; camPitch = clampf(camPitch + pitchDir * in.mdy * 0.004f * set.mouseSens, -1.3f, 1.4f); }
  if (in.pad) { float rx = fabsf(in.rx) > 0.2f ? in.rx : 0, ry = fabsf(in.ry) > 0.2f ? in.ry : 0; camYaw -= rx * 2.f * dt; camPitch = clampf(camPitch + pitchDir * ry * 1.5f * dt, -1.3f, 1.4f); }
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
    else { float rest = plane.spec->special ? -0.24f : -0.13f;   // XR-9: rest the view so the instrument console is in sight
      lookYaw = approach(lookYaw, 0, 2.f, dt); lookPitch = approach(lookPitch, rest, 2.f, dt); camYaw = lookYaw; camPitch = lookPitch + 0.12f; }
    camPos = plane.pos + plane.q.rotate(kModels[plane.spec - kAircraft].eye);
  } else if (camMode == 2) {
    float dist = (size * 1.4f + 8.f) * camZoom;
    quat orbit = quat::axisAngle(vec3(0, 1, 0), camYaw) * quat::axisAngle(vec3(1, 0, 0), -camPitch);
    camPos = plane.pos + orbit.rotate(vec3(0, 0, dist));
  } else {
    if (!botControl && length(camPos - plane.pos) > 700.f) camPos = plane.pos + normalize(vec3(plane.vel.x, 0, plane.vel.z) + vec3(0.01f, 0, 0)) * 400.f + plane.right() * 45.f + vec3(0, 8, 0);
  }
  float gh = std::max(g_world.height(camPos.x, camPos.z, 6), 0.f) + 1.5f;
  if (camPos.y < gh) camPos.y = gh;
}

// ------------------------------------------------------------------ gamepad menus
// Outside active flight the left stick drives an on-screen cursor: A clicks, B backs out (same as Esc),
// the right stick scrolls lists. Any real mouse movement hands control back to the mouse.
void Game::gamepadMenus(float dt) {
  if (fabsf(in.mdx) + fabsf(in.mdy) > 0.5f) padCursorT = -100.f;
  bool menus = screen != SCR_FLIGHT || paused || crashed;
  if (!in.pad || !menus) { if (padHoldA && !(in.buttons & PAD_A)) { in.mDown[0] = false; padHoldA = false; } return; }
  auto dz = [](float v) { return fabsf(v) < 0.15f ? 0.f : (v - (v > 0 ? 0.15f : -0.15f)) / 0.85f; };
  float sx = dz(in.lx), sy = dz(in.ly);
  if (in.buttons & (PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN)) {  // D-pad nudges the cursor too
    sx += ((in.buttons & PAD_RIGHT) ? 0.6f : 0.f) - ((in.buttons & PAD_LEFT) ? 0.6f : 0.f);
    sy += ((in.buttons & PAD_UP) ? 0.6f : 0.f) - ((in.buttons & PAD_DOWN) ? 0.6f : 0.f);
  }
  bool active = fabsf(sx) + fabsf(sy) > 0 || (in.buttonsPressed & (PAD_A | PAD_B));
  if (active) {
    if (padCursorT < realTime - 50.f) { in.mx = g_ren.W * 0.5f; in.my = g_ren.H * 0.5f; }   // first use: start centred
    padCursorT = realTime;
  }
  float speed = 1100.f * S() * dt;
  in.mx = clampf(in.mx + sx * fabsf(sx) * speed * 1.4f + sx * speed * 0.3f, 0.f, (float)g_ren.W - 1);
  in.my = clampf(in.my - sy * fabsf(sy) * speed * 1.4f - sy * speed * 0.3f, 0.f, (float)g_ren.H - 1);
  if (in.buttonsPressed & PAD_A) { in.mPressed[0] = true; in.mDown[0] = true; padHoldA = true; }
  if (padHoldA && !(in.buttons & PAD_A)) { in.mDown[0] = false; in.mReleased[0] = true; padHoldA = false; }
  if (in.buttonsPressed & PAD_B) {
    in.pressed[K_ESC] = true;
    if (screen == SCR_DEBRIEF) in.pressed[K_ENTER] = true;   // B on the results screen continues to the hub
  }
  float ry = dz(in.ry);
  if (ry != 0) { static float acc = 0; acc += ry * dt * 8.f; while (fabsf(acc) >= 1.f) { in.wheel += acc > 0 ? 1.f : -1.f; acc -= acc > 0 ? 1.f : -1.f; } }
  // research menu: D-pad also steps through launch sites, Start launches
  if (screen == SCR_RESEARCH) {
    int n = (int)g_world.airports.size();
    if (in.buttonsPressed & PAD_START) in.pressed[K_ENTER] = true;
    if (in.buttonsPressed & PAD_RB) resAirport = (resAirport + 1) % n;
    if (in.buttonsPressed & PAD_LB) resAirport = (resAirport + n - 1) % n;
  }
}

void Game::drawPadCursor() {
  if (!in.pad || realTime - padCursorT > 6.f) return;
  if (screen == SCR_FLIGHT && !paused && !crashed) return;
  float s = S(), x = in.mx, y = in.my;
  float pulse = 0.5f + 0.5f * sinf(realTime * 5.f);
  bool down = in.mDown[0];
  g_ren.glow(x - 9 * s, y - 9 * s, 18 * s, 18 * s, vec3(0.32f, 0.86f, 1.f), 0.35f + 0.2f * pulse, 9 * s, 10 * s);
  g_ren.rectOutline(x - 11 * s, y - 11 * s, 22 * s, 22 * s, vec3(0.32f, 0.86f, 1.f), 0.9f, 11 * s, 2 * s);
  g_ren.rect(x - (down ? 5.f : 3.f) * s, y - (down ? 5.f : 3.f) * s, (down ? 10.f : 6.f) * s, (down ? 10.f : 6.f) * s, vec3(1, 1, 1), 1, 5 * s);
  for (int i = 0; i < 4; i++) {
    float a = i * 1.5708f + realTime * 0.8f;
    g_ren.line(x + cosf(a) * 14 * s, y + sinf(a) * 14 * s, x + cosf(a) * 19 * s, y + sinf(a) * 19 * s, 2 * s, vec3(0.32f, 0.86f, 1.f), 0.8f);
  }
}

// ------------------------------------------------------------------ XR-9 research flights
void Game::launchResearch() {
  Contract c;
  bool wr = resCraft == kWraith;
  c.id = wr ? "XR11" : "XR9"; c.title = wr ? "XR-11 Research Flight" : "XR-9 Research Flight"; c.type = CT_FERRY;
  c.from = c.to = resAirport; c.payout = 0;
  c.wx = Weather(); c.wx.timeOfDay = resTime; c.wx.windSpeed = 3; c.wx.turbulence = 0.05f;
  if (resWx == 0) { c.wx.cloudCover = 0.15f; c.wx.visibility = 60000; }
  else if (resWx == 1) { c.wx.cloudCover = 0.6f; c.wx.cloudBase = 1300; }
  else { c.wx.cloudCover = 0.95f; c.wx.cloudBase = 800; c.wx.precip = 1; c.wx.storm = true; c.wx.windSpeed = 9; c.wx.gust = 5; c.wx.turbulence = 0.5f; c.wx.visibility = 9000; }
  startFlight(c, resCraft, Career::SRC_OWNED);
  researchFlight = true;
  toasts.clear();
  toast(wr ? "XR-11 WRAITH // RESEARCH FLIGHT" : "XR-9 SPECTER // RESEARCH FLIGHT", wr ? vec3(0.75f, 0.45f, 1.f) : vec3(0.4f, 0.9f, 1));
  if (wr) toast("X cloak   Y weapons hot   LMB / Enter or RB fire   Backspace / MMB or LB bomb", vec3(0.85f, 0.7f, 1.f));
  if (resAirborne) {
    const Airport& a = g_world.airports[resAirport];
    vec3 p = plane.pos + a.dir() * 1500.f; p.y = std::max(a.elev, g_world.height(p.x, p.z)) + 900.f;
    plane.reset(&kAircraft[resCraft], p, plane.heading(), kAircraft[resCraft].maxFuel, 85, true, 200.f);
    plane.ctl.throttle = 0.7f; takeoffAnnounced = true;
    camQ = plane.q; camPos = plane.pos + plane.q.rotate(vec3(0, 4, 26));
  } else toast(wr ? "F/V tilts the four thruster pods: full down for vertical takeoff" : "F/V swivels the nozzles: full down for vertical takeoff", vec3(0.7f, 0.9f, 1));
  prevMach = 0;
}

void Game::jetEffects(float dt) {
  const float thr = plane.ctl.throttle;
  const float sp = plane.engineRunning ? plane.engineSpool : 0.f;
  float ab = plane.engineRunning ? smoothstepf(0.85f, 1.f, plane.engineSpool) : 0.f;
  bool wr = plane.spec->special == 2;
  vec3 exP[4], exD[4]; float exS[4];
  int nEx = jetExhausts(plane, exP, exD, exS);
  auto frand = [] { return (rand() % 1000) * 0.001f; };
  // The plume itself is ray-marched in the shader; particles add the hot debris it sheds: blue plasma sparks when
  // dry, a storm of amber embers in reheat (violet on the XR-11). Spread over the frame's flight path so they stream.
  for (int s = 0; s < nEx; s++) {
    vec3 exDir = plane.q.rotate(exD[s]);
    vec3 r = normalize(cross(exDir, plane.up()) + plane.right() * 1e-3f), u = cross(r, exDir);
    vec3 ex = plane.pos + plane.q.rotate(exP[s]);
    float rate = sp > 0.3f ? (40.f * sp + 260.f * ab) * (wr ? 0.6f * exS[s] : 1.f) : 0.f;    // sparks per second per nozzle
    int n = (int)(rate * dt + frand());
    for (int i = 0; i < n; i++) {
      float k = frand(), hot = frand();
      vec3 jitter = r * ((frand() - 0.5f) * 0.55f) + u * ((frand() - 0.5f) * 0.35f);
      vec3 v = plane.vel + exDir * (90.f + 160.f * ab) * (0.6f + 0.6f * frand()) + jitter * (25.f + 40.f * ab);
      vec3 col = ab > 0.05f ? lerp(wr ? vec3(0.8f, 0.3f, 1.f) : vec3(1.f, 0.42f, 0.1f), vec3(1.f, 0.9f, 0.7f), hot * hot) * (2.f + 3.f * ab)
                            : lerp(wr ? vec3(0.45f, 0.35f, 1.f) : vec3(0.25f, 0.5f, 1.f), vec3(0.8f, 0.92f, 1.f), hot * hot) * 2.2f;
      spawn(ex + jitter - plane.vel * (dt * k) + exDir * (k * 1.5f), v, 0.12f + 0.25f * frand() + 0.2f * ab, 0.07f + 0.08f * hot + 0.05f * ab, -0.15f,
            col, 1.f, SPR_SPARK, 2.5f, 0.f);
    }
    // reheat: a faint heat haze trailing the plume at low level
    if (ab > 0.05f) {
      if (plane.agl() < 800.f && frand() < 30.f * dt)
        spawn(ex + exDir * (8.f + 6.f * ab), plane.vel * 0.2f + exDir * 20.f, 1.2f, 1.6f, 3.5f, vec3(0.32f, 0.3f, 0.3f), 0.06f * ab, SPR_SMOKE, 1.5f, 0.2f);
    }
  }
  // reheat light-off: a shock ring and a burst of sparks out of both nozzles
  if (ab > 0.08f && prevAB <= 0.08f) {
    for (int s = 0; s < nEx; s++) {
      vec3 exDir = plane.q.rotate(exD[s]);
      vec3 r = normalize(cross(exDir, plane.up()) + plane.right() * 1e-3f), u = cross(r, exDir);
      vec3 ex = plane.pos + plane.q.rotate(exP[s]) + exDir * 0.2f;
      spawn(ex, plane.vel + exDir * 30.f, 0.35f, 0.6f, 9.f, vec3(1.f, 0.7f, 0.4f) * 1.5f, 1.f, SPR_SHOCK, 0.f, 0.f);
      for (int i = 0; i < 30; i++) {
        vec3 j = r * (frand() - 0.5f) + u * (frand() - 0.5f);
        spawn(ex, plane.vel + exDir * (60.f + 120.f * frand()) + j * 70.f, 0.25f + 0.3f * frand(), 0.12f, -0.2f, vec3(1.f, 0.75f, 0.4f) * 4.f, 1.f, SPR_SPARK, 2.f, 0.f);
      }
    }
    g_audio.trigger(SFX_BOOM, 0.25f);
  }
  prevAB = ab;
  // hover downwash: dust or spray blown out in a ring under the jet
  float agl = plane.agl();
  if (plane.nozzle > 0.5f && thr > 0.25f && agl < 35.f) {
    float gy = std::max(g_world.height(plane.pos.x, plane.pos.z), 0.f);
    bool water = g_world.height(plane.pos.x, plane.pos.z) < 0.3f;
    int n = (int)((1.f - agl / 35.f) * 6.f);
    for (int i = 0; i < n; i++) {
      float ang = (rand() % 628) * 0.01f;
      vec3 d(cosf(ang), 0, sinf(ang));
      spawn(vec3(plane.pos.x, gy + 0.5f, plane.pos.z) + d * 3.f, d * (12.f + (rand() % 100) * 0.1f) + vec3(0, 1.5f, 0), 1.6f, 1.2f, 3.f,
            water ? vec3(0.9f, 0.95f, 1.f) : vec3(0.5f, 0.46f, 0.38f), water ? 0.5f : 0.35f, SPR_SMOKE, 1.2f, 0.3f);
    }
  }
  // transonic vapour cone and the sonic boom when passing Mach 1
  float M = plane.mach;
  if (M > 0.93f && M < 1.05f && !plane.onGround) {
    vec3 c = plane.pos + plane.q.rotate(vec3(0, 0.1f, 0.5f));
    vec3 r = plane.right(), u = plane.up();
    for (int i = 0; i < 10; i++) {
      float ang = (rand() % 628) * 0.01f, rad = 2.2f + (rand() % 100) * 0.03f;
      spawn(c + (r * cosf(ang) + u * sinf(ang) * 0.6f) * rad - plane.vel * (0.97f * dt), plane.vel * 0.97f, 0.25f, 0.8f, 2.f, vec3(1.f), 0.35f, SPR_SMOKE, 0.f, 0.f);
    }
  }
  if (prevMach < 1.f && M >= 1.f && !plane.onGround) {
    g_audio.trigger(SFX_BOOM, 1.f);
    toast("MACH 1 - SONIC BOOM", vec3(0.4f, 0.9f, 1));
    vec3 f = normalize(plane.vel), r = normalize(cross(f, vec3(0, 1, 0)) + vec3(1e-4f, 0, 0)), u = cross(r, f);
    bursts.push_back({plane.pos, r * 25.f, u * 25.f, vec3(0.7f, 0.85f, 1.f), 0.f});
  }
  prevMach = M;
}

// ------------------------------------------------------------------ UFO encounter
void Game::startUfo() {
  ufo = Ufo(); ufo.on = true; ufo.side = (rand() & 1) ? 1.f : -1.f;
  g_audio.trigger(SFX_UFO_ARRIVE, 0.9f);
  toast("UNIDENTIFIED CONTACT", vec3(0.4f, 1.f, 0.6f));
}

void Game::updateUfo(float dt) {
  if (!ufo.on) {
    // summon: hold J and K together for a second while flying
    bool flying = screen == SCR_FLIGHT && !paused && !crashed && !plane.onGround;
    if (flying && in.down['J'] && in.down['K']) {
      ufoSummon += dt;
      if (ufoSummon >= 1.f) { ufoSummon = 0; startUfo(); return; }
    } else ufoSummon = 0;
    // random encounters: only while properly airborne
    bool ok = screen == SCR_FLIGHT && !crashed && !plane.onGround && plane.agl() > 150.f && length(plane.vel) > 25.f;
    if (ok) ufo.next -= dt;
    if (ufo.next <= 0 && ok) startUfo();
    return;
  }
  float t = (ufo.t += dt);
  // abort (zoom off) if the player crashes or lands
  if ((crashed || plane.onGround) && t < 23.f) { ufo.t = t = 23.f; }
  // frame that rides with the player: horizontal flight direction, its right, world up
  vec3 f = plane.vel; f.y = 0;
  f = length(f) > 1.f ? normalize(f) : normalize(vec3(plane.forward().x, 0, plane.forward().z) + vec3(1e-4f, 0, 0));
  vec3 r(-f.z, 0, f.x), u(0, 1, 0);
  float span = plane.spec->span;
  vec3 hold = r * (ufo.side * (span * 0.5f + 15.f)) + u * -0.6f + f * 2.f;         // station alongside, cabin at eye level
  vec3 from = -f * 520.f + u * 170.f + r * (ufo.side * 140.f);                      // swoops in from behind and above
  float k = smoothstepf(0.f, 6.5f, t);
  vec3 off = from + (hold - from) * k;
  off.y += sinf(t * 1.7f) * 0.6f + sinf(t * 0.9f) * 0.4f;                           // floating bob
  if (t > 23.f) { float z = t - 23.f; off += f * (z * z * z * 120.f + z * 30.f) + u * (z * z * 45.f); }   // zoom away
  ufo.pos = plane.pos + off;
  // orientation: hatch side (+x) towards the player, tilted into the swoop
  vec3 toP = plane.pos - ufo.pos; toP.y = 0; toP = length(toP) > 1.f ? normalize(toP) : r * -ufo.side;
  float tilt = (1.f - k) * 0.35f * ufo.side + (t > 23.f ? -0.25f * ufo.side : 0.f) + 0.04f * sinf(t * 1.3f);
  vec3 x = toP, z = normalize(cross(x, u));
  vec3 xr = x * cosf(tilt) + u * sinf(tilt), yr = u * cosf(tilt) - x * sinf(tilt);
  ufo.right = xr; ufo.up = yr; ufo.fwd = z;
  // the show: hatch opens 8-10 s, dance 10-16, laugh 16-18.5, wave 18.5-21, hatch closes 21-23, zoom 23-26
  ufo.hatch = smoothstepf(8.f, 10.f, t) * (1.f - smoothstepf(21.f, 23.f, t));
  ufo.laugh = smoothstepf(15.8f, 16.3f, t) * (1.f - smoothstepf(18.3f, 18.8f, t));
  ufo.wave = smoothstepf(18.4f, 18.9f, t) * (1.f - smoothstepf(20.8f, 21.3f, t));
  if (t > 16.f && !ufo.sfxLaugh) { ufo.sfxLaugh = true; g_audio.trigger(SFX_UFO_LAUGH, 1.f); }
  if (t > 23.f && !ufo.sfxZoom) {
    ufo.sfxZoom = true; g_audio.trigger(SFX_UFO_ZOOM, 1.f);
    vec3 c = ufo.pos;
    bursts.push_back({c, ufo.right * 14.f, ufo.fwd * 14.f, vec3(0.4f, 1.f, 0.7f), 0.f});
    for (int i = 0; i < 40; i++) spawn(c, plane.vel + vec3((rand() % 200 - 100) * 0.3f, (rand() % 200 - 100) * 0.3f, (rand() % 200 - 100) * 0.3f), 0.6f, 0.25f, -0.3f, vec3(0.5f, 1.f, 0.8f) * 3.f, 1.f, SPR_SPARK, 1.f, 0.f);
  }
  if (t > 23.f && t < 26.f) spawn(ufo.pos, plane.vel * 0.5f, 0.5f, 3.f, -4.f, vec3(0.4f, 1.f, 0.8f) * 1.5f, 1.f, SPR_GLOW, 0.f, 0.f);   // light streak
  if (t > 26.5f) { ufo.on = false; ufo.next = 480.f + (rand() % 1000) * 0.42f; }   // next one in 8-15 minutes
}

// ------------------------------------------------------------------ crash wreckage
float Game::wreckGround(float x, float z) const {
  float g = g_world.height(x, z, 6);
  if (craterR > 0 && g > 0.3f) {
    float d = sqrtf((x - craterX) * (x - craterX) + (z - craterZ) * (z - craterZ)) / craterR;
    if (d < 1.8f) { g = g_world.groundHeight(x, z, 6) - craterD * std::max(1.f - d * d, 0.f) + 0.22f * craterD * expf(-(d - 1.f) * (d - 1.f) * 14.f); }
  }
  return g;
}

// Splits the airframe into nose, centre section, both wings and tail (each the ray-traced model clipped to a
// body-space box), throws them apart with the impact energy, scatters skin fragments and digs a crater.
void Game::breakUp(vec3 impactVel, bool water, bool air) {
  const ModelDef& m = kModels[plane.spec - kAircraft];
  Rng r(1234 + (uint32_t)(flightClock * 100));
  float speed = length(impactVel);
  float xs = std::max(m.wing[0], m.ht[0]) + 0.6f;
  float y0 = -(plane.gearHeight() + 0.7f), y1 = std::max(m.vt[4] + m.vt[0], m.ht[4]) + 0.6f;
  float z0 = std::min(m.st[0][0], m.engine >= 2 && m.engine <= 3 ? m.nacZ0 : 0.f) - 0.9f;
  float z1 = std::max(std::max(m.st[7][0], m.vt[5] + m.vt[1]), m.ht[5] + m.ht[1]) + 0.6f;
  float zA = m.wing[5] - 0.25f, zB = m.wing[5] + std::max(m.wing[1], m.wing[3] + m.wing[2]) + 0.35f;
  float xr = modelHalfWidth(m, m.wing[5] + m.wing[1] * 0.5f) * 1.08f + 0.05f;
  float wy0 = m.wing[4] - 0.45f, wy1 = m.wing[4] + 0.5f + m.winglet * 1.2f;
  if (m.engine == 2 || m.engine == 3) { wy0 = std::min(wy0, m.nacY - m.nacR - 0.3f); wy1 = std::max(wy1, m.nacY + m.nacR + 0.2f); }
  if (m.gear == 3) wy0 = y0;
  if (m.strut) wy0 = std::min(wy0, m.st[3][3] - m.st[3][2]);
  auto box = [](vec3 lo, vec3 hi, vec3& C, vec3& H) { C = (lo + hi) * 0.5f; H = (hi - lo) * 0.5f; };
  vec3 lo[5] = {vec3(-xs, y0, z0), vec3(-xr, y0, zA), vec3(-xs, wy0, zA), vec3(xr, wy0, zA), vec3(-xs, y0, zB)};
  vec3 hi[5] = {vec3(xs, y1, zA), vec3(xr, y1, zB), vec3(-xr, wy1, zB), vec3(xs, wy1, zB), vec3(xs, y1, z1)};
  if (plane.spec->special) {   // XR-9: its own airframe (mapJet) - nose, centre, tail and both outer wings, no overlaps
    const float y0j = -1.7f, y1j = 2.9f;
    vec3 jl[5] = {vec3(-2.2f, y0j, -9.6f), vec3(-2.2f, y0j, -3.f), vec3(-5.9f, -1.1f, -3.f), vec3(2.2f, -1.1f, -3.f), vec3(-2.2f, y0j, 3.6f)};
    vec3 jh[5] = {vec3(2.2f, y1j, -3.f), vec3(2.2f, y1j, 3.6f), vec3(-2.2f, 0.8f, 6.1f), vec3(5.9f, 0.8f, 6.1f), vec3(2.2f, y1j, 9.6f)};
    for (int i = 0; i < 5; i++) { lo[i] = jl[i]; hi[i] = jh[i]; }
  }
  wreck.clear();
  vec3 centre = plane.pos;
  float energy = clampf(speed / 50.f, 0.4f, 2.5f);
  airBreak = air;
  if (air) {
    // In-flight break-up: every piece keeps the airframe's momentum and rotation, is flung apart by the failure
    // and tumbles violently; nothing touches the ground (no crater) until the pieces fall out of the sky.
    vec3 vRot = plane.q.rotate(plane.w);
    for (int i = 0; i < 5; i++) {
      WreckPiece w;
      box(lo[i], hi[i], w.C, w.H);
      w.q = plane.q;
      w.c = plane.pos + plane.q.rotate(w.C);
      vec3 arm = w.c - centre, out = length(arm) > 0.1f ? normalize(arm) : vec3(0, 1, 0);
      w.v = plane.vel * r.range(0.9f, 1.f) + cross(vRot, arm) + out * r.range(8.f, 20.f) + vec3(r.range(-4, 4), r.range(-2, 6), r.range(-4, 4));
      vec3 ax = normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1)));
      w.w = vRot + ax * r.range(4.f, 9.f) * (i == 1 ? 0.5f : 1.f);   // light wings and tail spin hardest
      w.rest = false; w.fire = r.range(0.7f, 1.f); w.landed = false;
      wreck.push_back(w);
    }
    debris.clear();
    for (int i = 0; i < 16; i++) {
      Debris d;
      d.p = centre + vec3(r.range(-3, 3), r.range(-1.5f, 1.5f), r.range(-4, 4));
      d.v = plane.vel * r.range(0.6f, 0.95f) + normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))) * r.range(10.f, 35.f);
      d.w = normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))) * r.range(6.f, 18.f);
      d.q = quat::axisAngle(normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))), r.range(0, 6.f));
      d.size = r.range(0.25f, 0.8f); d.charred = (i % 3) == 0; d.rest = false;
      debris.push_back(d);
    }
    craterR = 0;
    crashEndT = 1e9f;   // set once everything is down
    // the failure: a fireball and a spray of sparks and skin that the slipstream tears away
    for (int i = 0; i < 70; i++) {
      vec3 v(r.range(-14, 14), r.range(-10, 14), r.range(-14, 14));
      spawn(centre, plane.vel * 0.55f + v, r.range(0.6f, 1.3f), r.range(2.f, 4.5f), 4.f, vec3(1.f, 0.55f, 0.18f), 1.f, SPR_FIRE, 2.5f, 1.f);
    }
    for (int i = 0; i < 90; i++) spawn(centre, plane.vel * 0.7f + vec3(r.range(-40, 40), r.range(-30, 40), r.range(-40, 40)), r.range(0.8f, 2.2f), r.range(0.5f, 1.4f), -0.3f, vec3(1.f, 0.7f, 0.3f) * r.range(2.f, 5.f), 1.f, SPR_SPARK, 1.2f, -9.f);
    for (int i = 0; i < 30; i++) spawn(centre, plane.vel * 0.4f + vec3(r.range(-8, 8), r.range(-6, 8), r.range(-8, 8)), r.range(3.f, 6.f), r.range(3.f, 6.f), 2.5f, vec3(0.12f, 0.11f, 0.1f), 0.7f, SPR_SMOKE, 1.5f, 0.5f);
    {
      vec3 f = length(plane.vel) > 1.f ? normalize(plane.vel) : vec3(0, 0, -1), rr = normalize(cross(f, vec3(0, 1, 0)) + vec3(1e-4f, 0, 0)), u = cross(rr, f);
      bursts.push_back({centre, rr * 30.f, u * 30.f, vec3(1.f, 0.6f, 0.3f), 0.f});
    }
    return;
  }
  for (int i = 0; i < 5; i++) {
    WreckPiece w;
    box(lo[i], hi[i], w.C, w.H);
    w.q = plane.q;
    w.c = plane.pos + plane.q.rotate(w.C);
    vec3 out = w.c - centre; out.y = 0; out = length(out) > 0.1f ? normalize(out) : normalize(vec3(r.range(-1, 1), 0, r.range(-1, 1)));
    w.v = impactVel * r.range(0.25f, 0.45f) + out * r.range(4.f, 9.f) * energy + vec3(0, r.range(5.f, 11.f) * energy, 0);
    if (water) w.v = w.v * 0.4f;
    vec3 ax = normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1)));
    w.w = ax * r.range(1.f, 4.f) * energy;
    w.rest = false; w.fire = r.range(0.6f, 1.f);
    wreck.push_back(w);
  }
  // skin fragments
  debris.clear();
  for (int i = 0; i < 16; i++) {
    Debris d;
    d.p = centre + vec3(r.range(-2, 2), r.range(0.5f, 2.f), r.range(-2, 2));
    float a = r.range(0, 6.2832f);
    d.v = vec3(cosf(a), 0, sinf(a)) * r.range(6.f, 22.f) * energy + vec3(0, r.range(6.f, 18.f) * energy, 0) + impactVel * r.range(0.2f, 0.5f);
    if (water) d.v = d.v * 0.5f;
    d.w = normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))) * r.range(3.f, 12.f);
    d.q = quat::axisAngle(normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))), r.range(0, 6.f));
    d.size = r.range(0.25f, 0.8f); d.charred = (i % 3) == 0; d.rest = false;
    debris.push_back(d);
  }
  // crater (on land only)
  if (!water) {
    craterX = centre.x; craterZ = centre.z;
    craterR = clampf(3.f + speed * 0.07f + std::max(plane.spec->fusLen, plane.spec->span) * 0.12f, 4.f, 13.f);
    craterD = craterR * 0.28f;
  } else craterR = 0;
  // fireball, sparks, dirt and smoke
  for (int i = 0; i < 90; i++) {
    vec3 v(r.range(-12, 12), r.range(0, 16), r.range(-12, 12));
    if (water) spawn(centre, v * 0.9f + vec3(0, 6, 0), 2.5f, 1.5f, 2.f, vec3(0.9f, 0.95f, 1.f), 0.8f, SPR_SMOKE, 1.f, -6.f);
    else spawn(centre + vec3(0, 1, 0), v, r.range(0.8f, 1.6f), r.range(2.f, 4.f), 4.f, vec3(1.f, 0.6f, 0.2f), 1.f, SPR_FIRE, 1.2f, 2.f);
  }
  if (!water) {
    for (int i = 0; i < 80; i++) spawn(centre, vec3(r.range(-25, 25), r.range(5, 30), r.range(-25, 25)), r.range(1.f, 2.5f), r.range(0.8f, 2.f), -0.3f, vec3(1.f, 0.7f, 0.3f) * r.range(2.f, 5.f), 1.f, SPR_SPARK, 0.4f, -9.f);
    for (int i = 0; i < 40; i++) spawn(centre, vec3(r.range(-10, 10), r.range(4, 14), r.range(-10, 10)), r.range(1.5f, 3.f), r.range(1.5f, 3.f), 2.f, vec3(0.32f, 0.25f, 0.18f), 0.9f, SPR_SMOKE, 1.5f, -5.f);
  }
}

void Game::updateWreck(float dt) {
  const float G = 9.81f;
  for (WreckPiece& w : wreck) {
    // fire and smoke from the burning pieces (stronger right after the impact)
    float heat = w.fire * clampf(1.2f - crashTimer * 0.06f, 0.3f, 1.f);
    bool wet = g_world.height(w.c.x, w.c.z) < 0.5f;
    if (!wet && rand() % 100 < (int)(heat * 40)) {
      vec3 jp = w.c + w.q.rotate(vec3((rand() % 100 - 50) * 0.01f * w.H.x, 0, (rand() % 100 - 50) * 0.01f * w.H.z));
      spawn(jp, vec3(0, 2.f + (rand() % 100) * 0.02f, 0), 0.7f, 0.45f + 0.5f * heat, 0.8f, vec3(1.f, 0.42f, 0.1f) * 0.55f, 1.f, SPR_FIRE, 0.5f, 1.f);
    }
    bool sunk = wet && w.c.y < -1.5f;
    if (!sunk && rand() % 100 < (int)(heat * 22)) spawn(w.c + vec3(0, 2.5f, 0), vec3((rand() % 100 - 50) * 0.02f, 3.5f, (rand() % 100 - 50) * 0.02f) + plane.windVel * 0.5f, 7.f, 1.5f, 2.5f, wet ? vec3(0.8f) : vec3(0.1f, 0.095f, 0.09f), 0.45f, SPR_SMOKE, 0.25f, 1.5f);
    if (w.rest) continue;
    if (wet && w.c.y < 0.3f) {
      if (!w.landed) {   // a piece falling into the sea: a tall splash
        w.landed = true;
        if (airBreak) { g_audio.trigger(SFX_CRASH, 0.5f); for (int i = 0; i < 40; i++) spawn(vec3(w.c.x, 0.2f, w.c.z), vec3((rand() % 200 - 100) * 0.06f, 8.f + (rand() % 100) * 0.14f, (rand() % 200 - 100) * 0.06f), 2.2f, 1.4f, 2.f, vec3(0.9f, 0.95f, 1.f), 0.75f, SPR_SMOKE, 1.f, -7.f); }
      }
      // in the sea: heavy drag, a short float on trapped air, then the piece sinks out of sight to the seabed
      float floatT = 1.f + 1.5f * w.fire;
      float vy = crashTimer < floatT ? (-0.35f - w.c.y) * 2.f : -2.2f - 1.2f * w.fire;
      w.v.x *= expf(-1.5f * dt); w.v.z *= expf(-1.5f * dt);
      w.v.y += (vy - w.v.y) * std::min(1.f, 3.f * dt);
      w.w = w.w * expf(-1.2f * dt);
      if (crashTimer > floatT && w.c.y > -12.f && rand() % 100 < 25)   // air escaping as it goes down
        spawn(vec3(w.c.x + (rand() % 100 - 50) * 0.03f, 0.05f, w.c.z + (rand() % 100 - 50) * 0.03f), vec3(0, 0.6f, 0), 0.9f, 0.35f, 1.5f, vec3(0.9f, 0.95f, 1.f), 0.45f, SPR_SMOKE, 1.f, 0.f);
    } else if (airBreak && !w.landed) {
      // falling and tumbling: quadratic drag brings a chunk from supersonic down to ~90 m/s terminal; it keeps spinning
      w.v.y -= G * dt;
      w.v = w.v - w.v * (0.0012f * length(w.v) * dt);
      w.w = w.w * expf(-0.05f * dt);
      // burning smoke trail: spread along this frame's path so it streams continuously even at high speed
      int n = (int)clampf(length(w.v) * dt / 2.f, 1.f, 4.f);
      for (int k = 0; k < n; k++) {
        vec3 tp = w.c - w.v * (dt * (float)k / n);
        spawn(tp, w.v * 0.05f, 2.6f + (rand() % 100) * 0.01f, 1.4f + w.fire, 3.f, vec3(0.07f, 0.065f, 0.06f), 0.5f, SPR_SMOKE, 1.f, 0.6f);
        if (k % 2 == 0) spawn(tp, w.v * 0.1f, 0.1f + (rand() % 100) * 0.001f, 0.6f + w.fire * 0.5f, -2.f, vec3(1.f, 0.42f, 0.1f) * 0.6f, 1.f, SPR_FIRE, 2.f, 0.f);
      }
    } else w.v.y -= G * dt;
    w.v = w.v * expf(-0.08f * dt);
    w.c += w.v * dt;
    float wl = length(w.w);
    if (wl > 1e-4f) { w.q = quat::axisAngle(w.w, wl * dt) * w.q; w.q.normalize(); }
    // ground contact against the corners of the piece's box
    float pen = 0; vec3 hitArm;
    for (int k = 0; k < 8; k++) {
      vec3 cl((k & 1) ? w.H.x : -w.H.x, (k & 2) ? w.H.y : -w.H.y, (k & 4) ? w.H.z : -w.H.z);
      vec3 cw = w.c + w.q.rotate(cl * 0.85f);
      float g = wreckGround(cw.x, cw.z);
      if (g - cw.y > pen) { pen = g - cw.y; hitArm = cw - w.c; }
    }
    if (pen > 0 && !w.landed) {
      // first contact of a piece that fell out of the sky: it hits hard
      w.landed = true;
      float sp = length(w.v);
      if (airBreak && sp > 8.f) {
        g_audio.trigger(SFX_CRASH, clampf(sp / 90.f, 0.3f, 1.f));
        for (int i = 0; i < 30; i++) {
          vec3 v((rand() % 200 - 100) * 0.12f, (rand() % 100) * 0.16f, (rand() % 200 - 100) * 0.12f);
          spawn(w.c, v, 0.8f + (rand() % 100) * 0.008f, 2.f, 3.f, vec3(1.f, 0.55f, 0.2f), 1.f, SPR_FIRE, 1.2f, 2.f);
          spawn(w.c, v * 1.3f, 2.f, 1.5f, 2.f, vec3(0.32f, 0.25f, 0.18f), 0.8f, SPR_SMOKE, 1.5f, -5.f);
        }
        if (craterR <= 0 && &w == &wreck[1]) {   // the heavy centre section digs the crater
          craterX = w.c.x; craterZ = w.c.z; craterR = clampf(3.f + sp * 0.06f, 4.f, 10.f); craterD = craterR * 0.28f;
        }
      }
    }
    if (pen > 0) {
      w.c.y += pen;
      if (w.v.y < 0) w.v.y = -w.v.y * 0.22f;
      w.v.x *= 0.72f; w.v.z *= 0.72f;
      w.w = w.w * 0.75f + cross(hitArm, vec3(w.v.x, 0, w.v.z)) * 0.02f;
      if (length(w.v) < 0.8f && length(w.w) < 0.35f) w.rest = true;
    }
  }
  // in-flight break-up: the camera rides with the centre section; results come up a while after the last piece is down
  if (airBreak && !wreck.empty()) {
    plane.pos = wreck.size() > 1 ? wreck[1].c : wreck[0].c;
    bool all = true; for (auto& w : wreck) all = all && (w.landed || w.c.y < -1.f);
    if (crashEndT > 1e8f && (all || crashTimer > 150.f)) crashEndT = crashTimer + 6.f;
  }
  for (Debris& d : debris) {
    if (d.rest) continue;
    d.v.y -= G * dt; d.v = d.v * expf(-0.3f * dt);
    if (airBreak) d.v = d.v - d.v * (0.02f * length(d.v) * dt);   // light skin panels flutter down
    d.p += d.v * dt;
    float wl = length(d.w);
    if (wl > 1e-4f) { d.q = quat::axisAngle(d.w, wl * dt) * d.q; d.q.normalize(); }
    float g = wreckGround(d.p.x, d.p.z);
    if (g < 0 && d.p.y < 0.f) {   // fragments in the sea flutter down to the seabed
      d.v = d.v * expf(-4.f * dt); d.v.y = std::max(d.v.y, -0.9f - d.size); d.w = d.w * expf(-1.f * dt);
      if (d.p.y < g + d.size * 0.15f) { d.p.y = g + d.size * 0.15f; d.rest = true; }
      continue;
    }
    if (d.p.y < g + d.size * 0.15f) {
      d.p.y = g + d.size * 0.15f;
      d.v.y = fabsf(d.v.y) * 0.25f; d.v.x *= 0.6f; d.v.z *= 0.6f; d.w = d.w * 0.6f;
      if (length(d.v) < 0.7f) {  // settle flat on the ground
        d.rest = true;
        float yaw = atan2f(d.q.rotate(vec3(1, 0, 0)).z, d.q.rotate(vec3(1, 0, 0)).x);
        d.q = quat::axisAngle(vec3(0, 1, 0), -yaw) * quat::axisAngle(vec3(1, 0, 0), (d.size - 0.5f) * 0.4f);
      }
    }
  }
}

// ------------------------------------------------------------------ particles
void Game::updateParticles(float dt) {
  for (size_t i = 0; i < bursts.size();) { bursts[i].t += dt; if (bursts[i].t > 0.9f) { bursts[i] = bursts.back(); bursts.pop_back(); } else i++; }
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
  int idx = (int)(p.spec - kAircraft);
  const ModelDef& md = kModels[idx];
  pv.on = true;
  pv.pos = p.pos;
  vec3 r = p.right(), u = p.up(), b = p.q.rotate(vec3(0, 0, 1));
  float m[9] = {r.x, r.y, r.z, u.x, u.y, u.z, b.x, b.y, b.z};
  memcpy(pv.rot, m, sizeof(m));
  packModel(s, idx, p.gearHeight(), pv.M);
  // nose/tail wheel steering: same angle the physics applies to the wheel (geometry angle in the x-z plane)
  float spd = length(p.vel);
  float steer = p.ctl.yaw * 0.45f * smoothstepf(30.f, 4.f, spd);
  if (s.taildragger) steer = -steer;
  pv.PS[0] = p.gear; pv.PS[1] = p.flaps; pv.PS[2] = steer; pv.PS[3] = inside ? 1.f : 0.f;
  pv.Ctl[0] = clampf(p.ctl.pitch + p.ctl.trim * 0.3f, -1, 1); pv.Ctl[1] = clampf(p.ctl.roll, -1, 1); pv.Ctl[2] = clampf(p.ctl.yaw, -1, 1); pv.Ctl[3] = p.ctl.throttle;
  float blur = s.engineType == ENG_JET ? 1.f : smoothstepf(250.f, 700.f, p.rpm);
  pv.Pr[0] = propAngle; pv.Pr[1] = blur; pv.Pr[2] = (float)std::max(s.blades, 2); pv.Pr[3] = 0;
  pv.I0[0] = p.ias * MS_TO_KT; pv.I0[1] = p.pos.y * M_TO_FT; pv.I0[2] = p.heading(); pv.I0[3] = p.vel.y * 196.85f;
  pv.I1[0] = p.pitchDeg(); pv.I1[1] = p.bankDeg();
  pv.I1[2] = s.engineType == ENG_PISTON ? p.rpm / std::max(s.maxRpm, 1.f) : p.n1 / 100.f; pv.I1[3] = p.fuel / std::max(s.maxFuel, 1.f);
  pv.I2[0] = -p.q.rotate(p.w).y / DEG; pv.I2[1] = p.beta / DEG; pv.I2[2] = p.flaps; pv.I2[3] = p.gear;
  pv.colBase = s.colBase; pv.colStripe = s.colStripe;
  pv.propCount = modelProps(md, pv.prop);
  pv.hud[0] = p.ias; pv.hud[1] = p.pos.y; pv.hud[2] = p.heading(); pv.hud[3] = p.mach;
  pv.hud2[0] = p.gLoad; pv.hud2[1] = p.ctl.throttle; pv.hud2[2] = p.nozzle; pv.hud2[3] = p.gear > 0.5f ? 1.f : 0.f;
  vec3 vb = length(p.vel) > 2.f ? p.q.conj().rotate(normalize(p.vel)) : vec3(0, 0, -1);
  pv.hudV[0] = vb.x; pv.hudV[1] = vb.y; pv.hudV[2] = vb.z;
  pv.hud3[0] = p.engineSpool; pv.hud3[1] = p.alpha / DEG; pv.hud3[2] = p.vel.y; pv.hud3[3] = p.agl();
  if (s.special) {
    pv.flame[0] = p.engineRunning ? p.engineSpool : 0.f; pv.flame[1] = p.engineRunning ? smoothstepf(0.85f, 1.f, p.engineSpool) : 0.f;
    pv.flame[2] = jetNozzleAngle(p); pv.flame[3] = p.mach;
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
  if ((screen == SCR_FLIGHT || screen == SCR_DEBRIEF) && plane.spec) {
    fillPlaneVisual(fp.plane, plane, propAngle, camMode == 1);
    if (!wreck.empty()) {
      WreckVisual& wv = fp.wreck;
      wv.pieces = std::min((int)wreck.size(), 5);
      for (int i = 0; i < wv.pieces; i++) {
        const WreckPiece& w = wreck[i];
        wv.pos[i] = w.c; wv.C[i] = w.C; wv.H[i] = w.H;
        vec3 ax = w.q.rotate(vec3(1, 0, 0)), ay = w.q.rotate(vec3(0, 1, 0)), az = w.q.rotate(vec3(0, 0, 1));
        float r[9] = {ax.x, ax.y, ax.z, ay.x, ay.y, ay.z, az.x, az.y, az.z};
        memcpy(wv.rot[i], r, sizeof r);
      }
    }
    fp.wreck.debris = std::min((int)debris.size(), 16);
    for (int i = 0; i < fp.wreck.debris; i++) {
      const Debris& d = debris[i];
      fp.wreck.deb[i][0] = d.p.x; fp.wreck.deb[i][1] = d.p.y; fp.wreck.deb[i][2] = d.p.z; fp.wreck.deb[i][3] = d.charred ? -d.size : d.size;
      fp.wreck.debQ[i][0] = d.q.w; fp.wreck.debQ[i][1] = d.q.x; fp.wreck.debQ[i][2] = d.q.y; fp.wreck.debQ[i][3] = d.q.z;
    }
    fp.wreck.craterN = 0;
    if (craterR > 0) { float* c = fp.wreck.crater[fp.wreck.craterN++]; c[0] = craterX; c[1] = craterZ; c[2] = craterR; c[3] = craterD; }
    vec3 fwd = camMode == 1 ? plane.q.rotate(quat::axisAngle(vec3(0, 1, 0), lookYaw).rotate(quat::axisAngle(vec3(1, 0, 0), lookPitch).rotate(vec3(0, 0, -1))))
                            : normalize(plane.pos + vec3(0, plane.spec->fusRad * 0.3f, 0) - camPos);
    vec3 upRef = camMode == 1 ? plane.up() : vec3(0, 1, 0);
    if (camMode == 0) upRef = normalize(lerp(vec3(0, 1, 0), camQ.rotate(vec3(0, 1, 0)), 0.3f));
    if (dbgCam && dbgFollow) { dbgCamPos = plane.pos + dbgFollowOff; dbgCamLook = plane.pos; }
    if (dbgCam) { camPos = dbgCamPos; fwd = normalize(dbgCamLook - dbgCamPos); upRef = vec3(0, 1, 0); }
    fp.camPos = camPos;
    fp.camBack = -fwd;
    fp.camRight = normalize(cross(fwd, upRef));
    fp.camUp = cross(fp.camRight, fwd);
    fp.fovY = (camMode == 1 ? 74.f : 55.f) * DEG;
    if (camMode == 3) fp.fovY = clampf(2.f * atanf(std::max(plane.spec->span, plane.spec->fusLen) * (botControl ? 0.42f : 1.5f) / length(plane.pos - camPos)), 4.f * DEG, 60.f * DEG);
    if (camMode == 1) fp.fovY /= std::min(camZoom, 1.4f) > 0 ? 1.f : 1.f;
    fp.landLight = landingLight && plane.engineRunning ? (0.3f + 0.7f * fp.night) : 0.f;
    fp.landLightPos = plane.pos + plane.forward() * (plane.spec->fusLen * 0.4f);
    fp.landLightDir = normalize(plane.forward() - plane.up() * 0.1f);
    if (plane.spec->special && plane.engineRunning && !crashed && camMode != 1) {
      // the plume lights its surroundings: blue plasma when dry, white-amber and much brighter in reheat
      float sp = plane.engineSpool, ab = fp.plane.flame[1];
      vec3 exP[4], exD[4], c(0, 0, 0); float exS[4]; int nEx = jetExhausts(plane, exP, exD, exS);
      for (int i = 0; i < nEx; i++) c += exP[i] + exD[i] * (1.2f + 2.5f * ab);
      fp.flameLightPos = plane.pos + plane.q.rotate(c / (float)nEx);
      float flick = 0.85f + 0.15f * sinf(realTime * 57.f) * sinf(realTime * 23.f + 1.f);
      fp.flameLight = lerp(vec3(0.3f, 0.55f, 1.f), vec3(1.f, 0.62f, 0.3f), ab) * ((25.f * sp * sp + 260.f * ab) * flick);
    }
    wraithVisual(fp);
    // craters flatten the scenery that stood in them (a plasma blast clears a far wider circle than its pit)
    g_scenery.craters.clear();
    for (int i = 0; i < fp.wreck.craterN; i++) { const float* c = fp.wreck.crater[i]; g_scenery.craters.push_back(vec3(c[0], c[1], c[3] < 0 ? c[2] * 4.f : c[2] * 1.5f)); }
    fp.rainLens = camMode == 1 && wx.precip == 1 ? 1.f : 0.f;
    fp.sealedCockpit = camMode == 1 && plane.spec->special && !crashed;
    fp.trafficN = traffic.fillVisuals(fp.camPos, fp.traffic, kMaxTrafficDrawn, nullptr);
    fp.ufoOn = ufo.on && length(ufo.pos - fp.camPos) < 20000.f;
    if (fp.ufoOn) {
      fp.ufoPos = ufo.pos;
      float m[9] = {ufo.right.x, ufo.right.y, ufo.right.z, ufo.up.x, ufo.up.y, ufo.up.z, ufo.fwd.x, ufo.fwd.y, ufo.fwd.z};
      memcpy(fp.ufoRot, m, sizeof m);
      fp.ufoAnim[0] = ufo.hatch; fp.ufoAnim[1] = ufo.t; fp.ufoAnim[2] = ufo.laugh; fp.ufoAnim[3] = ufo.wave;
    }
    if (crashed) fp.fade = clampf(1.f - (crashTimer - (crashEndT - 1.f)), 0, 1);
    fp.gLoad = gTunnel;
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
  bool res = screen == SCR_RESEARCH;
  demo.spec = &kAircraft[res ? resCraft : 1];
  demo.rpm = 2400; demo.gear = res ? 0.f : 1.f; demo.flaps = 0; demo.nozzle = 0; demo.ctl = Controls(); demo.ctl.throttle = res ? 0.6f : 0.f;
  fillPlaneVisual(fp.plane, demo, realTime * 250.f, false);
  float ca = realTime * 0.05f, cr = res ? 24.f : 16.f;
  vec3 off = vec3(cosf(ca) * cr, (res ? 5.f : 3.5f) + 2.f * sinf(ca * 0.7f), sinf(ca) * cr) + vdir * -6.f;
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
    float navI = (0.6f + 2.5f * night) * (1.f - wraith.stealth);   // a cloaked XR-11 runs dark
    const ModelDef& md = kModels[plane.spec - kAircraft];
    bool jet = s.special != 0;   // the XR-9 is an SDF of its own: its lights don't follow the generic model layout
    vec3 tip = s.special == 2 ? kWraithWingTip : jet ? kJetWingTip : modelWingTip(md);
    vec3 lt = plane.pos + plane.q.rotate(vec3(-tip.x, tip.y, tip.z)), rtp = plane.pos + plane.q.rotate(tip);
    if (camMode != 1) {
      bill(add, lt, ls, vec3(1.f, 0.1f, 0.05f) * navI, 1, SPR_GLOW, 0.3f);
      bill(add, rtp, ls, vec3(0.1f, 1.f, 0.2f) * navI, 1, SPR_GLOW, 0.3f);
      bill(add, plane.pos + plane.q.rotate(jet ? vec3(0, 0.05f, 8.3f) : modelTailTip(md)), ls, vec3(1.f) * navI, 1, SPR_GLOW, 0.3f);
      if (plane.engineRunning && wraith.stealth < 0.5f && fmodf(t, 1.0f) < 0.12f) bill(add, plane.pos + plane.q.rotate(jet ? vec3(0, 0.7f, 1.6f) : modelFinTop(md) + vec3(0, 0.06f, 0)), ls * 1.6f, vec3(1.f, 0.05f, 0.02f) * (2.f + 3.f * night), 1, SPR_GLOW, 0.3f);
      float st = fmodf(t, 1.3f);
      if (!plane.onGround && wraith.stealth < 0.5f && (st < 0.05f || (st > 0.12f && st < 0.16f))) { bill(add, lt, ls * 3.f, vec3(4.f), 1, SPR_GLOW, 0.3f); bill(add, rtp, ls * 3.f, vec3(4.f), 1, SPR_GLOW, 0.3f); }
      if (jet && plane.engineRunning) {   // exhaust bloom at the nozzle exits (blue when dry, white-amber in reheat)
        float sp = plane.engineSpool, ab = smoothstepf(0.85f, 1.f, sp);
        vec3 exP[4], exD[4]; float exS[4]; int nEx = jetExhausts(plane, exP, exD, exS);
        bool wr = plane.spec->special == 2;
        float fl = 0.85f + 0.15f * sinf(t * 71.f + 0.7f), cl = 1.f - wraith.stealth * 0.9f;
        for (int k = 0; k < nEx; k++)
          bill(add, plane.pos + plane.q.rotate(exP[k] + exD[k] * (0.2f + 0.6f * ab)), (0.8f + 0.7f * ab) * (wr ? 0.7f : 1.f),
               lerp(wr ? vec3(0.45f, 0.35f, 1.f) : vec3(0.3f, 0.55f, 1.f), wr ? vec3(1.f, 0.6f, 1.f) : vec3(1.f, 0.7f, 0.4f), ab) * ((0.25f * sp * sp + 0.5f * ab) * fl * exS[k] * cl), 1, SPR_GLOW, 0.6f);
      }
    }
    // AI traffic lights: nav lights, beacon, strobes when airborne, landing lights on the runway and on approach,
    // reheat glow on XR-9 formations
    for (const TrafficCraft& c : traffic.craft) {
      float dc = length(c.pos - fp.camPos);
      if (dc > 9000.f) continue;
      const ModelDef& tm = kModels[c.spec];
      float ls2 = std::max(0.25f, dc * 0.002f);
      vec3 tip = c.spec == kResearchJet ? kJetWingTip : modelWingTip(tm);
      vec3 lt = c.pos + c.q.rotate(vec3(-tip.x, tip.y, tip.z)), rt = c.pos + c.q.rotate(tip);
      bill(add, lt, ls2, vec3(1.f, 0.1f, 0.05f) * navI, 1, SPR_GLOW, 0.3f);
      bill(add, rt, ls2, vec3(0.1f, 1.f, 0.2f) * navI, 1, SPR_GLOW, 0.3f);
      float ph = fmodf(t + c.id * 0.37f, 1.f);
      if (ph < 0.1f) bill(add, c.pos + c.q.rotate(vec3(0, kAircraft[c.spec].fusRad * 1.05f, 0)), ls2 * 1.4f, vec3(1.f, 0.05f, 0.02f) * (2.f + 3.f * night), 1, SPR_GLOW, 0.3f);
      bool airborne = c.state >= TrafficCraft::TAKEOFF && c.state <= TrafficCraft::ROLLOUT;
      if ((airborne || c.role != TrafficCraft::AIRPORT) && fmodf(t * 0.77f + c.id * 0.13f, 1.3f) < 0.05f) { bill(add, lt, ls2 * 2.5f, vec3(4.f), 1, SPR_GLOW, 0.3f); bill(add, rt, ls2 * 2.5f, vec3(4.f), 1, SPR_GLOW, 0.3f); }
      if (c.role == TrafficCraft::AIRPORT && (c.state == TrafficCraft::TAKEOFF || c.state == TrafficCraft::FINAL || c.state == TrafficCraft::ROLLOUT || c.state == TrafficCraft::LINEUP))
        bill(add, c.pos + c.q.rotate(vec3(0, -kAircraft[c.spec].fusRad * 0.6f, -kAircraft[c.spec].fusLen * 0.4f)), ls2 * 2.f, vec3(1.f, 0.95f, 0.85f) * (1.f + 4.f * night), 1, SPR_GLOW, 0.3f);
      if (c.ab > 0.5f) for (int k = -1; k <= 1; k += 2)
        bill(add, c.pos + c.q.rotate(vec3(k * 0.82f, -0.12f, 8.9f)), 1.6f + ls2, vec3(1.f, 0.65f, 0.35f) * 1.6f, 1, SPR_GLOW, 0.6f);
    }
    if (landingLight && plane.engineRunning && camMode != 1)
      bill(add, plane.pos + plane.q.rotate(vec3(s.engLayout == 0 ? -s.span * 0.25f : 0, s.engLayout == 0 ? s.wingY * s.fusRad : -s.fusRad * 0.6f, s.engLayout == 0 ? s.wingZ - s.chord * 0.5f : -0.35f * s.fusLen)), ls * 1.8f,
           vec3(1.f, 0.95f, 0.85f) * (1.f + 4.f * night), 1, SPR_GLOW, 0.3f);
    // checkpoint gates: the active one spins, breathes and shows a moving breadcrumb trail to the next gate
    for (int i = wpIndex; i < (int)contract.wps.size() && i < wpIndex + 3; i++) {
      vec3 c, ax, ay;
      ringGeom(i, c, ax, ay);
      bool act = i == wpIndex;
      float br = act ? 1.f + 0.035f * sinf(t * 2.6f) : 0.85f;
      vec3 col = act ? vec3(0.2f, 1.f, 0.5f) * 2.2f : vec3(0.85f, 0.4f, 1.f) * 1.1f;
      quadAx(add, c, ax * br, ay * br, col, act ? 1.f : 0.55f, SPR_RING, 5.f);
      if (act) quadAx(add, c, ax * 1.25f, ay * 1.25f, col * 0.35f, 0.2f, SPR_SHOCK, 5.f);   // faint outer halo
      vec3 c2, ax2, ay2;
      if (act && ringGeom(i + 1, c2, ax2, ay2)) {
        float len = length(c2 - c);
        int n = std::min(14, std::max(3, (int)(len / 180.f)));
        for (int k = 0; k < n; k++) {
          float f = (k + fmodf(t * 0.8f, 1.f)) / n;
          float fade = sinf(f * 3.14159f);
          bill(add, c + (c2 - c) * f, std::max(9.f, length(c + (c2 - c) * f - fp.camPos) * 0.012f), vec3(0.85f, 0.4f, 1.f) * 3.f * fade, 1.f, SPR_SPARK, 5.f);
        }
      }
    }
    for (const RingBurst& b : bursts) {
      float k = b.t / 0.9f, s = 1.f + 2.2f * (1.f - (1.f - k) * (1.f - k));
      quadAx(add, b.c, b.ax * s, b.ay * s, b.col * 3.f, (1.f - k) * (1.f - k), SPR_SHOCK, 5.f);
      if (k < 0.35f) bill(add, b.c, 90.f * (0.5f + k * 3.f), b.col * 2.f * (1.f - k / 0.35f), 1.f, SPR_GLOW, 5.f);
    }
  }
  // particles
  std::vector<std::pair<float, const Particle*>> order;
  for (const Particle& p : particles) order.push_back({-length(p.p - fp.camPos), &p});
  std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first < b.first; });
  for (auto& o : order) {
    const Particle& p = *o.second;
    float fade = clampf(p.life / p.maxLife, 0, 1);
    float a = p.alpha * (p.kind == SPR_FIRE || p.instant ? fade : fade * smoothstepf(0.f, 0.15f, 1.f - fade + 0.15f));
    if (p.kind == SPR_FIRE || p.kind == SPR_SPARK) bill(add, p.p, std::max(p.size, 0.05f), p.col, p.kind == SPR_SPARK ? fade : a, p.kind, 1.f);
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
  if (in.pressed['M'] || (screen == SCR_FLIGHT && !paused && (in.buttonsPressed & PAD_LS))) { muffled = !muffled; toast(muffled ? "Engine noise muffled (headset ANR on)" : "Headset ANR off"); }
  ap.muffled = muffled;
  if (screen == SCR_FLIGHT && plane.spec && !crashed) {
    const AircraftSpec& s = *plane.spec;
    ap.inFlight = true;
    ap.engineType = s.engineType; ap.engines = s.engines; ap.cylinders = s.cylinders; ap.blades = s.blades;
    ap.research = s.special != 0; ap.nozzle = plane.nozzle; ap.mach = plane.mach;
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
  // the scene is always ray traced at the full display resolution (no dynamic resolution); the display is paced to 60 Hz
  fpsAvg = lerpf(fpsAvg, dt, 0.05f);
  if (!headless && g_ren.ok && g_ren.renderScale != 1.f) g_ren.setRenderScale(1.f);
  dt = std::min(dt, 0.05f);
  realTime += dt;
  for (auto& t : toasts) t.t += dt;
  while (!toasts.empty() && toasts.front().t > 5.f) toasts.erase(toasts.begin());
  hubMsgTime = std::max(0.f, hubMsgTime - dt);
  if (in.pressed[K_F11]) wantFullscreenToggle = true;
  if (in.pressed[0x72]) showPerf = !showPerf;   // F3
  gamepadMenus(dt);
  bool padCombo = in.pad && (in.buttons & PAD_LS) && (in.buttons & PAD_RS) && (in.buttonsPressed & (PAD_LS | PAD_RS));
  if (screen == SCR_MENU && ((in.down['U'] && in.down['I'] && (in.pressed['U'] || in.pressed['I'])) || padCombo)) {
    screen = SCR_RESEARCH; resOpened = realTime; g_audio.trigger(SFX_BEEP);
  }
  if (in.pressed['R'] && screen == SCR_FLIGHT) showRadio = !showRadio;
  radio.poll();
  if (screen == SCR_FLIGHT) {
    if (in.pressed[K_ESC] || (in.buttonsPressed & PAD_START)) {
      if (showMap) showMap = false; else if (showRadio) showRadio = false;
      else if (paused && settingsFromPause && !(in.buttonsPressed & PAD_START)) settingsFromPause = false;   // B / Esc: back to the pause menu
      else { paused = !paused; settingsFromPause = false; }
    }
    if (in.pressed['N']) { showMap = !showMap; g_audio.trigger(SFX_CLICK); }
    if (showMap && !paused) {   // GPS open: Tab / D-pad pick the autoland airport, Enter / A engages the autopilot to it
      if (in.pressed[K_TAB] || (in.buttonsPressed & PAD_RIGHT)) cycleApDest(in.down[K_SHIFT] ? -1 : 1);
      if (in.buttonsPressed & PAD_LEFT) cycleApDest(-1);
      if ((in.pressed[K_ENTER] || (in.buttonsPressed & PAD_A)) && apDest >= 0 && !plane.onGround) engageAutopilot();
    } else if (in.pressed[K_TAB]) { showMinimap = !showMinimap; toast(showMinimap ? "Minimap shown" : "Minimap hidden"); }
    if (in.pressed['H']) hudOn = !hudOn;
    if (!paused) {
      updateFlight(dt);
      if (screen == SCR_FLIGHT) { updateCamera(dt); updateParticles(dt * timeAccel); }
      if (plane.spec) {
        float rps = plane.spec->engineType == ENG_TURBOPROP ? plane.rpm / 60.f : plane.rpm / 60.f;
        propAngle = fmodf(propAngle + rps * 2 * PI * dt * (plane.rpm < 400 ? 1.f : 0.0f) + (plane.rpm >= 400 ? dt * 3.f : 0.f), 2 * PI * 100);
      }
    }
  } else if (screen == SCR_DEBRIEF) {
    if (crashed) { crashTimer += dt; updateWreck(dt); updateParticles(dt); }   // the wreck keeps settling/sinking behind the results
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
  uiDt = clampf(realTime - uiLastT, 0.f, 0.1f); uiLastT = realTime;
  switch (screen) {
    case SCR_MENU: drawMenu(); break;
    case SCR_HUB: drawHub(); break;
    case SCR_FLIGHT: drawHud(fp); drawMapOverlay(); if (paused) drawPause(); break;
    case SCR_DEBRIEF: drawDebrief(); break;
    case SCR_RESEARCH: drawResearch(); break;
  }
  drawToasts();
  drawPadCursor();
  if (showPerf) {
    float s = S();
    std::string t = fmt("%.0f fps  %.1f ms   GPU %s   res %.0f%% (%dx%d)", 1.f / std::max(fpsAvg, 1e-4f), fpsAvg * 1000.f,
                        g_ren.gpuMs > 0 ? fmt("%.1f ms", g_ren.gpuMs).c_str() : "n/a", g_ren.renderScale * 100.f,
                        (int)(g_ren.W * g_ren.renderScale), (int)(g_ren.H * g_ren.renderScale));
    t += fmt("   scenery %d drawn, %d chunks, %.1f ms CPU", g_ren.entDrawn, g_ren.entChunks, g_ren.entCpuMs);
    g_ren.rect(6 * s, 6 * s, 420 * s, 24 * s, vec3(0, 0, 0), 0.55f);
    g_ren.text(14 * s, 10 * s, 14 * s, t, fpsAvg < 1.f / 57.f ? vec3(1, 0.5f, 0.3f) : vec3(0.5f, 1, 0.6f), 1, 0, false);
  }
  g_ren.uiEnd();
}

// ------------------------------------------------------------------ development scenes for the render harness
void Game::debugScene(const std::string& name) {
  career.newGame(); career.license = LIC_ATP;
  if (name == "menu") { screen = SCR_MENU; realTime = 20; return; }
  if (name == "hub") { screen = SCR_HUB; realTime = 20; return; }
  if (name.compare(0, 4, "jcam") == 0) {  // XR-9 close-up from an orbit angle: jcam<yaw deg>_<pitch deg>
    float yawD = 0, pitD = 10, thrP = -1, nozP = 0, zoom = 0.55f, tod = -1; sscanf(name.c_str() + 4, "%f_%f_%f_%f_%f_%f", &yawD, &pitD, &thrP, &nozP, &zoom, &tod);
    if (tod >= 0) resTime = tod;
    resAirborne = true; realTime = 20; launchResearch(); wx.cloudCover = 0.3f;
    if (thrP >= 0) {   // optional throttle (and nozzle) percent: let the exhaust plumes and sparks develop
      plane.ctl.throttle = thrP / 100.f; plane.engineSpool = thrP / 100.f; plane.ctl.flaps = plane.flaps = plane.nozzle = nozP / 100.f;
      for (int i = 0; i < 30; i++) { realTime += 1 / 60.f; update(1 / 60.f); plane.engineSpool = thrP / 100.f; }
    }
    camMode = 2; camYaw = yawD * DEG; camPitch = pitD * DEG; camZoom = zoom; hudOn = false;
    for (int i = 0; i < 5; i++) updateCamera(0.1f);
    toasts.clear(); return;
  }
  if (name == "vapour") {  // XR-9 pulling g near the cloud base: wingtip vapour must trail behind the tips
    resAirborne = true; realTime = 20; launchResearch(); wx.cloudBase = 300; wx.cloudCover = 0.2f;
    plane.vel = plane.forward() * 280.f; plane.ctl.throttle = 0.9f; botControl = true; plane.ctl.pitch = 0.6f; plane.ctl.gearDown = false; plane.gear = 0;
    for (int i = 0; i < 40; i++) { realTime += 1 / 60.f; update(1 / 60.f); }
    camMode = 2; camYaw = 1.2f; camPitch = 0.25f; camZoom = 1.2f; for (int i = 0; i < 5; i++) updateCamera(0.1f); toasts.clear();
    { int ni = 0; float dmin = 1e9f; for (auto& q : particles) if (q.instant) { ni++; dmin = std::min(dmin, length(q.p - plane.pos)); } printf("vapour: %d trail particles, nearest %.1f m from the CG, g=%.1f\n", ni, dmin, plane.gLoad); }
    return;
  }
  if (name == "pad") {  // gamepad menu navigation self-test
    auto frame = [&](unsigned btn) { in.pad = true; in.buttonsPressed = btn & ~in.buttons; in.buttons = btn; realTime += 1 / 30.f; update(1 / 30.f); render(); in.endFrame(); };
    screen = SCR_MENU; realTime = 20;
    frame(PAD_LS | PAD_RS); frame(0);
    printf("pad: combo -> research menu: %s\n", screen == SCR_RESEARCH ? "ok" : "FAIL");
    for (int i = 0; i < 50; i++) frame(0);           // let the access sequence finish
    int site = resAirport; frame(PAD_RB); frame(0);
    printf("pad: RB steps launch site: %s\n", resAirport == (site + 1) % (int)g_world.airports.size() ? "ok" : "FAIL");
    frame(PAD_B); frame(0);
    printf("pad: B backs out to main menu: %s\n", screen == SCR_MENU ? "ok" : "FAIL");
    screen = SCR_HUB; hubTab = 0; frame(0);
    float s = S(); float tx = 20 * s + 1 * (150 * s + 10 * s) + 75 * s, ty = 76 * s + 19 * s;
    in.lx = 1.f; float x0 = in.mx; frame(0); in.lx = 0;
    printf("pad: stick moves cursor: %s\n", in.mx > x0 ? "ok" : "FAIL");
    in.mx = tx; in.my = ty; frame(PAD_A); frame(0);
    printf("pad: A clicks the Hangar tab: %s\n", hubTab == 1 ? "ok" : "FAIL");
    in.lx = 0.5f; frame(0); in.lx = 0; in.mx = g_ren.W * 0.5f; in.my = g_ren.H * 0.4f;
    // crash, then A skips the crash sequence straight to the results (and the press doesn't click through them)
    resAirborne = true; launchResearch();
    plane.vel = plane.forward() * 620.f; botControl = true; plane.ctl.pitch = 1;
    for (int i = 0; i < 600 && !crashed; i++) frame(0);
    for (int i = 0; i < 60; i++) frame(0);
    bool wasCrash = crashed && screen == SCR_FLIGHT;
    in.mx = g_ren.W * 0.5f; in.my = g_ren.H * 0.75f; frame(PAD_A); frame(0); frame(0);
    printf("pad: A skips the research crash back to the research menu: %s\n", wasCrash && screen == SCR_RESEARCH ? "ok" : "FAIL");
    // career flight: overstress it, skip with A, land on the results screen without clicking through
    startFlight(g_story[0], 1, Career::SRC_OWNED);
    plane.pos.y += 1500.f; plane.vel = plane.forward() * 95.f; plane.onGround = false; botControl = true; plane.ctl.pitch = -1;
    for (int i = 0; i < 900 && !crashed; i++) frame(0);
    for (int i = 0; i < 60; i++) frame(0);
    wasCrash = crashed && screen == SCR_FLIGHT;
    frame(PAD_A); frame(0); frame(0);
    printf("pad: A skips the career crash to the results: %s\n", wasCrash && screen == SCR_DEBRIEF ? "ok" : "FAIL");
    return;
  }
  if (name == "research") { screen = SCR_RESEARCH; realTime = 20; resOpened = 15; return; }
  if (name == "research11") { screen = SCR_RESEARCH; realTime = 20; resOpened = 15; resCraft = kWraith; return; }
  if (name.compare(0, 3, "wr_") == 0) {   // XR-11: wr_<mode>_<cam yaw>_<cam pitch>_<cam dist>_<seconds>
    // modes: 0 cruise, 1 hover, 2 parked, 3 cloak spreading, 4 cloaked, 5 turrets out + bay open, 6 lasers firing,
    // 7 plasma bomb (camera on the impact), 8 cockpit
    int mode = 0; float yawD = 210, pitD = 12, dist = 30, secs = 1.5f;
    sscanf(name.c_str() + 3, "%d_%f_%f_%f_%f", &mode, &yawD, &pitD, &dist, &secs);
    resCraft = kWraith; realTime = 20; resAirborne = mode != 2; resTime = getenv("TOD") ? (float)atof(getenv("TOD")) : 12.f; launchResearch();
    botControl = true;
    if (mode == 1) { plane.ctl.flaps = 1; flapNotch = 1; plane.flaps = plane.nozzle = 1; plane.vel = vec3(); plane.ctl.throttle = 0.66f; plane.engineRunning = true; plane.engineSpool = 0.66f; }
    else if (mode != 2) { plane.apEngage(Plane::AP_HOLD, -1, wx); }
    if (mode == 3 || mode == 4) wraith.cloakOn = true;
    if (mode == 5 || mode == 6) { wraith.armed = true; }
    if (getenv("WRSPD")) { plane.vel = plane.forward() * (float)atof(getenv("WRSPD")); plane.apSpeed = (float)atof(getenv("WRSPD")); }
    if (mode == 5) wraith.bayHold = 100.f;
    if (mode == 7) wraith.bombQueue = 1;
    float firstDrop = 0; vec3 blastAt;
    int frames = (int)((mode == 3 ? 0.55f : mode == 7 ? 60.f : secs) * 60.f);
    for (int i = 0; i < frames; i++) {
      realTime += 1 / 60.f; update(1 / 60.f);
      if (mode == 6) wraith.wantFire = true;   // trigger held
      if (mode == 7 && !wraith.blasts.empty() && firstDrop == 0) { firstDrop = realTime; blastAt = wraith.blasts[0].p; }
      if (mode == 7 && firstDrop > 0 && realTime - firstDrop > secs) break;
    }
    if (mode == 6) {
      for (auto& b : wraith.bolts) b.life = b.age + 1.f;   // keep the bolts in flight bright for the screenshot
      // the streak of the youngest visible bolt must start at its lens, however fast the craft flies
      // (in the craft's frame the tail sits on the lens's firing line, 2000 m/s x age - streak out from it)
      float worstOff = 0, worstAlong = 0; int n = 0;
      for (auto& b : wraith.bolts) if (b.len > 0.05f && !b.hit && b.age < 0.5f) {
        float off = 1e9f, alongErr = 0;
        for (int s = 0; s < 2; s++) {
          vec3 lens = plane.pos + plane.q.rotate(vec3(s ? 0.95f : -0.95f, -0.68f, -6.44f));
          vec3 r = b.h - b.d * b.len - lens; float al = dot(r, b.d), o = length(r - b.d * al);
          if (o < off) { off = o; alongErr = al - (2000.f * b.age - b.len); }
        }
        worstOff = std::max(worstOff, off); worstAlong = std::max(worstAlong, fabsf(alongErr)); n++;
      }
      printf("wr: speed %.0f m/s, %d young bolts: streak tails off the firing line by <= %.2f m, along-error <= %.2f m\n", length(plane.vel), n, worstOff, worstAlong);
    }
    toasts.clear(); hint.clear();
    for (auto& b : wraith.blasts) printf("wr: blast age %.2f R %.0f at %.0f %.0f %.0f particles %d\n", b.age, b.R, b.p.x, b.p.y, b.p.z, (int)particles.size());
    printf("wr: mode %d stealth %.2f lasers %.2f bay %.2f bombs %d blasts %d tilt %.2f %.2f %.2f %.2f thr %.2f\n", mode, wraith.stealth, wraith.lasers, wraith.bay, (int)wraith.bombs.size(), (int)wraith.blasts.size(),
           plane.podTilt[0], plane.podTilt[1], plane.podTilt[2], plane.podTilt[3], plane.podThr[0]);
    if (mode == 8) { camMode = 1; lookYaw = yawD * DEG; lookPitch = pitD * DEG; camYaw = lookYaw; camPitch = lookPitch + 0.12f; return; }
    hudOn = false; dbgCam = true;
    float h = plane.heading() * DEG, yw = h + yawD * DEG, pt = pitD * DEG;
    vec3 focus = mode == 7 && firstDrop > 0 ? blastAt + vec3(0, 30.f, 0) : plane.pos;
    dbgCamLook = focus; dbgCamPos = focus + vec3(sinf(yw) * cosf(pt), sinf(pt), -cosf(yw) * cosf(pt)) * dist;
    if (!(mode == 7 && firstDrop > 0)) { dbgFollow = true; dbgFollowOff = dbgCamPos - plane.pos; }
    return;
  }
  if (name == "rjet" || name == "rjetc" || name == "rhover" || name == "rjetl" || name == "rjetd" || name == "rjetr") { realTime = 20; resAirborne = name != "rhover"; launchResearch(); if (name == "rjetc" || name == "rjetl" || name == "rjetd" || name == "rjetr") camMode = 1; if (name == "rhover") { plane.ctl.flaps = 1; flapNotch = 1; plane.flaps = plane.nozzle = 1; plane.ctl.throttle = 0.7f; plane.engineRunning = true; plane.engineSpool = 0.7f; } for (int i = 0; i < 90; i++) { realTime += 1 / 30.f; update(1 / 30.f); } toasts.clear(); if (name == "rjetl") lookYaw = 1.75f; if (name == "rjetr") { lookYaw = -1.2f; lookPitch = -0.6f; } if (name == "rjetd") lookPitch = -0.75f; return; }
  if (name == "radio") { loadStations(); screen = SCR_HUB; showRadio = true; realTime = 20; radioScroll = 6; return; }
  if (name.size() == 4 && name.compare(0, 3, "hub") == 0) { screen = SCR_HUB; hubTab = name[3] - '0'; realTime = 20; return; }
  Contract c = g_story[0];
  int spec = 0;
  if (name.size() == 3 && name[0] == 'm') {
    // model inspection: m<aircraft><view>  views: e = exterior 3/4 front, r = controls deflected from behind,
    // c = cockpit, g = on the ground, s = side, d = cockpit with controls deflected
    spec = name[1] - '0'; char v = name[2];
    c.wx = Weather(); c.wx.cloudCover = 0.25f; c.wx.timeOfDay = 10.5f; c.wx.windSpeed = 0; c.wx.turbulence = 0;
    startFlight(c, spec, Career::SRC_OWNED);
    realTime = 10; engineAutoStarted = true;
    const AircraftSpec& s = kAircraft[spec];
    if (v != 'g') { plane.reset(&s, vec3(-6000, 700, 16000), 0, s.maxFuel, 100, true, (v == 'r' || v == 'd' || v == 'b') ? s.vref * 1.3f : s.cruise); plane.gear = 1; plane.ctl.gearDown = true; }
    else { plane.starterTime = 0.01f; plane.engineRunning = true; plane.rpm = 900; plane.n1 = 60; }
    takeoffAnnounced = true;
    float size = std::max(s.fusLen, s.span);
    camMode = 2; camZoom = (v == 'g' ? 0.6f : 0.42f);
    camYaw = 3.14159f - 0.75f; camPitch = 0.18f;
    if (v == 'r' || v == 'd' || v == 'b') { botControl = true; plane.ctl.roll = 1; plane.ctl.pitch = 1; plane.ctl.yaw = 1; plane.ctl.flaps = v == 'b' ? 0.f : 1.f; plane.flaps = plane.ctl.flaps; camYaw = v == 'b' ? 0.f : -0.55f; camPitch = v == 'b' ? 0.22f : 0.35f; if (v == 'b') camZoom = 0.3f; }
    if (v == 's') { camYaw = 1.5708f; camPitch = 0.05f; }
    if (v == 'c' || v == 'd') { camMode = 1; lookYaw = 0; lookPitch = v == 'd' ? -0.45f : -0.13f; }
    if (v == 'y' || v == 'p' || v == 'a') {
      botControl = true; plane.ctl = Controls(); plane.ctl.throttle = 0.6f;
      if (v == 'y') plane.ctl.yaw = 1; if (v == 'p') plane.ctl.pitch = 1; if (v == 'a') plane.ctl.roll = 1;
      camMode = 3;
      vec3 off = v == 'y' ? vec3(0, size * 1.6f, size * 0.9f) : v == 'p' ? vec3(size * 1.5f, 0.3f, 0) : vec3(0, size * 0.25f, size * 1.8f);
      hint.clear(); toasts.clear(); hudOn = false;
      camPos = plane.pos + off;
      return;
    }
    (void)size;
    hint.clear(); toasts.clear(); hudOn = false;
    camPos = plane.pos;
    updateCamera(0.016f);
    if (v == 'd') { lookPitch = -0.35f; }
    return;
  }
  if (name == "storm") { c = g_story[g_story.size() - 1]; spec = 5; }
  if (name == "jet") { c = g_story[g_story.size() - 4]; spec = 6; }
  if (name == "night") { c = g_story[11]; c.wx.timeOfDay = 21.5f; spec = 1; }
  if (name == "snow") { c = g_story[20]; spec = 2; }
  startFlight(c, spec, Career::SRC_OWNED);
  realTime = 10;
  plane.starterTime = 0.01f; plane.engineRunning = true; plane.rpm = 1000; engineAutoStarted = true;
  {
    float px, pz, agl, hdg; char cm = 'c';
    float yw, pt, dist, lh = 8, tod = -1;
    if (sscanf(name.c_str(), "look_%f_%f_%f_%f_%f_%f_%f", &px, &pz, &yw, &pt, &dist, &lh, &tod) >= 5) {   // free camera on a ground point
      if (tod >= 0) timeOfDay = tod;
      plane.reset(&kAircraft[1], vec3(px + 4000.f, std::max(g_world.height(px + 4000.f, pz), 0.f) + 900.f, pz), 90, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
      takeoffAnnounced = true; hudOn = false; hint.clear(); toasts.clear(); dbgCam = true; dbgFollow = false;
      dbgCamLook = vec3(px, std::max(g_world.height(px, pz), 0.f) + lh, pz);
      dbgCamPos = dbgCamLook + vec3(sinf(yw * DEG) * cosf(pt * DEG), sinf(pt * DEG), -cosf(yw * DEG) * cosf(pt * DEG)) * dist;
      dbgCamPos.y = std::max(dbgCamPos.y, std::max(g_world.height(dbgCamPos.x, dbgCamPos.z), 0.f) + 2.f);
      for (int i = 0; i < 5; i++) updateCamera(0.1f);
      return;
    }
    if (sscanf(name.c_str(), "at_%f_%f_%f_%f_%c", &px, &pz, &agl, &hdg, &cm) >= 4) {
      plane.reset(&kAircraft[1], vec3(px, std::max(g_world.height(px, pz), 0.f) + agl, pz), hdg, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
      takeoffAnnounced = true; camQ = plane.q; hudOn = false; hint.clear(); toasts.clear();
      if (cm == 'k') camMode = 1;
      if (cm == 'f') { camMode = 3; botControl = true; camPos = plane.pos + vec3(0, 40, 0) - plane.forward() * 60.f; }
      for (int i = 0; i < 30; i++) updateCamera(0.1f);
      return;
    }
  }
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
  if (name == "rings" || name == "ringburst") {
    vec3 c, ax, ay; ringGeom(0, c, ax, ay);
    vec3 nd = normalize(cross(vec3(0, 1, 0), ax));
    float dist = name == "rings" ? 420.f : 160.f;
    float hdg = atan2f(nd.x, -nd.z) / DEG;
    plane.reset(&kAircraft[0], c - nd * dist - vec3(0, 12, 0), hdg, kAircraft[0].maxFuel, 100, true, kAircraft[0].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); toasts.clear(); botControl = true;
    plane.ctl = Controls(); plane.ctl.throttle = 0.7f;
    for (int i = 0; i < 30; i++) updateCamera(0.1f);
    for (int i = 0; i < (name == "rings" ? 60 : 42); i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    return;
  }
  if (name.compare(0, 8, "airbreak") == 0) {   // XR-9 overstressed at speed: breaks up in the air; airbreak<seconds after>
    bool cessna = name.size() > 8 && name[8] == 'c';   // airbreakc<s>: a light aircraft pushed over hard in a dive instead
    float after = name.size() > 8 + cessna ? atof(name.c_str() + 8 + cessna) : 3.f;
    resAirborne = true; realTime = 20; launchResearch(); hudOn = false;
    if (cessna) { plane.reset(&kAircraft[1], plane.pos, 90, kAircraft[1].maxFuel, 100, true, 90); researchFlight = false; }
    plane.pos.y = std::max(plane.pos.y, g_world.height(plane.pos.x, plane.pos.z) + 2500.f);
    plane.vel = plane.forward() * (cessna ? 95.f : 620.f); plane.ctl.throttle = 1; plane.engineSpool = 1; botControl = true; plane.ctl.pitch = cessna ? -1.f : 1.f;
    camMode = 2; camYaw = 0.9f; camPitch = 0.25f; camZoom = 1.2f;
    float tb = -1, tEnd = -1;
    for (int i = 0; i < 60 * 200; i++) {
      realTime += 1 / 60.f; update(1 / 60.f);
      if (crashed && tb < 0) { tb = flightClock; printf("airbreak: broke up (%s), %d pieces, %.0f m AGL\n", plane.ev.crashReason.c_str(), (int)wreck.size(), plane.agl()); }
      if (crashed && tEnd < 0 && crashEndT < 1e8f) { tEnd = crashTimer; float sp = 0; for (auto& w : wreck) sp = std::max(sp, length(w.v));
        printf("airbreak: all pieces down %.1f s after the break-up (crater %.1f m) %s\n", crashTimer, craterR, crashTimer > 5.f ? "ok" : "FAIL"); }
      if (crashed && crashTimer >= after) break;
    }
    if (!crashed) printf("airbreak: FAIL - no break-up\n");
    for (int i = 0; i < 5; i++) updateCamera(1 / 60.f);
    toasts.clear(); return;
  }
  if (name.compare(0, 3, "ckv") == 0) {   // cockpit view of aircraft N: ckv<N>_<look yaw deg>_<look pitch deg>_<hour>
    int idx = 0; float ly = 0, lpch = -8, hour = 11; sscanf(name.c_str() + 3, "%d_%f_%f_%f", &idx, &ly, &lpch, &hour);
    if (idx == kResearchJet) { resAirborne = true; resTime = hour; launchResearch(); }
    else { timeOfDay = hour; plane.reset(&kAircraft[idx], vec3(-4000, 600, 9000), 40, kAircraft[idx].maxFuel, 100, true, kAircraft[idx].cruise); camQ = plane.q; takeoffAnnounced = true; }
    camMode = 1; hint.clear(); toasts.clear();
    for (int i = 0; i < 4; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    lookYaw = ly * DEG; lookPitch = lpch * DEG; camYaw = lookYaw; camPitch = lookPitch + 0.12f;
    toasts.clear(); hint.clear(); return;
  }
  if (name.compare(0, 3, "trf") == 0) {   // AI traffic: trf<seconds>_<view> at Solace Capital; view 0 = airport overview, k = chase craft k-1
    float secs = 60; int view = 0; sscanf(name.c_str() + 3, "%f_%d", &secs, &view);
    set.traffic = getenv("NOTRF") == nullptr;
    int ai = g_world.findAirport("CAP"); const Airport& a = g_world.airports[ai];
    float h = a.heading * DEG, sn = sinf(h), cs = cosf(h), side = (ai & 1) ? 1.f : -1.f;
    auto W = [&](float u, float v) { return vec3(a.x + u * sn + v * cs, 0, a.z - u * cs + v * sn); };
    vec3 pp = W(a.length * 0.47f, side * (a.width * 0.5f + 130.f)); pp.y = g_world.height(pp.x, pp.z) + 1.5f;
    plane.reset(&kAircraft[1], pp, a.heading, kAircraft[1].maxFuel, 100, false, 0);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); timeOfDay = getenv("TOD") ? (float)atof(getenv("TOD")) : 15.f;
    for (float tt = 0; tt < secs; tt += 1 / 20.f) { realTime += 1 / 20.f; update(1 / 20.f); }
    const char* st[] = {"PARKED", "TAXI_OUT", "HOLD", "LINEUP", "TAKEOFF", "CLIMB", "CIRCUIT", "FINAL", "ROLLOUT", "TAXI_IN", "FLY"};
    const char* rl[] = {"airport", "cruiser", "formation", "stunt", "escort"};
    for (int i = 0; i < (int)traffic.craft.size(); i++) { const TrafficCraft& c = traffic.craft[i];
      printf("trf %2d %-9s %-17s %-8s agl %6.0f spd %5.1f dist %6.0f man %d\n", i, rl[c.role], kAircraft[c.spec].name, st[c.state], c.pos.y - g_world.height(c.pos.x, c.pos.z), c.speed, length(c.pos - plane.pos), c.man); }
    dbgCam = true; hudOn = false; toasts.clear();
    if (view == 0) { dbgCamLook = W(0, side * 60.f); dbgCamLook.y = a.elev; dbgCamPos = W(-a.length * 0.2f, -side * 700.f); dbgCamPos.y = a.elev + 250.f; }
    else if (view - 1 < (int)traffic.craft.size()) { const TrafficCraft& c = traffic.craft[view - 1]; float sz = kAircraft[c.spec].fusLen;
      dbgCamLook = c.pos; dbgCamPos = c.pos + c.q.rotate(vec3(sz * 0.9f, sz * 0.35f, sz * 1.6f)); }
    return;
  }
  if (name == "ufosummon") {   // J + K held for a second while flying summons the UFO
    plane.reset(&kAircraft[1], vec3(-4000, 700, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; botControl = true; plane.ctl.throttle = 0.75f;
    for (int i = 0; i < 10; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    in.down['J'] = true; in.down['K'] = true;
    bool early = false;
    for (int i = 0; i < 36; i++) { realTime += 1 / 30.f; update(1 / 30.f); if (i == 20 && ufo.on) early = true; }
    in.down['J'] = in.down['K'] = false;
    printf("ufosummon: %s\n", ufo.on && !early ? "ok" : "FAIL");
    return;
  }
  if (name == "escsummon") {   // O + P held for a second while flying summons the display pair
    plane.reset(&kAircraft[1], vec3(-4000, 700, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; botControl = true; plane.ctl.throttle = 0.75f;
    for (int i = 0; i < 10; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    in.down['O'] = true; in.down['P'] = true;
    bool early = false;
    for (int i = 0; i < 36; i++) { realTime += 1 / 30.f; update(1 / 30.f); if (i == 20 && traffic.count(TrafficCraft::ESCORT)) early = true; }
    in.down['O'] = in.down['P'] = false;
    printf("escsummon: %s\n", traffic.count(TrafficCraft::ESCORT) == 2 && !early ? "ok" : "FAIL");
    return;
  }
  if (name == "escdismiss") {   // O + P again mid-helix: the pair blends out of the act, waves and leaves without jumps
    plane.reset(&kAircraft[1], vec3(-4000, 900, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; plane.engineRunning = true; plane.engineSpool = 0.7f;
    plane.apEngage(Plane::AP_HOLD, -1, wx);
    traffic.spawnEscort(plane.pos, plane.vel);
    float worstJump = 0; std::vector<vec3> last;
    for (float x = 0; x < 50.f; x += 1 / 60.f) {
      if (fabsf(x - 27.f) < 1e-3f) traffic.dismissEscort();
      realTime += 1 / 60.f; update(1 / 60.f);
      std::vector<vec3> now; std::vector<float> spd;
      for (auto& c : traffic.craft) if (c.role == TrafficCraft::ESCORT) { now.push_back(c.pos); spd.push_back(c.speed); }
      if (now.size() == last.size()) for (size_t i = 0; i < now.size(); i++) worstJump = std::max(worstJump, length(now[i] - last[i]) * 60.f / std::max(spd[i], 30.f));
      last = now;
    }
    // a frame's step must match the jet's own speed (no teleports when the act is cut short)
    printf("escdismiss: worst step / speed %.2f, %d escorts left %s\n", worstJump, traffic.count(TrafficCraft::ESCORT),
           worstJump < 1.3f && traffic.count(TrafficCraft::ESCORT) == 0 ? "ok" : "FAIL");
    return;
  }
  if (name.compare(0, 3, "esc") == 0) {   // display pair at t seconds: esc<t>_<view>_<aircraft> (0 chase, 1 cockpit, 2 side camera)
    float tt = 20; int view = 0, sp = 1; sscanf(name.c_str() + 3, "%f_%d_%d", &tt, &view, &sp);
    plane.reset(&kAircraft[sp], vec3(-4000, 900, 9000), 40, kAircraft[sp].maxFuel, 100, true, kAircraft[sp].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); timeOfDay = getenv("TOD") ? (float)atof(getenv("TOD")) : 14.f;
    plane.engineRunning = true; plane.engineSpool = 0.7f;
    plane.apEngage(Plane::AP_HOLD, -1, wx);
    traffic.spawnEscort(plane.pos, plane.vel);
    float worst = 0;
    for (float x = 0; x < tt; x += 1 / 60.f) {
      realTime += 1 / 60.f; update(1 / 60.f);
      for (auto& c : traffic.craft) if (c.role == TrafficCraft::ESCORT && length(c.vel) > 1.f) worst = std::max(worst, acosf(clampf(dot(c.q.rotate(vec3(0, 0, -1)), normalize(c.vel)), -1.f, 1.f)) / DEG);
    }
    for (auto& c : traffic.craft) if (c.role == TrafficCraft::ESCORT)
      printf("esc: act %d t %.1f  jet %+.0f  %.0f m from the player  %.0f m/s  agl %.0f\n", traffic.escAct, traffic.escT, c.escSide, length(c.pos - plane.pos), c.speed, c.pos.y - g_world.height(c.pos.x, c.pos.z));
    printf("esc: worst nose-off-path %.1f deg %s\n", worst, worst < 2.f ? "ok" : "FAIL");
    toasts.clear();
    if (view == 1) { camMode = 1; lookYaw = 0; lookPitch = -0.05f; camYaw = 0; camPitch = 0.12f; }
    else if (view == 2) { hudOn = false; dbgCam = true; vec3 f = normalize(plane.vel), r = normalize(cross(f, vec3(0, 1, 0))); dbgCamPos = plane.pos + r * 160.f + vec3(0, 40, 0) - f * 60.f; dbgCamLook = plane.pos + f * 60.f; }
    return;
  }
  if (name.compare(0, 3, "ufo") == 0) {   // UFO encounter at t seconds: ufo<t>_<view> (0 chase-style, 1 cockpit, 2 close-up of the hatch)
    float tt = 12; int view = 0; sscanf(name.c_str() + 3, "%f_%d", &tt, &view);
    plane.reset(&kAircraft[1], vec3(-4000, 700, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); timeOfDay = getenv("TOD") ? (float)atof(getenv("TOD")) : 14.f;
    botControl = true; plane.ctl.throttle = 0.75f;
    startUfo(); ufo.side = 1.f;
    for (float x = 0; x < tt; x += 1 / 30.f) { realTime += 1 / 30.f; update(1 / 30.f); }
    toasts.clear(); hudOn = false;
    if (view == 1) { camMode = 1; lookYaw = -80.f * DEG; lookPitch = 0.f; camYaw = lookYaw; camPitch = 0.12f; }
    else { dbgCam = true; dbgCamLook = ufo.pos + ufo.up * 1.5f;
      dbgCamPos = view == 2 ? ufo.pos + ufo.right * 9.f + ufo.up * 2.5f : plane.pos - ufo.right * 3.f + ufo.up * 2.f; }
    printf("ufo: t %.1f hatch %.2f laugh %.2f wave %.2f, %.1f m from the player\n", ufo.t, ufo.hatch, ufo.laugh, ufo.wave, length(ufo.pos - plane.pos));
    return;
  }
  if (name.compare(0, 3, "xrf") == 0) {   // XR-9 formation pass: xrf<seconds after spawn>; camera at the player looking at the leader
    float secs = 20; sscanf(name.c_str() + 3, "%f", &secs);
    plane.reset(&kAircraft[1], vec3(-4000, 700, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); timeOfDay = 14.f;
    traffic.reset(); traffic.spawnFormation(plane.pos, vec3());
    for (float tt = 0; tt < secs; tt += 1 / 30.f) { realTime += 1 / 30.f; update(1 / 30.f); }
    for (auto& c : traffic.craft) if (c.role == TrafficCraft::FORMATION && c.leader < 0) {
      printf("xrf: leader %.0f m from the player, %.0f m/s\n", length(c.pos - plane.pos), c.speed);
      dbgCam = true; dbgCamPos = plane.pos + vec3(0, 3, 0); dbgCamLook = c.pos; break; }
    hudOn = false; toasts.clear(); return;
  }
  if (name.compare(0, 4, "gtun") == 0) {   // g-force tunnel at a forced strength (percent), chase view: gtun<pct>
    resAirborne = true; realTime = 20; launchResearch();
    for (int i = 0; i < 10; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    gTunnel = atof(name.c_str() + 4) / 100.f; toasts.clear(); return;
  }
  if (name == "seacrash") {  // ditches into deep sea: the wreck must float briefly, then sink to the seabed
    float gx = 0, gz = 0;
    for (int i = 0; i < 4000 && g_world.height(gx, gz) > -25.f; i++) { gx = -30000.f + (i % 63) * 1000.f; gz = -30000.f + (i / 63) * 1000.f; }
    plane.reset(&kAircraft[1], vec3(gx, 50.f, gz), 30, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    plane.q = plane.q * quat::axisAngle(vec3(1, 0, 0), -0.6f);
    plane.vel = plane.q.rotate(vec3(0, 0, -kAircraft[1].cruise));
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); toasts.clear(); botControl = true; hudOn = false;
    for (int i = 0; i < 600 && !crashed; i++) { realTime += 1 / 60.f; update(1 / 60.f); }
    float hiW = -1e9f, hiD = -1e9f;
    for (int i = 0; i < 60 * 40; i++) {
      realTime += 1 / 60.f; update(1 / 60.f);
      if (i == 60 * 2) { for (auto& w : wreck) hiW = std::max(hiW, w.c.y); }
    }
    float topW = -1e9f; for (auto& w : wreck) topW = std::max(topW, w.c.y);
    for (auto& d : debris) hiD = std::max(hiD, d.p.y);
    printf("seacrash: seabed %.0f m, highest piece %.1f m at 2 s, %.1f m at 40 s, highest fragment %.1f m %s\n", g_world.height(gx, gz), hiW, topW, hiD,
           crashed && hiW > -2.f && topW < -3.f && hiD < -3.f ? "ok" : "FAIL");
    toasts.clear(); return;
  }
  if (name.compare(0, 5, "crash") == 0) {
    // flies into a field nose-down, then advances the wreck simulation by the given number of tenths of a second
    int sp = name.size() > 6 ? name[5] - '0' : 1;
    float after = name.size() > 7 ? atof(name.c_str() + 7) * 0.1f : 1.f;
    float gx = -9000, gz = 11500;
    plane.reset(&kAircraft[sp], vec3(gx, g_world.height(gx, gz) + 60.f, gz), 30, kAircraft[sp].maxFuel, 100, true, kAircraft[sp].cruise);
    plane.q = plane.q * quat::axisAngle(vec3(1, 0, 0), -0.6f);
    plane.vel = plane.q.rotate(vec3(0, 0, -kAircraft[sp].cruise));
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); toasts.clear(); botControl = true; hudOn = false;
    for (int i = 0; i < 30; i++) updateCamera(0.1f);
    for (int i = 0; i < 600 && !crashed; i++) { realTime += 1 / 60.f; update(1 / 60.f); }
    for (float tt = 0; tt < after; tt += 1 / 60.f) { realTime += 1 / 60.f; crashTimer = std::min(crashTimer, 5.f); update(1 / 60.f); }
    toasts.clear();
    return;
  }
  if (name == "top") { camMode = 2; camYaw = 0.3f; camPitch = 1.35f; camZoom = 4.f; }
  if (name == "orbit") { camMode = 2; camYaw = 2.3f; camPitch = 0.25f; camZoom = 0.6f; timeOfDay = 9.0f; }
  for (int i = 0; i < 30; i++) updateCamera(0.1f);
  if (name == "hud") hint = contract.hints.size() ? contract.hints[2] : "";
  if (name == "gps" || name == "pause" || name == "minimap") {
    plane.reset(&kAircraft[1], vec3(-4000, 600, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); toasts.clear();
    for (int i = 0; i < 60; i++) trail.push_back(vec2(-4000 - sinf(40 * DEG) * i * 110.f + sinf(i * 0.1f) * 300.f, 9000 + cosf(40 * DEG) * i * 110.f));
    if (name == "gps") { showMap = true; uiAnim[0x6e61u] = 1.f; }
    if (name == "pause") paused = true;
    if (name == "minimap") showMinimap = true;
    for (int i = 0; i < 30; i++) updateCamera(0.1f);
  }
  if (name == "gpsap" || name == "apfinal" || name == "apvtol") {   // autopilot: GPS autoland pick, then the approach
    int sp = name == "apvtol" ? 7 : 4;
    plane.reset(&kAircraft[sp], vec3(-4000, 900, 9000), 40, kAircraft[sp].maxFuel, 100, true, kAircraft[sp].cruise * 0.8f);
    plane.engineRunning = true; plane.engineSpool = 0.7f; plane.ctl.throttle = 0.7f;
    takeoffAnnounced = true; engineAutoStarted = true; camQ = plane.q; hint.clear();
    apDest = g_world.findAirport("CAP"); engageAutopilot();
    int target = name == "gpsap" ? -1 : name == "apfinal" ? Plane::APS_FINAL : Plane::APS_HOVER;
    for (int i = 0; i < (target < 0 ? 90 : 60 * 1500); i++) {
      realTime += 1 / 60.f; update(1 / 60.f);
      if (target >= 0 && plane.apStage == target && plane.apStageT > (target == Plane::APS_HOVER ? 14.f : 25.f)) break;
    }
    toasts.clear();
    if (name == "gpsap") { showMap = true; uiAnim[0x6e61u] = 1.f; }
    for (int i = 0; i < 30; i++) updateCamera(0.1f);
  }
}
