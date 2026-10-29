// Military service terminal. The research terminal's three-column organization,
// with a quiet olive palette and only live career/mission information.
#include "game.h"
#include "military_missions.h"
#include "ui_identity.h"

std::vector<std::string> wrap(const std::string& s, float width, float size);

namespace {
const vec3 M_INK = svui::field, M_PANEL = svui::fieldPanel, M_EDGE(.34f,.40f,.29f);
const vec3 M_ACCENT = svui::olive, M_TEXT(.96f,.95f,.88f), M_DIM(.69f,.73f,.64f);
const vec3 M_WARN(.94f,.72f,.37f), M_BAD(.96f,.49f,.36f);
float militaryScale(float requested) {
  return std::min(requested, std::min(g_ren.H / 720.f, g_ren.W / 1120.f));
}
std::string fit(const std::string& text, float width, float size) {
  if (g_ren.textWidth(text,size)<=width) return text;
  std::string out=text;
  while(!out.empty() && g_ren.textWidth(out+"...",size)>width) out.pop_back();
  return out+"...";
}
void surface(float x,float y,float w,float h,float s,float opacity=1.f) {
  svui::fieldSurface(g_ren,x,y,w,h,s,opacity);
}
void label(float x,float y,float width,float s,const std::string& text) {
  const float tw=g_ren.text(x,y,11.5f*s,text,M_DIM,1,0,false);
  if(tw+12*s<width) g_ren.rect(x+tw+12*s,y+6*s,width-tw-12*s,s,M_EDGE,.5f);
}
void rule(float x,float y,float w,float s) { g_ren.rect(x,y,w,s,M_EDGE,.45f); }
const char* objectiveName(const military::MissionSpec& m) {
  if(m.id==military::Interception) return "INTERCEPTION";
  if(m.objective==hive::MissionKind::Recon) return "RECONNAISSANCE";
  if(m.objective==hive::MissionKind::Defense) return "AIR DEFENSE";
  if(m.objective==hive::MissionKind::Strike) return "PRECISION STRIKE";
  return "INTERCEPTION";
}
int boardRow(int missionId) {
  for(int row=0;row<military::kBoardMissionCount;++row)
    if(military::kBoardMissionIds[row]==missionId) return row;
  return 0;
}
}

Game::ResLayout Game::militaryLayout() const {
  const float s=militaryScale(S()),W=float(g_ren.W),H=float(g_ren.H);
  ResLayout L{};
  L.lx=24*s;L.lw=270*s;L.rw=322*s;L.rx=W-24*s-L.rw;
  L.top=138*s;L.bot=H-112*s;
  L.px0=L.lx+L.lw+18*s;L.px1=L.rx-18*s;
  L.cx=(L.px0+L.px1)*.5f;
  L.cy=(L.top+79*s+L.bot-56*s)*.5f;
  L.r=std::max(32*s,.43f*std::min(L.px1-L.px0,L.bot-L.top-135*s));
  return L;
}

// Same input contract as the shared button, with stable IDs and a local palette.
// No global palette mutation: civilian and research screens keep their own style.
bool Game::militaryButton(uint32_t id,float x,float y,float w,float h,const std::string& text,bool enabled,bool primary) {
  const float s=militaryScale(S());
  if(enabled) focusList.push_back({id,x,y,w,h});
  const bool focused=enabled && focusNav && focusId==id;
  const bool hoveredNow=enabled && (hovered(x,y,w,h)||focused);
  const float glow=anim(id,hoveredNow?1.f:0.f,14);
  const vec3 fill=primary ? svui::amber*(.88f+.12f*glow) : M_PANEL+M_ACCENT*(.045f+.065f*glow);
  g_ren.rect(x,y,w,h,enabled?fill:M_PANEL,.97f,3*s);
  g_ren.rectOutline(x,y,w,h,enabled?M_ACCENT:M_EDGE,enabled?(.25f+.35f*glow):.35f,3*s,s);
  if(focused) g_ren.rectOutline(x-3*s,y-3*s,w+6*s,h+6*s,M_ACCENT,.95f,5*s,2*s);
  const float fs=std::min(14.f*s,h*.34f);
  g_ren.text(x+w*.5f,y+h*.5f-fs*.53f,fs,fit(text,w-16*s,fs),!enabled?M_DIM*.66f:primary?M_INK:M_TEXT,1,1,false);
  const bool take=focused && (in.pressed[K_ENTER]||in.pressed[' ']);
  const bool clicked=enabled && !focusNav && hoveredNow && in.mPressed[0];
  if(take||clicked) {
    in.mPressed[0]=false;
    if(take) in.pressed[K_ENTER]=in.pressed[' ']=false;
    g_audio.trigger(SFX_CLICK);
    return true;
  }
  return false;
}

