#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/enemy_bake_guard.h"
#include <cassert>
#include <functional>
#include <cstdio>
int main(){
 enemyBake::State s;int bakes=0,nested=0;
 std::function<void()> warm=[&]{enemyBake::Lease w(s,true);if(!w){nested++;return;}for(int k=0;k<4;k++){enemyBake::Lease b(s,false);assert(b);bakes++;warm();enemyBake::Lease second(s,false);assert(!second);}};
 warm();assert(bakes==4&&nested==4&&!s.warming&&!s.baking);
 try{enemyBake::Lease b(s,false);assert(b);throw 1;}catch(int){}assert(!s.baking);
 {enemyBake::Lease b(s,false);assert(b);enemyBake::Lease w(s,true);assert(!w);}
 assert(!s.warming&&!s.baking);puts("PASS warmup and per-type bake reentrancy guards, nested yield and exception cleanup");
}
