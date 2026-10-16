// Streaming stereo resampling for packet-based audio decoders. No platform dependencies.
#pragma once
#include "common.h"
#include <numeric>

// Precompute phase filters once per rate pair. In particular, 16 -> 48 kHz
// speech needs only three phases, avoiding millions of trig calls on the game thread.
struct AudioSincKernel {
  int rate=0,target=0,phases=0,radius=16;
  std::vector<std::vector<float>> coefficients;
  void prepare(int from,int to) {
    if(rate==from && target==to)return;
    rate=from;target=to;phases=std::min(1024,to/std::gcd(from,to));
    // Keep the same number of lobes at the lower Nyquist frequency. A fixed
    // 32-tap window aliases high-rate decoders into audible tones when reduced.
    radius=int(std::ceil(16. / std::min(1.,double(to)/from)));
    coefficients.assign(phases,std::vector<float>(radius*2));
    const double cutoff=std::min(1.,double(to)/from)*.94;
    for(int phase=0;phase<phases;++phase) {
      double fraction=double(phase)/phases;
      for(int j=1-radius;j<=radius;++j) {
        double x=fraction-j,a=PI*x*cutoff;
        coefficients[phase][j+radius-1]=fabs(x)>=radius?0.f:float((fabs(a)<1e-9?1:sin(a)/a)*(.5+.5*cos(PI*x/radius)));
      }
    }
  }
  const std::vector<float>& at(double fraction,int& center) const {
    int phase=std::max(0,int(fraction*phases+.5));
    if(phase>=phases) {++center;phase=0;}
    return coefficients[phase];
  }
};

struct AudioStreamResampler {
  std::vector<float> input=std::vector<float>(32,0);double position=16;
  AudioSincKernel kernel;
  void append(const float* samples,int frames,int channels) {
    if(!samples || frames<=0 || channels<1 || channels>2)return;
    for(int i=0;i<frames;++i) {
      float l=samples[i*channels],r=channels>1?samples[i*channels+1]:l;
      input.push_back(l);input.push_back(r);
    }
  }
  std::vector<float> take(int rate,int target) {
    std::vector<float> output;output.reserve(4096);double step=double(rate)/target;kernel.prepare(rate,target);
    while(position+kernel.radius<input.size()/2 && output.size()<4096) {
      int center=int(position);double sum[2]={},weight=0;const auto& coefficients=kernel.at(position-center,center);
      for(int j=1-kernel.radius;j<=kernel.radius;++j) {
        int at=center+j;if(at<0 || at>=int(input.size()/2))continue;double w=coefficients[j+kernel.radius-1];weight+=w;
        sum[0]+=input[at*2]*w;sum[1]+=input[at*2+1]*w;
      }
      output.push_back(float(sum[0]/weight));output.push_back(float(sum[1]/weight));position+=step;
    }
    int discard=std::clamp(int(position)-kernel.radius,0,std::max(0,int(input.size()/2)-kernel.radius));
    if(discard){input.erase(input.begin(),input.begin()+discard*2);position-=discard;}
    return output;
  }
};
