// Somnium Volandi - career progression and hand-designed story campaign
#include "career.h"
#include "entities.h"
#include "profile.h"
#include "military_missions.h"
#include "procedural_regions.h"
#include "earth_charts.h"
#include "region_travel.h"
#include "finite_float.h"
#include <charconv>
#include <cmath>
#include <limits>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
  S& type(const char* spec) { for (int i : kCareerAircraft) if (!strcmp(kAircraft[i].id, spec)) c.requireSpec = i; return *this; }
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
     .brief("Welcome to Meadowbrook Flight Academy! Today you'll fly the Dawnlark trainer for the first time. "
            "Release the brakes, apply full power, rotate gently at 50 knots and climb straight ahead through the rings.")
     .wp(wpRel("MDB", 2.2f, 0, 150)).wp(wpRel("MDB", 4.0f, 0, 300)).wp(wpRel("MDB", 5.5f, -0.6f, 380))
     .hints({"Press {parkingBrake} to release the parking brake, then hold {throttleUp} (or gamepad RT) to add full throttle.",
             "Keep the nose on the centreline with {yawLeft}/{yawRight} rudder. At 50 kt gently pull back ({pitchUp}) to lift off.",
             "Nice! Hold a gentle climb about 7 degrees nose-up. Fly through the green rings.",
             "Reduce power slightly ({throttleDown}) once level and trim with {trimDown} and {trimUp}. Rings show the path.",
             "", "", ""});
    add(s); }
  { S s("L2", 0, CT_LESSON, "MDB", "MDB", "Lesson 2: Traffic Pattern and Landing");
    s.lesson().pay(200).wx(W(40, 6, 0, 0.08f, 0.3f, 4000, 40, 0, false, 10.5f))
     .brief("Fly a full traffic pattern: upwind, crosswind, downwind, base and final, then land back on runway 05. "
            "Aim for a gentle touchdown below 300 feet per minute.")
     .wp(wpRel("MDB", 2.4f, 0, 200)).wp(wpRel("MDB", 2.8f, 1.4f, 420)).wp(wpRel("MDB", 0.0f, 1.8f, 420))
     .wp(wpRel("MDB", -2.4f, 1.8f, 400)).wp(wpRel("MDB", -3.0f, 0.8f, 260)).wp(wpRel("MDB", -2.4f, 0, 165))
     .hints({"Release brakes ({parkingBrake}), full power, and take off as in Lesson 1.",
             "Rudder to stay straight. Rotate at 50 kt.",
             "Climb through the rings. The pattern turns LEFT. Use gentle 20 degree banks.",
             "On downwind reduce power to about 60% and set one notch of flaps ({flapsDown}).",
             "Turn final, add full flaps ({flapsDown}), and follow the PAPI lights: two white, two red is on glidepath.",
             "Reduce power to idle over the threshold, then gently raise the nose to flare just above the runway.",
             "Hold {brake} to brake to a full stop and complete the lesson."});
    add(s); }
  { S s("L3", 0, CT_LESSON, "MDB", "HFS", "Lesson 3: Cross-Country to Harlan Farm");
    s.lesson().pay(300).wx(W(200, 7, 3, 0.15f, 0.35f, 3500, 30, 0, false, 13.0f))
     .brief("Your first cross-country flight. Navigate to Harlan Farm Strip using the GPS arrow and land on its short grass runway. "
            "Grass is slower, so touch down early and brake firmly.")
     .hints({"Release brakes and take off. The magenta arrow on the HUD points to your destination.",
             "Stay on the centreline.",
             "Climb to about 1,500 feet and head toward the arrow. Press {autopilot} for autopilot heading/altitude hold.",
             "Watch your fuel gauge and the distance readout. Press {gpsMap} to view the map.",
             "Harlan Farm runway 17 is short: slow to 60 kt, full flaps, aim for the very start of the strip.",
             "Flare gently and get the wheels down early.",
             "Hold {brake} to brake to a full stop and finish."});
    add(s); }
  { S s("L4", 0, CT_LESSON, "HFS", "ORC", "Checkride: Private Pilot License");
    s.lesson().pay(400).grant(LIC_PPL).wx(W(330, 11, 5, 0.25f, 0.5f, 3000, 25, 0, false, 15.5f))
     .brief("Examiner Rosa Vance will ride with you to Orchard Valley. There's a gusty crosswind today. "
            "Land on the runway without a hard landing (over 600 fpm) and keep to the tower's instructions to earn your Private Pilot License.")
     .hints({"Final exam: take off when ready.", "Crosswind! Use rudder to stay on the centreline.",
             "Climb out and navigate to Orchard Valley.", "Plan your descent early - Orchard Valley sits at 690 ft.",
             "Crab into the wind on final. Just before touchdown, use rudder to align with the runway.",
             "Keep the upwind wing slightly low as you flare.", "Stop on the runway to complete the checkride."});
    add(s); }
  // ------------------------------------------------------------ Chapter 1: Private Pilot (cargo by rental)
  { S s("C1", 1, CT_CARGO, "ORC", "MDB", "Seed Delivery");
    s.load(90, 0).pay(700).lic(LIC_PPL).wx(W(250, 6, 0, 0.1f, 0.25f, 4000, 40, 0, false, 9.0f))
     .brief("The Orchard Valley co-op needs seed sacks flown to Meadowbrook. Rent a Dawnlark - the fee comes out of your pay."); add(s); }
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
     .brief("A replacement generator for the weather station on Gull Rock. Too heavy for the Dawnlark - rent an Ambler 180. "
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
            "Stay below the clouds, follow the GPS, and land safely: no hard landing (over 600 fpm), and keep to the tower's instructions. Pass to earn your Commercial Pilot License."); add(s); }
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
            "Rent the Scrubrunner - a taildragger, so keep the tail down with back pressure and use rudder constantly."); add(s); }
  { S s("P4", 2, CT_MEDEVAC, "SMP", "CAP", "Medevac: Injured Climber");
    s.load(80, 1).pay(5600).lic(LIC_CPL).limit(20).wx(W(260, 16, 9, 0.55f, 0.5f, 6000, 30, 0, false, 10.3f))
     .brief("An injured climber needs a hospital. Get her to Solace Capital within 20 minutes - and gently."); add(s); }
  { S s("P5", 2, CT_PAX, "CAP", "KLO", "Island Hopper");
    s.load(100, 8).pay(6200).lic(LIC_CPL).wx(W(70, 12, 4, 0.2f, 0.4f, 3500, 35, 0, false, 12.0f))
     .brief("Eight tourists to tropical Kaleo. Rent a Skerry Twin for the crossing."); add(s); }
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
            "Time to visit the Hangar and buy your first plane. A Scrubrunner fits the job."); add(s); }
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
     .brief("A huge catch for the capital's morning market. You'll need something bigger - the Longshore Hauler hauls 1.4 tonnes."); add(s); }
  { S s("O7", 3, CT_PAX, "CAP", "FAR", "Far Isle Grand Opening");
    s.load(200, 11).pay(36000).lic(LIC_CPL).owned().wx(W(140, 10, 3, 0.15f, 0.3f, 4500, 40, 0, false, 17.5f))
     .brief("Guests for the Far Isle resort's grand opening. 52 km over open water - golden hour on arrival."); add(s); }
  { S s("O8", 3, CT_PAX, "FAR", "KLO", "Checkride: Airline Transport Pilot");
    s.load(100, 6).pay(18000).lic(LIC_CPL).owned().grant(LIC_ATP).wx(W(110, 14, 8, 0.4f, 0.9f, 1500, 6, 1, false, 21.0f))
     .brief("Your ATP checkride: a night flight to Kaleo in rain and gusty crosswind. Use the runway lights and PAPI. The examiner fails a hard landing (over 600 fpm) or a tower instruction ignored."); add(s); }
  // ------------------------------------------------------------ Chapter 4: Airline captain
  { S s("A1", 4, CT_PAX, "KLO", "CAP", "Solace Regional: Launch Day");
    s.load(600, 36).pay(55000).lic(LIC_ATP).wx(W(60, 9, 0, 0.15f, 0.35f, 4500, 40, 0, false, 9.0f))
     .brief("You've founded Solace Regional! Your first scheduled service needs a real airliner - lease an Archipel 400 until you can afford your own."); add(s); }
  { S s("A2", 4, CT_PAX, "CAP", "FJH", "Northern Service");
    s.load(500, 34).pay(60000).lic(LIC_ATP).wx(W(10, 18, 10, 0.4f, 0.8f, 2500, 6, 2, false, 13.0f))
     .brief("Snow and strong northerly winds at Fjordhaven. The fjord channels the wind straight across the runway."); add(s); }
  { S s("A3", 4, CT_PAX, "FJH", "PVI", "West Coast Express");
    s.load(600, 40).pay(65000).lic(LIC_ATP).wx(W(250, 12, 6, 0.3f, 0.6f, 3000, 20, 1, false, 18.8f))
     .brief("A full cabin to Port Verde across the Spine. Sunset over the sea."); add(s); }
  { S s("A4", 4, CT_VIP, "PVI", "FAR", "The Minister's Jet");
    s.load(150, 5).pay(110000).lic(LIC_ATP).owned().type("emissary").limit(12).wx(W(130, 8, 0, 0.1f, 0.2f, 6000, 50, 0, false, 11.5f))
     .brief("The Minister needs to reach Far Isle in 12 minutes, in comfort. Only a jet will do: the Emissary 500. The autopilot's full arrival takes too long: fly the approach straight in."); add(s); }
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

std::vector<Contract> Career::legendaryContracts() {
  std::vector<Contract> L;
  auto add = [&](S& s) { s.c.story = false; s.c.chapter = 5; L.push_back(s.c); };
  { S s("LEG1", 5, CT_CARGO, "MDB", "FAR", "Legend: The Island Marathon");
    s.load(300, 0).pay(65000).lic(LIC_ATP).type("emissary").wx(W(240, 12, 4, 0.25f, 0.4f, 6500, 40, 0, false, 9.0f))
     .wp(wpAbs("LHK", 600)).wp(wpAbs("NPT", 600)).wp(wpAbs("GLS", 700)).wp(wpAbs("VCF", 1800))
     .brief("The islands end to end in one flight: over Lighthouse Key, Northpoint, the Glacier and Mount Kaleo, then down at Far Isle. "
            "The longest flight in the islands, at full speed in the Emissary."); add(s); }
  { S s("LEG2", 5, CT_CARGO, "CDR", "SMP", "Legend: Summit Pass in the Snow");
    s.load(250, 0).pay(45000).lic(LIC_ATP).type("scrubrunner").wx(W(320, 14, 8, 0.4f, 0.8f, 9000, 8, 2, false, 13.0f))
     .brief("The pass is cut off by snow and the hut needs supplies. Six hundred metres of gravel at 5,400 ft, a gusting crosswind and "
            "falling snow: only the Scrubrunner can do it, and only just."); add(s); }
  { S s("LEG3", 5, CT_MEDEVAC, "GLS", "CAP", "Legend: The Midnight Medevac");
    s.load(60, 1).pay(70000).lic(LIC_ATP).type("skerry").limit(45).wx(W(200, 16, 9, 0.45f, 0.85f, 2500, 10, 1, true, 0.5f))
     .brief("A climber is hurt at Glacier Station, and the storm has grounded the rescue helicopter. Get the patient to the hospital at "
            "the Capital inside 45 minutes, gently, at night, through the weather."); add(s); }
  { S s("LEG4", 5, CT_PAX, "PMB", "VCF", "Legend: Into the Crater");
    s.load(120, 6).pay(55000).lic(LIC_ATP).type("skerry").wx(W(90, 18, 10, 0.6f, 0.45f, 7000, 25, 0, false, 15.0f))
     .brief("Six volcanologists for the crater field on Mount Kaleo: a short strip in the caldera, the mountain's own wind tumbling over "
            "the rim. Come in steady and land in the first third."); add(s); }
  { S s("LEG5", 5, CT_PAX, "CAP", "PVI", "Legend: The Galleon Night Run");
    s.load(2800, 170).pay(260000).lic(LIC_ATP).type("galleon_180").wx(W(150, 14, 7, 0.35f, 0.9f, 1200, 6, 1, false, 23.5f))
     .brief("A full Galleon 180 to Port Verde, at midnight, in rain under a low ceiling. One hundred and seventy passengers who would like to "
            "sleep through the landing."); add(s); }
  return L;
}

// the flight-time estimate's weights: a constant (taxi, takeoff, landing roll) and the en-route, climb, orbit and
// approach parts (Career::plan)
// (fitted to 53 autopilot flights of the story and freelance jobs: on 35 other freelance flights the median error is
// 10% and three in four are within 15%. The orbit's own term fitted to zero: its time is already in the approach)
float kEstK[5] = {1.24f, 1.03f, 0.43f, 0.f, 1.21f};

const Airport& arrivalAirport(const Contract& c) {
  if (c.longHaul()) if (const Airport* a = procedural::airportOf(c.toRegion, c.to)) return *a;
  return g_world.airports[std::clamp(c.to, 0, (int)g_world.airports.size() - 1)];
}
geo::Position airportPlace(const std::string& region, const Airport& a) {
  const auto d = procedural::find(region);
  return regionTravel::Chart(d ? d->origin : geo::solaceOrigin()).position(vec3(a.x, 0.f, a.z));
}

static float contractKm(const Contract& c) {
  if (c.longHaul()) return c.haulKm;   // (across the ocean: the great circle)
  float km = g_world.distanceKm(c.from, c.to);
  if ((size_t)c.wpStart < c.wps.size()) {   // (the checkpoints still to fly)
    km = 0; float x = g_world.airports[c.from].x, z = g_world.airports[c.from].z;
    for (size_t i = c.wpStart; i < c.wps.size(); i++) { const Waypoint& w = c.wps[i]; km += sqrtf((w.x - x) * (w.x - x) + (w.z - z) * (w.z - z)) / 1000.f; x = w.x; z = w.z; }
    km += sqrtf((g_world.airports[c.to].x - x) * (g_world.airports[c.to].x - x) + (g_world.airports[c.to].z - z) * (g_world.airports[c.to].z - z)) / 1000.f;
  }
  return km;
}

void Career::newGame() {
  *this = Career();
  for (int i = 0; i < solaceAirportCount(); ++i) if (!strcmp(solaceAirport(i).code, "MDB")) location = i;   // (whatever g_world is)
  realEarth = earthCharts::pack() != nullptr;   // (beyond the islands: the real Earth, when its pack is there)
  refreshBoard();
}

bool Career::chooseWorld(bool real, std::string* msg) {
  auto no = [&](const std::string& why) { if (msg) *msg = why; return false; };
  if (real == realEarth) return true;
  if (real && !earthCharts::pack()) return no("The real Earth needs its pack in an earth folder beside the game.");
  if (!atHome() || job || attemptOpen) return no("Change the world beyond the islands from home, with no job open.");
  for (const auto& p : fleet) if (p.region != "solace") return no("Bring your aircraft home first: one is parked beyond the islands.");
  realEarth = real; board.clear(); refreshBoard();
  if (msg) *msg = real ? "Beyond the islands: the real Earth's airports." : "Beyond the islands: the generated regions.";
  return true;
}

// ------------------------------------------------------------------ travel between the regions
std::vector<Career::Destination> Career::destinations() const {
  std::vector<Destination> out;
  const auto at = procedural::find(region);
  const geo::Position from = at ? at->origin : geo::solaceOrigin();
  if (realEarth) {   // the real Earth: the airports an airline flies to - scheduled service, a medium or large field - the
                     // nearest within 3,000 km, each at its own chart (the islands always among them when away)
    if (auto pack = earthCharts::pack()) {
      const double reach = 3000.0, dLat = reach / 111.32, dLon = dLat / std::max(0.05, std::cos(from.latitude * geo::kDegrees));
      const earth::Box box = dLon >= 180.0 ? earth::Box{std::max(-90.0, from.latitude - dLat), -180, std::min(90.0, from.latitude + dLat), 180} :
                             earth::Box{std::max(-90.0, from.latitude - dLat), from.longitude - dLon, std::min(90.0, from.latitude + dLat), from.longitude + dLon};
      std::vector<std::pair<double, earth::AirportRecord>> inReach;   // (not "near": windows.h makes that a macro)
      for (auto& a : pack->airports(box)) {
        if (a.type < 1 || !a.scheduled || a.runways.empty()) continue;
        const double km = geo::surfaceDistance(from, {a.lat, a.lon, 0}) / 1000.;
        if (km >= 30. && km <= reach) inReach.push_back({km, std::move(a)});
      }
      std::sort(inReach.begin(), inReach.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first < b.first : a.second.ident < b.second.ident; });
      for (const auto& [km, a] : inReach) {
        if (out.size() >= 12) break;
        const auto chart = earthCharts::chartForAirport(a.ident);
        if (!chart || chart->id == region) continue;
        int index = -1;
        for (int i = 0, n = procedural::airportCount(chart->id); i < n && index < 0; i++)
          if (const Airport* p = procedural::airportOf(chart->id, i); p && a.ident == p->code) index = i;
        if (index < 0) continue;
        bool dup = false;
        for (const Destination& o : out) dup = dup || (o.id == chart->id && o.airport == index);
        if (dup) continue;
        Destination t; t.id = chart->id; t.airport = index; t.km = km;
        t.name = a.name + (a.municipality.empty() ? "" : ", " + a.municipality);
        t.fare = (int)(120. + 0.45 * t.km) / 10 * 10;
        t.minLicense = t.km <= 450. ? LIC_PPL : t.km <= 900. ? LIC_CPL : LIC_ATP;
        out.push_back(t);
      }
    }
    if (!atHome()) {
      Destination t; t.id = "solace"; t.name = "The Solace Islands"; t.home = true;
      t.km = geo::surfaceDistance(from, geo::solaceOrigin()) / 1000.; t.fare = (int)(120. + 0.45 * t.km) / 10 * 10; t.minLicense = LIC_STUDENT;
      out.push_back(t);
    }
    std::stable_sort(out.begin(), out.end(), [](const Destination& a, const Destination& b) { return a.home != b.home && a.home; });
    return out;
  }
  auto add = [&](const procedural::Descriptor& d) {
    if (d.id == region) return;
    for (const Destination& o : out) if (o.id == d.id) return;
    Destination t; t.id = d.id; t.name = d.name; t.home = d.authored;
    t.km = geo::surfaceDistance(from, d.origin) / 1000.;
    t.fare = (int)(120. + 0.45 * t.km) / 10 * 10;   // (about $210 to the nearest, $800 across 1,500 km)
    t.minLicense = t.home ? LIC_STUDENT : t.km <= 450. ? LIC_PPL : t.km <= 900. ? LIC_CPL : LIC_ATP;
    out.push_back(t);
  };
  for (const auto& d : procedural::nearby(from, 12)) add(d);
  if (!atHome()) add(procedural::solaceDescriptor());   // (the way home, however far)
  std::sort(out.begin(), out.end(), [](const Destination& a, const Destination& b) { return a.home != b.home ? a.home : a.km != b.km ? a.km < b.km : a.id < b.id; });
  return out;
}
bool Career::travel(const Destination& d, std::string* msg) {
  auto no = [&](const std::string& why) { if (msg) *msg = why; return false; };
  if (job || attemptOpen) return no("Finish or release the open job before you travel.");
  if (d.id == region) return no("You are already here.");
  const auto to = procedural::find(d.id);
  if (!to) return no("No airline flies there.");
  const int main = procedural::mainAirport(d.id);
  if (main < 0) return no("No airline flies there.");
  if (!to->authored && license < std::max(d.minLicense, (int)LIC_PPL)) return no(std::string("The flight there takes a ") + licenseName(std::max(d.minLicense, (int)LIC_PPL)));
  const bool credit = to->authored && money < d.fare;   // (home is always within reach: never stranded)
  if (!credit && money < d.fare) return no(fmt("The ticket is $%d.", d.fare));
  if (!credit) money -= d.fare;
  const int arrive = d.airport >= 0 && d.airport < procedural::airportCount(d.id) ? d.airport : main;
  region = d.id; location = arrive; board.clear();
  const std::string where = to->authored ? std::string("home to the Solace Islands") : earthCharts::isEarthId(d.id) ? "to " + d.name : "to the " + to->name;
  if (msg) *msg = credit ? "The airline flew you home on account: the ticket's on us." : fmt("Flown %s for $%d.", where.c_str(), d.fare);
  return true;
}

