"""Pitch measurement (YIN), used to find instruments that sound an octave off (render.octave_offset)."""
import numpy as np

SR = 48000


def yin_f0(x, fmin=30, fmax=2500):
    x = x - x.mean()
    n = len(x)
    tmax = int(SR / fmin)
    tmin = int(SR / fmax)
    w = n - tmax
    if w < 512:
        return None
    d = np.array([np.sum((x[:w] - x[t:t + w]) ** 2) for t in range(tmin, tmax)])
    cmnd = d * np.arange(1, len(d) + 1) / np.maximum(np.cumsum(d), 1e-12)
    idx = np.where(cmnd < 0.15)[0]
    if len(idx) == 0:
        i = int(np.argmin(cmnd))
        if cmnd[i] > 0.4:
            return None
    else:
        i = idx[0]
        while i + 1 < len(cmnd) and cmnd[i + 1] < cmnd[i]:
            i += 1
    return SR / (i + tmin)
