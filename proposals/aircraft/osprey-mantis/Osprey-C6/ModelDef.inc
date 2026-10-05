// models.cpp — insert at the same index as AircraftSpec
{ // Fuselage stations: pointed luggage nose, six-seat cabin, tapered tailcone.
  {{-4.90f,0.06f,0.06f,-0.06f},{-4.45f,0.37f,0.35f,-0.04f},{-3.10f,0.63f,0.67f,0.03f},{-2.10f,0.72f,0.90f,0.09f},
   {0.55f,0.72f,0.90f,0.09f},{2.15f,0.49f,0.58f,0.16f},{4.25f,0.16f,0.24f,0.30f},{4.90f,0.06f,0.08f,0.34f}}, 0.72f,
  // Low cantilever wing: span 12.4 m, modest sweep, dihedral; wing-body gear wells.
  {6.20f,2.25f,1.35f,0.55f,-0.68f,-0.80f,4.0f,0.13f},
  0,0.0f,0.0f,0.59f,0,1, // no struts/winglets/slats; flaps to 59%; de-ice boots
  {2.15f,1.35f,0.85f,0.28f,0.34f,3.15f,0.0f},0, // conventional horizontal tail
  {1.85f,1.65f,0.65f,0.85f,0.32f,2.80f}, // tapered swept fin
  2,2.30f,-0.54f,0.40f,-1.50f,2.60f,0.16f,1.05f, // twin three-blade piston nacelles
  4,0.29f,0, // retract into low wing/body; no cargo pod
  2,-0.55f,1.45f,0.25f,0.27f,0.22f, // two large cabin windows per side
  vec3(-0.34f,0.74f,-1.90f),1, // left pilot eye; analog twin cockpit
  -3.05f,-2.62f,0.40f,-0.65f }, // windshield and pilot side windows