bool Career::militaryMissionCompleted(int kind) const {
  return (military.completedMissions & military::completionBit(kind)) != 0;
}
bool Career::militaryMissionUnlocked(int kind) const {
  if (!military::mission(kind)) return false;
  if (kind == military::LegacyRecon || militaryMissionCompleted(kind)) return true;
  // Require the complete prefix, not just the previous mission: an already accepted legacy
  // sortie can finish out of order, but its recorded success must not open a later shortcut.
  for (int predecessor : military::kBoardMissionIds) {
    if (predecessor == kind) return true;
    if (!militaryMissionCompleted(predecessor)) return false;
  }
  return false;
}

Contract Career::militaryContract(int kind) const {
  Contract c;
  const auto* mission = military::mission(kind);
  if (!mission || location < 0 || location >= (int)g_world.airports.size()) {
    c.id = "MIL_INVALID"; c.type = CT_MIL_RECON; c.forceAircraft = -1; return c;
  }
  c.id = fmt("MIL_%d_%u_%d", kind, boardSeed, location);
  c.title = mission->title; c.brief = mission->brief; c.type = mission->contractType;
  c.from = c.to = location; c.forceAircraft = mission->aircraft;
  c.startAirborne = true; c.courtesy = true; c.minLicense = LIC_STUDENT;
  c.payout = mission->reward;
  c.wx = W(45.f, 4.f, 0.f, 0.05f, 0.15f, 6500.f, 40.f, 0, false, 12.f);
  return c;
}
bool Career::beginMilitary(const Contract& c) {
  if (!isMilitaryContract(c.type) || military.activeAttempt || attemptOpen || attempt == std::numeric_limits<uint32_t>::max()) return false;
  if (!militaryMissionUnlocked(c.type - CT_MIL_RECON)) return false;
  if (canFly(c, c.forceAircraft) != SRC_MILITARY) return false;
  military.activeAttempt = ++attempt; attemptOpen = true;
  military.activeKind = c.type - CT_MIL_RECON; military.activeAirport = c.from;
  military.missionSeed = boardSeed ^ (attempt * 0x9e3779b9u) ^ uint32_t(military.activeKind + 1);
  return true;
}
void Career::abandonMilitaryAttempt() {
  if (!military.activeAttempt) return;
  military.lastSettledAttempt = military.activeAttempt;
  military.activeAttempt = 0; military.activeKind = military.activeAirport = -1; military.missionSeed = 0;
  attemptOpen = false;
}

const Contract* Career::nextStory() const { return storyIndex < (int)g_story.size() ? &g_story[storyIndex] : nullptr; }

int Career::ownedIndexFor(int specIdx) const {
  for (size_t i = 0; i < fleet.size(); i++) if (fleet[i].spec == specIdx) return (int)i;
  return -1;
}

static float routeTopOf(const Contract& c);
Career::Source Career::canFly(const Contract& c, int si, std::string* why) const {
  auto no = [&](const std::string& w) { if (why) *why = w; return SRC_NONE; };
  if (si < 0 || si >= kAircraftCount) return no("Unknown aircraft");
  if (isMilitaryContract(c.type)) {
    const int kind = c.type - CT_MIL_RECON;
    const auto* mission = military::mission(kind);
    if (!mission || c.id != fmt("MIL_%d_%u_%d", kind, boardSeed, c.from)) return no("Invalid military mission identity");
    const int expected = mission->aircraft;
    if (c.forceAircraft != expected || si != expected) return no("Use the supplied mission aircraft");
    if (c.from < 0 || c.from >= (int)g_world.airports.size() || c.to != c.from) return no("Invalid military staging location");
    // Preserve the exact accepted token from older saves. This exception can finish that
    // sortie only; beginMilitary still gates every new attempt on ordered progression.
    const bool acceptedAttempt = attemptOpen && military.activeAttempt && military.activeAttempt == attempt &&
      military.activeAttempt > military.lastSettledAttempt && military.activeKind == kind && military.activeAirport == c.from;
    if (!militaryMissionUnlocked(kind) && !acceptedAttempt) return no("CLASSIFIED: complete the preceding missions first");
    return SRC_MILITARY;
  }
  if (!isCareerAircraft(si)) return no("Not available in the career fleet");
  const AircraftSpec& s = kAircraft[si];
  if (c.forceAircraft >= 0) return si == c.forceAircraft ? SRC_LESSON : no("Lesson aircraft only");
  if (c.requireSpec >= 0 && si != c.requireSpec) return no(fmt("Client requires the %s", kAircraft[c.requireSpec].name));
  if (license < s.license) return no(std::string("Requires ") + licenseName(s.license));
  if (license < c.minLicense) return no(std::string("Contract requires ") + licenseName(c.minLicense));
  if (s.cargoKg < c.cargoKg) return no(fmt("Max cargo %.0f kg", s.cargoKg));
  if (s.pax < c.pax) return no(fmt("Only %d passenger seats", s.pax));
  float km = contractKm(c);
  float needKm;
  if (c.longHaul()) {   // across the ocean: the climb, the arrival and a quarter in reserve, with no field on the way
    if (job) return no("Finish or release the open job first");
    if (!procedural::airportOf(c.toRegion, c.to)) return no("Unknown destination");
    needKm = (km + 12.f) * 1.25f;
  } else {
    // the route plus the arrival (descent, intercept and final: ~6 km) and the climb over the highest ground on the way
    // (~8 km of cruise fuel per 1000 m), with a 20% reserve
    float top = std::max(0.f, routeTopOf(c) + 350.f - g_world.airports[c.from].elev);   // (along the checkpoints too)
    needKm = (km + 6.f + top * 0.008f) * 1.2f;
  }
  if (s.rangeKm < needKm) return no(fmt("Range %.0f km too short (need %.0f km incl. approach and reserve)", s.rangeKm, needKm));
  const Airport* ends[] = {&g_world.airports[c.from], &arrivalAirport(c)};
  for (const Airport* ap : ends) {
    const Airport& a = *ap;
    if (!surfaceOK(s, a.surface)) return no(fmt("Cannot use %s runway at %s", surfaceName(a.surface), a.code));
    if (a.length < s.runwayNeeded(a.elev)) return no(fmt("%s runway too short (%.0f m, needs %.0f m)", a.code, a.length, s.runwayNeeded(a.elev)));
  }
  if (c.timeLimitMin > 0 && km * 1000.f / Plane::perf(&s).cruiseV / 60.f > c.timeLimitMin * 0.8f) return no(fmt("Too slow to make the %.0f minute deadline", c.timeLimitMin));
  if (const int oi = ownedIndexFor(si); oi >= 0 && here(fleet[oi])) { if (routeOf(oi) >= 0) return no("On an airline route - recall it in the Airline tab"); return SRC_OWNED; }
  if (c.ownedOnly) return no("Client requires your own aircraft");
  if (s.rentFee <= 0) return no("Not available for rent - buy one in the Hangar");
  return SRC_RENT;
}

int Career::positioningCost(const Contract& c) const {
  if (isMilitaryContract(c.type) || c.from == location || c.type == CT_LESSON || c.courtesy) return 0;
  if (money < 1500) return 0;  // courtesy ride when broke - never softlock
  return (int)(80 + 6 * g_world.distanceKm(location, c.from));
}

// The route the flight takes: departure, the checkpoints still to fly, the destination
static std::vector<vec3> routePoints(const Contract& c) {
  std::vector<vec3> r; r.push_back(g_world.airports[c.from].pos());
  for (size_t i = c.wpStart; i < c.wps.size(); i++) r.push_back(vec3(c.wps[i].x, 0, c.wps[i].z));
  r.push_back(g_world.airports[c.to].pos());
  return r;
}
// the highest ground under the route (sampled every ~500 m along each leg, checkpoints included)
static float routeTop(const std::vector<vec3>& r) {
  float top = 0;
  for (size_t k = 0; k + 1 < r.size(); k++) {
    int n = std::max(2, (int)(length(r[k + 1] - r[k]) / 500.f));
    for (int i = 0; i <= n; i++) { vec3 q = r[k] + (r[k + 1] - r[k]) * (i / (float)n); top = std::max(top, g_world.height(q.x, q.z)); }
  }
  return top;
}

// The time estimate follows the autopilot's own plan for the arrival (Plane::apPlan: the runway end, the descent
// orbit, the intercept and the final), as a player on the autopilot flies it: en route at 0.85 x cruise with the wind
// along each leg, the climb to the cruise level (clear of the route's high ground) at the career autopilot's gentle
// climb rate, the height still to lose in the orbit when the route is too short to descend on the way, and the
// approach from the orbit down the final. The weights are fitted to autopilot flights of the story and freelance
// jobs (tests/gameplay_test.cpp checks A4): flight time within about 15% (minutesSigma).
static float routeTopOf(const Contract& c) { return routeTop(routePoints(c)); }

Career::LaunchPlan Career::plan(const Contract& c, int si, Source src) const {
  const AircraftSpec& s = kAircraft[si];
  LaunchPlan e; e.spec = si; e.src = src; e.startAirport = c.from;
  if (isMilitaryContract(c.type)) {
    e.src = src == SRC_MILITARY ? canFly(c, si) : SRC_NONE;
    e.fuel = LaunchPlan::FUEL_INCLUDED; e.fuelLoadKg = s.maxFuel;
    e.minutesEst = 12.f; e.minutesSigma = 4.f; e.net = e.src == SRC_MILITARY ? c.payout : 0;
    e.challenge = "Mission objectives and extraction required; supplied aircraft and fuel; service credits only";
    return e;
  }
  e.positioning = positioningCost(c);
  e.ferry = src == SRC_OWNED ? ferryCost(c, si) : 0;
  e.hire = src == SRC_RENT ? s.rentFee : 0;
  e.fuel = src == SRC_OWNED ? LaunchPlan::FUEL_PURCHASED : LaunchPlan::FUEL_INCLUDED;
  if (c.longHaul()) {   // the quick estimate along the great circle: a climb of 1,200 m, the cruise, an arrival as any field's
    const PerfModel& P = Plane::perf(&s);
    const float climbRate = std::max(std::min(P.roc * 1.1f, std::max(0.6f * P.roc, 1.5f)), 0.5f);
    e.tCruise = c.haulKm * 1000.f / std::max(s.cruise * 0.85f, 1.f) / 60.f;
    e.tClimb = 1200.f / climbRate / 60.f; e.tOrbit = 0.f; e.tApproach = 6.f;
    e.minutesEst = kEstK[0] + kEstK[1] * e.tCruise + kEstK[2] * e.tClimb + kEstK[4] * e.tApproach;
    e.flownFailed = true;   // (never flown in the background: the quick estimate is the quote)
    finishPlan(e, c, e.minutesEst);
    return e;
  }
  const Airport& A = g_world.airports[c.from];
  const Airport& B = g_world.airports[c.to];
  std::vector<vec3> route = routePoints(c);
  // the autopilot's arrival plan, from the last leg's start
  Plane pl;
  vec3 last = route[route.size() - 2];
  vec3 st = route.size() > 2 ? last : A.pos();
  st.y = std::max(g_world.height(st.x, st.z), A.elev) + 150.f;
  vec3 dir = B.pos() - st; dir.y = 0;
  pl.reset(&s, st, atan2f(dir.x, -dir.z) / DEG, s.maxFuel * 0.7f, 100, true, s.cruise * 0.8f);
  pl.apComfort = c.gentle(); pl.apTrees = false;   // (an estimate, never flown: the flown quote replaces it)
  pl.apEngage(Plane::AP_NAV, c.to, c.wx);
  const PerfModel& P = Plane::perf(&s);
  vec3 C = pl.apHoldC; C.y = 0;
  float R = pl.apHoldR, vh = std::max(s.vref * 1.45f, std::min(s.cruise * 0.6f, s.vref * 1.8f));
  // en route: every leg, the last one to the orbit
  route.back() = C;
  float wf = c.wx.windFrom * DEG;
  vec3 wind = vec3(-sinf(wf), 0, cosf(wf)) * c.wx.windSpeed * 0.8f;   // (the wind aloft the route sees, on average)
  float tEn = 0, dist = 0;
  for (size_t k = 0; k + 1 < route.size(); k++) {
    vec3 d = route[k + 1] - route[k]; d.y = 0; float L = length(d);
    if (k + 2 == route.size()) L = std::max(L - R, 0.f);
    if (L < 1.f) continue;
    float gs = std::max(s.cruise * 0.85f + dot(wind, d / length(d)), s.cruise * 0.4f);
    tEn += L / gs; dist += L;
  }
  e.tCruise = tEn / 60.f;
  // the climb to the cruise level (as Plane::apEngage sets it) at the comfort climb rate
  float cruiseAlt = std::max(std::max(routeTop(routePoints(c)) + 350.f, pl.apHoldAlt), A.elev + 150.f);
  float climbRate = std::max(std::min(P.roc * 1.1f, std::max(0.6f * P.roc, 1.5f)), 0.5f);
  e.tClimb = std::max(cruiseAlt - (A.elev + 150.f), 0.f) / climbRate / 60.f;
  // height still to lose at the orbit: the en-route descent profile runs at 6% (Plane::apGuidance)
  float atOrbit = std::min(cruiseAlt, pl.apHoldAlt + dist * 0.06f);
  e.tOrbit = std::max(atOrbit - pl.apHoldAlt - 60.f, 0.f) / std::max(vh * 0.1f, 3.f) / 60.f;
  // the approach: out of the orbit to the intercept, the intercept leg and the final
  vec3 ld = pl.apRev ? B.dir() * -1.f : B.dir();
  vec3 td = B.threshold(pl.apRev); td.y = 0;
  vec3 qi = td - ld * (pl.apFinalLen + 1500.f);
  float app = length(C - qi) + R * 1.6f + 1500.f;
  e.tApproach = (app / vh + pl.apFinalLen / (s.vref * 1.15f)) / 60.f;
  e.minutesEst = kEstK[0] + kEstK[1] * e.tCruise + kEstK[2] * e.tClimb + kEstK[3] * e.tOrbit + kEstK[4] * e.tApproach;
  finishPlan(e, c, e.minutesEst);
  return e;
}

// the plan's time made the flown one (simulateFlightMinutes), and everything that follows from the time
void Career::useFlownTime(LaunchPlan& e, const Contract& c, float minutes, float fuelKg) const {
  if (minutes > 0) { e.flown = true; finishPlan(e, c, minutes, fuelKg); }
  else e.flownFailed = true;
}

// Legacy non-jet quick estimate. These engines burn Plane::fuelFlowMax at full power and 20% at idle, so a
// flight's fuel is its minutes at each share of full power: the climb at full power, en route at the share this type's
// cruise on the autopilot (0.85 x cruise) takes - from where that speed sits against the speed 75% power holds
// (PerfModel::cruiseV), measured level on the autopilot for every type: 1.25 r - 0.37, within 0.02 - and the take-off and
// the approach at the shares fitted to 355 autopilot flights of the story and freelance jobs in every ordinary type.
// That comes to the burn within about 20% either way (the time estimate's own spread), so the quote is 15% above it:
// nine flights in ten burn less than it says. (It was the flight's minutes at 84% of full power, which took the
// engines to run near full power all the way: on average they run at 44-62%, and the quote was 1.4 to 2 times the burn
// - a 16 minute checkride in a 70 kg trainer was quoted 78 kg and told to expect to run dry, and it burns 43.)
static float cruisePowerShare(const AircraftSpec& s) {
  const float r = 0.85f * s.cruise / std::max(Plane::perf(&s).cruiseV, 1.f);
  return clampf(1.25f * r - 0.37f, 0.3f, 1.f);
}
static float fullPowerFlow(const AircraftSpec& s) { Plane p; p.spec = &s; return p.fuelFlowMax(); }
// Low-level local missions do not get the high-altitude brochure fuel economy.
// Use the flight model's same TSFC curve for each phase; the background flown
// quote replaces this approximation with actual integrated kilograms when ready.
static float jetMissionFuel(const AircraftSpec& s, float cruiseMinutes, float climbMinutes, float approachMinutes) {
  Plane p; p.spec = &s; p.soundSpeed = isa(1500.f).a;
  const float rho = isaDensity(1500.f);
  const float cruiseSpool = clampf((cruisePowerShare(s) - .2f) / .8f, .1f, .85f);
  const float takeoff = p.fuelFlowAt(.65f, s.vref * 1.5f, 1.225f) * kEstK[0];
  const float cruise = p.fuelFlowAt(cruiseSpool, .85f * s.cruise, rho) * kEstK[1] * cruiseMinutes;
  const float climb = p.fuelFlowAt(.85f, s.vref * 1.8f, rho) * kEstK[2] * climbMinutes;
  const float approach = p.fuelFlowAt(.32f, s.vref * 1.35f, rho) * kEstK[4] * approachMinutes;
  return (takeoff + cruise + climb + approach) * 60.f * 1.15f;
}
static float quickFuelKg(const AircraftSpec& s, const Career::LaunchPlan& e) {
  if (s.engineType == ENG_JET) return jetMissionFuel(s, e.tCruise, e.tClimb, e.tApproach);
  const float powerMinutes = 0.69f + cruisePowerShare(s) * kEstK[1] * e.tCruise + 0.24f * e.tClimb + 0.48f * e.tApproach;
  return fullPowerFlow(s) * 60.f * powerMinutes * 1.15f;
}