void Game::drawMilitary(const FrameParams& fp) {
  if (demo || (career.flights < kMilitaryFlights && career.military.sorties == 0)) { drawMilitaryLocked(); return; }   // (the demo: a teaser; else cleared with ten flights logged, and a record already begun stays open)
  if(in.pressed[K_ESC]) { in.pressed[K_ESC]=false;in.pressed[K_ENTER]=false;hubList=0;militaryView.release();return; }
  (void)fp; // The live aircraft is rendered by militaryPreviewCamera before the UI.
  const float s=militaryScale(S()),W=float(g_ren.W),H=float(g_ren.H);
  const ResLayout L=militaryLayout();
  militarySelection=military::kBoardMissionIds[boardRow(militarySelection)];
  // Snapshot the same selection that the aircraft renderer used this frame.
  // Row activation becomes visible next frame, keeping model, text and launch coherent.
  const int displayedMission=militarySelection;
  int nextSelection=displayedMission;
  const int selectedRow=boardRow(displayedMission);
  const military::MissionSpec& m=*military::mission(displayedMission);
  const bool unlocked=career.militaryMissionUnlocked(displayedMission);
  const bool completed=career.militaryMissionCompleted(displayedMission);
  const std::string prerequisite=fmt("Complete sortie %02d to unlock.",std::max(1,selectedRow));
  const AircraftSpec& aircraft=kAircraft[m.aircraft];
  const Contract missionContract=career.militaryContract(displayedMission);
  militaryView.select(m.aircraft,displayedMission);
  const bool ready=unlocked && !commitBlocked() && career.canFly(missionContract,m.aircraft)==Career::SRC_MILITARY;
  uiGlass=false;
  showRadio=false; // A hidden civilian radio overlay must not intercept terminal input.
  // Keep the actual aircraft visible. Opaque instrument surfaces belong to the
  // left and right columns; the central stage is intentionally unobstructed.
  g_ren.rect(0,0,W,H,M_INK,.30f);
  // Sparse field-chart contours occupy only the empty aircraft stage.
  for(int k=0;k<6;++k){const float yy=L.top+(42+k*43)*s;g_ren.line(L.px0,yy,L.px1,yy+22*s,s,M_EDGE,.16f);}
  g_ren.rectOutline(L.px0+12*s,L.top+18*s,54*s,54*s,M_EDGE,.35f,0,s);
  g_ren.rect(0,0,W,120*s,M_INK,.95f);
  g_ren.rect(0,H-94*s,W,94*s,M_INK,.97f);
  svui::chevrons(g_ren,24*s,4*s,160*s,s,svui::amber,.45f);
  rule(24*s,68*s,W-48*s,s);
  g_ren.text(24*s,16*s,28*s,"MILITARY CAREER",M_TEXT,1,0,false);
  g_ren.text(25*s,49*s,10.5f*s,"SOMNIUM VOLANDI  /  DEFENSE COMMAND",M_ACCENT,1,0,false);
  // Actual saved career record, without implying an invented rank threshold.
  const float statLeft=std::max(500*s,W-520*s),statW=(W-statLeft-24*s)/4;
  auto stat=[&](int col,const char* title,const std::string& value) {
    const float x=statLeft+col*statW;
    g_ren.text(x,20*s,10.5f*s,title,M_DIM,1,0,false);
    const float valueSize=std::min(18.f*s,18.f*s*(statW-10*s)/std::max(1.f,g_ren.textWidth(value,18*s)));
    g_ren.text(x,37*s,valueSize,value,M_TEXT,1,0,false);
  };
  stat(0,"SERVICE RANK",std::to_string(career.military.rank));
  stat(1,"CREDITS",fmt("%d SC",career.military.credits));
  stat(2,"SORTIES",std::to_string(career.military.sorties));
  stat(3,"SUCCESSES",std::to_string(career.military.successes));
  g_ren.text(24*s,86*s,14*s,"Funded sorties. Assigned airframes. A separate service record.",M_TEXT,1,0,false);
  g_ren.text(24*s,107*s,11.5f*s,"Complete each sortie to unlock the next. Cleared sorties remain replayable.",M_DIM,1,0,false);
  g_ren.text(W-24*s,94*s,12*s,fmt("INTELLIGENCE COLLECTED  %d",career.military.intelligence),M_ACCENT,1,2,false);

  // Mission register: rows scroll as a unit and remain reachable from the D-pad.
  surface(L.lx,L.top,L.lw,L.bot-L.top,s);
  label(L.lx+14*s,L.top+14*s,L.lw-28*s,s,fmt("SORTIE REGISTER  /  %02d",military::kBoardMissionCount));
  const float listTop=L.top+43*s,listBot=L.bot-46*s,rowH=61*s,step=67*s;
  const int visible=std::max(1,int((listBot-listTop+6*s)/step));
  int& firstRow=menu.military.firstRow; int& seenSelection=menu.military.seenSelection;
  if(seenSelection!=displayedMission) {
    seenSelection=displayedMission;
    if(selectedRow<firstRow) firstRow=selectedRow;
    if(selectedRow>=firstRow+visible) firstRow=selectedRow-visible+1;
  }
  bool wheeled=false;
  if(hovered(L.lx,listTop,L.lw,listBot-listTop)&&in.wheel!=0) {
    firstRow-=int(in.wheel);in.wheel=0;wheeled=true;
  }
  auto rowId=[](int row) { return 0x6D1000u+uint32_t(row); };
  firstRow=std::clamp(firstRow,0,std::max(0,military::kBoardMissionCount-visible));
  focusScrollRows(rowId,military::kBoardMissionCount,firstRow,visible,1,wheeled);
  firstRow=std::clamp(firstRow,0,std::max(0,military::kBoardMissionCount-visible));
  for(int row:{firstRow-1,firstRow+visible}) if(row>=0 && row<military::kBoardMissionCount)
    focusList.push_back({rowId(row),L.lx+12*s,listTop+(row-firstRow)*step,L.lw-24*s,rowH});
  for(int row=firstRow;row<std::min(firstRow+visible,military::kBoardMissionCount);++row) {
    const auto& entry=*military::mission(military::kBoardMissionIds[row]);
    const float x=L.lx+12*s,y=listTop+(row-firstRow)*step,w=L.lw-24*s;
    const bool selected=displayedMission==entry.id;
    const bool rowUnlocked=career.militaryMissionUnlocked(entry.id);
    const bool rowCompleted=career.militaryMissionCompleted(entry.id);
    if(militaryButton(rowId(row),x,y,w,rowH,"",true,false)) {
      nextSelection=entry.id;
      militaryView.idle=0;
      // Detail and the live rendered aircraft update together next frame.
    }
    if(selected) {
      g_ren.rect(x,y,3*s,rowH,M_ACCENT,1,1*s);
      g_ren.rect(x+3*s,y,w-3*s,rowH,M_ACCENT,.10f,3*s);
    }
    const std::string rowTitle=fmt("%02d  %s",row+1,rowUnlocked?entry.title:"CLASSIFIED");
    g_ren.text(x+12*s,y+8*s,13*s,fit(rowTitle,w-24*s,13*s),selected?M_TEXT:M_TEXT*.90f,1,0,false);
    if(rowUnlocked) {
      g_ren.text(x+12*s,y+28*s,10*s,fit(kAircraft[entry.aircraft].name,w-88*s,10*s),M_DIM,1,0,false);
      g_ren.text(x+w-10*s,y+28*s,10*s,fmt("%d SC",entry.reward),M_ACCENT,1,2,false);
    } else {
      g_ren.text(x+12*s,y+28*s,10*s,fit(fmt("Complete sortie %02d to unlock.",row),w-24*s,10*s),M_DIM,1,0,false);
    }
    g_ren.text(x+12*s,y+46*s,9*s,rowCompleted?"COMPLETED / REPLAY":rowUnlocked?"AVAILABLE":"LOCKED",rowUnlocked?M_ACCENT:M_DIM,1,0,false);
  }
  const float listY=L.bot-35*s;
  if(militaryButton(0x6D0010u,L.lx+12*s,listY,34*s,24*s,"<",firstRow>0)) { --firstRow; }
  if(militaryButton(0x6D0011u,L.lx+L.lw-46*s,listY,34*s,24*s,">",firstRow+visible<military::kBoardMissionCount)) { ++firstRow; }
  g_ren.text(L.lx+L.lw*.5f,listY+6*s,10.5f*s,fmt("%d-%d / %d",firstRow+1,std::min(firstRow+visible,military::kBoardMissionCount),military::kBoardMissionCount),M_DIM,1,1,false);

  // Assigned aircraft stage. Aircraft selection follows the mission catalog;
  // hostile Hive craft are never part of this register or the launch path.
  const float stageW=L.px1-L.px0;
  g_ren.rectGrad(L.px0,L.top-1*s,stageW,76*s,M_INK,M_PANEL,.91f,3*s);
  label(L.px0+6*s,L.top+1*s,stageW-12*s,s,unlocked?"ASSIGNED AIRFRAME":"RESTRICTED HANGAR");
  g_ren.text(L.px0+6*s,L.top+23*s,25*s,fit(unlocked?aircraft.name:"CLASSIFIED",stageW-12*s,25*s),M_TEXT,1,0,false);
  g_ren.text(L.px0+6*s,L.top+56*s,11.5f*s,fit(unlocked?m.role:prerequisite,stageW-12*s,11.5f*s),M_ACCENT,1,0,false);
  const float stageTop=L.top+79*s,stageBot=L.bot-56*s;
  // Small reference corners frame the real rendered model, not a diagram.
  for(int side=0;side<2;++side) {
    const float xx=side?L.px1-6*s:L.px0+6*s;
    const float dx=side?-12*s:12*s;
    for(float yy:{stageTop,stageBot}) {
      g_ren.line(xx,yy,xx+dx,yy,s,M_EDGE,.75f);
      g_ren.line(xx,yy,xx,yy+(yy==stageTop?10*s:-10*s),s,M_EDGE,.75f);
    }
  }
  updateMenuPreview(militaryView,L.px0,stageTop,stageW,stageBot-stageTop,0x6D0020u);
  drawMenuPreviewControls(militaryView,0x6D0020u,L.px0,L.bot-56*s,stageW,s,M_PANEL,M_ACCENT,M_TEXT,M_DIM,3*s);

  // Briefing is a real scrollable document: no dropped requirements at large scale.
  surface(L.rx,L.top,L.rw,L.bot-L.top,s);
  const float bx=L.rx+16*s,bw=L.rw-38*s,bodyTop=L.top+42*s,bodyBot=L.bot-47*s;
  label(bx,L.top+14*s,bw,s,"MISSION BRIEFING");
  float& briefScroll=menu.military.briefScroll; float& briefHeight=menu.military.briefHeight;
  int& briefMission=menu.military.briefMission;
  if(briefMission!=displayedMission) { briefMission=displayedMission;briefScroll=0;briefHeight=0; }
  if(hovered(L.rx,bodyTop,L.rw,bodyBot-bodyTop)&&in.wheel!=0) { briefScroll-=in.wheel*40*s;in.wheel=0; }
  const float maxScroll=std::max(0.f,briefHeight-(bodyBot-bodyTop));
  briefScroll=clampf(briefScroll,0,maxScroll);
  g_ren.uiClip(bx,bodyTop,L.rx+L.rw-12*s,bodyBot);
  float by=bodyTop-briefScroll;
  auto paragraph=[&](const std::string& text,vec3 col,float size,float leading) {
    for(const auto& line:wrap(text,bw,size*s)) { g_ren.text(bx,by,size*s,line,col,1,0,false);by+=leading*s; }
  };
  if(!unlocked) {
    paragraph("CLASSIFIED",M_TEXT,20,25);by+=12*s;
    paragraph(fmt("SORTIE %02d / ACCESS RESTRICTED",selectedRow+1),M_ACCENT,11.5f,17);by+=8*s;
    paragraph("Command has not released this assignment.",M_TEXT,13,19);by+=19*s;
    rule(bx,by,bw,s);by+=18*s;
    label(bx,by,bw,s,"CLEARANCE REQUIRED");by+=24*s;
    paragraph(prerequisite,M_TEXT,13,19);by+=10*s;
    paragraph("Start with the available sortie and complete the service assignments in order. A successful extraction unlocks the next sortie.",M_DIM,12.5f,18);by+=15*s;
    paragraph("The airframe, mission orders, equipment and service award will be declassified together.",M_DIM,12.5f,18);by+=15*s;
    paragraph("Completed sorties remain available to replay.",M_ACCENT,12.5f,18);
  } else {
    paragraph(m.title,M_TEXT,20,25);by+=12*s;
    paragraph(objectiveName(m),M_ACCENT,11.5f,17);by+=8*s;
    paragraph(m.brief,M_TEXT,13,19);by+=19*s;
    label(bx,by,bw,s,"SUPPLIED MISSION KIT");by+=24*s;
    paragraph(m.loadout,M_DIM,12.5f,18);by+=19*s;
    rule(bx,by,bw,s);by+=18*s;
    label(bx,by,bw,s,"SORTIE ORDERS");by+=24*s;
    if(m.objective==hive::MissionKind::Recon) {
      paragraph(fmt("SCAN  Hold each site in range for %.0f seconds.",m.scanSeconds),M_TEXT,12.5f,18);
      by+=7*s;
      paragraph(fmt("RANGE  Within %.0f m of the marked site.",m.scanRange),M_DIM,12.5f,18);
      if(m.minAltitude>0 || m.maxAltitude<99999.f) {
        by+=7*s;
        paragraph(fmt("ALTITUDE  %.0f-%.0f m %s.",m.minAltitude,m.maxAltitude,m.id==military::HoverScan?"above the target roof":"MSL"),M_DIM,12.5f,18);
      }
      if(m.minSpeed>0 || m.maxSpeed<99999.f) {
        by+=7*s;
        paragraph(m.maxSpeed>=99999.f?fmt("SPEED  At least %.0f kt ground speed.",m.minSpeed*1.9438445f):fmt("SPEED  %.0f-%.0f kt ground speed.",m.minSpeed*1.9438445f,m.maxSpeed*1.9438445f),M_DIM,12.5f,18);
      }
    } else {
      paragraph("Complete the marked combat objectives. Preserve the assigned aircraft.",M_TEXT,12.5f,18);
    }
    by+=10*s;
    paragraph("EXTRACT  Return to the airborne extraction gate after all objectives are complete.",M_ACCENT,12.5f,18);
    if(m.aircraft==kSomnus) {
      by+=20*s;rule(bx,by,bw,s);by+=18*s;
      label(bx,by,bw,s,"FLIGHT CONTROLS");by+=24*s;
      const bool pad=padPrompts();
      paragraph("ARM  "+actLabel(ACT_WEAPONS,pad)+" toggles weapons.",M_TEXT,12.5f,18);by+=7*s;
      paragraph("FIRE  "+actLabel(ACT_FIRE,pad)+" fires the forward weapon.",M_TEXT,12.5f,18);by+=7*s;
      paragraph("STORES  "+actLabel(ACT_BOMB,pad)+" releases the selected payload.",M_TEXT,12.5f,18);
      if(m.forward==hive::ForwardSet::Heavy) {
        by+=7*s;
        paragraph(fmt("Hold %s for %.2f s to charge the Heavy cannon. Release cancels the charge.",actLabel(ACT_FIRE,pad).c_str(),hive::forwardSpec(m.forward).charge),M_DIM,12,17);
      }
      if(m.id==military::HoverScan) {
        by+=7*s;
        paragraph("PODS  "+actLabel(ACT_FLAPS_DN,pad)+" tilts toward hover; "+actLabel(ACT_FLAPS_UP,pad)+" tilts forward.",M_ACCENT,12.5f,18);by+=7*s;
        paragraph("At 90 degrees, hold "+actLabel(ACT_THR_UP,pad)+" for lift; release to idle.",M_DIM,12,17);
      }
    }
    by+=20*s;rule(bx,by,bw,s);by+=18*s;
    label(bx,by,bw,s,"SERVICE AWARD");by+=24*s;
    paragraph(fmt("%d service credits on success",missionContract.payout),M_TEXT,15,21);
    if(m.intelligence) { by+=5*s;paragraph("Successful reconnaissance adds intelligence to your service record.",M_DIM,12,17); }
  }
  by+=10*s;
  briefHeight=by+briefScroll-bodyTop;
  g_ren.uiClipOff();
  const float actualMax=std::max(0.f,briefHeight-(bodyBot-bodyTop));
  if(actualMax>0) {
    const uint32_t scrollId=0x6D0040u;
    const float sx=L.rx+L.rw-10*s,track=bodyBot-bodyTop,thumb=std::max(28*s,track*track/briefHeight);
    focusList.push_back({scrollId,sx-6*s,bodyTop,12*s,track});
    g_ren.rect(sx,bodyTop,2*s,track,M_EDGE,.45f);
    g_ren.rect(sx,bodyTop+(track-thumb)*briefScroll/actualMax,2*s,thumb,M_ACCENT,.9f);
    if(focusNav&&focusId==scrollId) {
      g_ren.rectOutline(L.rx-2*s,bodyTop-2*s,L.rw+4*s,track+4*s,M_ACCENT,.85f,3*s,s);
      g_ren.text(L.rx+L.rw-18*s,L.bot-46*s,9.5f*s,"RIGHT STICK / WHEEL",M_DIM,1,2,false);
    }
  }
  if(militaryButton(0x6D0041u,bx,L.bot-34*s,88*s,23*s,"Read up",briefScroll>0)) briefScroll=std::max(0.f,briefScroll-120*s);
  if(militaryButton(0x6D0042u,L.rx+L.rw-104*s,L.bot-34*s,88*s,23*s,"Read down",briefScroll<actualMax)) briefScroll=std::min(actualMax,briefScroll+120*s);

  rule(24*s,H-94*s,W-48*s,s);
  if(militaryButton(0x6D0001u,24*s,H-75*s,150*s,42*s,"< Civilian work")) { hubList=0;militaryView.release();return; }
  const float noteX=195*s,noteW=L.rx-noteX-20*s;
  const std::string status=!unlocked?"CLASSIFIED / LAUNCH LOCKED":commitBlocked()?"SAVE REQUIRED BEFORE LAUNCH":ready?(completed?"COMPLETED / READY TO REPLAY":"AVAILABLE / READY TO DEPLOY"):"SORTIE UNAVAILABLE";
  g_ren.text(noteX,H-76*s,11.5f*s,fit(status,noteW,11.5f*s),unlocked&&commitBlocked()?M_WARN:M_ACCENT,1,0,false);
  const std::string detail=!unlocked?prerequisite:commitBlocked()?saveWhy:"No civilian purchase, rental or fuel charge.";
  g_ren.text(noteX,H-54*s,12*s,fit(detail,noteW,12*s),unlocked&&commitBlocked()?M_WARN:M_DIM,1,0,false);
  const char* launchLabel=!unlocked?"CLASSIFIED / Locked":completed?"Replay funded sortie":"Deploy funded sortie";
  if(militaryButton(0x6D0002u,L.rx,H-78*s,L.rw,46*s,launchLabel,ready,true)) { launchMilitary(displayedMission);return; }
  if(commitBlocked() && militaryButton(0x6D0003u,noteX,H-28*s,120*s,22*s,"Retry save")) retryCommit();
  militarySelection=nextSelection;
}

