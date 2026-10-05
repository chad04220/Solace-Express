//! kGBWrite
//! The G-buffer outputs of a raster pass (see kGBuffer for the layout)
layout(location=0) out vec4 oG0; layout(location=1) out vec4 oG1; layout(location=2) out vec4 oG2; layout(location=3) out vec4 oG3;
void gbWrite(float t, vec3 n, int cls, Mat m, float ao){
  oG0 = vec4(t, gbOctEnc(normalize(n)), float(cls));
  oG1 = vec4(sqrt(clamp(m.alb, 0.0, 1.0)), clamp(m.rough, 0.03, 1.0));
  oG2 = vec4(max(m.emit, vec3(0.0)), m.metal);
  oG3 = vec4(ao, 0.0, 0.0, 0.0);
}
// a surface shaded in its own pass: its colour rides in the emission channel
void gbWritePrelit(float t, vec3 n, int cls, vec3 col){
  oG0 = vec4(t, gbOctEnc(normalize(n)), float(cls));
  oG1 = vec4(0.0, 0.0, 0.0, 1.0);
  oG2 = vec4(max(col, vec3(0.0)), 0.0);
  oG3 = vec4(1.0, 0.0, 0.0, 0.0);
}