void Career::finishPlan(LaunchPlan& e, const Contract& c, float minutes, float fuelKg) const {
  const AircraftSpec& s = kAircraft[e.spec];
  const Airport& B = arrivalAirport(c);
  e.minutesEst = minutes;
  e.minutesSigma = 0.15f * e.minutesEst;
  // (flown in the background: what the autopilot burned; else the quick estimate's, above)
  e.fuelKgEst = fuelKg >= 0 ? fuelKg : quickFuelKg(s, e);
  if (e.fuel == LaunchPlan::FUEL_PURCHASED) {   // bought at the departure: the estimate's fuel with a quarter to spare, less what the tanks hold
    int oi = ownedIndexFor(e.spec);
    float have = oi >= 0 ? fleet[oi].fuel : 0.f;
    e.fuelUpliftKg = std::max(0.f, std::min(e.fuelKgEst * 1.25f, s.maxFuel) - have);
    e.fuelCostEst = (int)(e.fuelUpliftKg * fuelPrice(c.from, e.spec));
  } else e.fuelCostEst = e.fuel == LaunchPlan::FUEL_BILL_CONSUMED ? (int)(e.fuelKgEst * s.fuelPriceBase()) : 0;
  e.net = c.payout - e.fees() - e.fuelCostEst;
  // the one thing most likely to cost stars or the job
  float need = s.runwayNeeded(B.elev);
  float wkt = c.wx.windSpeed * MS_TO_KT, xw = 0;
  { float d = (c.wx.windFrom - B.heading) * DEG; xw = fabsf(sinf(d)) * (wkt + c.wx.gust * MS_TO_KT); }
  if (c.crossCountry()) e.challenge = fmt("Cross-country: %.0f km to %s, beyond this chart - find the field on the GPS", c.haulKm, B.name);
  else if (c.longHaul()) e.challenge = earthCharts::isEarthId(c.toRegion) ? fmt("Long haul: %.0f km to %s - carry the fuel", c.haulKm, B.name) :
                                       fmt("Long haul: %.0f km across the ocean to %s - no field on the way, so carry the fuel", c.haulKm, B.name);
  else if (c.timeLimitMin > 0 && e.mayBeLate(c.timeLimitMin)) e.challenge = fmt("May miss the deadline: %.0f min for %.0f +- %.0f min of flying", c.timeLimitMin, e.minutesEst, e.minutesSigma);
  else if (c.timeLimitMin > 0 && e.minutesEst > c.timeLimitMin * 0.75f) e.challenge = fmt("Tight deadline: %.0f min for about %.0f min of flying", c.timeLimitMin, e.minutesEst);
  else if (c.wx.storm) e.challenge = "Thunderstorms on the route";
  else if (c.wxShift && fabsf(wrapAngle((c.wxEnd.windFrom - c.wx.windFrom) * DEG)) / DEG > 100.f && c.wxEnd.windSpeed * MS_TO_KT >= 8.f) e.challenge = fmt("Wind shift forecast: %03.0f at %.0f kt by arrival - expect the other runway", wrapDeg360(c.wxEnd.windFrom), c.wxEnd.windSpeed * MS_TO_KT);
  else if (c.wxShift && c.wxEnd.cloudBase < 700.f && c.wxEnd.cloudCover > 0.6f) e.challenge = fmt("Weather closing in: cloud base %.0f ft by arrival", c.wxEnd.cloudBase * M_TO_FT);
  else if (B.length < need * 1.25f) e.challenge = fmt("Short runway at %s: %.0f m for the %.0f m you need", B.code, B.length, need);
  else if (xw >= 12.f) e.challenge = fmt("Crosswind at %s: about %.0f kt", B.code, xw);
  else if (c.type == CT_MEDEVAC) e.challenge = "Medevac: under 1.5 g and 30 deg of bank, a soft touchdown, and the clock";
  else if (c.type == CT_VIP) e.challenge = "VIP: a limousine ride - gentle banks, no bumps, a soft touchdown";
  else if (c.type == CT_NIGHT) e.challenge = "Night: lit fields, landing light on for the touchdown";
  else if (c.type == CT_IFR) e.challenge = fmt("Low visibility: cloud base %.0f ft, %.1f km - lined up when you break out, or go around", (c.wx.cloudBase - B.elev) * M_TO_FT, c.wx.visibility / 1000.f);
  else if (c.type == CT_SURVEY) e.challenge = fmt("Survey: six checkpoints at %.0f ft, held within 150 ft", c.wps.empty() ? 0.f : c.wps[0].alt * M_TO_FT);
  else if (c.fragile) e.challenge = "Fragile cargo: gentle manoeuvres and a soft landing";
  else if (c.pax > 0) e.challenge = "Passengers: keep the bank under 45 degrees and the ride smooth";
  else if (B.surface != SURF_ASPHALT) e.challenge = fmt("%s strip at %s", surfaceName(B.surface), B.code);
  else if (c.timeLimitMin > 0) e.challenge = fmt("Deadline: %.0f minutes", c.timeLimitMin);
  else if (c.wx.visibility < 8000.f) e.challenge = "Low visibility";
  else e.challenge = "Straightforward";
}

float simulateFlightMinutes(const Contract& c, int si, float* fuelKgOut, bool climbOutOnly) {
  const AircraftSpec& s = kAircraft[si];
  const Airport& a = g_world.airports[c.from];
  Weather wx = c.wx;
  float h0 = a.heading;
  bool reverse = false;
  if (c.type != CT_LESSON || c.wps.empty()) {
    float hw0 = cosf((wx.windFrom - h0) * DEG), hw1 = cosf((wx.windFrom - h0 - 180.f) * DEG);
    reverse = hw1 > hw0;
  }
  vec3 start = a.threshold(reverse) + (reverse ? -a.dir() : a.dir()) * 30.f;
  const float payload = (float)c.cargoKg + c.pax * 85.f + 85.f;
  const float initialFuel = s.engineType == ENG_JET ? s.maxFuelForPayload(payload) : s.maxFuel;
  Plane p; p.reset(&s, start, reverse ? h0 + 180.f : h0, initialFuel, payload, c.startAirborne, s.cruise);
  // (the trees on the finals from scenery of its own: the quote is flown on a worker thread, and the game's scenery
  // is the game thread's)
  Scenery obstacles(0);
  p.apComfort = c.gentle(); p.sceneryHits = false; p.obstacles = &obstacles;
  if (!c.startAirborne) { p.engineRunning = true; p.engineSpool = 0.f; }
  const float dt = 1 / 30.f;
  float t = c.startAirborne ? 0.f : 4.f;   // (the engine start and the brake release before the roll)
  size_t wp = (size_t)std::max(c.wpStart, 0); int phase = c.startAirborne ? 1 : 0;   // 0 takeoff roll and initial climb, 1 the checkpoints, 2 the approach
  for (int k = 0; k < 60 * 60 * 30; k++) {
    if (phase == 0) {
      p.ctl.brake = 0; p.ctl.throttle = 1; p.ctl.gearDown = true; p.ctl.flaps = s.fullCabinEnvelope ? s.takeoffFlap : s.retract ? 0.15f : 0.1f;
      p.ctl.pitch = p.ias > s.vref * 0.95f ? clampf(0.08f * (10.f - p.pitchDeg()), -1, 1) : 0.f;
      // the roll steered down the centreline - rudder and nosewheel on the runway's heading, eased back towards the line;
      // left alone the torque and a crosswind ran one take-off in five off the side (the review of v3.44.0, CAR-3)
      if (p.onGround) {
        const float hr = reverse ? h0 + 180.f : h0;
        const vec3 rel = p.pos - start, right(cosf(hr * DEG), 0.f, sinf(hr * DEG));
        const float off = rel.x * right.x + rel.z * right.z;   // (right of the centreline: +)
        const float he = wrapAngle((hr - clampf(off * 0.5f, -8.f, 8.f) - p.heading()) * DEG) / DEG;
        p.ctl.yaw = clampf(he * DEG * 3.f + p.w.y * 2.f, -1.f, 1.f);   // (w.y: yaw left)
      } else p.ctl.yaw = clampf(p.beta * 3.f, -1.f, 1.f);
      // (and the runway's heading with the wings kept near level: a gust under one wing rolls it, and hands off the
      // climb-out wandered into the hills)
      if (!p.onGround) { float he = wrapAngle(((reverse ? h0 + 180.f : h0) - p.heading()) * DEG) / DEG; p.ctl.roll = clampf(0.05f * (clampf(he * 0.8f, -10.f, 10.f) - p.bankDeg()), -1, 1); }
      if (p.agl() > 120.f) { p.ctl.flaps = 0; if (s.retract) p.ctl.gearDown = false; phase = 1; }
    }
    if (climbOutOnly && (phase == 1 || t > 300.f)) return phase == 1 ? t / 60.f : -1.f;
    if (phase == 1) {
      if (wp < c.wps.size()) {   // the checkpoints in turn, on the hold modes at each one's height
        vec3 d(c.wps[wp].x - p.pos.x, 0, c.wps[wp].z - p.pos.z);
        if (p.apMode != Plane::AP_HOLD || !p.apOn) { p.apEngage(Plane::AP_HOLD, -1, wx); p.apComfort = c.gentle(); p.apSpeed = s.cruise * 0.85f; }
        p.apHeading = wrapDeg360(atan2f(d.x, -d.z) / DEG); p.apAlt = c.wps[wp].alt; p.apUseVS = false;
        if (length(d) < 300.f) wp++;
      } else { p.apEngage(Plane::AP_NAV, c.to, wx); p.apComfort = c.gentle(); phase = 2; }
    }
    p.step(dt, wx, t);
    t += dt;
    if (p.ev.crashed) return -1.f;
    if (phase == 2 && p.apDone) { if (fuelKgOut) *fuelKgOut = initialFuel - p.fuel; return t / 60.f; }
  }
  return -1.f;
}

int Career::ferryCost(const Contract& c, int si) const {
  int oi = ownedIndexFor(si);
  if (oi < 0 || fleet[oi].location == c.from) return 0;
  return (int)(150 + 12 * g_world.distanceKm(fleet[oi].location, c.from));
}

