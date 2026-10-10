//! kWreckClip
//! A broken-up aircraft's pieces, each drawn from the airframe's own mesh (aircraft_mesh.cpp drawWreck): which piece a
//! body point belongs to - the first piece whose box holds it, the last piece (the centre section) whatever no box
//! holds (breakup.h breakOwner) - and how far it is from where its piece tore away (breakCuts: thin boxes over the
//! stretches of the boxes' faces the airframe crossed). uWreck: the number of pieces.
uniform vec3 uPcC[16]; uniform vec3 uPcH[16];   // each piece's box (body coords)
uniform mat3 uPcRot[16]; uniform vec3 uPcPos[16];   // each piece's placing: body to world, and the airframe's origin as it carries it (from the camera; world in the shadow maps)
uniform float uPcBurn[16];                       // how far each piece's fire has blackened it, 0..1
uniform int uPcK;                                // the piece whose own triangles are being drawn
uniform int uWreckParts;                         // 1: the rigid parts, every piece's in one draw - each with the piece its middle is in (uPartC)
uniform vec3 uPartC;                             // (the middle of the part being drawn, in its own frame)
uniform int uPartType; uniform vec3 uGearOwner;   // explicit detached leg/wheel owners; bay doors remain with their bays
uniform int uCutN; uniform vec3 uCutC[12]; uniform vec3 uCutH[12];   // the piece's tears
bool gWreckIn = false;     // the inside of a piece's skin, seen through where it tore (the mesh pass)
bool gWreckPart = false;   // a rigid part (a control surface, a wheel): it comes off whole, no tear of its own
int brkOwner(vec3 b){
  for (int i = 0; i < 15; i++) {
    if (i >= uWreck - 1) break;
    vec3 q = abs(b - uPcC[i]) - uPcH[i];
    if (max(q.x, max(q.y, q.z)) <= 0.0) return i;
  }
  return max(uWreck - 1, 0);
}
int brkRigidOwner(vec3 b){
  int gear = -1;
  if (uPartType == 33 || uPartType == 39 || uPartType == 40) gear = int(b.x < 0.0 ? uGearOwner.x : uGearOwner.y);
  else if (uPartType == 34 || uPartType == 35 || uPartType == 41 || uPartType == 42) gear = int(uGearOwner.z);
  return gear >= 0 ? gear : brkOwner(b);
}
float brkCut(vec3 b){
  float d = 1e9;
  if (gWreckPart) return d;
  for (int i = 0; i < 12; i++) {
    if (i >= uCutN) break;
    vec3 q = abs(b - uCutC[i]) - uCutH[i];
    d = min(d, length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0));
  }
  return d;
}
