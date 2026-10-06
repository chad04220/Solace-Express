// Solace Express - the pre-rendered main-menu montage (Windows, Media Foundation)
//
// render_menu.bat runs SolaceExpress.exe --menuvideo, which renders the montage offline and encodes it with
// MenuVideoWriter (H.264 in an .mp4). When that file sits next to the exe, MenuVideoPlayer plays it on the main menu
// in place of the live montage: decoded on a background thread, uploaded to a texture each frame.
#pragma once
#include <cstdint>
#include <string>
#include <memory>

class MenuVideoWriter {
public:
  MenuVideoWriter();
  ~MenuVideoWriter();
  bool open(const std::string& path, int w, int h, int fps, int kbps);
  bool write(const uint8_t* rgbBottomUp);   // one frame: RGB8 rows bottom first (as glReadPixels returns them)
  bool finish();
  std::string error;
private:
  struct Impl; std::unique_ptr<Impl> d;
};

class MenuVideoPlayer {
public:
  MenuVideoPlayer();
  ~MenuVideoPlayer();
  bool open(const std::string& path);   // starts decoding in the background; false if the file is missing
  // the texture to show at this moment (0 while the first frame is still decoding or if the video failed); call
  // every frame while the menu is up: playback pauses in between and resumes where it was
  unsigned frame(float now);
  int width() const, height() const;
private:
  struct Impl; std::unique_ptr<Impl> d;
};
