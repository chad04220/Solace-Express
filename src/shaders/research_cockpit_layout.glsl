//! kResearchCockpitLayout
//! Physical instrument modules for the XR-30 and XR-40, shared by their fields and exact glass masks.
// Pure layout only: no camera, target, light or moving-part state. Sizes are metres in the eye-relative cabin.
struct ResearchPanel { vec3 c; vec3 n; vec2 h; float corner; int page; };
vec3 researchPanelFrame(vec3 q, ResearchPanel p){
  vec3 t = normalize(cross(vec3(0.0, 1.0, 0.0), p.n));
  vec3 b = cross(p.n, t), d = q - p.c;
  return vec3(dot(d, t), dot(d, b), dot(d, p.n));
}
float researchPanelShape(vec2 q, ResearchPanel p){
  vec2 a = abs(q);
  return max(max(a.x - p.h.x, a.y - p.h.y), (a.x + a.y - p.h.x - p.h.y + p.corner)*0.70710678);
}
// The Nocturne's stepped arc is five individually tilted modules. Its broad, central attitude display takes
// priority; the adjacent left map and right power module have a shorter scan than the two outer system pages.
ResearchPanel nocturnePanel(int i){
  ResearchPanel p;
  float a = i == 0 ? -0.91 : i == 1 ? -0.48 : i == 2 ? 0.0 : i == 3 ? 0.48 : 0.91;
  float r = (i == 0 || i == 4) ? 0.66 : i == 2 ? 0.61 : 0.605;
  float sa = sin(a), ca = cos(a);
  p.c = vec3(sa*r, i == 2 ? -0.407 : -0.414, -ca*r);
  p.n = vec3(-sa*0.89302855, 0.45, ca*0.89302855);
  p.h = i == 2 ? vec2(0.16, 0.102) : (i == 0 || i == 4) ? vec2(0.095, 0.078) : vec2(0.102, 0.080);
  p.corner = i == 2 ? 0.013 : 0.010;
  p.page = i == 0 ? 0 : i == 1 ? 2 : i == 2 ? 3 : i == 3 ? 1 : 4;
  return p;
}
// Shared with the layout clearance test: the panorama frame's actual solid, including its window cut-out.
float nocturnePanoramaBezel(vec3 q){
  float r = length(vec3(q.x, 0.0, q.z)), ang = atan(q.x, -q.z);
  vec2 a = abs(vec2(r - 0.675, ang)), b = abs(vec2(q.y - 0.02, 0.0));
  float d = max(max(a.x - 0.02, a.y - 1.3), b.x - 0.345);
  d = max(d, -max(max(r - 0.66, a.y - 1.25), b.x - 0.30));
  // The curved lower rim used to cross the flat primary glass at its upper corners. Relieve only its mount:
  // the 14 mm margin clears the glass while the module's 18 mm border still covers the opening.
  ResearchPanel panel = nocturnePanel(2);
  vec3 l = researchPanelFrame(q, panel);
  float keepout = max(researchPanelShape(vec2(l.x, l.y), panel) - 0.014, abs(vec2(l.z - 0.012, 0.0)).x - 0.030);
  return max(d, -keepout);
}
int nocturnePanelIndex(vec3 q){
  float a = atan(q.x, -q.z);
  return a < -0.71 ? 0 : a < -0.29 ? 1 : a < 0.29 ? 2 : a < 0.71 ? 3 : 4;
}
// The Somnus retains its angular bridge and transparent footwell. Three unequal hex-clipped screens replace
// the two tiny dash screens: primary attitude in the centre, engines left and heading map right.
ResearchPanel somnusPanel(int i){
  ResearchPanel p;
  float side = float(i - 1);
  p.c = i == 1 ? vec3(0.0, -0.389, -0.96) : vec3(side*0.455, -0.409, -0.945);
  p.n = i == 1 ? vec3(0.0, 0.6, 0.8) : normalize(vec3(-side*0.18, 0.6, 0.8));
  p.h = i == 1 ? vec2(0.235, 0.120) : vec2(0.150, 0.090);
  p.corner = i == 1 ? 0.032 : 0.025;
  p.page = i == 0 ? 0 : i == 1 ? 3 : 2;
  return p;
}
// Recess the cross-dash support 35 mm behind its former position: the side modules cant toward the eye,
// so a bridge flush with the centre module crosses their inner lower glass. Keep its width and load path.
const vec3 WR_BRIDGE_C = vec3(0.0, -0.09, -0.083);
const vec3 WR_BRIDGE_HALF = vec3(0.68, 0.031, 0.035);
const float WR_BRIDGE_ROUND = 0.015;
int somnusPanelIndex(vec3 q){ return q.x < -0.28 ? 0 : q.x > 0.28 ? 2 : 1; }
// Kept off the central flight scan and deliberately smaller than the main page.
const vec3 WR_HOLO_EMITTER = vec3(-0.675, -0.409, -0.932);
const vec3 WR_HOLO_CENTRE = vec3(-0.675, -0.299, -0.866);
const float WR_HOLO_RADIUS = 0.037;
// The XR-50's ground station (morpheus_sdf.glsl mapMorpheusStation): its monitor bank, each pane facing the eye. 0 the
// main monitor (the nose camera), 1 and 2 the left and right ones angled in beside it (57 deg off the nose), 3 the chin
// monitor in the desk's console (forward and down, for the hover and the landing), 4 the telemetry strip above the main
// one (its own page, not a window). Each is a window as the research cockpits' displays are (cabin_windows.glsl), its
// symbology over the world it lets through (plane_screens.glsl morpheusHud)
const int MS_PANES = 5;
ResearchPanel morpheusPane(int i){
  ResearchPanel p;
  float sx = i == 1 ? -1.0 : 1.0;
  p.c = i == 0 ? vec3(0.0, 0.04, -0.78) : i <= 2 ? vec3(sx*0.671, 0.04, -0.436) : i == 3 ? vec3(0.0, -0.31, -0.48) : vec3(0.0, 0.47, -0.74);
  p.n = normalize(-p.c);
  p.h = i == 0 ? vec2(0.52, 0.29) : i <= 2 ? vec2(0.30, 0.19) : i == 3 ? vec2(0.20, 0.10) : vec2(0.40, 0.065);
  p.corner = i == 0 ? 0.02 : i <= 2 ? 0.015 : 0.01;
  p.page = i;
  return p;
}
// which pane a point of the bank is nearest (eye at the origin)
int morpheusPaneIndex(vec3 q){
  float a = atan(q.x, -q.z), e = atan(q.y, sqrt(q.x*q.x + q.z*q.z));
  return e > 0.45 ? 4 : e < -0.37 && a > -0.5 && a < 0.5 ? 3 : a < -0.61 ? 1 : a > 0.61 ? 2 : 0;
}
