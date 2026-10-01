// Air Xpress - career progression and hand-designed story campaign
#include "career.h"

std::vector<Contract> g_story;

static Weather W(float from, float kt, float gustKt, float turb, float cover, float baseFt, float visKm, int precip, bool storm, float tod) {
  Weather w; w.windFrom = from; w.windSpeed = kt / MS_TO_KT; w.gust = gustKt / MS_TO_KT; w.turbulence = turb;
  w.cloudCover = cover; w.cloudBase = baseFt / M_TO_FT; w.visibility = visKm * 1000.f; w.precip = precip; w.storm = storm; w.timeOfDay = tod;
  return w;
}

static Waypoint wpRel(const char* ap, float alongKm, float leftKm, float aglM) {
  const Airport& a = g_world.airports[g_world.findAirport(ap)];
  vec3 d = a.dir(), r(-d.z, 0, d.x);
  vec3 p = a.pos() + d * (alongKm * 1000.f) - r * (leftKm * 1000.f);
  return {p.x, p.z, a.elev + aglM};
}
static Waypoint wpAbs(const char* ap, float aglM) { const Airport& a = g_world.airports[g_world.findAirport(ap)]; return {a.x, a.z, a.elev + aglM}; }

struct S {  // compact story entry builder
  Contract c;
  S(const char* id, int ch, int type, const char* from, const char* to, const char* title) {
    c.id = id; c.chapter = ch; c.type = type; c.title = title; c.story = true;
    c.from = g_world.findAirport(from); c.to = g_world.findAirport(to);
  }
  S& load(int kg, int pax) { c.cargoKg = kg; c.pax = pax; return *this; }
  S& pay(int p) { c.payout = p; return *this; }
  S& lic(int l) { c.minLicense = l; return *this; }
  S& owned() { c.ownedOnly = true; return *this; }
  S& fragile() { c.fragile = true; return *this; }
  S& limit(float m) { c.timeLimitMin = m; return *this; }
  S& grant(int l) { c.grantLicense = l; return *this; }
  S& wx(Weather w) { c.wx = w; return *this; }
  S& brief(const char* b) { c.brief = b; return *this; }
  S& wp(Waypoint w) { c.wps.push_back(w); return *this; }
  S& hints(std::vector<std::string> h) { c.hints = h; return *this; }
  S& lesson() { c.forceAircraft = 0; return *this; }
};

