//! kRaytraceText
//! Third part (MSVC limits each concatenated string literal to 64 KB).
//! Display text for every cockpit screen and HUD, from the game's signed-distance-field UI font (sharp at any size)
// ---------------------------------------------------------------- display text
uniform sampler2D uFontTex;
const float FNT_W = 944.0, FNT_H = 342.0, FNT_CW = 59.0, FNT_CH = 57.0, FNT_PAD = 5.0, FNT_ASC = 37.25;
float gTxtSoft = 0.09;   // SDF edge softness (0.1 = one font pixel)
// coverage of glyph ch at p (font pixels, origin at the pen on the baseline, y up)
float glyphCov(vec2 p, int ch){
  float yy = FNT_ASC - p.y;
  if (p.x < -FNT_PAD || p.x > FNT_CW - FNT_PAD || yy < -FNT_PAD || yy > FNT_CH - FNT_PAD) return 0.0;
  int gi = ch - 32; float col = float(gi - (gi/16)*16), row = float(gi/16);
  vec2 a = vec2(col*FNT_CW + FNT_PAD + p.x, row*FNT_CH + FNT_PAD + yy);
  float d = textureLod(uFontTex, a/vec2(FNT_W, FNT_H), 0.0).r;
  return smoothstep(0.5 - gTxtSoft, 0.5 + gTxtSoft, d);
}
float hudLine(float d, float w){ return 1.0 - smoothstep(w*0.5, w*1.5, d); }
// a number, nd digits wide (leading zeros), q: origin at the field's lower-left; each digit sits centred in a slot
// 1.35 cells wide with its height filling the cell
float hudNum(vec2 q, float v, int nd, vec2 cell){
  float on = 0.0; v = max(floor(v + 0.5), 0.0);
  float s = min(cell.y/29.0, cell.x*1.25/27.83);   // field units per font pixel
  for (int i = 0; i < 6; i++) {
    if (i >= nd) break;
    float pw = pow(10.0, float(nd - 1 - i));
    int dg = int(mod(floor(v/pw), 10.0));
    vec2 o = q - vec2(float(i)*cell.x*1.35 + cell.x*0.5 - 13.92*s, 0.0);
    on = max(on, glyphCov(o/s, 48 + dg));
  }
  return on;
}
float hudBox(vec2 q, vec2 c, vec2 h, float w){ vec2 d = abs(q - c) - h; return hudLine(abs(max(d.x, d.y)), w); }
// cockpit displays, pre-drawn into a mipmapped texture this frame (see kDispMain)
uniform sampler2D uDispTex; uniform sampler2D uPanelTex;   // display pages / instrument panel
vec3 pageTex(int page, vec2 uv, float fp){   // fp: page uv units per screen pixel
  if (abs(uv.x) > 1.0 || abs(uv.y) > 1.0) return vec3(0.0);
  vec2 cell = vec2(float(page - (page/4)*4), float(page/4));
  vec2 a = (cell + clamp(uv*0.5 + 0.5, 0.003, 0.997))/vec2(4.0, 2.0);
  float lod = log2(max(fp*float(textureSize(uDispTex, 0).y)*0.25, 1e-4));
  return textureLod(uDispTex, a, max(lod - 0.75, 0.0)).rgb;   // (sharper than the footprint: the TAA settles it; at the footprint's own level they read soft)
}
vec4 panelTex(vec2 q, float px){   // q: panel metres; px: metres per screen pixel; rgb premultiplied by coverage (a)
  vec2 a = vec2((q.x + 0.16)/0.58, (q.y + 0.11)/0.22);
  if (a.x < 0.0 || a.x > 1.0 || a.y < 0.0 || a.y > 1.0) return vec4(0.0);
  float lod = log2(max(px*float(textureSize(uPanelTex, 0).x)/0.58, 1e-4));
  return textureLod(uPanelTex, a, max(lod, 0.0));
}
