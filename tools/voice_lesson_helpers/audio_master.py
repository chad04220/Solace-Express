import numpy as np
from scipy.signal import butter,sosfilt

def master(x, sr):
    x = np.asarray(x, dtype=np.float64).reshape(-1)
    assert len(x) > 0.15 * sr and np.isfinite(x).all()
    x -= x.mean()
    x = sosfilt(butter(2, 70, btype='highpass', fs=sr, output='sos'), x)
    x *= min(0.8 / max(float(np.max(np.abs(x))), 1e-08), 10 ** (-22 / 20) / max(float(np.sqrt(np.mean(x * x))), 1e-08))
    fade = min(int(sr * 0.008), len(x) // 10)
    x[:fade] *= np.linspace(0, 1, fade)
    x[-fade:] *= np.linspace(1, 0, fade)
    return np.pad(x, (int(sr * 0.055), int(sr * 0.12))).astype('float32')

def clean_fragment(raw, sr):
    raw = np.asarray(raw, dtype='float32').reshape(-1)
    hop = max(1, int(sr * 0.01))
    energy = np.array([np.sqrt(np.mean(raw[i:i + hop] ** 2)) for i in range(0, len(raw), hop)])
    active = np.flatnonzero(energy > max(0.0003, float(energy.max()) * 0.004))
    assert len(active), 'Silent fragment'
    return raw[max(0, int(active[0] * hop - sr * 0.03)):min(len(raw), int((active[-1] + 1) * hop + sr * 0.05))]

def master_kokoro(x, sr, fragment):
    x = np.asarray(x, dtype=np.float64).reshape(-1)
    assert np.isfinite(x).all()
    x -= x.mean()
    x = sosfilt(butter(2, 70, btype='highpass', fs=sr, output='sos'), x)
    x *= min(0.8 / max(float(np.max(np.abs(x))), 1e-08), 10 ** (-22 / 20) / max(float(np.sqrt(np.mean(x * x))), 1e-08))
    fade = min(int(sr * 0.008), len(x) // 10)
    x[:fade] *= np.linspace(0, 1, fade)
    x[-fade:] *= np.linspace(1, 0, fade)
    return np.pad(x, (int(sr * (0.015 if fragment else 0.055)), int(sr * (0.025 if fragment else 0.12)))).astype('float32')
