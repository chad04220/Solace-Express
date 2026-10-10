#pragma once
// Test-adapter-only readback state. No production renderer declarations change.
struct EnvironmentReviewShadowState {
  unsigned gbuffer=0, shadow[2]={};
  int width=0, height=0, shadowResolution=0, valid=0;
  float matrices[32]={}, radii[2]={}, jitter[2]={};
  float centers[4]={};
  float committedRadius=0, fadeScale=1, filteredSpeed=0, agl=0, nearFade[2]={};
  int phase=-1, tier=-1, nearDirty=0, farDirty=0, radiusChanged=0;
  float camera[3]={}, basis[9]={}, sun[3]={}, tanHalf=0, aspect=0;
};
extern EnvironmentReviewShadowState g_environmentReviewShadowState;