int Career::netQuick(const Contract& c, int si, Source src) const {
  const AircraftSpec& s = kAircraft[si];
  int fees = positioningCost(c) + (src == SRC_OWNED ? ferryCost(c, si) : 0) + (src == SRC_RENT ? s.rentFee : 0);
  int fuel = 0;
  if (src == SRC_OWNED) {   // the owned aircraft's fuel for the distance at its cruise power, the take-off, climb and approach on top (as the plan's quick estimate)
    float minutes = (contractKm(c) + 6.f) * 1000.f / (s.cruise * 0.85f) / 60.f;
    fuel = (int)((s.engineType == ENG_JET ? jetMissionFuel(s, minutes, 1.f, 3.f) : fullPowerFlow(s) * 60.f * (2.6f + cruisePowerShare(s) * minutes) * 1.15f) * fuelPrice(c.from, si));
  }
  return c.payout - fees - fuel;
}
bool Career::earningPath() const {
  const Contract* st = nextStory();
  if (st && st->forceAircraft >= 0) return true;   // a free lesson
  for (auto& c : board)
    for (int i : kCareerAircraft) { Source src = canFly(c, i); if (src != SRC_NONE && netQuick(c, i, src) > 0) return true; }
  return false;
}
Contract Career::recoveryContract() const {
  Contract c; c.type = CT_CARGO; c.cargoKg = 40; c.minLicense = LIC_PPL; c.courtesy = true;
  c.wx = W(220, 4, 0, 0.05f, 0.2f, 4500, 40, 0, false, 10.5f);
  // the licensed aircraft the player can rent (or owns), cheapest hire first
  std::vector<int> types;
  for (int i : kCareerAircraft) if (license >= kAircraft[i].license && license >= LIC_PPL && (kAircraft[i].rentFee > 0 || ownedIndexFor(i) >= 0)) types.push_back(i);
  std::sort(types.begin(), types.end(), [](int a, int b) { return kAircraft[a].rentFee < kAircraft[b].rentFee; });
  if (types.empty()) return c;
  // from the nearest field one of them can use, to the nearest other field it can use
  float bestD = 1e30f; int bestFrom = -1, bestTo = -1, bestSpec = -1;
  for (int si : types) {
    const AircraftSpec& s = kAircraft[si];
    for (int a = 0; a < (int)g_world.airports.size(); a++) {
      if (!runwayOK(s, g_world.airports[a])) continue;
      float dHome = g_world.distanceKm(location, a);
      for (int b = 0; b < (int)g_world.airports.size(); b++) {
        if (b == a || !runwayOK(s, g_world.airports[b])) continue;
        float km = g_world.distanceKm(a, b);
        if (km > s.rangeKm * 0.5f) continue;
        float score = dHome * 4.f + km;   // close to home first, then a short hop
        if (score < bestD) { bestD = score; bestFrom = a; bestTo = b; bestSpec = si; }
      }
    }
  }
  if (bestFrom < 0) return c;
  const AircraftSpec& s = kAircraft[bestSpec];
  c.from = bestFrom; c.to = bestTo; c.id = fmt("R%u_%d", boardSeed, bestFrom);
  c.title = fmt("Mail run to %s", g_world.airports[bestTo].name);
  c.brief = fmt("The postal service needs a light mail sack flown from %s to %s. %s", g_world.airports[bestFrom].name, g_world.airports[bestTo].name,
                bestFrom == location ? "No deadline, fair weather." : "They'll drive you to the field. No deadline, fair weather.");
  // net at least $150 after the hire and a hard landing (-20%): payout >= (150 + hire) / 0.8, rounded up to $10
  int hire = ownedIndexFor(bestSpec) >= 0 ? 0 : (int)s.rentFee;
  c.payout = ((int)((150 + hire) / 0.8f) + 9) / 10 * 10;
  c.payout = std::max(c.payout, (int)(200 + g_world.distanceKm(bestFrom, bestTo) * 20) / 10 * 10);
  return c;
}
void Career::refreshBoard() {
  board.clear();
  if (region != g_world.regionId || location < 0 || location >= (int)g_world.airports.size()) return;   // (drawn where the career is: its world loaded)
  auto snowy = [](const Airport& a) {   // (snow falls at the islands' cold south, and in a region's cold)
    if (!g_world.procedural) return a.z < -20000;
    float b[4]; g_world.sampleBase(a.x, a.z, b); return b[3] > 0.45f;
  };
  Rng r(boardSeed * 2654435761u + location * 97 + 13);
  // aircraft the player can access now
  std::vector<int> access;
  for (int i : kCareerAircraft)
    if (license >= kAircraft[i].license && license >= LIC_PPL && (kAircraft[i].rentFee > 0 || ownedIndexFor(i) >= 0)) access.push_back(i);
  if (access.empty()) { if (!earningPath()) { Contract rc = recoveryContract(); if (rc.payout > 0) board.push_back(rc); } return; }
  const char* cargoNames[] = {"Medical supplies", "Mail sacks", "Fresh produce", "Machine parts", "Fishing gear", "Coffee beans", "Newspapers", "Spare tyres", "Wine crates", "Lab samples"};
  const char* paxNames[] = {"Business travellers", "Holiday makers", "Wedding party", "Surveyors", "Film crew", "Tour group", "Island residents", "Students"};
  int tries = 0;
  while ((int)board.size() < 6 && tries++ < 200) {
    int si = access[r.next() % access.size()];
    const AircraftSpec& s = kAircraft[si];
    int to = r.next() % g_world.airports.size();
    if (to == location) continue;
    Contract c; c.from = location; c.to = to; c.id = fmt("F%u_%d", boardSeed, (int)board.size());
    const Airport& A = g_world.airports[location]; const Airport& B = g_world.airports[to];
    bool pax = s.pax >= 2 && license >= LIC_CPL && (r.next() & 1);
    c.type = pax ? CT_PAX : CT_CARGO;
    // the special kinds (C6), where the licence, the aircraft and the airports allow them: about a third of the board
    { float u = r.uni();
      if (u < 0.08f && license >= LIC_CPL && B.hospital && s.pax >= 1) c.type = CT_MEDEVAC;
      else if (u < 0.15f && license >= LIC_CPL && s.pax >= 4) c.type = CT_VIP;
      else if (u < 0.23f && A.size >= 1 && B.size >= 1) c.type = CT_NIGHT;
      else if (u < 0.30f && license >= LIC_CPL && B.size >= 1 && B.surface == SURF_ASPHALT && fabsf(A.elev - B.elev) < 60.f) c.type = CT_IFR;   // (one cloud base for both fields)
      else if (u < 0.37f && license >= LIC_PPL) c.type = CT_SURVEY;
      else if (u < 0.44f && license >= LIC_PPL && !profileSites().empty()) c.type = CT_TOUR;   // a photo flight over a landmark
      pax = c.type == CT_PAX || c.type == CT_VIP; }
    const Site* photo = nullptr;
    if (c.type == CT_TOUR) {   // the landmark nearest the way between the two fields, if the detour to it is a short one
      const auto& sites = profileSites();
      float bestDetour = 1e9f;
      for (const Site& st : sites) {
        const float da = length(vec3(st.pos.x - A.x, 0, st.pos.z - A.z)), db = length(vec3(st.pos.x - B.x, 0, st.pos.z - B.z)), dab = length(vec3(B.x - A.x, 0, B.z - A.z));
        if (da + db - dab < bestDetour) { bestDetour = da + db - dab; photo = &st; }
      }
      if (!photo || bestDetour > 25000.f) c.type = CT_CARGO;
      else {   // two gates: one lining up 1.2 km out on the way in, the shot over the landmark
        float hmax = photo->pos.y;
        for (int k = 0; k < 8; k++) { const float a = k * 0.785f; hmax = std::max(hmax, g_world.height(photo->pos.x + cosf(a) * 600.f, photo->pos.z + sinf(a) * 600.f)); }
        const float alt = std::max(hmax, 0.f) + 320.f;
        vec3 in = vec3(photo->pos.x - A.x, 0, photo->pos.z - A.z); const float il = length(in);
        in = il > 1.f ? in / il : vec3(1, 0, 0);
        c.wps.push_back({photo->pos.x - in.x * 1200.f, photo->pos.z - in.z * 1200.f, alt});
        c.wps.push_back({photo->pos.x, photo->pos.z, alt});
        c.pax = 1; c.cargoKg = 30;
      }
    }
    if (c.type == CT_MEDEVAC) { c.pax = 1; c.cargoKg = 60; }
    else if (c.type == CT_SURVEY) { c.pax = 1; c.cargoKg = 40; }
    else if (pax) { c.pax = std::max(1, (int)(s.pax * r.range(0.4f, 1.0f))); c.cargoKg = c.pax * 15; }
    else c.cargoKg = std::max(20, (int)(s.cargoKg * r.range(0.35f, 0.95f)) / 10 * 10);
    c.minLicense = (c.type == CT_PAX || c.type == CT_VIP || c.type == CT_MEDEVAC || c.type == CT_IFR) ? LIC_CPL : LIC_PPL;
    if (c.type == CT_SURVEY) {   // a ring of six checkpoints over the country between the two fields, flown at one altitude
      vec3 mid = (A.pos() + B.pos()) * 0.5f; float rad = clampf(g_world.distanceKm(location, to) * 1000.f * 0.2f, 1500.f, 4000.f);
      float hmax = 0; for (int k = 0; k < 12; k++) { float a = k * 0.5236f; hmax = std::max(hmax, g_world.height(mid.x + cosf(a) * rad, mid.z + sinf(a) * rad)); }
      float alt = std::max(hmax, std::max(A.elev, B.elev)) + 450.f;
      float a0 = atan2f(B.z - A.z, B.x - A.x);
      for (int k = 0; k < 6; k++) { float a = a0 + k * 1.0472f; c.wps.push_back({mid.x + cosf(a) * rad, mid.z + sinf(a) * rad, alt}); }
    }
    if (canFly(c, si) == SRC_NONE) continue;
    float km = contractKm(c);
    c.payout = (int)((250 + km * (30 + c.cargoKg * 0.13f + c.pax * 16)) * r.range(0.9f, 1.15f)) / 10 * 10;
    // a deadline is the planned flight in the type the job was drawn for, a quarter on top: the autopilot's whole
    // arrival can make it (it was the straight line at cruise x 1.5-1.6 + 3-4 min, and the arrival adds 5-8 min: the
    // review of v3.44.0, CAR-8 - 7 sampled timed jobs in 40 were late in every type)
    // (set below, once the weather the plan flies in is drawn)
    bool timed = c.type == CT_MEDEVAC;
    if (c.type == CT_MEDEVAC) c.payout = c.payout * 2;
    else if (c.type == CT_VIP) c.payout = c.payout * 17 / 10;
    else if (c.type == CT_NIGHT) c.payout = c.payout * 13 / 10;
    else if (c.type == CT_IFR) c.payout = c.payout * 15 / 10;
    else if (c.type == CT_SURVEY) c.payout = c.payout * 14 / 10 + 300;
    else if (c.type == CT_TOUR) c.payout = c.payout * 13 / 10 + 250;
    { int chapter = nextStory() ? nextStory()->chapter : 5; c.payout = (int)(c.payout * (1.f + 0.35f * chapter)) / 10 * 10; }   // the work pays more as the career advances (pacing)
    if (!atHome()) c.payout = c.payout * 115 / 100 / 10 * 10;   // (work beyond the islands pays 15% more)
    c.repBonusPct = repBonusPct();   // clients pay a reliable pilot a little more
    c.payout = c.payout * (100 + c.repBonusPct) / 100 / 10 * 10;
    bool plain = c.type == CT_CARGO || c.type == CT_PAX;
    c.fragile = c.type == CT_CARGO && r.uni() < 0.15f;
    if (plain && r.uni() < 0.15f) { timed = true; c.payout = c.payout * 13 / 10; }
    const char* vipNames[] = {"A minister", "A film star", "The island's governor", "A racing driver", "An opera singer", "A football squad's captain"};
    const char* medNames[] = {"Burns patient", "Diver with the bends", "Heart attack", "Road accident casualty", "Premature baby and nurse", "Stroke patient"};
    const char* surveyNames[] = {"Forestry survey", "Coastline mapping", "Power-line inspection", "Flood survey", "Pipeline patrol", "Wildlife count"};
    switch (c.type) {
      case CT_MEDEVAC: c.title = fmt("Medevac: %s to %s", medNames[r.next() % 6], B.name); c.brief = fmt("A patient at %s needs the hospital at %s within %.0f minutes. Keep it under 1.5 g and 30 degrees of bank, and the touchdown soft: the patient's condition is on the HUD.", A.name, B.name, c.timeLimitMin); break;
      case CT_VIP: {   // (one VIP for the title and the brief - they named two in 83% of jobs: the review of v3.44.0, CAR-9; the second draw kept, so the rest of the board stays as it was)
        const char* who = vipNames[r.next() % 6]; (void)r.next();
        c.title = fmt("VIP: %s to %s", who, B.name); c.brief = fmt("%s and party, %d aboard, expect a limousine ride to %s. The comfort meter on the HUD drops with every steep bank, bump and firm touchdown; a delighted client pays extra.", who, c.pax, B.name); break;
      }
      case CT_NIGHT: c.title = fmt("Night freight to %s", B.name); c.brief = fmt("%s for the morning at %s, flown overnight between two lit fields. Have the landing light on for the touchdown.", cargoNames[r.next() % 10], B.name); break;
      case CT_IFR: c.title = fmt("Low-vis run to %s", B.name); c.brief = fmt("%s to %s under a low overcast in poor visibility. Fly the approach on the instruments: if you are not lined up with the runway when you break out, go around.", cargoNames[r.next() % 10], B.name); break;
      case CT_SURVEY: c.title = fmt("%s near %s", surveyNames[r.next() % 6], B.name); c.brief = fmt("Fly the six survey checkpoints in order, holding %.0f ft within 150 ft, then land at %s. Pay follows the share of the pattern flown in the band.", c.wps[0].alt * M_TO_FT, B.name); break;
      case CT_TOUR: {   // (one draw, as the plain jobs' names take: the rest of the board stays as it was)
        (void)r.next();
        std::string nm = photo->name; if (nm.rfind("The ", 0) == 0) nm = "the " + nm.substr(4);
        c.title = "Photo flight: " + photo->name;
        c.brief = fmt("A photographer wants %s from the air. Fly through the first gate to line up and the second, over it, for the shot - a steady line, wings level - then land at %s.", nm.c_str(), B.name);
        break;
      }
      default: c.title = pax ? fmt("%s to %s", paxNames[r.next() % 8], B.name) : fmt("%s to %s", cargoNames[r.next() % 10], B.name); c.brief = fmt("Freelance job posted at %s. Distance %.0f km.", A.name, km); break;
    }
    float tod = c.type == CT_NIGHT ? (r.uni() < 0.5f ? r.range(21.f, 23.9f) : r.range(0.f, 5.f)) : r.range(7.f, 19.5f);
    c.wx = W(r.range(0, 360), r.range(0, 14), r.uni() < 0.3f ? r.range(3, 10) : 0, r.range(0.05f, 0.35f), r.range(0, 0.8f), r.range(2500, 7000), r.range(12, 50),
             r.uni() < 0.15f ? (snowy(g_world.airports[to]) ? 2 : 1) : 0, false, tod);
    if (r.uni() < 0.3f && c.type != CT_IFR && c.type != CT_SURVEY) {   // a front on the way: the wind backs or veers, the cloud and the visibility change
      c.wxShift = true; c.wxEnd = c.wx;
      c.wxEnd.windFrom = wrapDeg360(c.wx.windFrom + (r.uni() < 0.5f ? -1.f : 1.f) * r.range(70.f, 180.f));
      c.wxEnd.windSpeed = clampf(c.wx.windSpeed + r.range(-4.f, 8.f) / MS_TO_KT, 1.f / MS_TO_KT, 24.f / MS_TO_KT);
      c.wxEnd.gust = r.uni() < 0.4f ? r.range(3.f, 9.f) / MS_TO_KT : 0.f;
      c.wxEnd.cloudCover = clampf(c.wx.cloudCover + r.range(-0.4f, 0.5f), 0.f, 0.95f);
      c.wxEnd.cloudBase = clampf(c.wx.cloudBase + r.range(-500.f, 300.f), 450.f, 2500.f);
      c.wxEnd.visibility = clampf(c.wx.visibility * r.range(0.5f, 1.3f), 4000.f, 60000.f);
      if (c.wxEnd.cloudCover > 0.6f && r.uni() < 0.4f) c.wxEnd.precip = snowy(g_world.airports[to]) ? 2 : 1;
    }
    if (c.type == CT_IFR) { c.wx.cloudCover = 0.95f; c.wx.cloudBase = r.range(300.f, 600.f) / M_TO_FT + B.elev; c.wx.visibility = r.range(1500.f, 3000.f); c.wx.precip = r.uni() < 0.5f ? 1 : 0; c.wx.windSpeed = std::min(c.wx.windSpeed, 6.f / MS_TO_KT); }
    if (c.type == CT_SURVEY || c.type == CT_TOUR) { c.wx.cloudCover = std::min(c.wx.cloudCover, 0.4f); c.wx.cloudBase = std::max(c.wx.cloudBase, c.wps[0].alt + 300.f); c.wx.gust = 0; c.wx.turbulence = std::min(c.wx.turbulence, 0.12f); }
    if (c.type == CT_TOUR) { c.wx.precip = 0; c.wx.storm = false; c.wx.visibility = std::max(c.wx.visibility, 20000.f); c.wxShift = false; c.wx.timeOfDay = clampf(c.wx.timeOfDay, 8.f, 17.5f); }
    if (c.type == CT_MEDEVAC || c.type == CT_VIP) { c.wx.gust = std::min(c.wx.gust, 4.f / MS_TO_KT); c.wx.storm = false; }
    if (timed) c.timeLimitMin = ceilf(plan(c, si, SRC_RENT).minutesEst * 1.25f);
    bool dup = false;
    for (auto& b : board) if ((b.to == c.to && b.type == c.type) || (c.type == CT_TOUR && b.type == CT_TOUR)) dup = true;   // (one photo flight a board)
    if (!dup) board.push_back(c);
  }
  // cross-country work (the real Earth, docs/REAL_EARTH_PLAN.md E7): a load for a small field beyond this chart, 30 to
  // 180 km on - a strip, or a field no airline serves, which the long hauls never take - flown there yourself as a long
  // haul is, the charts on the way made ready as you fly. Two at most, the fields drawn afresh with each board
  if (!job && !access.empty() && realEarth && earthCharts::isEarthId(region))
    if (const auto pack = earthCharts::pack(); pack && procedural::find(region)) {
      const regionTravel::Chart chart(procedural::find(region)->origin);
      const geo::Position here = airportPlace(region, g_world.airports[location]);
      const double reach = 180000.0, dLat = reach / 111320.0 + 0.05, dLon = dLat / std::max(0.05, std::cos(here.latitude * geo::kDegrees));
      std::vector<std::pair<uint32_t, earth::AirportRecord>> fields;
      for (auto& a : pack->airports({here.latitude - dLat, here.longitude - dLon, here.latitude + dLat, here.longitude + dLon})) {
        if (a.runways.empty() || (a.type >= 1 && a.scheduled)) continue;
        const double m = geo::surfaceDistance(here, {a.lat, a.lon, 0});
        const vec3 at = chart.local({a.lat, a.lon, 0});
        if (m < 30000.0 || m > reach || std::max(fabsf(at.x), fabsf(at.z)) < WORLD_HALF) continue;   // (beyond the chart)
        uint32_t h = boardSeed * 2654435761u + 977u;
        for (char ch : a.ident) { h ^= (uint8_t)ch; h *= 16777619u; }
        fields.push_back({h, std::move(a)});
      }
      std::sort(fields.begin(), fields.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first < b.first : a.second.ident < b.second.ident; });
      static const char* bearings[] = {"north", "northeast", "east", "southeast", "south", "southwest", "west", "northwest"};
      int added = 0, tried = 0;
      for (const auto& field : fields) {
        if (added >= 2 || tried++ >= 8) break;
        const earth::AirportRecord& a = field.second;
        const auto to = earthCharts::chartForAirport(a.ident);
        if (!to) continue;
        int index = -1;
        for (int i = 0, n = procedural::airportCount(to->id); i < n && index < 0; i++)
          if (const Airport* p = procedural::airportOf(to->id, i); p && a.ident == p->code) index = i;
        const Airport* B = index >= 0 ? procedural::airportOf(to->id, index) : nullptr;
        if (!B) continue;
        const geo::Position there = airportPlace(to->id, *B);
        Contract c; c.from = location; c.to = index; c.toRegion = to->id; c.haulKm = (float)(geo::surfaceDistance(here, there) / 1000.);
        c.id = fmt("X%u_%d", boardSeed, added);
        int si = -1;
        for (int k = 0; k < (int)access.size() && si < 0; k++) {   // (a type that can use both fields, drawn from those you can fly)
          const int t = access[(r.next() + k) % access.size()];
          c.cargoKg = std::max(20, (int)(kAircraft[t].cargoKg * 0.5f) / 10 * 10);
          if (canFly(c, t) != SRC_NONE) si = t;
        }
        if (si < 0) continue;
        const AircraftSpec& s = kAircraft[si];
        const bool pax = license >= LIC_CPL && s.pax >= 2 && (r.next() & 1);
        if (pax) { c.type = CT_PAX; c.pax = std::max(1, (int)(s.pax * r.range(0.4f, 1.0f))); c.cargoKg = c.pax * 15; c.minLicense = LIC_CPL; }
        else { c.type = CT_CARGO; c.cargoKg = std::max(20, (int)(s.cargoKg * r.range(0.35f, 0.95f)) / 10 * 10); c.minLicense = LIC_PPL; }
        if (canFly(c, si) == SRC_NONE) continue;
        c.payout = (int)((250 + c.haulKm * (30 + c.cargoKg * 0.13f + c.pax * 16)) * r.range(0.9f, 1.15f)) / 10 * 10;   // (by the km, as any job)
        { int chapter = nextStory() ? nextStory()->chapter : 5; c.payout = (int)(c.payout * (1.f + 0.35f * chapter)) / 10 * 10; }
        if (!atHome()) c.payout = c.payout * 115 / 100 / 10 * 10;
        c.repBonusPct = repBonusPct();
        c.payout = c.payout * (100 + c.repBonusPct) / 100 / 10 * 10;
        const auto bearing = geo::initialBearing(here, there);
        const char* way = bearing ? bearings[((int)lround(*bearing / 45.0) % 8 + 8) % 8] : "out";
        const std::string what = pax ? fmt("%d passengers", c.pax) : fmt("%d kg of %s", c.cargoKg, cargoNames[r.next() % 10]);
        c.title = pax ? fmt("Cross-country: %s to %s", paxNames[r.next() % 8], B->name) : fmt("Cross-country: %s to %s", cargoNames[r.next() % 10], B->name);
        c.brief = fmt("%s for %s (%s), %.0f km %s, beyond this chart. Fly it there yourself - the land ahead is made ready as you "
                      "fly. Land at %s to deliver it; the job is called off anywhere else, and you fly back to %s.",
                      what.c_str(), B->name, B->code, c.haulKm, way, B->code, g_world.airports[location].code);
        c.wx = W(r.range(0, 360), r.range(0, 12), 0, r.range(0.05f, 0.3f), r.range(0, 0.6f), r.range(2500, 7000), r.range(20, 50), 0, false, r.range(8.f, 17.f));
        board.push_back(c); added++;
      }
    }
  // long hauls (C13): a load for another region's main airport, flown there across the ocean - two at most, the nearest
  // regions the licence reaches first (the islands among them when away), in a type with the range to make it (the
  // jets: the Emissary owned, the Galleon rented, so a long haul is an Airline Transport pilot's work)
  if (!job && !access.empty()) {
    const geo::Position here = airportPlace(region, g_world.airports[location]);
    static const char* bearings[] = {"north", "northeast", "east", "southeast", "south", "southwest", "west", "northwest"};
    int added = 0;
    for (const Destination& d : destinations()) {
      if (added >= 2) break;
      if (!d.home && license < std::max(d.minLicense, (int)LIC_PPL)) continue;
      const int main = d.airport >= 0 ? d.airport : procedural::mainAirport(d.id);
      const Airport* B = procedural::airportOf(d.id, main);
      if (!B) continue;
      const geo::Position there = airportPlace(d.id, *B);
      Contract c; c.from = location; c.to = main; c.toRegion = d.id; c.haulKm = (float)(geo::surfaceDistance(here, there) / 1000.);
      c.id = fmt("H%u_%d", boardSeed, added);
      int si = -1;
      for (int k = 0; k < (int)access.size() && si < 0; k++) {   // (a type with the range, drawn from those you can fly)
        const int t = access[(r.next() + k) % access.size()];
        c.cargoKg = std::max(20, (int)(kAircraft[t].cargoKg * 0.6f) / 10 * 10);
        if (canFly(c, t) != SRC_NONE) si = t;
      }
      if (si < 0) continue;
      const AircraftSpec& s = kAircraft[si];
      const bool pax = license >= LIC_CPL && s.pax >= 4 && (r.next() & 1);
      if (pax) { c.type = CT_PAX; c.pax = std::max(2, (int)(s.pax * r.range(0.5f, 0.9f))); c.cargoKg = c.pax * 15; c.minLicense = LIC_CPL; }
      else { c.type = CT_CARGO; c.cargoKg = std::max(20, (int)(s.cargoKg * r.range(0.4f, 0.8f)) / 10 * 10); c.minLicense = LIC_PPL; }
      if (canFly(c, si) == SRC_NONE) continue;
      c.payout = (int)((250 + c.haulKm * (30 + c.cargoKg * 0.13f + c.pax * 16)) * r.range(0.95f, 1.1f)) / 10 * 10;   // (by the km, as any job)
      { int chapter = nextStory() ? nextStory()->chapter : 5; c.payout = (int)(c.payout * (1.f + 0.35f * chapter)) / 10 * 10; }
      if (!atHome()) c.payout = c.payout * 115 / 100 / 10 * 10;
      c.repBonusPct = repBonusPct();
      c.payout = c.payout * (100 + c.repBonusPct) / 100 / 10 * 10;
      const auto bearing = geo::initialBearing(here, there);
      const char* way = bearing ? bearings[((int)lround(*bearing / 45.0) % 8 + 8) % 8] : "out";
      const std::string where = d.home ? std::string("the Solace Islands") : realEarth ? d.name : "the " + d.name;
      const char* kind = c.crossCountry() ? "Cross-country" : "Long haul";   // (the real Earth's nearest: a short hop)
      c.title = pax ? fmt("%s: passengers to %s", kind, B->name) : fmt("%s: %s to %s", kind, cargoNames[r.next() % 10], B->name);
      c.brief = fmt(realEarth ? "%s for %s, %s: %.0f km %s. Fly it there yourself - the land ahead is made ready as you fly. Land "
                                "at %s to deliver it; the job is called off anywhere else, and you fly back to %s." :
                                "%s for %s, in %s: %.0f km %s across the open ocean. Fly it there yourself - the islands ahead are made ready "
                                "as you cross. Land at %s to deliver it; the job is called off anywhere else, and you fly back to %s.",
                    pax ? fmt("%d passengers", c.pax).c_str() : fmt("%d kg of freight", c.cargoKg).c_str(), B->name, where.c_str(), c.haulKm, way,
                    B->code, g_world.airports[location].code);
      c.wx = W(r.range(0, 360), r.range(0, 10), 0, r.range(0.05f, 0.25f), r.range(0, 0.5f), r.range(3000, 7000), r.range(25, 50), 0, false, r.range(8.f, 14.f));
      board.push_back(c); added++;
    }
  }
  if (!earningPath()) { Contract rc = recoveryContract(); if (rc.payout > 0) board.insert(board.begin(), rc); }
}

