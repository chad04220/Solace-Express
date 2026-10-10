// Solace Express - per-aircraft 3D model definitions (shared by the aircraft's shape code, the cockpit camera and the props)
#pragma once
#include "common.h"

// Everything is in aircraft body coordinates: +x right, +y up, +z aft, origin at the CG.
struct ModelDef {
  float st[8][4];        // fuselage stations: z, half width, half height, centre y (nose -> tail)
  float roundness;       // cross-section: 1 = ellipse, 0 = box
  // main wing: half span, root chord, tip chord, LE sweep at tip, root y, root LE z, dihedral (deg), thickness ratio
  float wing[8];
  int strut; float strutX, winglet, flapFrac; int slats, deice;
  // horizontal tail: half span, root chord, tip chord, LE sweep, y, LE z, dihedral (deg); T-tail flag
  float ht[7]; int ttail;
  // fin: height, root chord, tip chord, LE sweep, base y, root LE z
  float vt[6];
  // engines: 0 nose piston, 1 nose turboprop, 2 wing piston nacelles, 3 wing turboprop nacelles, 4 aft jets
  int engine; float nacX, nacY, nacR, nacZ0, nacLen, spinnerR, propR;
  // gear: 0 fixed tricycle with spats, 1 fixed tricycle, 2 taildragger tundra, 3 retract into nacelles, 4 retract into wing/body
  int gear; float wheelR;
  int cargoPod;
  int winCount; float winZ0, winZ1, winY, winW, winH;  // passenger windows (winY relative to centre line)
  vec3 eye; int cockpit;                               // pilot eye; cockpit 0 analog single, 1 analog twin, 2 glass
  float wsZ0, wsZ1, wsY, sideZ1;                       // windshield z range & base height, side window aft limit
};

extern const ModelDef kModels[];

struct AircraftSpec;
// Authored rolling radii shared by player and traffic; matches their visible tyres.
void modelWheelRadii(int model, float& mainRadius, float& noseRadius);
// Packs a model into the shader's uM[] uniform array (24 vec4) using the physics gear geometry.
// uM[19].w keeps de-ice in bit0; authored new variants encode their roster ID in higher bits.
// Legacy entries retain their exact historic value (0/1). Integer data is read with texelFetch for traffic.
void packModel(const AircraftSpec& s, int idx, float gearHeight, float out[24 * 4]);
void packModelOf(int idx, float out[24 * 4]);   // type idx's as a Plane of that type packs it (the game, the traffic): shaders.h aircraftDefines
// Fuselage half width at a body z (for panel sizing)
float modelHalfWidth(const ModelDef& m, float z);
// the fuselage's section at a body z: half width, half height, centre height (the stations' monotone cubic)
void modelSection(const ModelDef& m, float z, float& hw, float& hh, float& cy);
// Propeller hubs (body coords) and radius; returns count
int modelProps(const ModelDef& m, float out[2][4]);
// Positions of light fixtures (right wingtip, top of fin, tail cone) in body coords
vec3 modelWingTip(const ModelDef& m);
vec3 modelFinTop(const ModelDef& m);
vec3 modelTailTip(const ModelDef& m);

// Fitted cockpit foot/seat mounts, computed on the CPU once per uniform upload rather than per fragment.
// foot: pedal y, station z, pair-centre magnitude, foot spacing; seat: rail-floor y, pan drop below eye.
void modelCabinFit(int model, float panelZ, float foot[4], float seat[2]);

// Nine vec4 of authored cockpit layout, separate from the full 24-vec4 packed aircraft model.
void packCockpitLayout(int model, float values[36]);

struct CockpitFocusTarget;
// Visible live instruments only; camera-window glass and decorative touch surfaces are excluded.
int modelCockpitFocusTargets(int model,CockpitFocusTarget* out,int capacity);
