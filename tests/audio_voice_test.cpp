#include "../src/atc.h"
#include "../src/audio.h"
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {
int failures=0,checks=0;
void check(bool ok,const char* name){++checks;if(!ok)++failures;printf("Voice: %s: %s\n",name,ok?"PASS":"FAIL");}
void render(float seconds){float pcm[960];for(int i=0;i<int(seconds*100);++i)g_audio.render(pcm,480);}
}
int main(int argc,char** argv){
 if(argc!=2)return 2;g_audio.init(48000);AtcVoice voice;
 check(voice.load(argv[1]),"voice index and supplemental cast load");
 int bad=0;for(const auto& id:voice.indexed())if(!voice.decodes(id)){++bad;printf("Missing/invalid: %s\n",id.c_str());}
 printf("Voice: %zu recordings decoded\n",voice.indexed().size());check(!bad,"every indexed recording decodes in the production player");
 const char* messages[]={
 "MACH 1 - SUPERSONIC","Combat runs at real time","Autoland: stopped past the end of the runway - parking brake set",
 "Autopilot: too much tailwind to stop on this runway - going around","Autopilot: the wind has shifted - unable to autoland at Meadowbrook - circling clear of the ground (it tries again as the wind changes)",
 "Autopilot: unable to autoland at a regional strip - circling clear of the ground (it tries again as the wind changes)","Autopilot: unable - no runway in reach",
 "Autopilot disconnected - engine failure","Autopilot: GLIDING to Regional Airport runway 12","Autopilot: AUTOLAND Regional Airport runway 08",
 "Engine running. Release the parking brake with Space.","ENGINE OFF - press custom binding to restart","VTOL: hold gamepad throttle; release to idle","Time x4",
 "Formation run complete - 12 s out of a steady platform. Land back at BKM.","TEST CARD X3 COMPLETE - HIGH SPEED SIGNED OFF",
 "TEST CARD X3 COMPLETE - SIGNED OFF THIS SESSION ONLY: THE RECORD COULD NOT BE SAVED","FLAP ASYMMETRY  left stuck at 30%: keep the lever there",
 "Below minimums and not lined up - GO AROUND","Gear won't come down - it's stuck up","Gear won't retract - it's stuck down",
 "Already airborne. Fly anywhere, then return from the pause menu.","Full tanks. Release the parking brake when ready.",
 "Destination region reached. Choose a local airport on the GPS map.","Northern Islands route set. Nearby regions prepare in the background as you fly.",
 "Route exceeds nominal fuel range. Choose a closer stop or a longer-range aircraft.","Entering Northern Islands.",
 "MILITARY LOANER / return to extraction after objectives","FIRST SCAN: Communications depot","PROTECT: Airport radar","Practice wave spawned (8 craft maximum)",
 "Intelligence site recorded","INCOMING FIRE - BREAK AWAY","PROTECTED SITE UNDER ATTACK","HOSTILE WAVE INBOUND","OBJECTIVES COMPLETE - RETURN TO EXTRACTION",
 "Military objectives secured","Military objective lost","Hull lost to Hive fire","Free flight complete","Practice flight complete","Practice flight over",
 "Research flight complete","Checkride not passed","Flight complete","Flight ended after impact","Flight ended","Autopilot: alternate runway selected",
 "Destination region reached. Local airports remain unavailable while detailed terrain prepares.","Plasma AWAY","Penetrator AWAY","EMP AWAY","Warehouse destroyed","WEAPONS OVERHEATED - COOLING","FORWARD AMMUNITION DEPLETED",
 "STALL","PULL UP","GEAR!","Gear down","Autopilot OFF","Engine start","CLOAK ENGAGED","WEAPONS HOT - lasers out, bomb bay open"
 };
 int unresolved=0;for(const char* msg:messages){AtcVoice::Tx tx;if(!voice.resolve(msg,"",false,tx)){++unresolved;printf("Unresolved: %s\n",msg);}}
 check(!unresolved,"operational, control-binding, warning, military and outcome speech resolves");
 std::ifstream index(std::string(argv[1])+"/voice_index_extra.txt");std::string line;int supplemental=0;
 while(std::getline(index,line)){if(line.empty()||line[0]=='#')continue;auto tab=line.find('\t');check(voice.has(line.substr(0,tab)),"supplemental cue remains indexed alongside legacy imports");++supplemental;}
 check(supplemental==54,"complete supplemental cast is packaged");
 AtcVoice::Tx routine,hazard;
 check(voice.resolve("Full tanks. Release the parking brake when ready.","",false,routine) && voice.resolve("INCOMING FIRE - BREAK AWAY","",false,hazard),"queue fixtures resolve");
 voice.say(routine);voice.update(1);render(.05f);size_t n=voice.history.size();
 voice.say(routine);voice.say(routine);voice.say(hazard);auto started=voice.update(.01f);
 check(started.text==hazard.text && voice.history.size()==n+1,"urgent threat interrupts routine speech and duplicates are suppressed");
 render(8);voice.update(1);check(voice.history.size()==n+1,"duplicate routine does not replay later");
 voice.cancel();voice.say(routine);voice.update(20);
 check(!voice.busy() && voice.dropped>0,"expired routine speech is dropped");
 voice.say(routine);voice.update(1);voice.cancel();render(.1f);check(!voice.busy(),"cancel silences speech and clears its queue");
 AtcVoice::Tx incomplete;incomplete.text="broken assembly";incomplete.ids={routine.ids[0],"missing.fragment"};incomplete.prio=70;
 n=voice.history.size();voice.say(incomplete);voice.update(1);
 check(!voice.busy() && voice.history.size()==n && voice.missingAudio>0,"missing fragments refuse a misleading partial instruction");
 printf("Voice: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