void buildStory() {
  g_story.clear();
  auto add = [](S& s) { g_story.push_back(s.c); };
  // ------------------------------------------------------------ Chapter 0: Student Permit
  { S s("L1", 0, CT_LESSON, "MDB", "MDB", "Lesson 1: Takeoff and Climb");
    s.lesson().pay(150).wx(W(50, 4, 0, 0.05f, 0.2f, 4500, 40, 0, false, 9.5f))
     .brief("Welcome to Meadowbrook Flight Academy! Today you'll fly the Kestrel trainer for the first time. "
            "Release the brakes, apply full power, rotate gently at 50 knots and climb straight ahead through the rings.")
     .wp(wpRel("MDB", 2.2f, 0, 150)).wp(wpRel("MDB", 4.0f, 0, 300)).wp(wpRel("MDB", 5.5f, -0.6f, 380))
     .hints({"Press B to release the parking brake, then hold SHIFT (or gamepad RT) to add full throttle.",
             "Keep the nose on the centreline with Q/E rudder. At 50 kt gently pull back (S) to lift off.",
             "Nice! Hold a gentle climb about 7 degrees nose-up. Fly through the green rings.",
             "Reduce power slightly (CTRL) once level and trim with [ and ]. Rings show the path.",
             "", "", ""});
    add(s); }
  { S s("L2", 0, CT_LESSON, "MDB", "MDB", "Lesson 2: Traffic Pattern and Landing");
    s.lesson().pay(200).wx(W(40, 6, 0, 0.08f, 0.3f, 4000, 40, 0, false, 10.5f))
     .brief("Fly a full traffic pattern: upwind, crosswind, downwind, base and final, then land back on runway 05. "
            "Aim for a gentle touchdown below 300 feet per minute.")
     .wp(wpRel("MDB", 2.4f, 0, 200)).wp(wpRel("MDB", 2.8f, 1.4f, 420)).wp(wpRel("MDB", 0.0f, 1.8f, 420))
     .wp(wpRel("MDB", -2.4f, 1.8f, 400)).wp(wpRel("MDB", -3.0f, 0.8f, 260)).wp(wpRel("MDB", -2.4f, 0, 165))
     .hints({"Release brakes (B), full power, and take off as in Lesson 1.",
             "Rudder to stay straight. Rotate at 50 kt.",
             "Climb through the rings. The pattern turns LEFT. Use gentle 20 degree banks.",
             "On downwind reduce power to about 60% and set one notch of flaps (F).",
             "Turn final, add full flaps (F), and follow the PAPI lights: two white, two red is on glidepath.",
             "Reduce power to idle over the threshold, then gently raise the nose to flare just above the runway.",
             "Brake (B) to a full stop to complete the lesson."});
    add(s); }
  { S s("L3", 0, CT_LESSON, "MDB", "HFS", "Lesson 3: Cross-Country to Harlan Farm");
    s.lesson().pay(300).wx(W(200, 7, 3, 0.15f, 0.35f, 3500, 30, 0, false, 13.0f))
     .brief("Your first cross-country flight. Navigate to Harlan Farm Strip using the GPS arrow and land on its short grass runway. "
            "Grass is slower, so touch down early and brake firmly.")
     .hints({"Release brakes and take off. The magenta arrow on the HUD points to your destination.",
             "Stay on the centreline.",
             "Climb to about 1,500 feet and head toward the arrow. Press Z for autopilot heading/altitude hold.",
             "Watch your fuel gauge and the distance readout. Press N to view the map.",
             "Harlan Farm runway 17 is short: slow to 60 kt, full flaps, aim for the very start of the strip.",
             "Flare gently and get the wheels down early.",
             "Full stop with brakes (B) to finish."});
    add(s); }
  { S s("L4", 0, CT_LESSON, "HFS", "ORC", "Checkride: Private Pilot License");
    s.lesson().pay(400).grant(LIC_PPL).wx(W(330, 11, 5, 0.25f, 0.5f, 3000, 25, 0, false, 15.5f))
     .brief("Examiner Rosa Vance will ride with you to Orchard Valley. There's a gusty crosswind today. "
            "Land on the runway without a hard landing to earn your Private Pilot License.")
     .hints({"Final exam: take off when ready.", "Crosswind! Use rudder to stay on the centreline.",
             "Climb out and navigate to Orchard Valley.", "Plan your descent early - Orchard Valley sits at 690 ft.",
             "Crab into the wind on final. Just before touchdown, use rudder to align with the runway.",
             "Keep the upwind wing slightly low as you flare.", "Stop on the runway to complete the checkride."});
    add(s); }
  // ------------------------------------------------------------ Chapter 1: Private Pilot (cargo by rental)
  { S s("C1", 1, CT_CARGO, "ORC", "MDB", "Seed Delivery");
    s.load(90, 0).pay(700).lic(LIC_PPL).wx(W(250, 6, 0, 0.1f, 0.25f, 4000, 40, 0, false, 9.0f))
     .brief("The Orchard Valley co-op needs seed sacks flown to Meadowbrook. Rent a Kestrel - the fee comes out of your pay."); add(s); }
  { S s("C2", 1, CT_CARGO, "MDB", "PVI", "Parts for the Port");
    s.load(110, 0).pay(950).lic(LIC_PPL).wx(W(270, 12, 4, 0.2f, 0.4f, 3500, 30, 0, false, 14.0f))
     .brief("Engine parts for a fishing trawler at Port Verde. Afternoon sea breeze on the west coast."); add(s); }
  { S s("C3", 1, CT_CARGO, "PVI", "LHK", "Lighthouse Provisions");
    s.load(100, 0).pay(1300).lic(LIC_PPL).wx(W(190, 13, 6, 0.3f, 0.5f, 2500, 20, 0, false, 11.0f))
     .brief("The keeper of Lighthouse Key needs supplies. Short grass runway on a tiny islet, with a crosswind straight off the sea."); add(s); }
  { S s("C4", 1, CT_CARGO, "LHK", "MDB", "Message in a Bottle");
    s.load(40, 0).pay(1000).lic(LIC_PPL).wx(W(220, 9, 0, 0.15f, 0.6f, 2200, 15, 1, false, 17.0f))
     .brief("The keeper's mail and a crate of hand-made bottles back to Meadowbrook. Light rain moving in. Fragile!").fragile(); add(s); }
  { S s("C5", 1, CT_CARGO, "MDB", "GLR", "Gull Rock Generator");
    s.load(260, 0).pay(1900).lic(LIC_PPL).wx(W(80, 10, 3, 0.2f, 0.3f, 3500, 30, 0, false, 10.0f))
     .brief("A replacement generator for the weather station on Gull Rock. Too heavy for the Kestrel - rent a Wren 180. "
            "The runway is only 480 m with water at both ends."); add(s); }
  { S s("C6", 1, CT_CARGO, "GLR", "CAP", "Urgent Documents");
    s.load(30, 0).pay(1700).lic(LIC_PPL).limit(16).wx(W(120, 8, 0, 0.1f, 0.2f, 5000, 40, 0, false, 8.0f))
     .brief("Signed contracts must reach Solace Capital within 16 minutes. Fly fast and direct."); add(s); }
  { S s("C7", 1, CT_CARGO, "CAP", "CDR", "Valley Hardware");
    s.load(220, 0).pay(2100).lic(LIC_PPL).wx(W(300, 14, 7, 0.45f, 0.45f, 4500, 30, 0, false, 15.0f))
     .brief("Building supplies for Cedar Ridge. Wind across the Spine makes the valley bumpy. Expect to fight for the centreline."); add(s); }
  { S s("C8", 1, CT_CARGO, "CDR", "NPT", "Checkride: Commercial Pilot");
    s.load(50, 0).pay(2500).lic(LIC_PPL).grant(LIC_CPL).wx(W(20, 10, 4, 0.2f, 0.85f, 1100, 5, 1, false, 18.5f))
     .brief("Your commercial checkride. Fly to Northpoint in low cloud and rain with 5 km visibility. "
            "Stay below the clouds, follow the GPS, and land safely. Pass to earn your Commercial Pilot License."); add(s); }
  // ------------------------------------------------------------ Chapter 2: Commercial (passengers, bigger rentals)
  { S s("P1", 2, CT_PAX, "NPT", "PVI", "First Passengers");
    s.load(30, 3).pay(2600).lic(LIC_CPL).wx(W(330, 8, 0, 0.1f, 0.3f, 4000, 35, 0, false, 10.0f))
     .brief("A family of three heading to Port Verde. Passengers hate steep banks and hard landings - keep it smooth."); add(s); }
  { S s("P2", 2, CT_PAX, "PVI", "CDR", "Hikers to the Hills");
    s.load(60, 2).pay(1900).lic(LIC_CPL).wx(W(280, 10, 4, 0.3f, 0.4f, 5000, 35, 0, false, 8.5f))
     .brief("Two hikers bound for the Cedar Ridge trailheads."); add(s); }
  { S s("P3", 2, CT_CARGO, "CDR", "SMP", "Summit Pass Rescue Gear");
    s.load(380, 0).pay(5200).lic(LIC_CPL).wx(W(250, 14, 8, 0.5f, 0.35f, 7500, 40, 0, false, 9.5f))
     .brief("Mountain rescue needs equipment at Summit Pass: a 600 m gravel shelf at 5,400 ft. Only a STOL aircraft can do this. "
            "Rent the Bushmaster - a taildragger, so keep the tail down with back pressure and use rudder constantly."); add(s); }
  { S s("P4", 2, CT_MEDEVAC, "SMP", "CAP", "Medevac: Injured Climber");
    s.load(80, 1).pay(5600).lic(LIC_CPL).limit(14).wx(W(260, 16, 9, 0.55f, 0.5f, 6000, 30, 0, false, 10.3f))
     .brief("An injured climber needs a hospital. Get her to Solace Capital within 14 minutes - and gently."); add(s); }
  { S s("P5", 2, CT_PAX, "CAP", "KLO", "Island Hopper");
    s.load(100, 8).pay(6200).lic(LIC_CPL).wx(W(70, 12, 4, 0.2f, 0.4f, 3500, 35, 0, false, 12.0f))
     .brief("Eight tourists to tropical Kaleo. Rent an Islander Twin for the crossing."); add(s); }
  { S s("P6", 2, CT_PAX, "KLO", "PMB", "Beach Resort Shuttle");
    s.load(120, 7).pay(4800).lic(LIC_CPL).wx(W(90, 11, 6, 0.3f, 0.7f, 2000, 12, 1, false, 16.0f))
     .brief("Resort guests to Palm Bay's sand strip. Afternoon showers are rolling through."); add(s); }
  { S s("P7", 2, CT_CARGO, "PMB", "VCF", "Volcano Research Station");
    s.load(820, 0).pay(8000).lic(LIC_CPL).wx(W(60, 15, 8, 0.6f, 0.5f, 6000, 25, 0, false, 9.0f))
     .brief("Seismometers for the volcano observatory. 650 m of gravel at 2,950 ft on the flank of Mount Kaleo, with treacherous winds."); add(s); }
  // ------------------------------------------------------------ Chapter 3: Owner-operator
  { S s("O1", 3, CT_CARGO, "VCF", "SMP", "Your Own Wings: Lodge Supplies");
    s.load(400, 0).pay(14000).lic(LIC_CPL).owned().wx(W(270, 10, 4, 0.35f, 0.3f, 8000, 40, 0, false, 10.0f))
     .brief("The Summit Lodge wants a regular supply operator - but only one with their own aircraft. "
            "Time to visit the Hangar and buy your first plane. A Bushmaster fits the job."); add(s); }
  { S s("O2", 3, CT_PAX, "SMP", "NPT", "Ski Team Transfer");
    s.load(120, 3).pay(13000).lic(LIC_CPL).owned().wx(W(10, 12, 6, 0.4f, 0.6f, 6500, 15, 2, false, 15.0f))
     .brief("The national ski team is heading home to Northpoint. Light snow over the Spine."); add(s); }
  { S s("O3", 3, CT_PAX, "NPT", "FJH", "Nordholm Crossing");
    s.load(150, 4).pay(24000).lic(LIC_CPL).owned().wx(W(350, 15, 6, 0.35f, 0.65f, 3500, 15, 2, false, 11.0f))
     .brief("Cross 40 km of cold open sea to Fjordhaven. Check your fuel before you go."); add(s); }
  { S s("O4", 3, CT_CARGO, "FJH", "GLS", "Glacier Station Resupply");
    s.load(450, 0).pay(20000).lic(LIC_CPL).owned().wx(W(40, 14, 8, 0.45f, 0.75f, 7000, 8, 2, false, 12.5f))
     .brief("The science camp on the Nordholm ice plateau is running low on food. Packed-snow runway at 4,750 ft, snow falling."); add(s); }
  { S s("O5", 3, CT_CARGO, "GLS", "FJH", "Ice Cores");
    s.load(300, 0).pay(17000).lic(LIC_CPL).owned().fragile().wx(W(60, 9, 3, 0.25f, 0.4f, 8000, 30, 0, false, 14.0f))
     .brief("Priceless ice core samples. Any rough handling (over 2 G) will crack them."); add(s); }
  { S s("O6", 3, CT_CARGO, "FJH", "CAP", "Fjord Fish to Market");
    s.load(1150, 0).pay(30000).lic(LIC_CPL).owned().wx(W(20, 12, 5, 0.2f, 0.5f, 4500, 30, 0, false, 6.5f))
     .brief("A huge catch for the capital's morning market. You'll need something bigger - the Pelican Caravan hauls 1.4 tonnes."); add(s); }
  { S s("O7", 3, CT_PAX, "CAP", "FAR", "Far Isle Grand Opening");
    s.load(200, 11).pay(36000).lic(LIC_CPL).owned().wx(W(140, 10, 3, 0.15f, 0.3f, 4500, 40, 0, false, 17.5f))
     .brief("Guests for the Far Isle resort's grand opening. 52 km over open water - golden hour on arrival."); add(s); }
  { S s("O8", 3, CT_PAX, "FAR", "KLO", "Checkride: Airline Transport Pilot");
    s.load(100, 6).pay(18000).lic(LIC_CPL).owned().grant(LIC_ATP).wx(W(110, 14, 8, 0.4f, 0.9f, 1500, 6, 1, false, 21.0f))
     .brief("Your ATP checkride: a night flight to Kaleo in rain and gusty crosswind. Use the runway lights and PAPI."); add(s); }
  // ------------------------------------------------------------ Chapter 4: Airline captain
  { S s("A1", 4, CT_PAX, "KLO", "CAP", "Solace Regional: Launch Day");
    s.load(600, 36).pay(55000).lic(LIC_ATP).wx(W(60, 9, 0, 0.15f, 0.35f, 4500, 40, 0, false, 9.0f))
     .brief("You've founded Solace Regional! Your first scheduled service needs a real airliner - lease a Meridian Q400 until you can afford your own."); add(s); }
  { S s("A2", 4, CT_PAX, "CAP", "FJH", "Northern Service");
    s.load(500, 34).pay(60000).lic(LIC_ATP).wx(W(10, 18, 10, 0.4f, 0.8f, 2500, 6, 2, false, 13.0f))
     .brief("Snow and strong northerly winds at Fjordhaven. The fjord channels the wind straight across the runway."); add(s); }
  { S s("A3", 4, CT_PAX, "FJH", "PVI", "West Coast Express");
    s.load(600, 40).pay(65000).lic(LIC_ATP).wx(W(250, 12, 6, 0.3f, 0.6f, 3000, 20, 1, false, 18.8f))
     .brief("A full cabin to Port Verde across the Spine. Sunset over the sea."); add(s); }
  { S s("A4", 4, CT_VIP, "PVI", "FAR", "The Minister's Jet");
    s.load(150, 5).pay(110000).lic(LIC_ATP).owned().limit(12).wx(W(130, 8, 0, 0.1f, 0.2f, 6000, 50, 0, false, 11.5f))
     .brief("The Minister needs to reach Far Isle in 12 minutes, in comfort. Only a jet will do: the Starling 500."); add(s); }
  { S s("A5", 4, CT_PAX, "FAR", "NPT", "Storm Run");
    s.load(500, 30).pay(85000).lic(LIC_ATP).wx(W(200, 20, 12, 0.6f, 0.95f, 1800, 6, 1, true, 16.0f))
     .brief("Thunderstorms across the islands. Crews are grounded everywhere but you. Get your passengers to Northpoint."); add(s); }
  { S s("A6", 4, CT_TOUR, "NPT", "CAP", "The Grand Tour");
    s.load(100, 6).pay(120000).lic(LIC_ATP).owned().wx(W(270, 6, 0, 0.1f, 0.25f, 9000, 60, 0, false, 17.8f))
     .wp(wpAbs("SMP", 600)).wp(wpAbs("GLS", 700)).wp(wpAbs("VCF", 1700)).wp(wpAbs("GLR", 400))
     .brief("A film crew wants aerial shots of the whole archipelago at golden hour: Summit Pass, the Glacier, Mount Kaleo and Gull Rock, "
            "then land at the Capital."); add(s); }
  { S s("A7", 4, CT_PAX, "CAP", "FAR", "Finale: The Red-Eye");
    s.load(600, 40).pay(150000).lic(LIC_ATP).wx(W(160, 16, 9, 0.5f, 0.85f, 2000, 8, 1, true, 23.0f))
     .brief("The last flight of the night, through a storm, to Far Isle. Bring them home, Captain."); add(s); }
}

