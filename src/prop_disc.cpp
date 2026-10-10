// Traffic and detached propellers over the lit frame: at most two small, depth-tested rectangles per aircraft.
#include "renderer.h"
#include "prop_disc.h"
#include "models.h"
#include "aircraft.h"

void Renderer::rasterTrafficProps(const FrameParams& fp) {
  static const bool off = getenv("TRAFFICPROPOFF") != nullptr;
  // The existing wreck hull path is perspective-only (wreckMesh); do not leave
  // floating prop discs in a panoramic feed where that hull is not drawn.
  const bool wreckProps = fp.plane.on && fp.wreck.pieces > 0 && fp.plane.propCount > 0 && fp.pano <= 0.f;
  if (!progTrafficProps || ((off || fp.trafficN <= 0) && !wreckProps)) return;
  struct Draw { TrafficPropDisc disc; PropDiscBounds bounds; float distance; } draws[(kMaxTrafficDrawn + 1)*2];
  int count = 0;
  const float tanY = fp.pano > 0.f ? fp.panoTanY : tanf(fp.fovY*.5f), aspect = (float)W/H;
  const vec2 pad(2.f/rw + 2.f*fabsf(jitX), 2.f/rh + 2.f*fabsf(jitY));
  auto camera = [&](vec3 v) { return vec3(dot(v, fp.camRight), dot(v, fp.camUp), dot(v, fp.camBack)); };
  auto append = [&](const TrafficPropDisc& disc) {
    Draw d; d.disc = disc; d.distance = dot(d.disc.centre, d.disc.centre);
    d.disc.centre = camera(d.disc.centre); d.disc.right = camera(d.disc.right); d.disc.up = camera(d.disc.up);
    if (propDiscBounds(d.disc.centre, d.disc.radius, tanY, aspect, fp.pano, pad, d.bounds)) draws[count++] = d;
  };
  for (int k = 0; !off && k < std::min(fp.trafficN, kMaxTrafficDrawn); ++k) {
    const float* t = fp.traffic[k].t;
    int blades = 2;
    // The row contains packed geometry, not a roster id. Resolve the matching spec once per craft, not per pixel.
    for (int m = 0; m < kAircraftCount; ++m) {
      if (fabsf(t[0] - kAircraft[m].fusLen) < .001f && fabsf(t[2] - kModels[m].engine) < .01f &&
          fabsf(t[9*4] - kModels[m].wing[0]) < .001f) { blades = kAircraft[m].blades; break; }
    }
    TrafficPropDisc discs[2]; const int n = trafficPropGeometry(t, fp.camPos, blades, discs);
    for (int i = 0; i < n; ++i) append(discs[i]);
  }
  // The intact player prop is already drawn by effects_fs. A wreck instead uses
  // its real spinner owner's transform, so blades neither vanish nor stay at the
  // original fuselage. The shared breakup body supplies its tumble and drag.
  if (wreckProps) for (int i = 0; i < std::min(fp.plane.propCount, 2); ++i) {
    const int owner = propWreckOwner(fp.plane.prop[i], fp.wreck.pieces, fp.wreck.C, fp.wreck.H);
    if (owner >= 0) append(wreckPropGeometry(fp.plane.prop[i], fp.wreck.rot[owner], fp.wreck.pos[owner],
                                            fp.camPos, fp.plane.Pr[0], (int)fp.plane.Pr[2], fp.plane.M[17*4 + 1]));
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
      glUniform1f(uniform("uPropHub"), d.hubRadius/d.radius);
      glDrawArrays(GL_TRIANGLES, 0, 6);
    }
  }
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDisable(GL_BLEND);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
