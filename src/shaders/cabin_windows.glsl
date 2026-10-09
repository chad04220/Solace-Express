//! kCabinWindows
//! The research cockpits' displays as windows (Renderer::screenWindows): which of the cabin's own surfaces a display
//! pane leaves out. A point is cut where the eye sees it through a pane - on or beyond the glass, its line of sight
//! crossing the glass inside the pane's outline - or where it lies inside an outline in front of the glass, below the
//! top of the frame round it: there the true cabin is air, and what the mesh has is the frame's inner edge zigzagging
//! with its 1.56 cm lattice. Decided per pixel from the panes' own shapes (wraith_cockpit_common.glsl, plane_sdf.glsl's
//! XR-30 pod). The cut-out was a pass of the triangles the bake labelled as screens: its edge came out a row of
//! triangle teeth, and the depth it was compared with - interpolated in log space across the large flat triangles of
//! the walls behind - let whole wall panels through at some angles (the owner's report on the XR-40).
const float kWinGlass = 0.004, kWinFrameTop = 0.019, kWinBand = 0.023;
// A line of sight is cut only where it comes down into the opening inside the outline at the frame's top and is still
// inside it at the glass: one that comes down over the frame meets the frame's top or its inner wall first (those are
// kept - the mesh's zigzag of the wall, either side of its true line, stands for the wall there), and one that enters
// the opening and leaves through the wall is stopped by it.
// a flat pane (wrFrame's centre, normal towards the eye, up; its outline: half size, corner chamfer). q: the point,
// eye at the origin; inFront: whether the band in front of the glass is cut too (the static cabin; a moving part,
// a pedal or the stick, is solid down to the glass)
bool winPaneCut(vec3 q, vec3 c, vec3 n, vec3 up, vec2 hs, float ch, bool inFront){
  float lz = dot(q - c, n);   // (its height over the glass first: most of the cabin is far from any one pane)
  if (lz > kWinBand || (lz > kWinGlass && !inFront)) return false;
  float qn = dot(q, n), cn = dot(c, n);
  if (qn > -1e-5) return false;
  if (wrShape(wrFrame(q*min((kWinFrameTop + cn)/qn, 1.0), c, n, up).xy, hs, ch) >= 0.0) return false;   // down over the frame
  // into the opening: cut only if it is still inside the outline at the glass (else the frame's inner wall stops it,
  // whichever side of its true line the mesh's fragment lies)
  return wrShape(wrFrame(q*((kWinGlass + cn)/qn), c, n, up).xy, hs, ch) < 0.0;
}
// the XR-40's curved front display: a cylinder about WF_C (the eye inside it), its glass kWinGlass and the frame's top
// kWinFrameTop inside WF_R
vec3 winCylAt(vec3 q, float rr, bool clampQ){   // where the line of sight to q crosses the cylinder of radius rr (clampQ: or q, if short of it)
  vec2 c = WF_C.xz; float a = dot(q.xz, q.xz), b = dot(q.xz, c), cc = dot(c, c) - rr*rr;
  float t = (b + sqrt(max(b*b - a*cc, 0.0)))/max(a, 1e-8);
  return q*(clampQ ? min(t, 1.0) : t);
}
bool winFrontCut(vec3 q, bool inFront){
  float lz = WF_R - length(q.xz - WF_C.xz);   // (wrFront's depth)
  if (lz > kWinBand || (lz > kWinGlass && !inFront)) return false;
  vec3 pt = winCylAt(q, WF_R - kWinFrameTop, true);
  if (wrFrontShape(pt, wrFront(pt).xy) >= 0.0) return false;   // down over the frame
  vec3 p = winCylAt(q, WF_R - kWinGlass, false);
  return wrFrontShape(p, wrFront(p).xy) < 0.0;
}
// model: 5 the XR-30, 6 the XR-40; bomb: the XR-40's floor panes show the bomb camera (kept); part: a moving part
bool cabinWindowCut(vec3 q, int model, bool bomb, bool part){
  bool inFront = !part;
  if (model == 6) {
    vec3 aq = vec3(abs(q.x), q.y, q.z);
    const vec3 Y = vec3(0.0, 1.0, 0.0), F = vec3(0.0, 0.0, -1.0);
    if (winFrontCut(q, inFront)) return true;
    if (winPaneCut(aq, WS_C, WS_N, Y, WS_S, 0.13, inFront) || winPaneCut(aq, WA_C, WS_N, Y, WA_S, 0.11, inFront)) return true;
    if (winPaneCut(q, WO_C, WO_N, F, WO_S, 0.12, inFront) || winPaneCut(q, WC_C, WC_N, Y, WC_S, 0.07, inFront)) return true;
    if (bomb) return false;
    if (winPaneCut(aq, WB_C, WB_N, F, WB_S, 0.07, inFront)) return true;
    // the footwell floor, under its grid of titanium ribs (wraith_cockpit_sdf.glsl): the ribs stand on the glass
    if (dot(q - WL_C, WL_N) > kWinBand) return false;
    vec3 l = wrFrame(q, WL_C, WL_N, F);
    vec2 gr = abs(fract(l.xy/vec2(0.2, 0.17) + 0.5) - 0.5)*vec2(0.2, 0.17);
    bool rib = min(gr.x, gr.y) < 0.01 && l.z > -0.002 && l.z < 0.024;
    return winPaneCut(q, WL_C, WL_N, F, WL_S, 0.1, inFront && !rib);
  }
  // the XR-30's pod: the panoramic display, a cylinder about the eye (glass at r 0.634, standing proud of its frame),
  // and the side bays (glass at |x| 0.63, flush with theirs). Within 8 mm in front of the glass is the glass too: the
  // mesh lies a millimetre or two either side of it, and the near side was left as dotted lines across the view
  float r = length(q.xz);
  if (r > 0.634 - 0.008) {
    vec3 p = q*(0.634/r);
    if (abs(atan(p.x, -p.z)) < 1.25 && abs(p.y - 0.02) < 0.30) return true;
  }
  if (abs(q.x) > 0.63 - 0.008) {
    vec3 p = q*(0.63/abs(q.x));
    if (abs(p.y - 0.04) < 0.2 && abs(p.z - 0.24) < 0.3) return true;
  }
  return false;
}
bool cabinScreenId(int mid){ return (mid >= 41 && mid <= 43) || (mid >= 61 && mid <= 63); }
// The cockpits' own display panels - flush with, or a few millimetres proud of, the console or mount they sit on -
// against the frame round them, decided from their shapes: the bake labels each vertex with one material, and along
// a panel's edge (a step far finer than its lattice) the label zigzagged, so a display came out with a ragged border
// or half of it the mount's (the XR-40's console displays, the owner's report). q: the point, eye at the origin.
int cabinPanelId(vec3 q, int model, int mid){
  if (model == 6) {
    if (mid != 65 && mid != 68 && mid != 69 && mid != 76) return mid;
    vec3 aq = vec3(abs(q.x), q.y, q.z);
    ResearchPanel panel = wraithPanel(wraithPanelIndex(q));
    vec3 m = researchPanelFrame(q, panel);
    if (abs(m.z) < 0.004 && researchPanelShape(m.xy, panel) < 0.0) return 69;
    vec3 l = wrFrame(q, WD_C, WD_N, vec3(0.0, 1.0, 0.0));
    vec3 a = l - vec3(0.0, -0.155, 0.008); a.x -= (floor(a.x/0.07) + 0.5)*0.07;
    if (abs(a.z) < 0.004 && max(max(abs(a.x) - 0.028, abs(a.y) - 0.009), abs(l.x) - 0.28) < 0.0) return 76;
    vec3 cq = vec3(aq.x - 0.56, q.y + 0.47, q.z + 0.12);    // the console's display on its tilted mount, and its touch glass
    vec3 mq = cq - vec3(0.015, 0.075, -0.24); mq.yz = mat2(cos(0.55), -sin(0.55), sin(0.55), cos(0.55))*mq.yz;   // (rot2(mq.yz, -0.55))
    if (mq.y > -0.004 && mq.y < 0.004 && wrShape(mq.xz, vec2(0.09, 0.055), 0.02) < 0.0) return 69;
    if (abs(cq.y - 0.04) < 0.004 && abs(cq.x - 0.015) < 0.1 && abs(cq.z - 0.265) < 0.09) return 68;
    return 65;
  }
  if (mid != 44 && mid != 45 && mid != 52 && mid != 53 && mid != 54) return mid;
  ResearchPanel panel = specterPanel(specterPanelIndex(q));
  vec3 m = researchPanelFrame(q, panel);
  if (abs(m.z) < 0.004 && researchPanelShape(m.xy, panel) < 0.0) return 45;
  vec3 cq = vec3(abs(q.x) - 0.52, q.y + 0.44, q.z - 0.08);   // the side shelves' small displays
  if (abs(cq.x - 0.02) < 0.075 && abs(cq.z + 0.2) < 0.06 && abs(cq.y - 0.05) < 0.004) return q.x < 0.0 ? 52 : 53;
  if (abs(cq.x) < 0.112 && abs(cq.z - 0.136) < 0.096 && abs(cq.y - 0.05) < 0.004) return 54;   // and their keys
  return 44;
}
// The frames round the panes, from the glass up to the frame's top: the normal of the surface the eye truly sees there
// (cabin frame). A line of sight that crosses the top's plane over the frame meets the top (the pane's normal); one
// that crosses it inside the outline has come down into the opening and meets the inner wall (the outline's normal
// there, facing into the opening). The mesh's top and wall zigzag into each other with its lattice, and their normals
// with them: lit, the frame read as a lumpy, glinting strip round every window with a sawtooth along its inner edge
// (the XR-40's overhead display, looking up: the owner's report). Decided along the line of sight, the light and
// shade part exactly on the outline. ok: q lies on a frame's top or wall, near its opening.
vec3 winWallN2(vec2 l, vec2 hs, float ch, vec3 t, vec3 b){
  const float e = 0.002;
  vec2 g = vec2(wrShape(l + vec2(e, 0.0), hs, ch) - wrShape(l - vec2(e, 0.0), hs, ch), wrShape(l + vec2(0.0, e), hs, ch) - wrShape(l - vec2(0.0, e), hs, ch));
  g = normalize(g + vec2(1e-6));
  return -(t*g.x + b*g.y);
}
// (outer: the ring's outer edge too, on a pane set in a facet of the shell parallel to it - the overhead's and the
// side displays': past the top's outer edge a line of sight meets the ring's outer wall or, beyond, the shell's hex
// panels; id: what it meets there, 65 the frame or 64 the shell)
bool winPaneWall(vec3 q, vec3 c, vec3 n, vec3 up, vec2 hs, float ch, bool outer, out vec3 wn, out int id){
  id = 65;
  float lz = dot(q - c, n);   // (its height over the glass first)
  if (lz > kWinFrameTop + 0.003 || lz < -0.03) return false;
  vec3 l = wrFrame(q, c, n, up);
  float s = wrShape(l.xy, hs, ch);
  if (l.z > kWinFrameTop + 0.003 || s < -0.012 || s > (outer ? 0.05 : 0.02) || l.z < (s > 0.02 ? -0.03 : kWinGlass - 0.002)) return false;
  float qn = dot(q, n), cn = dot(c, n);
  if (qn > -1e-5) return false;
  vec3 tt = normalize(cross(up, n)), bb = cross(n, tt);
  vec2 lt = wrFrame(q*((kWinFrameTop + cn)/qn), c, n, up).xy;   // where its line of sight crosses the top's plane
  float st = wrShape(lt, hs, ch);
  if (st < 0.0) { wn = winWallN2(lt, hs, ch, tt, bb); return true; }   // into the opening: the wall it comes to
  if (st <= 0.034 || !outer) { wn = n; return true; }                   // over the frame: its top
  vec2 lb = wrFrame(q*((-0.007 + cn)/qn), c, n, up).xy;               // past it: the ring's outer wall, down to its foot
  if (wrShape(lb, hs, ch) < 0.034) { wn = -winWallN2(lb, hs, ch, tt, bb); return true; }
  wn = n; id = 64;                                                     // or the shell beyond
  return true;
}
// mid: the pixel's material - only a frame's or the shell's is decided here (in, the same or the shell's); out: ok
bool cabinWallNormal(vec3 q, int model, inout int mid, out vec3 wn){
  wn = vec3(0.0);
  if (model != 6 || (mid != 64 && mid != 65)) return false;
  vec3 aq = vec3(abs(q.x), q.y, q.z);
  const vec3 Y = vec3(0.0, 1.0, 0.0), F = vec3(0.0, 0.0, -1.0);
  bool ok = false; int id = 65;
  if (winPaneWall(aq, WS_C, WS_N, Y, WS_S, 0.13, true, wn, id) || winPaneWall(aq, WA_C, WS_N, Y, WA_S, 0.11, true, wn, id)) { wn.x *= sign(q.x + 1e-9); ok = true; }
  else if (winPaneWall(aq, WB_C, WB_N, F, WB_S, 0.07, false, wn, id)) { wn.x *= sign(q.x + 1e-9); ok = true; }
  else if (winPaneWall(q, WO_C, WO_N, F, WO_S, 0.12, true, wn, id) || winPaneWall(q, WC_C, WC_N, Y, WC_S, 0.07, false, wn, id)) ok = true;
  else {   // the footwell floor: not its ribs, standing on the glass right up to the frame
    vec3 l = wrFrame(q, WL_C, WL_N, F);
    vec2 gr = abs(fract(l.xy/vec2(0.2, 0.17) + 0.5) - 0.5)*vec2(0.2, 0.17);
    ok = min(gr.x, gr.y) >= 0.01 && winPaneWall(q, WL_C, WL_N, F, WL_S, 0.1, false, wn, id);
  }
  if (ok) { mid = id; return true; }
  float lz = WF_R - length(q.xz - WF_C.xz);   // the front display's frame on its cylinder: the same along the line of sight
  if (lz < kWinGlass - 0.002 || lz > kWinFrameTop + 0.003) return false;
  vec3 l = wrFront(q);
  float s = wrFrontShape(q, l.xy);
  if (s < -0.012 || s > 0.02) return false;
  vec3 pt = winCylAt(q, WF_R - kWinFrameTop, false);
  vec3 rn = normalize(vec3(pt.x - WF_C.x, 0.0, pt.z - WF_C.z));   // (the cylinder's outward normal there)
  mid = 65;
  if (wrFrontShape(pt, wrFront(pt).xy) >= 0.0) { wn = -rn; return true; }
  const float e = 0.002;   // (the outline's gradient across the cylinder's surface, at the top's crossing)
  vec3 g = vec3(wrFrontShape(pt + vec3(e, 0.0, 0.0), wrFront(pt + vec3(e, 0.0, 0.0)).xy) - wrFrontShape(pt - vec3(e, 0.0, 0.0), wrFront(pt - vec3(e, 0.0, 0.0)).xy),
                wrFrontShape(pt + vec3(0.0, e, 0.0), wrFront(pt + vec3(0.0, e, 0.0)).xy) - wrFrontShape(pt - vec3(0.0, e, 0.0), wrFront(pt - vec3(0.0, e, 0.0)).xy),
                wrFrontShape(pt + vec3(0.0, 0.0, e), wrFront(pt + vec3(0.0, 0.0, e)).xy) - wrFrontShape(pt - vec3(0.0, 0.0, e), wrFront(pt - vec3(0.0, 0.0, e)).xy));
  g -= rn*dot(g, rn);
  wn = -normalize(g + vec3(1e-6));
  return true;
}
// The XR-40's side consoles and their displays' tilted mounts (wraith_cockpit_sdf.glsl) as a field of their own, for
// the normal of a pixel on them: the mounts' rounded edges bend tighter than the cabin's fine lattice, and the mesh's
// normals stepped round them in a sawtooth that the polished titanium's highlights picked out (the owner's report).
// q: the point, eye at the origin.
float wrConsoleField(vec3 q){
  vec3 cq = vec3(abs(q.x) - 0.56, q.y + 0.47, q.z + 0.12);
  float slab = max(max(abs(cq.x) - 0.13, abs(cq.y) - 0.04), abs(cq.z) - 0.36);
  slab = max(slab, (-cq.x*0.7 + cq.y) - 0.035);
  slab = max(slab, (abs(cq.z) + abs(cq.x)*0.5) - 0.4);
  vec3 mq = cq - vec3(0.015, 0.075, -0.24); mq.yz = mat2(cos(0.55), -sin(0.55), sin(0.55), cos(0.55))*mq.yz;   // (rot2(mq.yz, -0.55))
  vec3 b = abs(mq + vec3(0.0, 0.03, 0.0)) - vec3(0.105, 0.03, 0.07) + 0.014;   // (sdRoundBox, 14 mm)
  return min(slab, length(max(b, 0.0)) + min(max(b.x, max(b.y, b.z)), 0.0) - 0.014);
}
bool cabinConsoleNormal(vec3 q, int model, int mid, out vec3 cn){
  cn = vec3(0.0);
  if (model != 6 || (mid != 65 && mid != 68 && mid != 69)) return false;
  if (abs(abs(q.x) - 0.56) > 0.16 || abs(q.y + 0.415) > 0.12 || abs(q.z + 0.12) > 0.4) return false;
  if (abs(wrConsoleField(q)) > 0.006) return false;   // (on them, not on what stands near: the stick's base, a frame)
  const float e = 0.001; vec3 g = vec3(0.0);
  for (int i = 0; i < 4; i++) { vec3 k = 2.0*vec3(float(((i + 3) >> 1) & 1), float((i >> 1) & 1), float(i & 1)) - 1.0; g += k*wrConsoleField(q + k*e); }
  cn = normalize(g);
  return true;
}
