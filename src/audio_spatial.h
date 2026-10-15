// Analytic binaural stereo: metre-based attenuation, air absorption, head shadow,
// interaural delay and elevation/rear spectral cues. No external audio dependency.
// This is a compact head model, not a measured/personalized HRTF or surround output.
#pragma once
#include "common.h"

struct AudioListener {
  vec3 position, velocity;
  vec3 forward = vec3(0, 0, -1), right = vec3(1, 0, 0), up = vec3(0, 1, 0);
};

inline vec3 audioDisplacement(vec3 from, vec3 to, float period) {
  vec3 d = to - from;
  if (period > 0) { d.x -= std::round(d.x / period) * period; d.z -= std::round(d.z / period) * period; }
  return d;
}

struct AudioSpatialCue {
  float gain = 1, pan = 0, elevation = 0, rear = 0, cutoff = 18000, pitch = 1;
};

inline AudioSpatialCue audioSpatialCue(const AudioListener& listener, vec3 position, vec3 velocity,
                                       float radius, float range, float period = 0, float soundSpeed = 343) {
  AudioSpatialCue c;
  vec3 delta = audioDisplacement(listener.position, position, period);
  float distance = length(delta);
  vec3 direction = distance > .001f ? delta / distance : listener.forward;
  radius = std::max(radius, .1f); range = std::max(range, radius + 1);
  // Unity inside the emitter; spherical pressure falloff outside, then a smooth inaudibility fade.
  c.gain = radius / std::max(distance, radius) * (1 - smoothstepf(range * .75f, range, distance));
  c.pan = clampf(dot(direction, listener.right), -1, 1);
  c.elevation = clampf(dot(direction, listener.up), -1, 1);
  c.rear = smoothstepf(.1f, -.8f, dot(direction, listener.forward));
  c.cutoff = 18000.f / (1.f + distance * .0025f);
  // Source -> listener ray; clamp radial terms separately, avoiding singularities near Mach 1.
  float cs = clampf(soundSpeed, 280, 360);
  float vr = clampf(dot(listener.velocity, direction), -.7f * cs, .7f * cs);
  float vs = clampf(dot(velocity, direction), -.7f * cs, .7f * cs);
  c.pitch = clampf((cs + vr) / (cs + vs), .5f, 2.f);
  return c;
}

// Signed distance behind a supersonic aircraft's Mach cone. A crossing from
// negative to positive is the shock reaching this observer, not a Mach-1 toggle.
inline float audioMachCone(vec3 source, vec3 velocity, vec3 observer, float cs, float period = 0) {
  float speed = length(velocity), mach = speed / std::max(cs, 280.f);
  if (mach <= 1.01f) return -1e9f;
  vec3 d = audioDisplacement(source, observer, period), fwd = velocity / speed;
  float behind = -dot(d, fwd);
  float lateral = sqrtf(std::max(0.f, dot(d, d) - behind * behind));
  return behind - lateral * sqrtf(mach * mach - 1.f);
}

struct AudioBinaural {
  // Enough delay for a 0.65 ms interaural difference through 192 kHz.
  float delay[256] = {}, lp[2] = {}, pinna[2] = {};
  float gain = 0, targetGain = 0, pan = 0, targetPan = 0, itd = 0, targetItd = 0;
  float pole[2] = {1, 1}, notch = 0, notchPole = .5f, smooth = .003f;
  unsigned cursor = 0;
  void target(const AudioSpatialCue& cue, float level, float sr, float occlusion = 0) {
    targetGain = cue.gain * level;
    targetPan = cue.pan * .88f; // retain cross-ear energy rather than hard muting an ear
    targetItd = cue.pan * .00065f * sr;
    smooth = 1.f - expf(-1.f / (.012f * sr));
    for (int ear = 0; ear < 2; ++ear) {
      float shadow = std::max(0.f, ear == 0 ? cue.pan : -cue.pan);
      float cutoff = cue.cutoff / (1.f + 3.5f * shadow + .65f * cue.rear);
      cutoff = lerpf(cutoff, std::min(cutoff, 1100.f), occlusion);
      pole[ear] = 1.f - expf(-2 * PI * clampf(cutoff, 120, sr * .45f) / sr);
    }
    notch = .16f * cue.rear + .12f * fabsf(cue.elevation);
    notchPole = 1.f - expf(-2 * PI * (4200 + 1700 * cue.elevation) / sr);
  }
  void mix(float mono, float& L, float& R) {
    gain += (targetGain - gain) * smooth; pan += (targetPan - pan) * smooth;
    itd += (targetItd - itd) * smooth;
    delay[cursor & 255] = mono;
    float* out[2] = {&L, &R};
    for (int ear = 0; ear < 2; ++ear) {
      float lag = std::max(0.f, ear == 0 ? itd : -itd);
      int n = (int)lag; float frac = lag - n;
      float x = lerpf(delay[(cursor - n) & 255], delay[(cursor - n - 1) & 255], frac);
      lp[ear] += (x - lp[ear]) * pole[ear];
      pinna[ear] += (lp[ear] - pinna[ear]) * notchPole;
      x = lp[ear] - notch * (lp[ear] - pinna[ear]);
      float weight = sqrtf(std::max(0.f, .5f * (1 + (ear == 0 ? -pan : pan))));
      *out[ear] += x * gain * weight;
    }
    ++cursor;
  }
};
