// Air Xpress - internet radio streaming (Windows Media Foundation on Windows)
#pragma once
#include <string>

class Radio {
public:
  enum State { IDLE = 0, CONNECTING, PLAYING, FAILED };
  bool init();
  void play(const std::string& url);
  void stop();
  void setVolume(float v);  // 0..1
  void poll();
  State state() const { return st; }
  const std::string& status() const { return msg; }
  void shutdown();
private:
  State st = IDLE;
  std::string msg = "Radio off";
  void* impl = nullptr;
  float vol = 0.6f;
};