bool surfaceOK(const AircraftSpec& s, int surface) {
  if (surface == SURF_ASPHALT) return true;
  if (surface == SURF_GRASS || surface == SURF_SAND) return s.runwayM < 900;
  return s.roughOK;
}
bool runwayOK(const AircraftSpec& s, const Airport& a) { return surfaceOK(s, a.surface) && a.length >= s.runwayNeeded(a.elev); }

static float contractKm(const Contract& c) {
  float km = g_world.distanceKm(c.from, c.to);
  if (!c.wps.empty()) {
    km = 0; float x = g_world.airports[c.from].x, z = g_world.airports[c.from].z;
    for (auto& w : c.wps) { km += sqrtf((w.x - x) * (w.x - x) + (w.z - z) * (w.z - z)) / 1000.f; x = w.x; z = w.z; }
    km += sqrtf((g_world.airports[c.to].x - x) * (g_world.airports[c.to].x - x) + (g_world.airports[c.to].z - z) * (g_world.airports[c.to].z - z)) / 1000.f;
  }
  return km;
}

void Career::newGame() { *this = Career(); location = g_world.findAirport("MDB"); refreshBoard(); }

const Contract* Career::nextStory() const { return storyIndex < (int)g_story.size() ? &g_story[storyIndex] : nullptr; }