Career::JobPolicy Career::policyOf(const Contract& c) {
  if (isMilitaryContract(c.type) || c.longHaul()) return POL_RETAKE;   // (a long haul: flown whole)
  if (c.type == CT_FERRY || c.type == CT_TRIAL) return POL_UNSET;        // a free flight or a trial is not a job
  if (c.forceAircraft >= 0 || c.grantLicense >= 0 || c.type == CT_LESSON) return POL_RETAKE;   // lessons and checkrides are flown whole
  if (c.type == CT_MEDEVAC) return POL_MEDEVAC;
  if (c.timeLimitMin > 0 || c.type == CT_VIP) return POL_RESUME_CLOCK;
  return POL_RESUME;
}
void Career::accept(const Contract& c, int si, Source src, const LaunchPlan& p) {
  JobState j; j.c = c; j.plan = p; j.spec = si; j.src = src; j.state = JobState::ACTIVE; j.at = c.from; j.id = ++attempt;
  job = j;
}
std::vector<PayoutLine> Career::closeLeg(const FlightResult& r, const LaunchPlan& p, int at, int recoveryFee, const char* recoveryLabel) {
  std::vector<PayoutLine> L;
  if (!job) return L;
  JobState& J = *job;
  const AircraftSpec& s = kAircraft[J.spec];
  if (!J.positioningPaid && p.positioning) { L.push_back({"Positioning ticket to " + std::string(g_world.airports[J.c.from].code), -p.positioning}); }
  J.positioningPaid = true;
  if (!J.hirePaid && p.hire) L.push_back({"Rental: " + std::string(s.name), -p.hire});
  J.hirePaid = true;
  if (!J.ferryPaid && p.ferry) L.push_back({"Ferry service for your " + std::string(s.name), -p.ferry});   // (the positioning of an owned aircraft: charged once, on the first leg)
  J.ferryPaid = true;
  if (J.src == SRC_OWNED) {
    int oi = ownedIndexFor(J.spec);
    if (p.fuel == LaunchPlan::FUEL_PURCHASED) {
      if (p.fuelCostEst) { L.push_back({fmt("Fuel uplift at %s (%.0f kg)", g_world.airports[p.startAirport].code, p.fuelUpliftKg), -p.fuelCostEst}); J.fuelBilledKg += p.fuelUpliftKg; }
      if (oi >= 0 && r.fuelLeftKg >= 0) fleet[oi].fuel = std::clamp(r.fuelLeftKg, 0.f, s.maxFuel);
    } else {
      int fuelCost = (int)(r.fuelUsedKg * s.fuelPriceBase());
      if (fuelCost) { L.push_back({"Fuel (this leg)", -fuelCost}); J.fuelBilledKg += r.fuelUsedKg; }
    }
  }
  if (recoveryFee) L.push_back({recoveryLabel, -recoveryFee});
  flights++; hours += r.flightMin / 60.f;
  if (r.landed) { landings++; bestLandingFpm = std::min(bestLandingFpm, fabsf(r.touchdownFpm)); }
  J.jobClockMin += r.flightMin; J.legs++;
  J.maxG = std::max(J.maxG, r.maxG); J.minG = std::min(J.minG, r.minG); J.maxBank = std::max(J.maxBank, r.maxBank);
  J.patient = std::min(J.patient, r.patient); J.comfort = std::min(J.comfort, r.comfort);   // (the ride so far: the next leg starts from it)
  J.surveyHistoryKnown = J.surveyHistoryKnown && r.surveyHistoryKnown;
  J.surveySec = std::max(J.surveySec, r.surveySec); J.surveyInSec = std::max(J.surveyInSec, r.surveyInSec);   // (the leg's are the job's whole so far)
  J.wpDone = std::max(J.wpDone, r.wpDone);
  J.holdViolated = J.holdViolated || r.holdViolated; J.landedAgainstGoAround = J.landedAgainstGoAround || r.landedAgainstGoAround;
  if (J.c.fragile && (r.maxG > 2.0f || r.minG < 0.0f || (r.landed && fabsf(r.touchdownFpm) > 400))) J.fragileHit = true;
  J.at = at; J.state = JobState::RECOVERY;
  location = at;
  if (J.src == SRC_OWNED) { int oi = ownedIndexFor(J.spec); if (oi >= 0) fleet[oi].location = at; }
  wear(L, J.spec, J.src, r);
  if (routeFlightQualifies(r)) airlineTick(L, r.flightMin);
  payLoan(L);
  int total = 0; for (auto& l : L) total += l.amount;
  money += total;
  refreshBoard();
  return L;
}
std::vector<PayoutLine> Career::settleJob(const FlightResult& r, const LaunchPlan& p, int* stars) {
  if (!job) { *stars = 0; return {}; }
  JobState J = *job;
  // the whole job's record: the hardest moments of every leg, lateness against the job clock, the fees still unpaid
  FlightResult w = r;
  w.maxG = std::max(J.maxG, r.maxG); w.minG = std::min(J.minG, r.minG); w.maxBank = std::max(J.maxBank, r.maxBank);
  w.late = J.c.timeLimitMin > 0 && J.jobClockMin + r.flightMin > J.c.timeLimitMin;
  w.patient = std::min(J.patient, r.patient); w.comfort = std::min(J.comfort, r.comfort);
  w.surveyHistoryKnown = J.surveyHistoryKnown && r.surveyHistoryKnown;
  w.holdViolated = J.holdViolated || r.holdViolated; w.landedAgainstGoAround = J.landedAgainstGoAround || r.landedAgainstGoAround;
  {   // the survey band over every leg (the last leg's counters carry the earlier legs': the larger is the job's whole)
    float st = std::max(J.surveySec, r.surveySec), si = std::max(J.surveyInSec, r.surveyInSec);
    if (st > 1.f) w.surveyInBand = si / st;
  }
  if (J.fragileHit && J.c.fragile) w.maxG = std::max(w.maxG, 2.01f);   // (a leg already damaged it)
  LaunchPlan q = p;
  if (J.positioningPaid) q.positioning = 0;
  if (J.hirePaid) q.hire = 0;
  if (J.ferryPaid) q.ferry = 0;
  job.reset();
  Contract c = J.c;
  auto L = settle(c, J.spec, J.src, w, stars, &q);
  return L;
}
void Career::releaseJob() { job.reset(); refreshBoard(); }
void Career::planFuel(LaunchPlan& e, const Contract& c, float fuelKg) const {
  const AircraftSpec& s = kAircraft[e.spec];
  e.fuelLoadKg = clampf(fuelKg, 0.f, s.maxFuel);
  if (e.fuel != LaunchPlan::FUEL_PURCHASED) return;
  int oi = ownedIndexFor(e.spec);
  float have = oi >= 0 ? fleet[oi].fuel : 0.f;
  e.fuelUpliftKg = std::max(0.f, std::min(fuelKg, s.maxFuel) - have);
  e.fuelCostEst = (int)(e.fuelUpliftKg * fuelPrice(c.from, e.spec));
  e.net = c.payout - e.fees() - e.fuelCostEst;
}
bool Career::refuel(int fi, std::string* msg) {
  if (fi < 0 || fi >= (int)fleet.size()) return false;
  OwnedPlane& p = fleet[fi];
  const AircraftSpec& s = kAircraft[p.spec];
  if (!here(p)) { *msg = fmt("Your %s is parked in another region.", s.name); return false; }
  if (p.location != location) { *msg = fmt("Your %s is at %s, not here.", s.name, g_world.airports[p.location].code); return false; }
  float need = std::max(0.f, s.maxFuel - p.fuel);
  int cost = (int)(need * fuelPrice(location, p.spec));
  if (need < 1.f) { *msg = "Tanks are already full."; return false; }
  if (money < cost) { *msg = fmt("Not enough money (%s for %.0f kg)", fmt("$%d", cost).c_str(), need); return false; }
  money -= cost; p.fuel = s.maxFuel;
  *msg = fmt("Refuelled %s: %.0f kg for $%d.", s.name, need, cost);
  return true;
}

std::vector<PayoutLine> Career::settle(const Contract& c, int si, Source src, const FlightResult& r, int* stars, const LaunchPlan* plan) {
  std::vector<PayoutLine> L;
  if (isMilitaryContract(c.type) || src == SRC_MILITARY) {
    if (stars) *stars = 0;
    const bool accepted = isMilitaryContract(c.type) && src == SRC_MILITARY && attemptOpen &&
      military.activeAttempt != 0 && military.activeAttempt == attempt &&
      military.activeAttempt > military.lastSettledAttempt && military.activeKind == c.type - CT_MIL_RECON &&
      military.activeAirport == c.from && canFly(c, si) == SRC_MILITARY;
    if (!accepted) return {{"No unsettled military sortie", 0}};
    auto boundedAdd = [](int a, int b) { return int(std::min<int64_t>(int64_t(a) + b, 2000000000)); };
    // Freeze the complete outcome ledger together at its storage bound. Independent
    // saturation would break successes + failures == sorties and invalidate the save.
    const bool recordSortie = military.sorties < 2000000000;
    if (recordSortie) ++military.sorties;
    if (floatValidation::finite(r.flightMin)) military.hours = std::min(1000000.f, military.hours + clampf(r.flightMin, 0.f, 1440.f) / 60.f);
    if (r.success && r.outcome == OUT_SUCCESS) {
      const auto& mission = *military::mission(military.activeKind);
      const int reward = mission.reward;
      military.completedMissions |= military::completionBit(mission.id);
      if (recordSortie) ++military.successes;
      military.credits = boundedAdd(military.credits, reward);
      if (recordSortie && mission.intelligence) ++military.intelligence;
      military.rank = std::min(10, military.successes / 3);
      if (stars) *stars = 3;
      L.push_back({"Military service credits (separate from civilian funds)", reward});
    } else {
      if (recordSortie) ++military.failures;
      L.push_back({r.failReason.empty() ? "Military sortie incomplete" : r.failReason, 0});
    }
    abandonMilitaryAttempt();
    return L;
  }
  const AircraftSpec& s = kAircraft[si];
  int pos = plan ? plan->positioning : positioningCost(c), ferry = plan ? plan->ferry : src == SRC_OWNED ? ferryCost(c, si) : 0;
  int hire = plan ? plan->hire : src == SRC_RENT ? s.rentFee : 0;
  if (pos) L.push_back({"Positioning ticket to " + std::string(g_world.airports[c.from].code), -pos});
  if (ferry) L.push_back({"Ferry service for your " + std::string(s.name), -ferry});
  if (hire) L.push_back({"Rental: " + std::string(s.name), -hire});
  if (src == SRC_OWNED) {
    int oi = ownedIndexFor(si);
    if (plan && plan->fuel == LaunchPlan::FUEL_PURCHASED) {   // bought at the departure, as quoted; the tanks keep what is left
      if (plan->fuelCostEst) L.push_back({c.longHaul() ? fmt("Fuel uplift at the departure (%.0f kg)", plan->fuelUpliftKg) : fmt("Fuel uplift at %s (%.0f kg)", g_world.airports[plan->startAirport].code, plan->fuelUpliftKg), -plan->fuelCostEst});
      if (oi >= 0 && r.fuelLeftKg >= 0) fleet[oi].fuel = std::clamp(r.fuelLeftKg, 0.f, s.maxFuel);
    } else {
      int fuelCost = (int)(r.fuelUsedKg * s.fuelPriceBase());
      if (fuelCost) L.push_back({"Fuel", -fuelCost});
    }
  }
  *stars = 0;
  flights++; hours += r.flightMin / 60.f;
  const std::string crFault = r.success ? checkrideFault(c, r) : std::string();
  if (!r.success || !crFault.empty()) {
    if (!crFault.empty() || r.outcome == OUT_CHECKRIDE_FAILED) {   // landed at the field: you (and the aircraft) are there
      L.push_back({"Checkride not passed: " + (crFault.empty() ? checkrideFault(c, r) : crFault), 0});
      location = c.to;
      if (src == SRC_OWNED) { int oi = ownedIndexFor(si); if (oi >= 0) fleet[oi].location = c.to; }
    }
    if (r.outcome == OUT_OFF_AIRPORT && src != SRC_LESSON) L.push_back({"Aircraft recovery from the field", -(150 + s.rentFee)});
    if (r.outcome == OUT_CRASHED) {
      crashes++;
      int repair = src == SRC_RENT ? 300 + s.rentFee * 2 : src == SRC_OWNED ? s.price / 12 : 0;
      if (repair) L.push_back({src == SRC_RENT ? "Insurance deductible" : "Repairs", -repair});
      if (src == SRC_OWNED) { int oi = ownedIndexFor(si); if (oi >= 0) fleet[oi].condition = 1.f; }
    }
    reputation = std::max(0, reputation - 1);
    if (r.outcome == OUT_DIVERTED && r.divertedTo >= 0 && !c.longHaul()) {   // you (and your aircraft) are where you landed (a long haul called off: back where it set out)
      location = r.divertedTo;
      if (src == SRC_OWNED) { int oi = ownedIndexFor(si); if (oi >= 0) fleet[oi].location = r.divertedTo; }
    }
  } else {
    float fpm = fabsf(r.touchdownFpm);
    if (r.landed) { landings++; bestLandingFpm = std::min(bestLandingFpm, fpm); }
    int st = 3;
    if (c.payout > 0) L.push_back({"Contract payment", c.payout});
    // landing quality only counts when the flight actually ended with a touchdown
    if (r.landed) {
      bool longLdg = r.tdPastThrM >= 0 && r.rwyLenM > 0 && r.tdPastThrM > r.rwyLenM * 0.45f;   // floated half the runway
      const int grade = touchdownGrade(fpm);
      if (grade == TD_BUTTER && !longLdg) { L.push_back({"Butter landing bonus", c.payout / 10}); }
      else if (grade == TD_HARD) { L.push_back({"Hard landing", -c.payout / 5}); st--; }
      else if (grade == TD_FIRM) { L.push_back({"Firm landing", -c.payout / 20}); }
    }
    if (r.late) { L.push_back({"Late delivery", -c.payout / 2}); st--; }
    if (c.payout > 0 && r.landed) {   // airmanship on the arrival, from what was recorded (a needed go-around costs nothing)
      bool zone = r.tdPastThrM >= 0 && r.rwyLenM > 0 && r.tdPastThrM <= r.rwyLenM / 3.f;
      bool longLdg = r.tdPastThrM >= 0 && r.rwyLenM > 0 && r.tdPastThrM > r.rwyLenM * 0.5f;
      if (zone) L.push_back({"Touchdown in the zone", c.payout * 5 / 100});
      else if (longLdg) L.push_back({"Floated past the midpoint", -c.payout * 5 / 100});
      if (r.tdPastThrM >= 0 && r.centerlineErr >= 0 && r.centerlineErr < 2.f) L.push_back({"On the centreline", c.payout * 2 / 100});
      float vref = s.vref * MS_TO_KT;
      if (r.thrKt > 0 && r.thrKt >= vref - 5.f && r.thrKt <= vref + 15.f && r.thrAglM >= 9.f && r.thrAglM <= 24.5f) L.push_back({"Stable approach", c.payout * 3 / 100});
      if (r.shutDownAtStand) L.push_back({"Taxied clear and shut down", c.payout * 2 / 100});
    }
    if (c.payout > 0 && !s.special) {
      if (r.fuelLeftFrac >= 0.15f) L.push_back({"Fuel reserve kept", c.payout * 2 / 100});
      else if (r.fuelLeftFrac < 0.05f) { L.push_back({"Landed on fumes", -c.payout / 10}); reputation = std::max(0, reputation - 1); }
    }
    if (c.payout > 0 && r.holdViolated) { L.push_back({"Took off against a hold instruction", -c.payout / 10}); reputation = std::max(0, reputation - 1); st--; }
    if (c.payout > 0 && r.landedAgainstGoAround) { L.push_back({"Landed against a go-around instruction", -c.payout / 2}); reputation = std::max(0, reputation - 1); st--; }
    if (c.pax > 0 && (r.maxBank > 45 || r.maxG > 1.9f || r.minG < 0.2f)) { L.push_back({"Passenger discomfort", -c.payout * 15 / 100}); st--; }
    if (c.payout > 0) {   // the job types' own scoring (C6)
      if (c.type == CT_MEDEVAC) {
        if (r.patient < 0.35f) { L.push_back({fmt("Patient in distress (%.0f%%)", r.patient * 100.f), -c.payout * 40 / 100}); st -= 2; reputation = std::max(0, reputation - 1); }
        else if (r.patient < 0.7f) { L.push_back({fmt("Rough ride for the patient (%.0f%%)", r.patient * 100.f), -c.payout * 20 / 100}); st--; }
        else if (r.patient > 0.9f && !r.late) L.push_back({"Patient delivered in good shape", c.payout * 10 / 100});
      }
      if (c.type == CT_VIP) {
        if (r.comfort < 0.4f) { L.push_back({fmt("VIP displeased (comfort %.0f%%)", r.comfort * 100.f), -c.payout * 30 / 100}); st--; }
        else if (r.comfort < 0.7f) L.push_back({fmt("VIP unimpressed (comfort %.0f%%)", r.comfort * 100.f), -c.payout * 10 / 100});
        else if (r.comfort > 0.9f) { L.push_back({"VIP delighted: a tip", c.payout * 15 / 100}); reputation++; }
      }
      if (c.type == CT_NIGHT && r.landed && !r.landingLightOn) { L.push_back({"Landed without the landing light", -c.payout * 10 / 100}); st--; }
      if (c.type == CT_IFR) {
        if (r.belowMinimumsUnaligned) { L.push_back({"Continued below minimums without the runway lined up", -c.payout * 25 / 100}); st--; reputation = std::max(0, reputation - 1); }
        else if (r.landed) L.push_back({"Approach flown to minimums", c.payout * 5 / 100});
      }
      if (c.type == CT_TOUR && !c.story) {   // the photo flight: the shot over the landmark (its last gate)
        if (r.wpDone >= (int)c.wps.size()) L.push_back({"The photographs", c.payout * 10 / 100});
        else { L.push_back({"No photographs: the gate over the landmark was missed", -c.payout / 2}); st--; }
      }
      if ((c.type == CT_PAX || c.type == CT_VIP) && r.sightseen) { L.push_back({"Sightseeing tip from the passengers", c.payout * 8 / 100}); reputation++; }
      if (c.type == CT_SURVEY && !r.surveyHistoryKnown)
        L.push_back({"Survey altitude adjustment waived: older save has no altitude history", 0});
      if (c.type == CT_SURVEY && r.surveyHistoryKnown) {
        int pct = (int)(clampf(r.surveyInBand, 0.f, 1.f) * 100.f + 0.5f);
        if (pct < 95) { L.push_back({fmt("Survey altitude held %d%% of the pattern", pct), -c.payout * (100 - pct) * 6 / 1000}); if (pct < 60) st--; }
        else L.push_back({"Survey altitude held", c.payout * 5 / 100});
      }
    }
    if (c.fragile && (r.maxG > 2.0f || r.minG < 0.0f || (r.landed && fpm > 400))) { L.push_back({"Fragile cargo damaged", -c.payout * 4 / 10}); st--; }
    *stars = std::max(1, st);
    // three stars and an S for the landing: a flawless flight (the debrief's mark, landingMark)
    if (c.payout > 0 && *stars == 3 && landingMark(r, s).letter == 'S') { L.push_back({"Flawless flight", c.payout * 5 / 100}); reputation++; }
    reputation += *stars;
    if (c.story) {
      storyIndex++;
      if (c.grantLicense > license) { license = c.grantLicense; }
      if (storyIndex >= (int)g_story.size()) finished = true;
    }
    if (c.payout > 0 && r.failureKinds && r.landed) { L.push_back({"Emergency handled", c.payout * 15 / 100}); reputation++; }
    location = c.to;
    if (src == SRC_OWNED) { int oi = ownedIndexFor(si); if (oi >= 0) fleet[oi].location = c.to; }
    if (c.longHaul()) {   // delivered across the ocean: the career, and your aircraft, go on from there
      region = c.toRegion;
      if (src == SRC_OWNED) { int oi = ownedIndexFor(si); if (oi >= 0) fleet[oi].region = c.toRegion; }
    }
    boardSeed++;
  }
  if (r.outcome != OUT_CRASHED) wear(L, si, src, r);
  if (routeFlightQualifies(r)) airlineTick(L, r.flightMin);
  payLoan(L);
  int total = 0; for (auto& l : L) total += l.amount;
  money += total;
  refreshBoard();
  return L;
}

