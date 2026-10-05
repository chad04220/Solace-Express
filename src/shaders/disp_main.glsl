//! kDispMain
//! Cockpit display atlas: every display page (research jets) or the whole light-aircraft instrument panel, drawn once
//! per frame into a texture that the ray tracer samples with mipmapped filtering. Keeps the gauge code out of the
//! ray tracer (smaller, faster shader) and gives crisp, stable screens at any size or angle.

uniform vec4 uDispMode;   // x: 0 display pages (4 x 2 atlas), 1 instrument panel; y: cockpit type
uniform vec2 uDispRes;
void main(){
  vec2 px = gl_FragCoord.xy;
  vec4 o = vec4(0.0);
  if (uDispMode.x < 0.5) {
    vec2 cell = vec2(uDispRes.x/4.0, uDispRes.y/2.0);
    vec2 id = floor(px/cell); vec2 uv = (px - id*cell)/cell*2.0 - 1.0;
    int page = int(id.x) + int(id.y)*4;
    float fp = 2.0/cell.y; gAA = fp*0.85;
    // one evaluation per texel (each shape is anti-aliased over gAA already): four inlined copies of every page made
    // the shader too big for some NVIDIA drivers
    if (page < 7) o = vec4(mfdPage(page, uv), 1.0);
  } else {   // panel coordinates (m): x -0.16 .. 0.42, y -0.11 .. 0.11; colour premultiplied by gauge coverage
    float mpp = 0.58/uDispRes.x;
    vec2 q = vec2(-0.16, -0.11) + px*mpp;
    gAA = mpp*0.85;
    vec3 c4 = drawInstruments(q, int(uDispMode.y + 0.5), true);   // (one evaluation: see the pages above)
    o = c4.x >= 0.0 ? vec4(c4, 1.0) : vec4(0.0);
  }
  // the atlas is half float (max 65504): one overflowing or NaN pixel would turn to inf, and mipmapping would smear it
  // over the whole screen. Displays never need more than this.
  o = min(max(o, vec4(0.0)), vec4(64.0));
  if (!(o.r + o.g + o.b + o.a < 1e6)) o = vec4(0.0);
  oColor = o;
}
