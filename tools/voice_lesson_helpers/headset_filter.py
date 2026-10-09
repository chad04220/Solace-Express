"""Aviation headset/intercom treatment, applied without altering spoken timing.

This is a game audio design preset, not a measurement of a particular headset.
Fragments omit per-clip squelch; an assembled radio message may add it once.
"""
import hashlib
import numpy as np
from scipy.signal import butter, sosfilt, lfilter

VERSION=1
PROFILES={
 'radio': {'highpass_hz':300,'lowpass_hz':3400,'presence_db':1.5,'description':'External VHF-style radio into a pilot headset'},
 'intercom': {'highpass_hz':220,'lowpass_hz':4500,'presence_db':1.0,'description':'Nearby instructor/examiner over cockpit intercom'},
 'cockpit': {'highpass_hz':240,'lowpass_hz':4000,'presence_db':1.2,'description':'Aircraft computer routed to the pilot headset'},
}

def presence_coeff(sr,gain_db):
 # Peaking EQ: modest intelligibility lift at 1.45 kHz; no tinny treble boost.
 f=1450.;q=.85;a=10**(gain_db/40);w=2*np.pi*f/sr;alpha=np.sin(w)/(2*q);cw=np.cos(w)
 b=np.array([1+alpha*a,-2*cw,1-alpha*a]);d=np.array([1+alpha/a,-2*cw,1-alpha/a])
 return b/d[0],d/d[0]

def process(samples,sr,channel,key='',fragment=False):
 x=np.asarray(samples,dtype=np.float64).reshape(-1).copy()
 if not len(x) or not np.isfinite(x).all():raise ValueError('Invalid headset input')
 p=PROFILES[channel]
 x=sosfilt(butter(2,p['highpass_hz'],btype='highpass',fs=sr,output='sos'),x)
 x=sosfilt(butter(3,p['lowpass_hz'],btype='lowpass',fs=sr,output='sos'),x)
 b,a=presence_coeff(sr,p['presence_db']);x=lfilter(b,a,x)
 # A light 2:1 RMS compressor evens microphone level without flattening inflection.
 alpha=np.exp(-1/(sr*.008));env=np.sqrt(np.maximum(0,lfilter([1-alpha],[1,-alpha],x*x)))
 level=20*np.log10(np.maximum(env,1e-8));gain_db=-.5*np.maximum(level+22,0)
 smooth=np.exp(-1/(sr*.028));gain=lfilter([1-smooth],[1,-smooth],10**(gain_db/20),zi=[smooth])[0]
 x*=gain
 # Very slight rounded microphone saturation; never hard clipping.
 x=np.tanh(x*1.18)/1.18
 active=np.abs(x)>.003
 if not active.any():raise ValueError('Silent headset output')
 rms=np.sqrt(np.mean(x[active]**2));peak=np.max(np.abs(x))
 x*=min(10**(-20/20)/max(rms,1e-8),.72/max(peak,1e-8))
 if channel=='radio' and not fragment:
  seed=int(hashlib.sha256(key.encode()).hexdigest()[:8],16)
  rng=np.random.default_rng(seed)
  noise=sosfilt(butter(2,[600,3000],btype='bandpass',fs=sr,output='sos'),rng.normal(0,1,len(x)))
  first=int(sr*.005);n=min(int(sr*.035),len(x)//8)
  # Squelch is a quiet boundary cue in the existing silence, never over the first word.
  for start in [first,len(x)-n-int(sr*.015)]:
   if start>=0:
    burst=noise[start:start+n];window=np.sin(np.linspace(0,np.pi,n))**2
    x[start:start+n]+=burst*window*.012
  # A very faint receiver noise floor only while the microphone gate is open.
  gate=(env>.009).astype(float)
  x+=noise*gate*.0009
 edge=min(int(sr*.006),len(x)//8)
 x[:edge]*=np.linspace(0,1,edge);x[-edge:]*=np.linspace(1,0,edge)
 peak=np.max(np.abs(x))
 if peak>.79:x*=.79/peak
 return x.astype(np.float32)