int Career::ownedIndexFor(int specIdx) const {
  for (size_t i = 0; i < fleet.size(); i++) if (fleet[i].spec == specIdx) return (int)i;
  return -1;
}

Career::Source Career::canFly(const Contract& c, int si, std::string* why) const {
  const AircraftSpec& s = kAircraft[si];
  auto no = [&](const std::string& w) { if (why) *why = w; return SRC_NONE; };
  if (c.forceAircraft >= 0) return si == c.forceAircraft ? SRC_LESSON : no("Lesson aircraft only");
  if (license < s.license) return no(std::string("Requires ") + licenseName(s.license));
  if (license < c.minLicense) return no(std::string("Contract requires ") + licenseName(c.minLicense));
  if (s.cargoKg < c.cargoKg) return no(fmt("Max cargo %.0f kg", s.cargoKg));
  if (s.pax < c.pax) return no(fmt("Only %d passenger seats", s.pax));
  float km = contractKm(c);
  if (s.rangeKm < km * 1.25f) return no(fmt("Range %.0f km too short (need %.0f km incl. reserve)", s.rangeKm, km * 1.25f));
  for (int ap : {c.from, c.to}) {
    const Airport& a = g_world.airports[ap];
    if (!surfaceOK(s, a.surface)) return no(fmt("Cannot use %s runway at %s", surfaceName(a.surface), a.code));
    if (a.length < s.runwayNeeded(a.elev)) return no(fmt("%s runway too short (%.0f m, needs %.0f m)", a.code, a.length, s.runwayNeeded(a.elev)));
  }
  if (c.timeLimitMin > 0 && km * 1000.f / s.cruise / 60.f > c.timeLimitMin * 0.8f) return no(fmt("Too slow to make the %.0f minute deadline", c.timeLimitMin));
  if (ownedIndexFor(si) >= 0) return SRC_OWNED;
  if (c.ownedOnly) return no("Client requires your own aircraft");
  if (s.rentFee <= 0) return no("Not available for rent - buy one in the Hangar");
  return SRC_RENT;
}

