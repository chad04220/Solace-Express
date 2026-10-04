## What's new

**Aircraft detail**
- Riveted skin panels: frames around the fuselage and stringer seams along it, with rivet rows beside each seam
- Cabin door outlines and handles on both sides
- Wing rib and spar seams with rivets, and a fuel cap on top of each wing
- Single-engine aircraft: an oil-access door on the cowling and a ring of cowl fasteners
- A pitot tube under the left wing, static wicks on the wingtips' trailing edges and a VHF blade antenna under the belly
- Thin rims and bezels (air vents, instrument bezels) render more cleanly: every hit on the airframe now settles exactly onto the surface

**Performance**
- AI traffic uses the aircraft hulls built at launch: rays that can't meet a traffic aircraft skip it entirely, and the rest start right at it
- The terrain mesh only draws the chunks in view
- The ray-tracing shader is smaller (the aircraft's shape is no longer inlined many times over): a faster first-time shader compile

**Under the hood**
- Aircraft hull meshes are built watertight (checked by a new test), and the shaders are checked by a GLSL validator in the test suite
