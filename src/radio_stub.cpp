// Radio stub for non-Windows builds (development harness)
#include "radio.h"
bool Radio::init() { return true; }
void Radio::play(const std::string& url) { st = FAILED; msg = "Radio not supported on this platform: " + url; }
void Radio::stop() { st = IDLE; msg = "Radio off"; }
void Radio::setVolume(float v) { vol = v; }
void Radio::poll() {}
void Radio::shutdown() {}