int Career::positioningCost(const Contract& c) const {
  if (c.from == location || c.type == CT_LESSON) return 0;
  if (money < 1500) return 0;  // courtesy ride when broke - never softlock
  return (int)(80 + 6 * g_world.distanceKm(location, c.from));
}

int Career::ferryCost(const Contract& c, int si) const {
  int oi = ownedIndexFor(si);
  if (oi < 0 || fleet[oi].location == c.from) return 0;
  return (int)(150 + 12 * g_world.distanceKm(fleet[oi].location, c.from));
}

void Career::refreshBoard() {
  board.clear();
  Rng r(boardSeed * 2654435761u + location * 97 + 13);
  // aircraft the player can access now
  std::vector<int> access;
  for (int i = 0; i < kNumAircraft; i++)
    if (license >= kAircraft[i].license && license >= LIC_PPL && (kAircraft[i].rentFee > 0 || ownedIndexFor(i) >= 0)) access.push_back(i);
  if (access.empty()) return;
  const char* cargoNames[] = {"Medical supplies", "Mail sacks", "Fresh produce", "Machine parts", "Fishing gear", "Coffee beans", "Newspapers", "Spare tyres", "Wine crates", "Lab samples"};
  const char* paxNames[] = {"Business travellers", "Holiday makers", "Wedding party", "Surveyors", "Film crew", "Tour group", "Island residents", "Students"};
  int tries = 0;
  while ((int)board.size() < 6 && tries++ < 200) {
    int si = access[r.next() % access.size()];
    const AircraftSpec& s = kAircraft[si];
    int to = r.next() % g_world.airports.size();
    if (to == location) continue;
    Contract c; c.from = location; c.to = to; c.id = fmt("F%u_%d", boardSeed, (int)board.size());
    bool pax = s.pax >= 2 && license >= LIC_CPL && (r.next() & 1);
    c.type = pax ? CT_PAX : CT_CARGO;
    if (pax) { c.pax = std::max(1, (int)(s.pax * r.range(0.4f, 1.0f))); c.cargoKg = c.pax * 15; }
    else c.cargoKg = std::max(20, (int)(s.cargoKg * r.range(0.35f, 0.95f)) / 10 * 10);
    c.minLicense = pax ? LIC_CPL : LIC_PPL;
    if (canFly(c, si) == SRC_NONE) continue;
    float km = contractKm(c);
    c.payout = (int)((250 + km * (30 + c.cargoKg * 0.13f + c.pax * 16)) * r.range(0.9f, 1.15f)) / 10 * 10;
    c.fragile = !pax && r.uni() < 0.15f;
    if (r.uni() < 0.15f) { c.timeLimitMin = ceilf(km * 1000.f / s.cruise / 60.f * 1.6f + 3); c.payout = c.payout * 13 / 10; }
    c.title = pax ? fmt("%s to %s", paxNames[r.next() % 8], g_world.airports[to].name) : fmt("%s to %s", cargoNames[r.next() % 10], g_world.airports[to].name);
    c.brief = fmt("Freelance job posted at %s. Distance %.0f km.", g_world.airports[location].name, km);
    float tod = r.range(7.f, 19.5f);
    c.wx = W(r.range(0, 360), r.range(0, 14), r.uni() < 0.3f ? r.range(3, 10) : 0, r.range(0.05f, 0.35f), r.range(0, 0.8f), r.range(2500, 7000), r.range(12, 50),
             r.uni() < 0.15f ? (g_world.airports[to].z < -20000 ? 2 : 1) : 0, false, tod);
    bool dup = false;
    for (auto& b : board) if (b.to == c.to && b.type == c.type) dup = true;
    if (!dup) board.push_back(c);
  }
}

