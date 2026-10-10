  static int wreckPoseRegressions() {
    int fails=0,cases=0;
    auto same=[](const float* a,const float* b,int n){for(int i=0;i<n;++i)if(fabsf(a[i]-b[i])>1e-6f)return false;return true;};
    auto poseSame=[&](const PlaneVisual& a,const PlaneVisual& b){
      return same(a.PS,b.PS,4)&&same(a.Ctl,b.Ctl,4)&&same(a.Pr,b.Pr,4)&&same(a.wheel,b.wheel,3)&&
             same(a.flame,b.flame,4)&&same(&a.wr[0][0],&b.wr[0][0],28)&&same(a.engineHealth,b.engineHealth,4);
    };
    for(int type=0;type<kAircraftCount;++type)for(float gear:{0.f,.35f,1.f}) {
      Game q;q.initHeadless();q.screen=SCR_FLIGHT;q.contract=g_story[0];q.botControl=false;
      const auto& s=kAircraft[type];q.plane.reset(&s,vec3(0,1500,0),0,s.maxFuel*.5f,0,true,gear==1.f?12.f:80.f);
      q.plane.gear=gear;q.plane.flaps=.4f;q.plane.ctl.pitch=.3f;q.plane.ctl.roll=-.2f;q.plane.ctl.yaw=.7f;
      q.plane.ctl.throttle=.65f;q.plane.rpm=1200;q.propAngle=1.234f;q.camMode=0;
      q.plane.nozzle=.4f;q.plane.fanAngle=7.5f;q.plane.surf=vec3(.12f,.2f,-.1f);
      for(int i=0;i<4;++i){q.plane.podTilt[i]=.3f+.13f*i;q.plane.podYaw[i]=.07f*i;q.plane.podVane[i]=-.05f*i;}
      q.wraith.bay=.32f;q.wraith.lasers=.15f;
      for(int i=0;i<3;++i)q.plane.wheelMotion[i].travel=.21*(i+1);
      q.camPos=q.plane.pos+vec3(20,10,30);
      const PlaneVisual before=q.buildFrame().plane;
      q.crashed=true;q.breakUp(q.plane.vel,false,true);
      q.plane.vel=vec3();q.plane.w=vec3();
      bool ok=poseSame(q.buildFrame().plane,before);
      q.update(.01f);q.in.endFrame();
      ok=ok&&fabsf(q.propAngle-before.Pr[0])<1e-6f&&poseSame(q.buildFrame().plane,before);
      // Real keyboard and pad routes can still handle view/skip actions. They
      // must not steer an already detached nose leg or move a torn surface.
      q.in.down['S']=q.in.down['D']=q.in.down['Q']=q.in.down[K_SHIFT]=true;
      q.flightControls(.1f);q.in.endFrame();
      ok=ok&&poseSame(q.buildFrame().plane,before);
      q.in=Input();q.in.pad=true;q.in.lx=.8f;q.in.ly=-.8f;q.in.rt=.9f;q.in.buttons=PAD_LB;
      q.flightControls(.1f);q.in.endFrame();
      // Also catch future visual paths that accidentally read mutable actuator
      // data rather than the snapshot, including the research nozzle/pod poses.
      q.plane.ctlSurf=vec3(-.9f,.8f,-.6f);q.plane.gear=1.f-gear;q.plane.flaps=.9f;
      q.plane.nozzle=.7f;q.plane.fanAngle+=2;for(int i=0;i<4;++i){q.plane.podTilt[i]+=.3f;q.plane.podYaw[i]-=.2f;}
      q.wraith.bay=.8f;q.wraith.lasers=.7f;q.propAngle=2.345f;q.plane.fail.engineHealth[0]=0;
      for(int i=0;i<3;++i)q.plane.wheelMotion[i].travel+=.5;
      ok=ok&&poseSame(q.buildFrame().plane,before);
      // The followed centre section can drift far from the separation point.
      // Shadow-map caster selection uses PlaneVisual::pos within 80 m, so only
      // articulation may be frozen: the frame's world placement must stay live.
      for(auto& piece:q.wreck)piece.c+=vec3(130,5,-95);
      q.updateWreck(.01f);
      q.plane.q=quat::axisAngle(vec3(0,1,0),.6f);
      const FrameParams moved=q.buildFrame();
      const vec3 liveRight=q.plane.right();
      ok=ok&&poseSame(moved.plane,before)&&length(moved.plane.pos-before.pos)>80.f&&
         length(moved.plane.pos-q.wreck.back().c)<1e-5f&&
         length(vec3(moved.plane.rot[0],moved.plane.rot[1],moved.plane.rot[2])-liveRight)<1e-5f;
      int nearbyCasters=0;
      for(int i=0;i<moved.wreck.pieces;++i)if(length(moved.wreck.mid[i]-moved.plane.pos)<=80.f)++nearbyCasters;
      ok=ok&&nearbyCasters>0;
      // A normal new/live frame is never pinned to a previous wreck's controls.
      q.wreck.clear();q.crashed=false;
      const PlaneVisual live=q.buildFrame().plane;
      ok=ok&&!poseSame(live,before)&&fabsf(live.Pr[0]-q.propAngle)<1e-6f;
      ++cases;if(!ok){++fails;printf("FAIL wreck pose: %s gear %.2f\n",s.name,gear);}
    }
    printf("Wreck pose regression: %d cases, %d failures; separation pose, keyboard/pad, actuator mutation, moving shadow centre and live-frame recovery checks\n",cases,fails);
    return fails;
  }

  static int wheelVisualRegressions() {
    int failures=0,cases=0;
    for(int type=0;type<kAircraftCount;++type)for(double travel:{-12.3,-.21,0.,.21,12.3}) {
      Game q;q.initHeadless();q.screen=SCR_FLIGHT;q.contract=g_story[0];q.botControl=false;
      const auto& s=kAircraft[type]; const auto& m=kModels[type];
      q.plane.reset(&s,vec3(0,1500,0),0,s.maxFuel*.5f,0,true,12.f);q.camMode=0;
      Traffic traffic;TrafficCraft c;c.spec=type;c.pos=q.plane.pos;c.speed=12.f;
      for(int wi=0;wi<3;++wi)q.plane.wheelMotion[wi].travel=c.wheelMotion[wi].travel=travel*(wi+1);
      traffic.craft.push_back(c);TrafficVisual visual{};
      const PlaneVisual player=q.buildFrame().plane;
      bool ok=traffic.fillVisuals(q.plane.pos,&visual,1,nullptr)==1;
      // Independent expected radii retain every legacy value. Atlas's .4218 m
      // nose tyre is .74 of the .57 m main, matching gearNoseR in the field.
      const float mainR=s.special?.38f:m.wheelR;
      const float noseR=type==kAtlas?.4218f:s.special?.33f:s.taildragger?.10f:m.gear==3?mainR*.75f:mainR*.85f;
      for(int wi=0;wi<3;++wi) {
        const float expected=q.plane.wheelMotion[wi].angle(wi==2?noseR:mainR);
        ok=ok&&fabsf(player.wheel[wi]-expected)<1e-5f&&fabsf(visual.t[(25+wi)*4+3]-expected)<1e-5f;
      }
      for(int i=0;i<96;++i)ok=ok&&fabsf(player.M[i]-visual.t[i])<1e-6f;
      if(type==kAtlas)ok=ok&&fabsf(visual.t[19*4]-3.65f)<1e-6f&&fabsf(visual.t[18*4]-3.45f)<1e-6f;
      ++cases;if(!ok){++failures;printf("FAIL wheel visual parity: %s travel %.2f\n",s.name,travel);}
    }
    printf("Wheel visual parity: %d cases, %d failures; all player/traffic radii, signed travel and packed dimensions\n",cases,failures);
    return failures;
  }
