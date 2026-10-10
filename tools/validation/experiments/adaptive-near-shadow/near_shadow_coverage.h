// DISABLED EXPERIMENT: not part of the production renderer or CMake targets.
// See README.md for the rejected coverage tradeoff and standalone-only use.
// Solace Express - bounded, CPU-testable main-camera near-shadow coverage.
#pragma once
#include "common.h"

// Only the near cascade changes footprint; its allocation and the far cascade's
// policy stay unchanged. The committed radius is constant during each fade leg.
class NearShadowCoverage {
public:
  enum class Phase { Stable, Shrink, Expand };
  static constexpr float kFadeFloor = 0.0001f;
  static constexpr float kShrinkSeconds = 0.2f, kExpandSeconds = 0.3f;
  static constexpr float kUpgradeSeconds = 0.25f, kDowngradeSeconds = 1.5f;
  static constexpr float kSpeedSeconds = 0.35f;

  void reset() { *this = NearShadowCoverage{}; }
  float radius() const { return radius_; }
  float speed() const { return speed_; }
  float agl() const { return agl_; }
  float fadeScale() const { return fade_; }
  Phase phase() const { return phase_; }
  int tier() const { return tier_; }

  // Pass the map's ACTUAL radius, not a requested tier. Positive, strictly
  // ordered radii avoid smoothstep(0,0) while the far cascade takes over.
  // A positive actual radius is required for an enabled map. Before allocation
  // actualRadius is zero and the renderer disables shadow sampling entirely.
  vec2 fadeRadii(float actualRadius) const {
    const float r = std::max(actualRadius, 0.f) * fade_;
    return {r * 0.5f, r * 0.78f};
  }

  // The sampler is deliberately lazy: feeds and hangars neither read terrain
  // nor mutate camera history, dwell, radius or handover state.
  template<class GroundHeight>
  bool update(vec3 camera, float dt, float qualityBase, bool mainView, bool hangar, GroundHeight&& groundHeight) {
    if (!mainView || hangar) return false;
    const float base = std::isfinite(qualityBase) && qualityBase >= 256.f ? qualityBase : 420.f;
    const bool cameraOK = finite(camera);
    const bool timeOK = std::isfinite(dt) && dt > 0.f && dt <= 0.25f;
    if (!initialized_ || base != base_ || !cameraOK || !timeOK)
      return conservative(base, camera, cameraOK && timeOK);
    const float travel = length(camera - previous_);
    // Cuts, teleports and suspended rendering must not become sustained camera
    // speed. Ordinary flight up to 1200 m/s is supported at every test rate.
    if (!hasCamera_ || !std::isfinite(travel) || travel > std::max(64.f, 1200.f * dt))
      return conservative(base, camera, true);
    const float instantSpeed = travel / dt;
    if (!std::isfinite(instantSpeed)) return conservative(base, camera, true);
    const float ground = groundHeight(camera.x, camera.z);
    if (!std::isfinite(ground)) return conservative(base, camera, true);
    previous_ = camera;
    speed_ += (instantSpeed - speed_) * (-std::expm1(-dt / kSpeedSeconds));
    agl_ = std::max(camera.y - std::max(ground, 0.f), 0.f);

    if (phase_ != Phase::Stable) {
      phaseTime_ += dt;
      if (phase_ == Phase::Shrink) {
        fade_ = std::max(kFadeFloor, 1.f - smoothstepf(0.f, kShrinkSeconds, phaseTime_));
        if (phaseTime_ + 1e-6f < kShrinkSeconds) return false;
        // A sudden climb/acceleration during the fade may cancel a narrowing
        // request or promote it. Never commit a now-unsafe smaller footprint.
        target_ = std::max(target_, risingTier());
        const float next = radiusFor(target_);
        const bool changed = next != radius_;
        tier_ = target_; radius_ = next; phase_ = Phase::Expand;
        phaseTime_ = 0.f; fade_ = kFadeFloor;
        return changed;  // exactly one map-radius change, hidden by the far map
      }
      fade_ = std::max(kFadeFloor, smoothstepf(0.f, kExpandSeconds, phaseTime_));
      if (phaseTime_ + 1e-6f >= kExpandSeconds) {
        fade_ = 1.f; phase_ = Phase::Stable; phaseTime_ = 0.f;
        candidate_ = tier_; dwell_ = 0.f;
      }
      return false;
    }

    int desired = std::max(tier_, risingTier());
    if (desired == tier_) {
      static constexpr float downAGL[3] = {25.f, 60.f, 125.f};
      static constexpr float downSpeed[3] = {12.f, 25.f, 40.f};
      while (desired > 0 && agl_ < downAGL[desired - 1] && speed_ < downSpeed[desired - 1]) --desired;
    }
    if (desired == tier_) { candidate_ = tier_; dwell_ = 0.f; return false; }
    if (candidate_ != desired) { candidate_ = desired; dwell_ = 0.f; }
    dwell_ += dt;
    const float wait = desired > tier_ ? kUpgradeSeconds : kDowngradeSeconds;
    if (dwell_ + 1e-6f >= wait) {
      target_ = desired; phase_ = Phase::Shrink; phaseTime_ = 0.f; dwell_ = 0.f;
    }
    return false;
  }

private:
  static bool finite(vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
  float radiusFor(int tier) const { return tier == 0 ? 96.f : tier == 1 ? 160.f : tier == 2 ? 256.f : base_; }
  int risingTier() const {
    static constexpr float upAGL[3] = {35.f, 80.f, 160.f};
    static constexpr float upSpeed[3] = {18.f, 32.f, 50.f};
    int t = 0;
    while (t < 3 && (agl_ > upAGL[t] || speed_ > upSpeed[t])) ++t;
    return t;
  }
  bool conservative(float base, vec3 camera, bool remember) {
    const bool changed = radius_ != base;
    base_ = radius_ = base; tier_ = target_ = candidate_ = 3;
    initialized_ = true; hasCamera_ = remember; previous_ = remember ? camera : vec3{};
    speed_ = agl_ = dwell_ = phaseTime_ = 0.f; fade_ = 1.f; phase_ = Phase::Stable;
    return changed;
  }
  bool initialized_ = false, hasCamera_ = false;
  vec3 previous_;
  float base_ = 0.f, radius_ = 0.f, speed_ = 0.f, agl_ = 0.f;
  float dwell_ = 0.f, phaseTime_ = 0.f, fade_ = 1.f;
  int tier_ = 3, target_ = 3, candidate_ = 3;
  Phase phase_ = Phase::Stable;
};