std::vector<PayoutLine> Career::settle(const Contract& c, int si, Source src, const FlightResult& r, int* stars) {
  std::vector<PayoutLine> L;
  const AircraftSpec& s = kAircraft[si];
  int pos = positioningCost(c), ferry = src == SRC_OWNED ? ferryCost(c, si) : 0;
  if (pos) L.push_back({"Positioning ticket to " + std::string(g_world.airports[c.from].code), -pos});
  if (ferry) L.push_back({"Ferry service for your " + std::string(s.name), -ferry});
  if (src == SRC_RENT) L.push_back({"Rental: " + std::string(s.name), -s.rentFee});
  int fuelCost = (int)(r.fuelUsedKg * (s.engineType == ENG_PISTON ? 2.2f : 1.4f));
  if (src == SRC_OWNED && fuelCost) L.push_back({"Fuel", -fuelCost});
  *stars = 0;
  flights++; hours += r.flightMin / 60.f;
  if (!r.success) {
    if (r.failReason.find("Abandon") == std::string::npos) {
      crashes++;
      int repair = src == SRC_RENT ? 300 + s.rentFee * 2 : src == SRC_OWNED ? s.price / 12 : 0;
      if (repair) L.push_back({src == SRC_RENT ? "Insurance deductible" : "Repairs", -repair});
      if (src == SRC_OWNED) { int oi = ownedIndexFor(si); if (oi >= 0) fleet[oi].condition = 1.f; }
    }
    reputation = std::max(0, reputation - 1);
  } else {
    landings++;
    float fpm = fabsf(r.touchdownFpm);
    bestLandingFpm = std::min(bestLandingFpm, fpm);
    float mult = 1.f; int st = 3;
    if (c.payout > 0) L.push_back({"Contract payment", c.payout});
    if (fpm < 150) { L.push_back({"Butter landing bonus", c.payout / 10}); }
    else if (fpm > 600) { L.push_back({"Hard landing", -c.payout / 5}); st--; }
    else if (fpm > 350) { L.push_back({"Firm landing", -c.payout / 20}); }
    if (r.late) { L.push_back({"Late delivery", -c.payout / 2}); st--; }
    if (c.pax > 0 && (r.maxBank > 45 || r.maxG > 1.9f || r.minG < 0.2f)) { L.push_back({"Passenger discomfort", -c.payout * 15 / 100}); st--; }
    if (c.fragile && (r.maxG > 2.0f || r.minG < 0.0f || fpm > 400)) { L.push_back({"Fragile cargo damaged", -c.payout * 4 / 10}); st--; }
    (void)mult;
    *stars = std::max(1, st);
    reputation += *stars;
    if (c.story) {
      storyIndex++;
      if (c.grantLicense > license) { license = c.grantLicense; }
      if (storyIndex >= (int)g_story.size()) finished = true;
    }
    location = c.to;
    if (src == SRC_OWNED) { int oi = ownedIndexFor(si); if (oi >= 0) fleet[oi].location = c.to; }
    boardSeed++;
  }
  int total = 0; for (auto& l : L) total += l.amount;
  money += total;
  refreshBoard();
  return L;
}

