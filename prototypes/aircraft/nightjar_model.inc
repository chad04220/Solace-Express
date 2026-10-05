// models.cpp — same index as Nightjar AircraftSpec; existing engine 4 and gear 4 only.
{ // Eight closed stations; enough roof height for the offset glass-cockpit eye.
  {{-8.30f,.035f,.035f,-.035f},{-6.65f,.28f,.22f,-.030f},
   {-5.05f,.56f,.49f,-.010f},{-3.55f,.77f,.79f,.045f},
   {-.30f,.93f,.80f,.075f},{3.10f,.78f,.72f,.075f},
   {6.75f,.28f,.33f,.155f},{8.00f,.065f,.090f,.225f}}, .77f,
  {8.30f,3.50f,1.10f,.08f,-.75f,-1.50f,2.0f,.11f},   // near-straight LE, mildly forward quarter-chord; 11% thick
  0,0.0f,.42f,.58f,0,0,                              // winglets; 58% flaps; no special surfaces
  {2.65f,1.55f,.80f,.48f,.83f,5.72f,0.0f},0,          // conventional raised horizontal tail
  {2.22f,2.28f,.77f,1.40f,.55f,4.85f},                // tall swept fin
  4,1.63f,.16f,.45f,2.54f,3.37f,0.0f,0.0f,            // ordinary aft twin jets and generated pylons
  4,.35f,0,                                        // wing/body retracts; no cargo pod
  0,0.0f,0.0f,0.0f,0.0f,0.0f,                       // no passenger windows
  vec3(-.42f,.59f,-3.30f),2,-5.45f,-4.23f,.30f,-2.20f }, // glass cockpit; avoid centre-display overlap using data