void Game::drawMilitaryPause() {
  const float s=militaryScale(S()),W=float(g_ren.W),H=float(g_ren.H);
  const float w=460*s,h=504*s,x=(W-w)*.5f,y=(H-h)*.5f;
  g_ren.rect(0,0,W,H,M_INK,.68f);surface(x,y,w,h,s);
  label(x+28*s,y+24*s,w-56*s,s,"MILITARY SERVICE / ON HOLD");
  g_ren.text(x+28*s,y+52*s,30*s,"Sortie paused",M_TEXT,1,0,false);
  g_ren.text(x+28*s,y+94*s,12.5f*s,fit(contract.title,w-56*s,12.5f*s),M_DIM,1,0,false);
  float by=y+132*s;const float bx=x+28*s,bw=w-56*s,bh=44*s,step=56*s;
  if(militaryButton(0x6D3001u,bx,by,bw,bh,"Resume sortie",true,true)) { paused=false;return; } by+=step;
  if(militaryButton(0x6D3002u,bx,by,bw,bh,"Restart sortie",!commitBlocked())) { restartFlight();return; } by+=step;
  if(militaryButton(0x6D3003u,bx,by,(bw-10*s)*.5f,bh,"Settings")) { settingsFromPause=true;settingsPage=0; }
  if(militaryButton(0x6D3004u,bx+(bw+10*s)*.5f,by,(bw-10*s)*.5f,bh,"Controls")) { settingsFromPause=true;settingsPage=1; } by+=step;
  if(militaryButton(0x6D3005u,bx,by,(bw-10*s)*.5f,bh,showRadio?"Hide radio":"Radio")) showRadio=!showRadio;
  if(militaryButton(0x6D3006u,bx+(bw+10*s)*.5f,by,(bw-10*s)*.5f,bh,uiHidden?"Show flight UI":"Hide flight UI")) uiHidden=!uiHidden;
  by+=step;
  rule(bx,by+2*s,bw,s);by+=18*s;
  if(militaryButton(0x6D3007u,bx,by,bw,bh,"Abort sortie / debrief")) { endFlight(false,"Military sortie aborted",OUT_ABANDONED);return; }
  g_ren.text(bx,by+58*s,11*s,"Supplied equipment. Civilian finances are unaffected.",M_DIM,1,0,false);
  if(showRadio) drawRadioPanel(20*s,60*s);
}