bool Career::buy(int si, std::string* msg) {
  const AircraftSpec& s = kAircraft[si];
  if (ownedIndexFor(si) >= 0) { *msg = "You already own one."; return false; }
  if (license < s.license) { *msg = std::string("Requires ") + licenseName(s.license); return false; }
  if (money < s.price) { *msg = fmt("Not enough money ($%d needed)", s.price); return false; }
  money -= s.price;
  fleet.push_back({si, location, s.maxFuel, 0.f});
  *msg = fmt("Purchased %s! It's waiting at %s.", s.name, g_world.airports[location].code);
  refreshBoard();
  return true;
}

bool Career::sell(int fi, std::string* msg) {
  if (fi < 0 || fi >= (int)fleet.size()) return false;
  const AircraftSpec& s = kAircraft[fleet[fi].spec];
  int val = s.price * 7 / 10;
  money += val;
  *msg = fmt("Sold %s for $%d.", s.name, val);
  fleet.erase(fleet.begin() + fi);
  refreshBoard();
  return true;
}

bool Career::save(const std::string& path) const {
  FILE* f = fopen(path.c_str(), "w");
  if (!f) return false;
  fprintf(f, "airxpress_save 1\nmoney %d\nlicense %d\nrep %d\nlocation %d\nstory %d\nflights %d\nlandings %d\ncrashes %d\nhours %f\nbest %f\nseed %u\nfinished %d\n",
          money, license, reputation, location, storyIndex, flights, landings, crashes, hours, bestLandingFpm, boardSeed, finished ? 1 : 0);
  for (auto& p : fleet) fprintf(f, "plane %s %d %f\n", kAircraft[p.spec].id, p.location, p.fuel);
  fclose(f);
  return true;
}

