#pragma once
namespace enemyBake {
struct State { bool warming=false,baking=false; };
class Lease {
 bool* flag=nullptr;
public:
 Lease(State& s,bool warm) {
  if(warm){if(!s.warming&&!s.baking){flag=&s.warming;*flag=true;}}
  else if(!s.baking){flag=&s.baking;*flag=true;}
 }
 ~Lease(){if(flag)*flag=false;}
 Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
 explicit operator bool()const{return flag!=nullptr;}
};
}