bool Career::buy(int si, std::string* msg) {
  if (!isCareerAircraft(si)) { if (msg) *msg = "Not available in the career fleet"; return false; }
  const AircraftSpec& s = kAircraft[si];
  if (ownedIndexFor(si) >= 0) { *msg = "You already own one."; return false; }
  if (license < s.license) { *msg = std::string("Requires ") + licenseName(s.license); return false; }
  if (money < s.price) { *msg = fmt("Not enough money ($%d needed)", s.price); return false; }
  money -= s.price;
  fleet.push_back({si, location, s.maxFuel, 1.f, region});
  *msg = fmt("Purchased %s! It's waiting at %s.", s.name, g_world.airports[location].code);
  refreshBoard();
  return true;
}

bool Career::finance(int si, std::string* msg) {
  if (!isCareerAircraft(si)) { if (msg) *msg = "Not available in the career fleet"; return false; }
  const AircraftSpec& s = kAircraft[si];
  if (ownedIndexFor(si) >= 0) { *msg = "You already own one."; return false; }
  if (license < s.license) { *msg = std::string("Requires ") + licenseName(s.license); return false; }
  if (loan.open()) { *msg = "One loan at a time: finish paying for your " + std::string(kAircraft[loan.spec].name) + " first."; return false; }
  int down = downPayment(si);
  if (money < down) { *msg = fmt("Not enough for the down payment ($%d)", down); return false; }
  money -= down;
  loan.spec = si; loan.rate = loanRate(); loan.balance = (int)((s.price - down) * (1.f + loan.rate)); loan.payment = loanPayment(si); loan.missed = 0;
  fleet.push_back({si, location, s.maxFuel, 1.f, region});
  *msg = fmt("Financed %s: $%d down, $%d per flight for %d flights (%.0f%% interest). It's waiting at %s.", s.name, down, loan.payment, kLoanTerm, loan.rate * 100.f, g_world.airports[location].code);
  refreshBoard();
  return true;
}
bool Career::buyUsed(int si, std::string* msg) {
  if (!isCareerAircraft(si)) { if (msg) *msg = "Not available in the career fleet"; return false; }
  const AircraftSpec& s = kAircraft[si];
  if (ownedIndexFor(si) >= 0) { *msg = "You already own one."; return false; }
  if (license < s.license) { *msg = std::string("Requires ") + licenseName(s.license); return false; }
  int price = usedPrice(si);
  if (money < price) { *msg = fmt("Not enough money ($%d needed)", price); return false; }
  money -= price;
  fleet.push_back({si, location, s.maxFuel * 0.5f, 0.65f, region});
  *msg = fmt("Bought a used %s for $%d: it's seen some hours. Waiting at %s with half tanks.", s.name, price, g_world.airports[location].code);
  refreshBoard();
  return true;
}
// ------------------------------------------------------------------ the airline (C11)
int Career::routeOf(int fi) const { for (size_t i = 0; i < airline.routes.size(); i++) if (airline.routes[i].fleetIdx == fi) return (int)i; return -1; }
int Career::routeRevenue(const Route& r) const {
  if (r.fleetIdx < 0 || r.fleetIdx >= (int)fleet.size()) return 0;
  const AircraftSpec& s = kAircraft[fleet[r.fleetIdx].spec];
  float km = solaceDistanceKm(r.from, r.to);
  float load = r.pilot >= 0 && r.pilot < (int)airline.pilots.size() ? 0.5f + 0.17f * airline.pilots[r.pilot].rating : 0.5f;   // 0.67 / 0.84 / 1.0
  float gross = km * (s.pax * 4.5f + s.cargoKg * 0.02f) * load + 60.f;   // (a 28 km Archipel 400 leg grosses about $5,900 with a full cabin)
  return (int)gross / 10 * 10;
}
int Career::routeFuelCost(const Route& r) const {
  if (r.fleetIdx < 0 || r.fleetIdx >= (int)fleet.size()) return 0;
  const AircraftSpec& s = kAircraft[fleet[r.fleetIdx].spec];
  float km = solaceDistanceKm(r.from, r.to) + 8.f;
  float kg = s.engineType == ENG_JET ? jetMissionFuel(s, km * 1000.f / (s.cruise * .85f) / 60.f, 1.f, 3.f) : s.maxFuel / std::max(s.rangeKm, 1.f) * km * 1.1f;
  return (int)(kg * s.fuelPriceBase() * 1.1f);
}
std::vector<Career::Pilot> Career::pilotCandidates() const {
  static const char* names[] = {"A. Okafor", "M. Lindqvist", "R. Tanaka", "S. Delgado", "J. Mbeki", "E. Novak", "T. Haddad", "L. Marchetti", "K. Oyelaran", "P. Svensson", "D. Achebe", "N. Kowalski"};
  std::vector<Pilot> out;
  Rng r(boardSeed * 7919u + 17u);
  for (int i = 0; i < 3; i++) {
    Pilot p; p.name = names[(r.next() + i * 5) % 12]; p.rating = 1 + (int)(r.uni() * 2.99f); p.wage = p.rating == 1 ? 120 : p.rating == 2 ? 220 : 360;
    bool dup = false; for (auto& q : out) if (q.name == p.name) dup = true; for (auto& q : airline.pilots) if (q.name == p.name) dup = true;
    if (dup) p.name = std::string(names[(r.next() + 7) % 12]) + " II";
    out.push_back(p);
  }
  return out;
}
bool Career::hirePilot(const Pilot& p, std::string* msg) {
  if (!airlineOpen()) { *msg = "The airline opens with your Airline Transport licence."; return false; }
  if (airline.pilots.size() >= 6) { *msg = "Six pilots is the payroll's limit."; return false; }
  for (auto& q : airline.pilots) if (q.name == p.name) { *msg = p.name + " already flies for you."; return false; }
  airline.pilots.push_back(p); boardSeed++;
  *msg = fmt("%s hired: rating %d, $%d a flight.", p.name.c_str(), p.rating, p.wage);
  return true;
}
bool Career::firePilot(int pi, std::string* msg) {
  if (pi < 0 || pi >= (int)airline.pilots.size()) return false;
  for (auto& r : airline.routes) if (r.pilot == pi) { *msg = airline.pilots[pi].name + " is flying a route: recall it first."; return false; }
  *msg = airline.pilots[pi].name + " let go.";
  airline.pilots.erase(airline.pilots.begin() + pi);
  for (auto& r : airline.routes) if (r.pilot > pi) r.pilot--;
  return true;
}
bool Career::assignRoute(int fi, int to, int pilot, std::string* msg) {
  if (!airlineOpen()) { *msg = "The airline opens with your Airline Transport licence."; return false; }
  if (fi < 0 || fi >= (int)fleet.size()) return false;
  if (!atHome() || fleet[fi].region != "solace") { *msg = "The airline flies in the Solace Islands: put it on a route there."; return false; }
  if (routeOf(fi) >= 0) { *msg = "That aircraft is already on a route."; return false; }
  if (pilot < 0 || pilot >= (int)airline.pilots.size()) { *msg = "Hire a pilot first."; return false; }
  for (auto& r : airline.routes) if (r.pilot == pilot) { *msg = airline.pilots[pilot].name + " is already flying a route."; return false; }
  int from = fleet[fi].location;
  if (to == from) { *msg = "Pick another destination."; return false; }
  const AircraftSpec& s = kAircraft[fleet[fi].spec];
  Contract c; c.from = from; c.to = to; c.type = CT_CARGO;
  std::string why;
  if (canFly(c, fleet[fi].spec, &why) == SRC_NONE && why.find("airline route") == std::string::npos) { *msg = "That aircraft can't fly the route: " + why; return false; }
  Route r; r.fleetIdx = fi; r.from = from; r.to = to; r.pilot = pilot;
  airline.routes.push_back(r);
  *msg = fmt("%s on the %s - %s route with %s.", s.name, solaceAirport(from).code, solaceAirport(to).code, airline.pilots[pilot].name.c_str());
  return true;
}
bool Career::recallRoute(int ri, std::string* msg) {
  if (ri < 0 || ri >= (int)airline.routes.size()) return false;
  const Route& r = airline.routes[ri];
  *msg = fmt("%s recalled: it waits at %s.", kAircraft[fleet[r.fleetIdx].spec].name, solaceAirport(fleet[r.fleetIdx].location).code);
  airline.routes.erase(airline.routes.begin() + ri);
  return true;
}
LandingMark landingMark(const FlightResult& r, const AircraftSpec& s) {
  LandingMark m;
  if (!r.landed || r.tdPastThrM < 0 || r.rwyLenM <= 0) return m;
  const int rates[5] = {40, 33, 25, 12, 0};
  m.rate = rates[touchdownGrade(r.touchdownFpm)];
  m.zone = r.tdPastThrM <= r.rwyLenM / 3.f ? 25 : r.tdPastThrM <= r.rwyLenM * 0.45f ? 12 : 0;
  m.line = r.centerlineErr < 2.f ? 15 : r.centerlineErr < 5.f ? 9 : r.centerlineErr < 10.f ? 4 : 0;
  const float vref = s.vref * MS_TO_KT;
  if (r.thrKt < 0) m.approach = 10;   // (nothing recorded over the threshold: half)
  else {
    m.approach += r.thrKt >= vref - 5.f && r.thrKt <= vref + 15.f ? 10 : r.thrKt >= vref - 8.f && r.thrKt <= vref + 25.f ? 5 : 0;
    m.approach += r.thrAglM >= 9.f && r.thrAglM <= 24.5f ? 10 : r.thrAglM >= 5.f && r.thrAglM <= 35.f ? 5 : 0;
  }
  m.score = m.rate + m.zone + m.line + m.approach;
  m.letter = m.score >= 92 ? 'S' : m.score >= 80 ? 'A' : m.score >= 65 ? 'B' : m.score >= 45 ? 'C' : 'D';
  return m;
}
// The airline flies its routes once for each of your flights that got somewhere: one that took off and landed on a
// runway, at its destination or diverted. Abandoned, crashed and off-field flights, and starts that never left the
// ground, fly none (the review of v3.24.0, R3: three zero-time aborts earned $24,696)
// A checkride's standard, as its briefing states it: landed, no hard landing (over 600 fpm, the settlement's own
// line), and the tower's instructions kept (no take-off against a hold, no landing against a go-around). The examiner
// fails anything short of it: no licence, the story waits, the checkride is flown again (the review of v3.24.0, R8)
std::string Career::checkrideFault(const Contract& c, const FlightResult& r) {
  if (c.grantLicense < 0) return "";
  if (!r.landed) return "no landing";
  if (touchdownGrade(r.touchdownFpm) == TD_HARD) return fmt("hard landing (%.0f fpm)", fabsf(r.touchdownFpm));
  if (r.holdViolated) return "took off against a hold instruction";
  if (r.landedAgainstGoAround) return "landed against a go-around instruction";
  return "";
}
bool Career::routeFlightQualifies(const FlightResult& r) {
  return r.landed && r.flightMin > 0.f && (r.outcome == OUT_SUCCESS || r.outcome == OUT_DIVERTED);
}
float Career::routeLegMinutes(const Route& rt) const {
  if (rt.fleetIdx < 0 || rt.fleetIdx >= (int)fleet.size()) return 1e9f;
  const AircraftSpec& s = kAircraft[fleet[rt.fleetIdx].spec];
  return solaceDistanceKm(rt.from, rt.to) * 1000.f / Plane::perf(&s).cruiseV / 60.f + 6.f;   // (6 min on the stand)
}
void Career::airlineTick(std::vector<PayoutLine>& L, float minutes) {
  if (airline.routes.empty() || !(minutes > 0.f)) return;
  Rng r(boardSeed * 48271u + flights * 7u + 3u);
  int net = 0, flown = 0;
  for (auto& rt : airline.routes) {
    if (rt.fleetIdx < 0 || rt.fleetIdx >= (int)fleet.size() || rt.pilot < 0 || rt.pilot >= (int)airline.pilots.size()) continue;
    rt.progressMin = std::min(rt.progressMin + minutes, 24.f * 60.f);   // (a day's flying at most banked)
    for (float leg = routeLegMinutes(rt); rt.progressMin >= leg; leg = routeLegMinutes(rt)) {
    rt.progressMin -= leg;
    OwnedPlane& p = fleet[rt.fleetIdx]; const AircraftSpec& s = kAircraft[p.spec]; const Pilot& pl = airline.pilots[rt.pilot];
    int gross = routeRevenue(rt), fuel = routeFuelCost(rt), wage = pl.wage;
    float km = solaceDistanceKm(rt.from, rt.to);
    p.condition = clampf(p.condition - (km * 1000.f / Plane::perf(&s).cruiseV / 3600.f) * 0.015f - (pl.rating == 1 ? 0.01f : 0.f), 0.05f, 1.f);
    int repair = 0;
    float pInc = (0.09f - 0.03f * pl.rating) * (1.5f - p.condition);   // a weak pilot in a worn aircraft: up to ~9% a flight
    if (r.uni() < pInc) {
      repair = std::max(80, (int)(s.price * (0.004f + 0.012f * r.uni())) / 10 * 10);
      airline.incidents++;
      const char* what[] = {"hard landing", "bird strike", "gear scrape", "engine over-temp", "wing-tip strike on the stand"};
      std::string w = what[r.next() % 5];
      if (insured) { L.push_back({fmt("Airline: %s - %s, repairs covered", s.name, w.c_str()), -insurancePremium(p.spec)}); airline.log.push_back(fmt("%s: %s (insured)", pl.name.c_str(), w.c_str())); repair = 0; }
      else { L.push_back({fmt("Airline: %s - %s, repairs", s.name, w.c_str()), -repair}); airline.log.push_back(fmt("%s: %s, $%d", pl.name.c_str(), w.c_str(), repair)); }
      p.condition = clampf(p.condition - 0.04f, 0.05f, 1.f);
    }
    int flightNet = gross - fuel - wage;
    net += flightNet; flown++;   // (the repair is its own line above: not taken from the route total as well)
    rt.flights++; rt.earned += flightNet;
    std::swap(rt.from, rt.to); p.location = rt.from;   // it flew the leg and waits at the other end
    p.fuel = s.maxFuel * 0.6f;
    }
  }
  if (flown) {
    L.push_back({fmt("Airline: %d route flight%s (fares less fuel and wages)", flown, flown == 1 ? "" : "s"), net});
    airline.earned += net;
    if (airline.log.size() > 8) airline.log.erase(airline.log.begin(), airline.log.begin() + (airline.log.size() - 8));
  }
}