bool Career::load(const std::string& path) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) return false;
  Career c; char key[64];
  if (fscanf(f, "%63s %*d", key) != 1 || strcmp(key, "airxpress_save")) { fclose(f); return false; }
  while (fscanf(f, "%63s", key) == 1) {
    int fin = 0;
    if (!strcmp(key, "money")) (void)!fscanf(f, "%d", &c.money);
    else if (!strcmp(key, "license")) (void)!fscanf(f, "%d", &c.license);
    else if (!strcmp(key, "rep")) (void)!fscanf(f, "%d", &c.reputation);
    else if (!strcmp(key, "location")) (void)!fscanf(f, "%d", &c.location);
    else if (!strcmp(key, "story")) (void)!fscanf(f, "%d", &c.storyIndex);
    else if (!strcmp(key, "flights")) (void)!fscanf(f, "%d", &c.flights);
    else if (!strcmp(key, "landings")) (void)!fscanf(f, "%d", &c.landings);
    else if (!strcmp(key, "crashes")) (void)!fscanf(f, "%d", &c.crashes);
    else if (!strcmp(key, "hours")) (void)!fscanf(f, "%f", &c.hours);
    else if (!strcmp(key, "best")) (void)!fscanf(f, "%f", &c.bestLandingFpm);
    else if (!strcmp(key, "seed")) (void)!fscanf(f, "%u", &c.boardSeed);
    else if (!strcmp(key, "finished")) { (void)!fscanf(f, "%d", &fin); c.finished = fin != 0; }
    else if (!strcmp(key, "plane")) {
      char id[64]; int loc; float fuel;
      if (fscanf(f, "%63s %d %f", id, &loc, &fuel) == 3)
        for (int i = 0; i < kNumAircraft; i++) if (!strcmp(kAircraft[i].id, id)) c.fleet.push_back({i, loc, fuel, 0.f});
    }
  }
  fclose(f);
  c.location = std::clamp(c.location, 0, (int)g_world.airports.size() - 1);
  c.storyIndex = std::clamp(c.storyIndex, 0, (int)g_story.size());
  *this = c;
  refreshBoard();
  return true;
}
