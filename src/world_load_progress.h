// UI observations only. They do not enter the world cache or generation inputs.
#pragma once
#include <atomic>
inline std::atomic<float> g_worldLoadProgress{0.f};
inline std::atomic<const char*> g_worldLoadTask{"Preparing islands"};
inline void worldLoadProgress(float value) {
  float old=g_worldLoadProgress.load(std::memory_order_relaxed);
  while(value>old&&!g_worldLoadProgress.compare_exchange_weak(old,value,std::memory_order_relaxed)){}
}
inline void worldLoadStage(float done,const char* next) {worldLoadProgress(done);g_worldLoadTask=next;}
