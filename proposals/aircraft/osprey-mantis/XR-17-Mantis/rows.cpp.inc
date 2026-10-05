// aircraft.cpp — authoring row only; research registry insertion is an integration task.
// id, name, role, eng, n, cyl, blades, idle, max, empty, fuel, cargo, pax, S, b, c, CL0, CLa, CLmax, flapCL, CD0, gearCD, flapCD, e,
// power, v0, vr, vref, cruise, range, runway, rough, tail, retract, Ixx, Iyy, Izz, elev, ail, rud, lic, price, rent,
// fusLen, fusRad, wingY, wingZ, engLayout, tail, colBase, colStripe
{"xr17", "XR-17 Mantis", "Forward-swept experimental systems test aircraft", ENG_JET, 2, 0, 0, 0.0f, 0.0f,
 6100.0f, 1800.0f, 0.0f, 0, 30.0f, 13.2f, 2.27f,
 0.25f, 5.0f, 1.55f, 0.60f, 0.026f, 0.012f, 0.070f, 0.78f,
 15000.0f, 0.0f, 50.0f, 58.0f, 190.0f, 210.0f, 1350.0f, false, false, true,
 42000.0f, 92000.0f, 134000.0f, 0.42f, 0.055f, 0.060f, LIC_ATP, 480000, 0,
 16.0f, 1.05f, -0.62f, 0.65f, 2, 0, vec3(0.065f,0.085f,0.105f), vec3(0.95f,0.43f,0.055f), 0},

// models.cpp — same index; engine 7 is next free at baseline a298771; resolve collisions at integration.
{ // Eight hull proxy stations, nose to tail. Actual custom hull is independent.
 {{-8.0f,.07f,.07f,.05f},{-6.3f,.44f,.44f,.05f},{-4.8f,.79f,.79f,.05f},{-3.9f,.98f,.98f,.05f},
  {-.3f,1.02f,1.02f,.04f},{2.65f,1.05f,1.05f,.04f},{6.1f,.40f,.40f,.20f},{8.0f,.07f,.07f,.28f}}, 1.0f,
 {6.6f,3.15f,1.35f,-2.48f,-.62f,.65f,0.0f,.10f}, // forward-swept wing aerodynamic proxy
 0,0.0f,0.0f,.52f,0,0, // no struts, winglets, slats, boots
 {2.75f,1.18f,1.18f,0.0f,-.15f,-4.19f,0.0f},0, // canard proxy: physics support requires validation
 {2.3f,1.84f,1.10f,.80f,.20f,4.30f},
 7,1.55f,.05f,.48f,1.73f,3.07f,0.0f,0.0f, // custom shape dispatch; conventional twin-jet physics
 4,.26f,0, // custom gear visual travel; no cargo pod
 0,0.0f,0.0f,0.0f,0.0f,0.0f, // no passenger windows
 vec3(0.0f,.65f,-3.35f),2, // sealed cockpit eye, glass instruments
 -4.95f,-4.45f,.45f,-2.7f }, // inert windshield proxy; do not cut generic windows in sealed custom hull
