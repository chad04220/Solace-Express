//! kWraithMaterial
//! The XR-40's surface materials.
// XR-40 surfaces (PBR texture sets): radar-absorbent faceted skin with sawtooth panel seams, smoked gold canopy,
// heat-tinted titanium nozzles and vanes, glowing turbine cores, violet cloak-emitter strips, a dark bay, the
// dark-energy bomb, laser housings and their emitter lenses, chrome actuator rods
void shadeWraith(inout Mat m, int mid, vec3 lp, vec3 ln, float t){
  vec3 nT; vec4 tx;
  float pulse = 0.75 + 0.25*sin(uTime*2.5);
  if (mid == 80 || mid == 81 || mid == 83) {
    tx = triSample(lp, ln, M_PAINT, 0.6, nT);
    vec3 base = gColBase*(0.85 + 0.3*tx.r);
    // radar-absorbent coating: matte charcoal with a faint iridescent sheen and fine tile seams
    float tile = max(abs(fract(lp.x*1.6 + lp.y*0.4) - 0.5), abs(fract(lp.z*1.1) - 0.5));
    vec2 sz = vec2(lp.z*0.9 + abs(lp.x)*0.9, lp.z*0.9 - abs(lp.x)*0.9);       // sawtooth (chevron) panel lines
    float saw = min(abs(fract(sz.x) - 0.5), abs(fract(sz.y) - 0.5));
    m.alb = base; m.rough = mix(0.5, 0.72, tx.a); m.metal = 0.15; m.nrm = nT;
    if (tile > 0.49) m.alb *= 0.75;
    if (saw < 0.008 && t < 80.0 && abs(fract(lp.z*0.25) - 0.5) < 0.2) m.alb *= 0.6;   // sawtooth access-panel seams
    if (mid == 81) { m.alb *= 0.82; m.rough = 0.62; }                        // control surfaces: slightly darker
    if (mid == 83) { m.alb = base*0.9; m.metal = 0.3; m.rough = 0.45; }       // pod shells
    if (abs(lp.x) < 0.04 && ln.y > 0.6) m.alb = mix(m.alb, gColStripe*0.3, 0.6); // spine stripe
  }
  else if (mid == 82) { m.alb = vec3(0.28, 0.2, 0.08); m.metal = 0.95; m.rough = 0.08; }   // smoked gold film
  else if (mid == 84) {
    tx = triSample(lp, ln, M_METAL, 0.8, nT); m.alb = tx.rgb*vec3(0.24, 0.23, 0.25); m.metal = 0.85; m.rough = clamp(tx.a*0.7, 0.18, 0.55); m.nrm = nT;
    float heat = clamp(uWr[2].x + uWr[2].y + uWr[2].z + uWr[2].w, 0.0, 4.0)*0.25;
    m.alb = mix(m.alb, vec3(0.2, 0.12, 0.24), 0.45*heat);                     // heat-blued titanium
  }
  else if (mid == 85) { tx = triSample(lp, ln, M_METAL, 1.4, nT); m.alb = tx.rgb*vec3(0.4, 0.41, 0.43); m.metal = 1.0; m.rough = 0.25; m.nrm = nT; }
  else if (mid == 86) { float th = clamp((uWr[2].x + uWr[2].y + uWr[2].z + uWr[2].w)*0.25, 0.0, 1.5);
    m.alb = vec3(0.02); m.emit = mix(vec3(0.35, 0.3, 1.0), vec3(0.95, 0.6, 1.0), th)*(0.4 + 9.0*th*th); }
  else if (mid == 87) { m.alb = vec3(0.05); m.rough = 0.2; m.emit = gColStripe*(1.0 + 2.0*uNight)*pulse*(1.0 + 3.0*uWr[4].w); }
  else if (mid == 88) { tx = triSample(lp, ln, M_METAL, 0.5, nT); m.alb = tx.rgb*vec3(0.07, 0.07, 0.08); m.metal = 0.6; m.rough = 0.5; m.nrm = nT;
    if (abs(fract(lp.z*2.0) - 0.5) < 0.03) m.emit = gColStripe*0.4*uWr[4].y; }                       // bay lights when open
  else if (mid == 89) {   // dark-energy bomb: black glassy core, violet plasma veins crawling over it
    vec3 bc = lp - vec3(0.0, -0.3, 0.1);
    float vein = vnoise3(bc*9.0 + vec3(0.0, uTime*2.0, 0.0)) + 0.5*vnoise3(bc*21.0 - vec3(uTime*3.0));
    m.alb = vec3(0.005); m.rough = 0.05; m.metal = 0.0;
    m.emit = vec3(0.55, 0.15, 1.0)*pow(smoothstep(0.75, 1.15, vein), 2.0)*6.0 + vec3(0.2, 0.7, 1.0)*pow(smoothstep(1.05, 1.3, vein), 3.0)*8.0;
  }
  else if (mid == 90) { tx = triSample(lp, ln, M_METAL, 0.9, nT); m.alb = tx.rgb*vec3(0.12, 0.12, 0.13); m.metal = 0.8; m.rough = 0.4; m.nrm = nT;
    if (abs(fract(lp.z*14.0) - 0.5) < 0.08) m.alb *= 0.5; }                                           // cooling slots
  else if (mid == 91) { float f = uWr[5].w; m.alb = vec3(0.1, 0.02, 0.02); m.rough = 0.02; m.emit = vec3(1.0, 0.15, 0.25)*(0.6*uWr[4].z + 25.0*f); }
  else if (mid == 92) { m.alb = vec3(0.75, 0.76, 0.78); m.metal = 1.0; m.rough = 0.12; }
  else if (mid == 93) { m.alb = vec3(0.01); m.rough = 0.9; }
}
