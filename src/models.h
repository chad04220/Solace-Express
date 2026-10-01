// Air Xpress - per-aircraft 3D model definitions (shared by the ray tracer, cockpit camera and props)
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
// Packs a model into the shader's uM[] uniform array (24 vec4) using the physics gear geometry
void packModel(const AircraftSpec& s, int idx, float gearHeight, float out[24 * 4]);
// Fuselage half width at a body z (for panel sizing)
float modelHalfWidth(const ModelDef& m, float z);
// Propeller hubs (body coords) and radius; returns count
int modelProps(const ModelDef& m, float out[2][4]);
// Positions of light fixtures (right wingtip, top of fin, tail cone) in body coords
vec3 modelWingTip(const ModelDef& m);
vec3 modelFinTop(const ModelDef& m);
vec3 modelTailTip(const ModelDef& m);
