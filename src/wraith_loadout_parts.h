// Rigid loadout selection shared by pose generation and clearance tests.
#pragma once
#include "aircraft_mesh_build_types.h"
namespace wraithLoadout {
inline bool isPayload(int type) {
 using namespace aircraftBuild;
 return type==PT_WR_BOMB || type==PT_WR_PENETRATOR || type==PT_WR_EMP;
}
inline bool selected(int type,int forward,int bomb,bool loaded=true) {
 using namespace aircraftBuild;
 forward=std::max(0,std::min(2,forward));bomb=std::max(0,std::min(2,bomb));
 if(type==PT_WR_TURRET || type==PT_WR_MUZZLE)return forward==0;
 if(type==PT_WR_KINETIC)return forward==1;
 if(type==PT_WR_CHARGED)return forward==2;
 if(type==PT_WR_BOMB)return bomb==0&&loaded;
 if(type==PT_WR_PENETRATOR)return bomb==1&&loaded;
 if(type==PT_WR_EMP)return bomb==2&&loaded;
 return true;
}
}
