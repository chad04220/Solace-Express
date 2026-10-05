// models.cpp — insert at the Swift AircraftSpec index; +x right, +y up, +z aft.
{ // Eight closed nose-to-tail fuselage stations: z, half width, half height, centre y.
  {{-4.35f,.10f,.09f,-.055f},{-4.10f,.35f,.31f,-.060f},
   {-2.95f,.49f,.41f,-.035f},{-1.90f,.62f,.66f,.075f},
   {-.25f,.63f,.65f,.105f},{1.55f,.43f,.43f,.160f},
   {3.70f,.14f,.18f,.270f},{4.25f,.055f,.075f,.315f}}, .94f, // elliptical cabin
  {5.70f,1.98f,1.02f,.72f,-.74f,-1.13f,5.0f,.125f}, // low tapered wing; spar below floor
  0,0.0f,.34f,.57f,0,0,                               // no struts; winglets; 57% flaps
  {1.90f,1.08f,.62f,.40f,.28f,3.06f,0.0f},0,           // conventional horizontal tail
  {1.55f,1.55f,.55f,.76f,.23f,2.70f},                  // fin and rudder
  0,0.0f,0.0f,0.0f,0.0f,0.0f,.13f,1.03f,             // nose piston; spinner and prop
  4,.28f,0,                                         // wing/body retracts; no cargo pod
  1,.35f,1.45f,.24f,.28f,.205f,                       // one passenger window per side
  vec3(-.32f,.52f,-1.40f),0,-2.80f,-2.14f,.31f,.50f }, // left-seat analog; windshield ahead of panel