void Game::drawMilitaryDebrief() {
  showRadio=false; // Flight radio can keep playing; its hidden panel cannot own input.
  // Controller B also synthesizes Enter globally; consume the back action first.
  if(in.pressed[K_ESC]) { in.pressed[K_ESC]=false;in.pressed[K_ENTER]=false;returnToMilitary();return; }
  const float s=militaryScale(S()),W=float(g_ren.W),H=float(g_ren.H);
  const float w=784*s,h=620*s,x=(W-w)*.5f,y=(H-h)*.5f,px=x+30*s,iw=w-60*s;
  g_ren.rect(0,0,W,H,M_INK,.76f);surface(x,y,w,h,s);
  label(px,y+24*s,iw,s,"MILITARY SERVICE / AFTER-ACTION REPORT");
  g_ren.text(px,y+54*s,29*s,lastSuccess?"Sortie complete":"Sortie ended",lastSuccess?M_ACCENT:M_WARN,1,0,false);
  g_ren.text(px,y+95*s,18*s,fit(contract.title,iw,18*s),M_TEXT,1,0,false);
  g_ren.text(px,y+123*s,12*s,fit(debriefTitle,iw,12*s),M_DIM,1,0,false);
  rule(px,y+154*s,iw,s);
  const float statY=y+179*s,statW=iw/4;
  auto metric=[&](int col,const char* title,const std::string& value) {
    const float xx=px+col*statW;
    g_ren.text(xx,statY,10.5f*s,title,M_DIM,1,0,false);
    g_ren.text(xx,statY+21*s,23*s,value,M_TEXT,1,0,false);
  };
  metric(0,"FLIGHT TIME",fmt("%d:%02d",int(flightClock)/60,int(flightClock)%60));
  metric(1,"AIRFRAME HULL",fmt("%.0f%%",militaryHull));
  metric(2,"HOSTILES DESTROYED",std::to_string(somnus.kills));
  metric(3,"MAXIMUM G",fmt("%.1f",plane.maxG));
  label(px,y+253*s,iw,s,"SERVICE SETTLEMENT");
  float py=y+285*s;int total=0;
  for(const auto& line:payout) {
    total+=line.amount;
    if(py>y+383*s) continue;
    g_ren.text(px,py,14*s,fit(line.label,iw-135*s,14*s),M_TEXT,1,0,false);
    g_ren.text(x+w-30*s,py,14*s,fmt("%+d SC",line.amount),line.amount>=0?M_ACCENT:M_BAD,1,2,false);
    py+=25*s;
  }
  rule(px,y+412*s,iw,s);
  g_ren.text(px,y+432*s,17*s,"Service credits earned",M_TEXT,1,0,false);
  g_ren.text(x+w-30*s,y+432*s,23*s,fmt("%+d SC",total),total>=0?M_ACCENT:M_BAD,1,2,false);
  g_ren.text(px,y+474*s,12*s,"Aircraft, fuel and stores funded by command. Civilian account unchanged.",M_DIM,1,0,false);
  if(commitBlocked()) {
    g_ren.text(px,y+501*s,12*s,fit("NOT SAVED: "+saveWhy,iw,12*s),M_WARN,1,0,false);
    if(militaryButton(0x6D2003u,px,y+h-70*s,215*s,44*s,"Retry save")) retryCommit();
  } else if(!lastSuccess) {
    if(militaryButton(0x6D2002u,px,y+h-70*s,215*s,44*s,"Try sortie again")) { retryFromDebrief();return; }
  } else {
    g_ren.text(px,y+501*s,12*s,fmt("Saved service record: rank %d / %d SC / %d successes",career.military.rank,career.military.credits,career.military.successes),M_ACCENT,1,0,false);
  }
  if(militaryButton(0x6D2001u,x+w-290*s,y+h-70*s,260*s,44*s,"Return to military service",true,true)) { returnToMilitary();return; }
}
