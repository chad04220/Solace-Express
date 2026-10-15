// Streaming stereo resampling for packet-based audio decoders. No platform dependencies.
#pragma once
#include "common.h"
#include <array>
#include <numeric>

// Precompute phase filters once per rate pair. In particular, 16 -> 48 kHz
// speech needs only three phases, avoiding millions of trig calls on the game thread.
struct AudioSincKernel {
  int rate=0,target=0,phases=0;
  std::vector<std::array<float,32>> coefficients;
  void prepare(int from,int to) {
    if(rate==from && target==to)return;
    rate=from;target=to;phases=std::min(1024,to/std::gcd(from,to));
    coefficients.resize(phases);
    const double cutoff=std::min(1.,double(to)/from)*.94;
    for(int phase=0;phase<phases;++phase) {
      double fraction=double(phase)/phases;
      for(int j=-15;j<=16;++j) {
        double x=fraction-j,a=PI*x*cutoff;
        coefficients[phase][j+15]=fabs(x)>=16?0.f:float((fabs(a)<1e-9?1:sin(a)/a)*(.5+.5*cos(PI*x/16)));
      }
    }
  }
  const std::array<float,32>& at(double fraction,int& center) const {
    int phase=std::max(0,int(fraction*phases+.5));
    if(phase>=phases) {++center;phase=0;}
    return coefficients[phase];
  }
};

struct AudioStreamResampler {
  std::vector<float> input=std::vector<float>(32,0);double position=16;
  AudioSincKernel kernel;
  void append(const float* samples,int frames,int channels) {
    for(int i=0;i<frames;++i) {
      float l=samples[i*channels],r=channels>1?samples[i*channels+1]:l;
      if(channels>2){float center=samples[i*channels+2]*.5f;l+=center;r+=center;}
      input.push_back(l);input.push_back(r);
    }
  }
  std::vector<float> take(int rate,int target) {
    std::vector<float> output;output.reserve(4096);double step=double(rate)/target;kernel.prepare(rate,target);
    while(position+16<input.size()/2 && output.size()<4096) {
      int center=int(position);double sum[2]={},weight=0;const auto& coefficients=kernel.at(position-center,center);
      for(int j=-15;j<=16;++j) {
        int at=center+j;if(at<0 || at>=int(input.size()/2))continue;double w=coefficients[j+15];weight+=w;
        sum[0]+=input[at*2]*w;sum[1]+=input[at*2+1]*w;
      }
      output.push_back(float(sum[0]/weight));output.push_back(float(sum[1]/weight));position+=step;
    }
    int discard=std::clamp(int(position)-16,0,std::max(0,int(input.size()/2)-16));
    if(discard){input.erase(input.begin(),input.begin()+discard*2);position-=discard;}
    return output;
  }
};