float Career::failureChance(Source src, int si) const {
  if (src == SRC_MILITARY || src == SRC_LESSON || src == SRC_NONE || kAircraft[si].special) return 0.f;
  if (src == SRC_RENT) return 0.02f;
  int oi = ownedIndexFor(si);
  float cond = oi >= 0 ? clampf(fleet[oi].condition, 0.f, 1.f) : 1.f;
  return clampf(0.05f * (1.5f - cond), 0.025f, 0.08f);   // 2.5% as new, 7.5% worn out
}
int Career::serviceCost(int fi) const {
  if (fi < 0 || fi >= (int)fleet.size()) return 0;
  const AircraftSpec& s = kAircraft[fleet[fi].spec];
  return (int)(s.price * 0.05f * (1.f - clampf(fleet[fi].condition, 0.f, 1.f))) / 10 * 10 + 50;
}
bool Career::service(int fi, std::string* msg) {
  if (fi < 0 || fi >= (int)fleet.size()) return false;
  if (!here(fleet[fi]) || fleet[fi].location != location) { *msg = "The aircraft isn't here."; return false; }
  int cost = serviceCost(fi);
  if (money < cost) { *msg = fmt("Not enough money ($%d)", cost); return false; }
  money -= cost; fleet[fi].condition = 1.f;
  *msg = fmt("%s serviced for $%d: as new.", kAircraft[fleet[fi].spec].name, cost);
  return true;
}
int Career::repairCost(int si, int kinds, bool belly) {
  const AircraftSpec& s = kAircraft[si];
  float f = belly ? 0.08f : 0.f;
  if (kinds & (1 << FAIL_ENGINE_TOTAL)) f += 0.04f;
  if (kinds & (1 << FAIL_ENGINE_PARTIAL)) f += 0.015f;
  if (kinds & (1 << FAIL_ALTERNATOR)) f += 0.006f;
  if (kinds & (1 << FAIL_PITOT)) f += 0.002f;
  if (kinds & (1 << FAIL_GEAR_STUCK)) f += 0.012f;
  if (kinds & (1 << FAIL_FLAP_ASYM)) f += 0.008f;
  return f > 0 ? std::max(60, (int)(s.price * f) / 10 * 10) : 0;
}
void Career::wear(std::vector<PayoutLine>& L, int si, Source src, const FlightResult& r) {
  const AircraftSpec& s = kAircraft[si];
  int repair = repairCost(si, r.failureKinds, r.bellyLanding);
  std::string what;
  for (int k = 1; k < FAIL_COUNT; k++) if (r.failureKinds & (1 << k)) { if (!what.empty()) what += ", "; what += failureName(k); }
  if (r.bellyLanding) { if (!what.empty()) what += ", "; what += "belly landing"; }
  if (src == SRC_OWNED) {
    int oi = ownedIndexFor(si);
    if (oi >= 0) {
      float loss = r.flightMin / 60.f * 0.015f;   // 1.5% an hour
      float fpm = fabsf(r.touchdownFpm);
      if (r.landed && touchdownGrade(fpm) == TD_HARD) loss += 0.08f; else if (r.landed && touchdownGrade(fpm) == TD_FIRM) loss += 0.03f;
      if (r.bellyLanding) loss += 0.3f;
      fleet[oi].condition = clampf(fleet[oi].condition - loss, 0.05f, 1.f);
      if (repair) {
        if (insured) L.push_back({"Repairs covered by insurance (" + what + ")", 0});
        else L.push_back({"Repairs: " + what, -repair});
      }
      if (insured) L.push_back({"Insurance premium", -insurancePremium(si)});
    }
  } else if (src == SRC_RENT && r.bellyLanding) L.push_back({"Insurance deductible (belly landing)", -(300 + s.rentFee * 2)});
}
void Career::payLoan(std::vector<PayoutLine>& L) {
  if (!loan.open()) return;
  int oi = ownedIndexFor(loan.spec);
  if (oi < 0) { loan = Loan(); return; }   // (sold: the sale settled it)
  int due = std::min(loan.payment, loan.balance);
  int total = 0; for (auto& l : L) total += l.amount;
  if (money + total >= due) {
    L.push_back({fmt("Loan payment on the %s (%d left)", kAircraft[loan.spec].name, std::max(0, (loan.balance - due + loan.payment - 1) / std::max(loan.payment, 1))), -due});
    loan.balance -= due; loan.missed = 0;
    if (loan.balance <= 0) { L.push_back({"Loan paid off - the aircraft is yours", 0}); loan = Loan(); }
  } else {
    loan.missed++;
    if (loan.missed >= 3) {
      L.push_back({fmt("%s repossessed: three payments missed", kAircraft[loan.spec].name), 0});
      // the airline loses the aircraft with it: its route goes, the routes of the aircraft after it move down (as a sale)
      int ri = routeOf(oi);
      if (ri >= 0) { L.push_back({"Its airline route is closed", 0}); airline.routes.erase(airline.routes.begin() + ri); }
      for (auto& rt : airline.routes) if (rt.fleetIdx > oi) rt.fleetIdx--;
      fleet.erase(fleet.begin() + oi); loan = Loan();
    } else L.push_back({fmt("Loan payment missed (%d of 3 before repossession)", loan.missed), 0});
  }
}
int Career::saleValue(int fi) const {
  if (fi < 0 || fi >= (int)fleet.size()) return 0;
  return (int)(kAircraft[fleet[fi].spec].price * 7 / 10 * (0.6f + 0.4f * clampf(fleet[fi].condition, 0.f, 1.f)));
}
bool Career::sell(int fi, std::string* msg) {
  if (fi < 0 || fi >= (int)fleet.size()) return false;
  if (routeOf(fi) >= 0) { *msg = "It's flying a route: recall it first."; return false; }
  for (auto& rt : airline.routes) if (rt.fleetIdx > fi) rt.fleetIdx--;
  const AircraftSpec& s = kAircraft[fleet[fi].spec];
  const int val = saleValue(fi);
  money += val;
  *msg = fmt("Sold %s for $%d.", s.name, val);
  if (loan.open() && loan.spec == fleet[fi].spec) { money -= loan.balance; *msg += fmt(" The loan's $%d balance was settled from it.", loan.balance); loan = Loan(); }
  fleet.erase(fleet.begin() + fi);
  refreshBoard();
  return true;
}

// Saves go to a sibling temp file first; only a fully written, flushed and closed file replaces the career, and the
// previous save is kept as <path>.bak. A failed write leaves the old save untouched and reports false.
bool replaceFile(const std::string& from, const std::string& to) {
#ifdef _WIN32
  return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return rename(from.c_str(), to.c_str()) == 0;
#endif
}

bool Career::save(const std::string& path) const {
  std::string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "w");
  if (!f) return false;
  // (version 7 is a career with something beyond the islands - its own region, or an aircraft parked in one: a career
  // that never left them writes version 6 exactly, as every release before could read)
  // (version 8: a career on the real Earth - "earth 1"; one with the generated regions writes 7 or 6 as before)
  bool away = !atHome();
  for (auto& p : fleet) away = away || p.region != "solace";
  bool ok = fprintf(f, "somnium_save %d\n", realEarth ? 8 : away ? 7 : 6) > 0;
  if (realEarth) ok = ok && fprintf(f, "earth 1\n") > 0;
  if (away) ok = ok && fprintf(f, "region %s\n", region.c_str()) > 0;
  ok = ok && fprintf(f, "money %d\nlicense %d\nrep %d\nlocation %d\nstory %d\nflights %d\nlandings %d\ncrashes %d\nhours %f\nbest %f\nseed %u\nfinished %d\nattempt %u\nattempt_open %d\n",
                    money, license, reputation, location, storyIndex, flights, landings, crashes, hours, bestLandingFpm, boardSeed, finished ? 1 : 0, attempt, attemptOpen ? 1 : 0) > 0;
  ok = ok && fprintf(f, "military %d %d %d %d %d %d %.9g %u %u %u %d %d\n",
    military.rank, military.credits, military.sorties, military.successes, military.failures, military.intelligence,
    military.hours, military.activeAttempt, military.lastSettledAttempt, military.missionSeed, military.activeKind, military.activeAirport) > 0;
  ok = ok && fprintf(f, "military_progress %u\n", military.completedMissions) > 0;
  ok = ok && fprintf(f, "fleet %d\n", (int)fleet.size()) > 0;
  for (auto& p : fleet) ok = ok && fprintf(f, "plane %s %d %f %f\n", kAircraft[p.spec].id, p.location, p.fuel, p.condition) > 0;
  for (size_t i = 0; i < fleet.size(); i++) if (fleet[i].region != "solace") ok = ok && fprintf(f, "plane_region %d %s\n", (int)i, fleet[i].region.c_str()) > 0;
  if (loan.open()) ok = ok && fprintf(f, "loan %s %d %d %d %f\n", kAircraft[loan.spec].id, loan.balance, loan.payment, loan.missed, loan.rate) > 0;
  ok = ok && fprintf(f, "insured %d\n", insured ? 1 : 0) > 0;
  for (auto& p : airline.pilots) { std::string n = p.name; for (char& ch : n) if (ch == ' ') ch = '_'; ok = ok && fprintf(f, "pilot %s %d %d\n", n.c_str(), p.rating, p.wage) > 0; }
  for (auto& r : airline.routes) ok = ok && fprintf(f, "route %d %d %d %d %d %d %.3f\n", r.fleetIdx, r.from, r.to, r.pilot, r.flights, r.earned, r.progressMin) > 0;
  if (!airline.routes.empty() || !airline.pilots.empty() || airline.earned) ok = ok && fprintf(f, "airline %d %d\n", airline.earned, airline.incidents) > 0;
  if (job) {   // the open job: its state, then its contract (a story contract by id, a freelance one in full)
    const JobState& J = *job;
    ok = ok && fprintf(f, "job %d %s %d %d %d %d %f %f %f %f %d %f %d %d %u\n", (int)J.state, kAircraft[J.spec].id, (int)J.src, J.at, J.legs, J.wpDone, J.jobClockMin,
                       J.maxG, J.minG, J.maxBank, J.fragileHit ? 1 : 0, J.fuelBilledKg, J.hirePaid ? 1 : 0, J.positioningPaid ? 1 : 0, J.id) > 0;
    ok = ok && fprintf(f, "job2 %f %f %d\n", J.patient, J.comfort, J.ferryPaid ? 1 : 0) > 0;
    ok = ok && fprintf(f, "job3 %f %f\n", J.surveySec, J.surveyInSec) > 0;
    ok = ok && fprintf(f, "survey_known %d\n", J.surveyHistoryKnown ? 1 : 0) > 0;
    if (J.holdViolated || J.landedAgainstGoAround) ok = ok && fprintf(f, "job_atc %d %d\n", J.holdViolated ? 1 : 0, J.landedAgainstGoAround ? 1 : 0) > 0;   // (only when set: a save without one stays readable by v3.44)
    ok = ok && fprintf(f, "plan %d %d %d %d %f %f %f %f %d\n", J.plan.positioning, J.plan.ferry, J.plan.hire, (int)J.plan.fuel, J.plan.fuelKgEst, J.plan.minutesEst, J.plan.minutesSigma, J.plan.fuelUpliftKg, J.plan.fuelCostEst) > 0;
    ok = ok && fprintf(f, "plan2 %d %f %d\n", J.plan.startAirport, J.plan.fuelLoadKg, J.plan.flown ? 1 : 0) > 0;
    const Contract& c = J.c;
    bool story = false; for (auto& s : g_story) if (s.id == c.id) story = true;
    if (story) ok = ok && fprintf(f, "story_contract %s\n", c.id.c_str()) > 0;
    else {
      ok = ok && fprintf(f, "contract %s %d %d %d %d %d %d %f %d %d %d %d %d %d %d %d %d\n", c.id.c_str(), c.type, c.from, c.to, c.cargoKg, c.pax, c.payout, c.timeLimitMin,
                         c.minLicense, c.ownedOnly ? 1 : 0, c.fragile ? 1 : 0, c.startAirborne ? 1 : 0, c.repBonusPct, c.chapter, c.grantLicense, c.forceAircraft, c.courtesy ? 1 : 0) > 0;
      const Weather& w = c.wx;
      ok = ok && fprintf(f, "wx %f %f %f %f %f %f %f %d %d %f\n", w.windFrom, w.windSpeed, w.gust, w.turbulence, w.cloudCover, w.cloudBase, w.visibility, w.precip, w.storm ? 1 : 0, w.timeOfDay) > 0;
      if (c.wxShift) { const Weather& v = c.wxEnd; ok = ok && fprintf(f, "wx2 %f %f %f %f %f %f %f %d %d %f\n", v.windFrom, v.windSpeed, v.gust, v.turbulence, v.cloudCover, v.cloudBase, v.visibility, v.precip, v.storm ? 1 : 0, v.timeOfDay) > 0; }
      ok = ok && fprintf(f, "wps %d\n", (int)c.wps.size()) > 0;
      for (auto& p : c.wps) ok = ok && fprintf(f, "wp %f %f %f\n", p.x, p.z, p.alt) > 0;
      ok = ok && fprintf(f, "title %d %s\nbrief %d %s\n", (int)c.title.size(), c.title.c_str(), (int)c.brief.size(), c.brief.c_str()) > 0;
    }
  }
  ok = ok && fprintf(f, "end\n") > 0;
  ok = ok && fflush(f) == 0 && !ferror(f);
  ok = (fclose(f) == 0) && ok;
  if (!ok) { remove(tmp.c_str()); return false; }
  // the current save becomes the backup only if it is itself a good save: a damaged one (the game may have just
  // recovered from the backup) must never replace the good backup. If the final replacement then fails, the
  // backup still holds the last good career.
  { Career probe; if (probe.load(path)) replaceFile(path, path + ".bak"); }
  if (!replaceFile(tmp, path)) { remove(tmp.c_str()); return false; }
  return true;
}

