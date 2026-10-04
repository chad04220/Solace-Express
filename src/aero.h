// Solace Express - aerodynamics from each airframe's geometry
//
// The drag is built up component by component (fuselage, wing, tails, nacelles, struts, gear, cooling, leakage) from
// the shape in the spec: wetted areas, fineness and thickness ratios give the form factors, and the skin friction comes
// from the Reynolds number at the speed and air density of the moment, so the drag of each part changes the way a real
// one does with speed, altitude and size. The lift slope and the span efficiency come from the wing's aspect ratio and
// the fuselage width, and the wave drag from the critical Mach number of the wing section. Weight enters through the
// lift the wing has to make (induced drag). Nothing is tuned to hit a published cruise speed: that comes out.
#pragma once
#include "common.h"

struct AircraftSpec;

struct AeroModel {
  struct Part { const char* name; float swet, len, ffq, laminar; };   // wetted area, reference length, form factor x interference
  Part parts[8]; int nParts = 0;
  float extraDq = 0;        // drag areas that don't scale with skin friction: struts, cooling, protuberances (m^2)
  float gearDq = 0;         // gear down drag area (m^2)
  float misc = 0;           // leakage and excrescences, fraction of the friction drag
  float tc = 0.15f;         // wing section thickness ratio
  float korn = 0.87f;       // Korn factor of the section (0.87 conventional, ~0.95 supercritical)
  float e = 0.8f;           // span efficiency (Oswald)
  float AR = 7, exposed = 1, fuseFactor = 1;   // aspect ratio, lift-slope factors (exposed area, fuselage lift)
  float sideArea = 1, planArea = 1;            // body side / plan areas for crossflow drag (m^2)
  float roughness = 1e-5f;  // equivalent sand-grain roughness (m)
};

// the aircraft's aerodynamic model (built once per type)
const AeroModel& aeroModel(const AircraftSpec& s);
// zero-lift drag coefficient (on the wing area) at this true airspeed (m/s), air density (kg/m^3) and Mach number,
// clean (no gear, no flaps)
float aeroCD0(const AeroModel& a, const AircraftSpec& s, float V, float rho, float mach);
// lift-curve slope (per rad) at this Mach number
float aeroCLa(const AeroModel& a, float mach);
// wave drag of the wing section at this Mach number and lift coefficient
float aeroWave(const AeroModel& a, float mach, float CL);
