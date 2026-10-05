// Optional bespoke Osprey C6 static cabin trim. Body coordinates: metres, +z aft.
// Append after sdFuselage, before mapPlaneBody. No ModelDef/AircraftSpec fields added.
// Integrator explicitly gates this hook by the Osprey model; never infer identity from dimensions.
// IDs 120 ivory composite, 121 copper anodized trim, 122 tobacco upholstery,
// 123 dark cocoa textile, 124 warm-white lens. Static geometry ONLY.
vec2 mapOspreyCabinTrim(vec3 p) {
  vec2 r=vec2(1e5,120.0);
  // Copper brow sits below the existing glareshield and above all gauge faces.
  r=opU(r,vec2(sdRoundBox(p-vec3(0,.590,-2.516),vec3(.48,.009,.008),.004),121.0));
  // Sculpted ivory sill and copper inset: well below the side-window opening.
  vec3 q=vec3(abs(p.x),p.y,p.z);
  r=opU(r,vec2(sdRoundBox(q-vec3(.598,.125,-1.93),vec3(.023,.080,.47),.016),120.0));
  r=opU(r,vec2(sdRoundBox(q-vec3(.570,.154,-1.93),vec3(.009,.008,.40),.004),121.0));
  // Cocoa inset map pocket on each door. Recessed opening is geometry, not paint.
  float pocket=sdRoundBox(q-vec3(.580,-.090,-1.90),vec3(.025,.100,.245),.014);
  pocket=max(pocket,-sdRoundBox(q-vec3(.555,-.040,-1.90),vec3(.022,.052,.205),.012));
  r=opU(r,vec2(pocket,123.0));
  // Aft floor extension, centre aisle runner and paired copper edge strips.
  r=opU(r,vec2(sdRoundBox(p-vec3(0,-.330,.075),vec3(.53,.014,1.435),.008),120.0));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,-.308,.15),vec3(.095,.008,1.31),.004),123.0));
  r=opU(r,vec2(sdRoundBox(q-vec3(.109,-.306,.15),vec3(.004,.008,1.31),.004),121.0));
  // Four passenger seats, two abreast in two rows, inspired by the twin's pilot seats.
  for(int row=0;row<2;row++) {
    float z=-.38+float(row)*.92;
    vec3 s=vec3(abs(p.x)-.325,p.y,p.z-z);
    r=opU(r,vec2(sdRoundBox(s-vec3(0,-.105,0),vec3(.173,.065,.245),.035),122.0));
    vec3 b=s-vec3(0,.20,.235); b.yz=rot2(b.yz,-.12);
    r=opU(r,vec2(sdRoundBox(b,vec3(.163,.255,.050),.035),120.0));
    r=opU(r,vec2(sdRoundBox(b-vec3(0,0,-.052),vec3(.133,.220,.018),.018),122.0));
    r=opU(r,vec2(sdRoundBox(s-vec3(0,.480,.270),vec3(.100,.055,.045),.028),122.0));
    // broad sewn channels (minimum 8 mm thickness); lower frame terminates on floor.
    for(int rib=0;rib<3;rib++) {
      float x=float(rib-1)*.072;
      r=opU(r,vec2(sdRoundBox(b-vec3(x,0,-.073),vec3(.004,.180,.004),.004),123.0));
    }
    vec3 leg=vec3(abs(s.x)-.118,s.y+.247,s.z);
    r=opU(r,vec2(sdRoundBox(leg,vec3(.013,.064,.175),.008),121.0));
    r=opU(r,vec2(sdRoundBox(s-vec3(0,-.028,-.025),vec3(.142,.010,.015),.006),123.0));
    r=opU(r,vec2(sdRoundBox(s-vec3(0,-.015,-.025),vec3(.024,.008,.022),.006),121.0));
  }
  // Low cabin side liners and luggage-wall inlay stay under the windows.
  r=opU(r,vec2(sdRoundBox(q-vec3(.585,.040,.20),vec3(.022,.190,1.19),.018),120.0));
  r=opU(r,vec2(sdRoundBox(q-vec3(.557,.202,.20),vec3(.008,.008,1.14),.004),121.0));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,.23,1.548),vec3(.405,.340,.014),.025),120.0));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,.23,1.525),vec3(.215,.140,.008),.016),123.0));
  // Physical containment: every custom point is at least 90 mm inside the outer SDF.
  r.x=max(r.x,sdFuselage(p)+.090);
  return r;
}
vec3 ospreyCabinAlbedo(int id) {
  if(id==120)return vec3(.82,.76,.62);
  if(id==121)return vec3(.54,.235,.095);
  if(id==122)return vec3(.30,.115,.052);
  if(id==123)return vec3(.062,.040,.028);
  return vec3(.95,.82,.58);
}
