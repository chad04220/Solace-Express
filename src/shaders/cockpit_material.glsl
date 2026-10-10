//! kCockpitMaterial
//! Material / atlas mapping for the data-authored conventional fleet interiors.
// The utility strip is narrow in atlas space but wide on its physical glass. A scalar LOD used the
// compressed vertical axis for BOTH axes and blurred letters horizontally. Preserve the two footprints;
// the existing anisotropic sampler can then retain sharp glyph strokes without a larger atlas.
vec4 cockpitUtilityTex(vec2 q,vec2 footprint){
  vec2 uv=(q+vec2(.16,.11))/vec2(.58,.22);
  return textureGrad(uPanelTex,uv,vec2(footprint.x/.58,0),vec2(0,footprint.y/.22));
}
void shadeFleetCabin(inout Mat m,int mid,vec3 lp,vec3 ln,float px){
  if(!fleetCabin())return;
  CockpitLayout L=cockpitLayout();
#if HAS_ATLAS
  if(MODEL_IS(14)&&(mid==147||mid==148)){
    vec3 q=mid==147?atlasNavFrame(lp):atlasOverheadFrame(lp);
    bool face=mid==147?ln.z>.35:ln.y<-.55;vec3 sc=vec3(.003,.010,.017);
    if(face){
      if(mid==147)sc=pageTex(2,q.xy/vec2(.165,.115),px/.115);
      else{vec2 scale=vec2(.150,.108)/vec2(.038,.100);vec4 pt=cockpitUtilityTex(vec2(q.x,-q.z)/scale+vec2(.376,0),vec2(px)/scale);if(pt.a>.003)sc=pt.rgb/pt.a;}
      gDispPx=true;
    }
    m.alb=vec3(.004,.009,.013);m.rough=.085;m.metal=0.0;m.emit=sc*(.84+.31*uNight);m.nrm=vec3(0,0,1);return;
  }
#endif
  if(mid>=140 && mid<=143){
    int tile=mid-140; vec4 mount=fleetModule(tile); vec2 h=fleetModuleHalf(tile,mount);
    vec2 local=fleetModuleFrame(lp,tile,mount).xy;
    m.alb=vec3(.019,.023,.027);m.rough=.60;m.metal=0.0;m.nrm=vec3(0,0,1);
    if(ln.z>.55 && abs(local.x)<h.x && abs(local.y)<h.y){
      vec4 pt=vec4(0.0);vec3 sc=vec3(0.0);bool glass=false;
      if(tile<2){
        vec2 q=local/mount.w+(cockpitGlass()?vec2(.02,0):vec2(0));
        pt=panelTex(q,px/mount.w);glass=cockpitGlass();
      }else if(tile==2){
        if(cockpitGlass()) { sc=pageTex(0,local/h,px/min(h.x,h.y));pt=vec4(sc,1);glass=true; }
        else pt=panelTex(local/mount.w+vec2(cockpitTwin()?.2425:.20,0),px/mount.w);
      }else{
        // Reuse the atlas's rightmost spare strip. Physical aspect is free to vary by cockpit.
        vec2 scale=h/vec2(.038,.100);
        pt=cockpitUtilityTex(local/scale+vec2(.376,0),vec2(px)/scale);glass=true;
      }
      if(pt.a>.003){
        sc=pt.rgb/max(pt.a,.001);m.alb=mix(m.alb,sc*(glass?.035:.19),pt.a);
        m.emit=sc*(glass?(.84+.31*uNight):(.25+.52*uNight))*pt.a;
        m.rough=mix(m.rough,glass?.085:.17,pt.a);gDispPx=true;
      }
    }
  }else if(mid==145){
    vec3 nT;vec4 tx=triSample(lp,ln,M_PLASTIC,.30,nT);
    m.alb=(MODEL_IS(13)?vec3(.115,.135,.090):MODEL_IS(14)?vec3(.055,.075,.100):vec3(.055,.060,.067))*(.74+.35*tx.r);m.rough=.72;m.metal=.04;m.nrm=nT*.35;
  }else if(mid==146){
    m.alb=MODEL_IS(8)?vec3(.54,.235,.095):MODEL_IS(13)?vec3(.44,.39,.23):MODEL_IS(14)?vec3(.12,.31,.43):mix(L.trim.rgb,gColStripe,.40);
    m.rough=.48;m.metal=MODEL_IS(8)?.50:.10;m.emit=vec3(0.0);m.nrm=vec3(0,0,1);
  }else if(mid==11 || mid==63){
    bool floorPart=lp.y<gM[22].y-1.0,roofPart=lp.y>gM[22].y+.10;
    vec3 nT;vec4 tx=triSample(lp,ln,floorPart?M_CARPET:roofPart?M_FABRIC:M_PLASTIC,floorPart?.4:.35,nT);
    m.alb=tx.rgb*(floorPart?L.cloth.rgb*.42:roofPart?mix(L.trim.rgb,vec3(.74),.42):L.trim.rgb);
    m.rough=floorPart||roofPart?.94:.74;m.metal=0.0;m.nrm=nT*.4;
  }else if(mid==12){
    vec3 nT;vec4 tx=triSample(lp,ln,L.cloth.w>.5?M_LEATHER:M_FABRIC,.3,nT);
    m.alb=vec3(dot(tx.rgb,vec3(.3333)))*L.cloth.rgb*1.9;
    m.rough=mix(.96,.69,L.cloth.w);m.metal=0.0;m.nrm=nT*.65;
  }else if(mid==64){
    // Dome/map fittings retain warm readability; decorative lips are never brighter than instruments.
    m.emit=vec3(1,.76,.49)*(.055+.20*uNight);
  }
}
