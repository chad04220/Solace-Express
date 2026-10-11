//! kEnemyMaterial
Mat enemyCraftMaterial(float id,vec3 p,vec3 n,vec4 state){
 Mat m;m.alb=vec3(.055,.065,.072);m.rough=.54;m.metal=.65;m.nrm=vec3(0,0,1);m.emit=vec3(0);
 int i=int(id+.5);
 if(i==111){m.alb=vec3(.15,.165,.172);m.rough=.40;m.metal=.74;}
 if(i==112){m.alb=vec3(.34,.145,.068);m.rough=.38;m.metal=.78;}
 if(i==113){m.alb=vec3(.020,.026,.029);m.rough=.71;m.metal=.35;}
 if(i==114){m.alb=vec3(.50,.17,.023);m.rough=.24;m.metal=.35;m.emit=vec3(1.,.27,.025)*(.32+.38*clamp(state.y,0.,1.));}
 if(i==115){m.alb=vec3(.11,.40,.28);m.rough=.25;m.metal=.45;m.emit=vec3(.24,.92,.61)*(.14+2.2*clamp(state.x,0.,1.));}
 if(i==116){m.alb=vec3(.20,.07,.015);m.rough=.3;m.metal=.45;m.emit=vec3(1.,.32,.035)*(.07+1.8*clamp(state.y,0.,1.));}
 if(i==117){m.alb=vec3(.11,.31,.30);m.rough=.27;m.metal=.5;m.emit=vec3(.15,.72,.68)*(.1+1.4*clamp(state.y,0.,1.));}
 // Fine manufactured grain and panel grooves are material-only, never holes.
 if(i<=112){
  float phase=p.x*173.+p.y*131.+p.z*197.;
  // Keep authored close-up grain, remove frequencies above the pixel Nyquist limit.
  float grainFootprint=max(abs(dFdx(phase)),abs(dFdy(phase)));
  float grainWeight=1.-smoothstep(1.5,3.14159265,grainFootprint);
  float grain=.96+.04*sin(phase)*grainWeight;
  m.alb*=grain;
  float longitudinal=abs(fract(p.z*.60+.5)-.5);
  float seamFootprint=fwidth(p.z*.60);
  float seamAA=max(.5*seamFootprint-.0055,0.);
  float seam=1.-smoothstep(.007-seamAA,.018+seamAA,longitudinal);
  // Subpixel seam cycles converge to their authored average coverage (2*.0125).
  seam=mix(seam,.025,smoothstep(.06,.25,seamFootprint));
  m.alb*=1.-.21*seam*abs(n.y);
 }
 return m;
}
