//! kWraithStoreMaterial
// Exact shared bay/released hardware materials; no airframe, cockpit or weapon code.
bool shadeWraithStore(inout Mat m,int mid,vec3 lp,vec3 ln){
 vec3 nT;vec4 tx;
  if (mid == 82) { m.alb = vec3(0.28, 0.2, 0.08); m.metal = 0.95; m.rough = 0.08; }   // smoked gold film
  else if (mid == 89 && uWrBombSet==2) {
    m.alb=vec3(.025,.11,.12);m.metal=.35;m.rough=.24;
    m.emit=vec3(.08,.65,.72)*(.22+.06*sin(uTime*2.));
  }
  else if (mid == 89) {   // dark-energy bomb: black glassy core, violet plasma veins crawling over it
    vec3 bc = lp - vec3(0.0, -0.3, 0.1);
    float vein = vnoise3(bc*9.0 + vec3(0.0, uTime*2.0, 0.0)) + 0.5*vnoise3(bc*21.0 - vec3(uTime*3.0));
    m.alb = vec3(0.005); m.rough = 0.05; m.metal = 0.0;
    m.emit = vec3(0.55, 0.15, 1.0)*pow(smoothstep(0.75, 1.15, vein), 2.0)*6.0 + vec3(0.2, 0.7, 1.0)*pow(smoothstep(1.05, 1.3, vein), 3.0)*8.0;
  }
  else if (mid == 90) { tx = triSample(lp, ln, M_METAL, 0.9, nT); m.alb = tx.rgb*vec3(0.12, 0.12, 0.13); m.metal = 0.8; m.rough = 0.4; m.nrm = nT;
    if (abs(fract(lp.z*14.0) - 0.5) < 0.08) m.alb *= 0.5; }                                           // cooling slots
  else if (mid == 92) { m.alb = vec3(0.75, 0.76, 0.78); m.metal = 1.0; m.rough = 0.12; }
  else if (mid == 93) { m.alb = vec3(0.01); m.rough = 0.9; }
 else return false;
 return true;
}
