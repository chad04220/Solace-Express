// AI propellers over the lit frame: at most two small, depth-tested screen rectangles per traffic aircraft.
#include "renderer.h"
#include "prop_disc.h"
#include "models.h"
#include "aircraft.h"

void Renderer::rasterTrafficProps(const FrameParams& fp) {
  static const bool off = getenv("TRAFFICPROPOFF") != nullptr;
  if (off || !progTrafficProps || fp.trafficN <= 0) return;
  struct Draw { TrafficPropDisc disc; PropDiscBounds bounds; float distance; } draws[kMaxTrafficDrawn*2];
  int count = 0;
  const float tanY = fp.pano > 0.f ? fp.panoTanY : tanf(fp.fovY*.5f), aspect = (float)W/H;
  const vec2 pad(2.f/rw + 2.f*fabsf(jitX), 2.f/rh + 2.f*fabsf(jitY));
  auto camera = [&](vec3 v) { return vec3(dot(v, fp.camRight), dot(v, fp.camUp), dot(v, fp.camBack)); };
  for (int k = 0; k < std::min(fp.trafficN, kMaxTrafficDrawn); ++k) {
    const float* t = fp.traffic[k].t;
    int blades = 2;
    // The row contains packed geometry, not a roster id. Resolve the matching spec once per craft, not per pixel.
    for (int m = 0; m < kNumAircraft; ++m) {
      if (fabsf(t[0] - kAircraft[m].fusLen) < .001f && fabsf(t[2] - kModels[m].engine) < .01f &&
          fabsf(t[9*4] - kModels[m].wing[0]) < .001f) { blades = kAircraft[m].blades; break; }
    }
    TrafficPropDisc discs[2]; const int n = trafficPropGeometry(t, fp.camPos, blades, discs);
    for (int i = 0; i < n; ++i) {
      Draw d; d.disc = discs[i]; d.distance = dot(d.disc.centre, d.disc.centre);
      d.disc.centre = camera(d.disc.centre); d.disc.right = camera(d.disc.right); d.disc.up = camera(d.disc.up);
      if (propDiscBounds(d.disc.centre, d.disc.radius, tanY, aspect, fp.pano, pad, d.bounds)) draws[count++] = d;
    }
  }
  if (!count) return;
  std::sort(draws, draws + count, [](const Draw& a, const Draw& b) { return a.distance > b.distance; });
  glBindFramebuffer(GL_FRAMEBUFFER, fboComp);
  GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0); glViewport(0, 0, rw, rh);
  glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  glUseProgram(progTrafficProps); glBindVertexArray(vaoEmpty);
  auto uniform = [&](const char* name) { return U(progTrafficProps, name); };
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texDepth); glUniform1i(uniform("uSceneDepth"), 0);
  glUniform2f(uniform("uRes"), (float)rw, (float)rh); glUniform2f(uniform("uJit"), jitX, jitY);
  glUniform2f(uniform("uPano"), fp.pano, fp.panoTanY); glUniform1f(uniform("uTanHalf"), tanY); glUniform1f(uniform("uAspect"), aspect);
  glUniform1f(uniform("uFogB"), fp.fogB);
  const vec3 light = fp.sunCol*std::max(fp.sunDir.y, 0.f) + vec3(.2f);
  glUniform3f(uniform("uPropLight"), light.x, light.y, light.z);
  // RGB blending and the TAA class have different meanings. A bounded alpha-only second draw labels visible
  // prop pixels as moving (.2), without reading from the framebuffer being written or corrupting blend opacity.
  for (int pass = 0; pass < 2; ++pass) {
    glUniform1i(uniform("uClassOnly"), pass);
    glColorMask(pass == 0, pass == 0, pass == 0, pass == 1);
    if (!pass) { glEnable(GL_BLEND); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE); }
    else glDisable(GL_BLEND);
    for (int i = 0; i < count; ++i) {
      const auto& d = draws[i].disc; const auto& b = draws[i].bounds;
      glUniform4f(uniform("uDiscBounds"), b.x0, b.y0, b.x1, b.y1);
      glUniform3f(uniform("uDiscCentre"), d.centre.x, d.centre.y, d.centre.z);
      glUniform3f(uniform("uDiscRight"), d.right.x, d.right.y, d.right.z);
      glUniform3f(uniform("uDiscUp"), d.up.x, d.up.y, d.up.z);
      glUniform4f(uniform("uDisc"), d.radius, d.angle, d.blur, (float)d.blades);
      glDrawArrays(GL_TRIANGLES, 0, 6);
    }
  }
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDisable(GL_BLEND);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