// Parses into a temporary career and only commits it when the file is complete and every value is in range:
// a damaged save is rejected (the caller then falls back to the backup) instead of crashing later or silently
// resetting progress.
bool Career::load(const std::string& path) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) return false;
  Career c; char key[64]; int ver = 0;
  if (fscanf(f, "%63s %d", key, &ver) != 2 || (strcmp(key, "somnium_save") && strcmp(key, "solace_save") && strcmp(key, "airxpress_save")) || ver < 1 || ver > 8) { fclose(f); return false; }
  // the career's airports are its region's (the islands' until a "region" line, which comes first), wherever g_world is;
  // the airline's routes and the military are the islands'; an aircraft's are the region it is parked in (checked last)
  int nApt = solaceAirportCount();
  const int nHome = solaceAirportCount();
  bool ok = true;
  bool earthGone = false;   // (a real-Earth career without its pack: home, its job released - never lost)
  unsigned have = 0;   // mandatory fields seen (version 2: every field, the fleet count and the end marker)
  int fleetN = -1; bool ended = false, militaryRead = false, militaryProgressRead = false, surveyRead = false, surveyKnownRead = false, plan2Read = false;
  auto rdI = [&](int& v, unsigned bit) { ok = ok && fscanf(f, "%d", &v) == 1; have |= bit; };
  auto rdF = [&](float& v) { ok = ok && fscanf(f, "%f", &v) == 1 && std::isfinite(v); };
  while (ok && fscanf(f, "%63s", key) == 1) {
    int fin = 0;
    if (!strcmp(key, "region")) {
      char id[64]; ok = ver >= 7 && (have & 4) == 0 && fscanf(f, "%63s", id) == 1;
      if (ok && earthCharts::isEarthId(id) && procedural::airportCount(id) == 0) { earthGone = true; continue; }
      ok = ok && procedural::airportCount(id) > 0;
      // (a generated region's airports are its recipe's: read up to the most a region has, and any its recipe no longer
      // has are moved to its main airport below - a save is never lost to a change in the landscape; a real-Earth
      // chart's, as many as it has now)
      if (ok) { c.region = id; nApt = id == std::string("solace") ? nHome : std::max((int)procedural::kMaxAirports, procedural::airportCount(id)); }
    }
    else if (!strcmp(key, "earth")) { int e = 0; ok = ver >= 8 && fscanf(f, "%d", &e) == 1 && (e == 0 || e == 1); c.realEarth = e == 1; }
    else if (!strcmp(key, "money")) rdI(c.money, 1);
    else if (!strcmp(key, "license")) rdI(c.license, 2);
    else if (!strcmp(key, "rep")) rdI(c.reputation, 16);
    else if (!strcmp(key, "location")) rdI(c.location, 4);
    else if (!strcmp(key, "story")) rdI(c.storyIndex, 8);
    else if (!strcmp(key, "flights")) rdI(c.flights, 32);
    else if (!strcmp(key, "landings")) rdI(c.landings, 64);
    else if (!strcmp(key, "crashes")) rdI(c.crashes, 128);
    else if (!strcmp(key, "hours")) { rdF(c.hours); have |= 256; }
    else if (!strcmp(key, "best")) { rdF(c.bestLandingFpm); have |= 512; }
    else if (!strcmp(key, "seed")) { ok = fscanf(f, "%u", &c.boardSeed) == 1; have |= 1024; }
    else if (!strcmp(key, "finished")) { rdI(fin, 2048); ok = ok && (fin == 0 || fin == 1); c.finished = fin != 0; }
    else if (!strcmp(key, "attempt")) ok = fscanf(f, "%u", &c.attempt) == 1;
    else if (!strcmp(key, "attempt_open")) { int ao = 0; ok = fscanf(f, "%d", &ao) == 1 && (ao == 0 || ao == 1); c.attemptOpen = ao != 0; }
    else if (!strcmp(key, "military")) {
      MilitaryRecord& m = c.military;
      ok = ver >= 5 && !militaryRead && fscanf(f, "%d %d %d %d %d %d %f %u %u %u %d %d",
        &m.rank, &m.credits, &m.sorties, &m.successes, &m.failures, &m.intelligence, &m.hours,
        &m.activeAttempt, &m.lastSettledAttempt, &m.missionSeed, &m.activeKind, &m.activeAirport) == 12;
      militaryRead = true;
    }
    else if (!strcmp(key, "military_progress")) {
      char bits[32] = {};
      ok = ver >= 6 && !militaryProgressRead && fscanf(f, "%31s", bits) == 1;
      if (ok) {
        const char* end = bits + strlen(bits);
        const auto parsed = std::from_chars(bits, end, c.military.completedMissions);
        ok = parsed.ec == std::errc() && parsed.ptr == end;
      }
      militaryProgressRead = true;
    }
    else if (!strcmp(key, "fleet")) ok = fscanf(f, "%d", &fleetN) == 1 && fleetN >= 0;
    else if (!strcmp(key, "plane")) {
      char id[64]; int loc = -1; float fuel = 0, cond = 1.f; int spec = -1;
      ok = fscanf(f, "%63s %d %f", id, &loc, &fuel) == 3;
      { long pos = ftell(f); float cv = 0; if (fscanf(f, "%f", &cv) == 1 && std::isfinite(cv)) cond = cv; else fseek(f, pos, SEEK_SET); }   // (version 3 adds the condition)
      for (int i : kCareerAircraft) if (ok && !strcmp(kAircraft[i].id, id)) spec = i;
      ok = ok && spec >= 0 && loc >= 0 && std::isfinite(fuel) && c.ownedIndexFor(spec) < 0;   // (one of each type; its airport checked once its region is known)
      if (ok) c.fleet.push_back({spec, loc, std::clamp(fuel, 0.f, kAircraft[spec].maxFuel), std::clamp(cond, 0.f, 1.f)});
    }
    else if (!strcmp(key, "plane_region")) {
      int i = -1; char id[64];
      ok = ver >= 7 && fscanf(f, "%d %63s", &i, id) == 2 && i >= 0 && i < (int)c.fleet.size();
      if (ok && earthCharts::isEarthId(id) && procedural::airportCount(id) == 0) { c.fleet[i].location = procedural::mainAirport("solace"); continue; }   // (its pack gone: home)
      ok = ok && procedural::airportCount(id) > 0;
      if (ok) c.fleet[i].region = id;
    }
    else if (!strcmp(key, "insured")) { int v = 0; ok = fscanf(f, "%d", &v) == 1 && (v == 0 || v == 1); c.insured = v == 1; }
    else if (!strcmp(key, "pilot")) { char n[64]; Pilot p; ok = fscanf(f, "%63s %d %d", n, &p.rating, &p.wage) == 3 && p.rating >= 1 && p.rating <= 3 && p.wage >= 0 && c.airline.pilots.size() < 6; if (ok) { p.name = n; for (char& ch : p.name) if (ch == '_') ch = ' '; c.airline.pilots.push_back(p); } }
    else if (!strcmp(key, "route")) {
      Route r; ok = fscanf(f, "%d %d %d %d %d %d", &r.fleetIdx, &r.from, &r.to, &r.pilot, &r.flights, &r.earned) == 6 && r.fleetIdx >= 0 && r.from >= 0 && r.from < nHome && r.to >= 0 && r.to < nHome && r.pilot >= 0 && r.flights >= 0;
      { long pos = ftell(f); float pm = 0; if (fscanf(f, "%f", &pm) == 1 && std::isfinite(pm) && pm >= 0.f) r.progressMin = std::min(pm, 24.f * 60.f); else fseek(f, pos, SEEK_SET); }   // (v3.45 adds the leg's progress)
      for (const Route& o : c.airline.routes) ok = ok && o.fleetIdx != r.fleetIdx;   // (an aircraft flies one route)
      if (ok) c.airline.routes.push_back(r);
    }
    else if (!strcmp(key, "airline")) ok = fscanf(f, "%d %d", &c.airline.earned, &c.airline.incidents) == 2;
    else if (!strcmp(key, "loan")) {
      char id[64]; Loan l;
      ok = fscanf(f, "%63s %d %d %d %f", id, &l.balance, &l.payment, &l.missed, &l.rate) == 5 && l.balance >= 0 && l.payment >= 0 && l.missed >= 0 && l.missed < 3 && std::isfinite(l.rate) && l.rate >= 0.f
           && (l.balance == 0 || l.payment > 0);   // (an open loan is paid down: one with no payment never closed)
      l.spec = -1; for (int i : kCareerAircraft) if (ok && !strcmp(kAircraft[i].id, id)) l.spec = i;
      ok = ok && l.spec >= 0;
      if (ok) c.loan = l;
    }
    else if (!strcmp(key, "job")) {   // (version 3) the open job, followed by its plan and contract lines
      JobState J; char id[64]; int st = 0, src = 0, fh = 0, hp = 0, pp = 0;
      ok = fscanf(f, "%d %63s %d %d %d %d %f %f %f %f %d %f %d %d %u", &st, id, &src, &J.at, &J.legs, &J.wpDone, &J.jobClockMin, &J.maxG, &J.minG, &J.maxBank, &fh, &J.fuelBilledKg, &hp, &pp, &J.id) == 15;
      J.spec = -1; for (int i : kCareerAircraft) if (ok && !strcmp(kAircraft[i].id, id)) J.spec = i;
      ok = ok && J.spec >= 0 && st >= 0 && st <= 5 && src >= 0 && src <= 3 && J.at >= 0 && J.at < nApt && J.legs >= 0 && J.wpDone >= 0
           && std::isfinite(J.jobClockMin) && std::isfinite(J.maxG) && std::isfinite(J.minG) && std::isfinite(J.maxBank) && std::isfinite(J.fuelBilledKg);
      J.state = (JobState::State)st; J.src = (Source)src; J.fragileHit = fh != 0; J.hirePaid = hp != 0; J.positioningPaid = pp != 0;
      J.spec = std::max(J.spec, 0);
      if (ok) { c.job = J; c.job->c.id.clear(); }
    }
    else if (!strcmp(key, "job2")) {   // the leg's carried ride and fees (saves before this line had none: the defaults stand)
      float pa = 1, co = 1; int fp = 0;
      ok = fscanf(f, "%f %f %d", &pa, &co, &fp) == 3 && std::isfinite(pa) && std::isfinite(co);
      if (ok && c.job) { c.job->patient = clampf(pa, 0.f, 1.f); c.job->comfort = clampf(co, 0.f, 1.f); c.job->ferryPaid = fp != 0; }
    }
    else if (!strcmp(key, "job3")) {   // the survey pattern so far (saves before this line had none: the defaults stand)
      float st = 0, si = 0;
      ok = fscanf(f, "%f %f", &st, &si) == 2 && std::isfinite(st) && std::isfinite(si);
      if (ok && c.job) { c.job->surveySec = std::max(st, 0.f); c.job->surveyInSec = clampf(si, 0.f, std::max(st, 0.f)); surveyRead = true; }
    }
    else if (!strcmp(key, "survey_known")) {
      int known = 0;
      ok = c.job && fscanf(f, "%d", &known) == 1 && (known == 0 || known == 1);
      if (ok) { c.job->surveyHistoryKnown = known != 0; surveyKnownRead = true; }
    }
    else if (!strcmp(key, "job_atc")) {   // the tower's instructions ignored on an earlier leg (absent: none)
      int hv = 0, ga = 0;
      ok = c.job && fscanf(f, "%d %d", &hv, &ga) == 2 && (hv == 0 || hv == 1) && (ga == 0 || ga == 1);
      if (ok) { c.job->holdViolated = hv != 0; c.job->landedAgainstGoAround = ga != 0; }
    }
    else if (!strcmp(key, "plan2")) {
      int airport = 0, flown = 0; float load = -1;
      ok = c.job && fscanf(f, "%d %f %d", &airport, &load, &flown) == 3 && airport >= 0 && airport < nApt
           && std::isfinite(load) && load >= -1.f && load <= kAircraft[c.job->spec].maxFuel && (flown == 0 || flown == 1);
      if (ok) { c.job->plan.startAirport = airport; c.job->plan.fuelLoadKg = load; c.job->plan.flown = flown != 0; plan2Read = true; }
    }
    else if (!strcmp(key, "plan")) {
      int fuel = 0; LaunchPlan p;
      ok = c.job && fscanf(f, "%d %d %d %d %f %f %f %f %d", &p.positioning, &p.ferry, &p.hire, &fuel, &p.fuelKgEst, &p.minutesEst, &p.minutesSigma, &p.fuelUpliftKg, &p.fuelCostEst) == 9 && fuel >= 0 && fuel <= 2
           && std::isfinite(p.fuelKgEst) && std::isfinite(p.minutesEst) && std::isfinite(p.minutesSigma) && std::isfinite(p.fuelUpliftKg);
      if (ok) { p.fuel = (LaunchPlan::FuelPolicy)fuel; p.spec = c.job->spec; p.src = c.job->src; c.job->plan = p; }
    }
    else if (!strcmp(key, "story_contract")) {
      char id[64]; ok = c.job && fscanf(f, "%63s", id) == 1;
      const Contract* found = nullptr; for (auto& s : g_story) if (s.id == id) found = &s;
      ok = ok && found; if (ok) c.job->c = *found;
    }
    else if (!strcmp(key, "contract")) {
      Contract k; char id[64]; int own = 0, fr = 0, sa = 0, cy = 0;
      ok = c.job && fscanf(f, "%63s %d %d %d %d %d %d %f %d %d %d %d %d %d %d %d %d", id, &k.type, &k.from, &k.to, &k.cargoKg, &k.pax, &k.payout, &k.timeLimitMin,
                           &k.minLicense, &own, &fr, &sa, &k.repBonusPct, &k.chapter, &k.grantLicense, &k.forceAircraft, &cy) == 17;
      k.courtesy = cy != 0;
      ok = ok && k.type >= 0 && k.type < CT_COUNT && !isMilitaryContract(k.type) && k.from >= 0 && k.from < nApt && k.to >= 0 && k.to < nApt && k.cargoKg >= 0 && k.pax >= 0 && std::isfinite(k.timeLimitMin)
           && k.minLicense >= LIC_STUDENT && k.minLicense <= LIC_ATP;
      if (ok) { k.id = id; k.ownedOnly = own != 0; k.fragile = fr != 0; k.startAirborne = sa != 0; k.story = false; c.job->c = k; }
    }
    else if (!strcmp(key, "wx") || !strcmp(key, "wx2")) {
      bool end = key[2] == '2';
      Weather w; int precip = 0, storm = 0;
      ok = c.job && fscanf(f, "%f %f %f %f %f %f %f %d %d %f", &w.windFrom, &w.windSpeed, &w.gust, &w.turbulence, &w.cloudCover, &w.cloudBase, &w.visibility, &precip, &storm, &w.timeOfDay) == 10;
      ok = ok && std::isfinite(w.windFrom) && std::isfinite(w.windSpeed) && std::isfinite(w.gust) && std::isfinite(w.turbulence) && std::isfinite(w.cloudCover) && std::isfinite(w.cloudBase)
           && std::isfinite(w.visibility) && std::isfinite(w.timeOfDay) && precip >= 0 && precip <= 2;
      if (ok) { w.precip = precip; w.storm = storm != 0; if (end) { c.job->c.wxEnd = w; c.job->c.wxShift = true; } else c.job->c.wx = w; }
    }
    else if (!strcmp(key, "wps")) { int n = 0; ok = c.job && fscanf(f, "%d", &n) == 1 && n >= 0 && n <= 64; if (ok) c.job->c.wps.clear(); }
    else if (!strcmp(key, "wp")) { Waypoint p; ok = c.job && fscanf(f, "%f %f %f", &p.x, &p.z, &p.alt) == 3 && std::isfinite(p.x) && std::isfinite(p.z) && std::isfinite(p.alt); if (ok) c.job->c.wps.push_back(p); }
    else if (!strcmp(key, "title") || !strcmp(key, "brief")) {
      int n = 0; ok = c.job && fscanf(f, "%d", &n) == 1 && n >= 0 && n < 4096 && fgetc(f) == ' ';
      std::string s; for (int i = 0; ok && i < n; i++) { int ch = fgetc(f); if (ch == EOF) ok = false; else s += (char)ch; }
      if (ok) { if (!strcmp(key, "title")) c.job->c.title = s; else c.job->c.brief = s; }
    }
    else if (!strcmp(key, "end")) { ended = true; break; }   // version 1 saves may have no marker and end at EOF
    else ok = false;   // unknown key: not a file this version wrote
  }
  fclose(f);
  if (ver >= 2) ok = ok && have == 4095 && ended && fleetN == (int)c.fleet.size();   // a truncated save is rejected
  if (earthGone) { c.region = "solace"; c.location = procedural::mainAirport("solace"); c.job.reset(); c.attemptOpen = false; }
  for (auto& r : c.airline.routes) if (r.fleetIdx >= (int)c.fleet.size() || r.pilot >= (int)c.airline.pilots.size() || c.fleet[r.fleetIdx].region != "solace") ok = false;   // (a route needs its aircraft, in the islands, and its pilot)
  for (auto& p : c.fleet)   // (each at an airport of its own region: the islands' exactly, a generated one's moved to its main one)
    if (p.location >= procedural::airportCount(p.region)) { if (p.region == "solace") ok = false; else p.location = procedural::mainAirport(p.region); }
  if (ok && !c.atHome()) {
    const int n = procedural::airportCount(c.region);
    if (c.location >= n) c.location = procedural::mainAirport(c.region);
    if (c.job && (c.job->at >= n || c.job->c.from >= n || c.job->c.to >= n || c.job->plan.startAirport >= n)) c.job.reset();   // (its stops gone: released)
  }
  if (c.job && (c.job->c.id.empty() || c.job->c.to < 0 || c.job->c.to >= nApt)) ok = false;   // a job needs its contract
  if (c.job && c.job->c.story && !c.atHome()) ok = false;   // (the campaign is flown in the islands)
  ok = ok && (have & 15) == 15
       && c.license >= LIC_STUDENT && c.license <= LIC_ATP && c.reputation >= 0   // (malformed states rejected too: the review of v3.44.0, CAR-11)
       && c.location >= 0 && c.location < nApt
       && c.storyIndex >= 0 && c.storyIndex <= (int)g_story.size()
       && c.flights >= 0 && c.landings >= 0 && c.crashes >= 0 && c.hours >= 0;
  if (ver >= 5) {
    const MilitaryRecord& m = c.military;
    ok = ok && militaryRead && m.rank >= 0 && m.rank <= 10 && m.credits >= 0 && m.credits <= 2000000000 &&
      m.sorties >= 0 && m.sorties <= 2000000000 && m.successes >= 0 && m.failures >= 0 &&
      int64_t(m.successes) + m.failures == m.sorties && m.rank == std::min(10, m.successes / 3) &&
      m.intelligence >= 0 && m.intelligence <= m.successes && floatValidation::finite(m.hours) && m.hours >= 0 && m.hours <= 1000000.f &&
      m.lastSettledAttempt <= c.attempt;
    if (m.activeAttempt) ok = ok && c.attemptOpen && m.activeAttempt == c.attempt && m.activeAttempt > m.lastSettledAttempt &&
      military::mission(m.activeKind) != nullptr && m.activeAirport >= 0 && m.activeAirport < nHome && c.atHome();
    else ok = ok && m.activeKind == -1 && m.activeAirport == -1 && m.missionSeed == 0;
    if (ver >= 6) {
      int completed = 0;
      for (int id = 0; id < military::kMissionCount; ++id) if (c.militaryMissionCompleted(id)) ++completed;
      // The aggregate ledger freezes as one unit at its storage bound. Genuine later
      // successes still earn per-mission completion, even if the frozen success total is zero.
      ok = ok && militaryProgressRead && !(m.completedMissions & ~military::kKnownMissionMask) &&
        (completed <= m.successes || m.sorties == 2000000000);
    }
    // v5 has only aggregate totals. Keep those and valid accepted tokens, but never infer
    // mission identities from successes, rank, credits or the last mission flown.
  }
  if (!ok) return false;
  if (c.job) {
    if (ver >= 4 && (!surveyRead || !surveyKnownRead || !plan2Read)) return false;
    // v3 added job3 without changing the format number. Presence, not version, determines known history.
    if (!surveyRead && c.job->c.type == CT_SURVEY && c.job->wpDone > 0) c.job->surveyHistoryKnown = false;
    if (!plan2Read) c.job->plan.startAirport = c.job->at;
    c.job->plan.net = c.job->c.payout - c.job->plan.fees() - c.job->plan.fuelCostEst;
  }
  *this = c;
  refreshBoard();
  return true;
}
