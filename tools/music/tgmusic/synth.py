"""Analog-style subtractive synthesizer and drum synthesis (numba-compiled).

Band-limited oscillators (polyBLEP saw / pulse), supersaw unison, zero-delay-feedback
state-variable filter (2- or 4-pole), ADSR amp + filter envelopes, drive, vibrato.
Everything renders at SR (48 kHz) into float32 numpy arrays.
"""
import math

import numpy as np
from numba import njit

SR = 48000


@njit(cache=True)
def _polyblep(t, dt):
    if t < dt:
        t /= dt
        return t + t - t * t - 1.0
    if t > 1.0 - dt:
        t = (t - 1.0) / dt
        return t * t + t + t + 1.0
    return 0.0


@njit(cache=True)
def _env(n, sr, a, d, s, r, gate_n):
    out = np.zeros(n, np.float32)
    level = 0.0
    a_inc = 1.0 / max(1.0, a * sr)
    d_coef = math.exp(-1.0 / max(1.0, d * sr / 4.0))
    r_coef = math.exp(-1.0 / max(1.0, r * sr / 4.0))
    stage = 0
    for i in range(n):
        if i >= gate_n:
            stage = 3
        if stage == 0:
            level += a_inc
            if level >= 1.0:
                level = 1.0
                stage = 1
        elif stage == 1:
            level = s + (level - s) * d_coef
        else:
            level *= r_coef
        out[i] = level
    return out


@njit(cache=True)
def _osc_bank(freq, sr, wave, n_uni, detune, pw, seed, spread):
    """wave: 0 saw, 1 pulse, 2 triangle, 3 sine. Returns stereo (n,2)."""
    n = freq.shape[0]
    out = np.zeros((n, 2), np.float32)
    np.random.seed(seed)
    for u in range(n_uni):
        if n_uni > 1:
            off = (u / (n_uni - 1)) * 2.0 - 1.0
        else:
            off = 0.0
        ratio = 2.0 ** (off * detune / 12.0)
        phase = np.random.random()
        pan = 0.5 + 0.5 * off * spread
        gl = math.cos(pan * math.pi / 2)
        gr = math.sin(pan * math.pi / 2)
        amp = 1.0 / math.sqrt(n_uni)
        tri = 0.0
        for i in range(n):
            dt = freq[i] * ratio / sr
            if dt > 0.45:
                dt = 0.45
            if wave == 0:
                v = 2.0 * phase - 1.0 - _polyblep(phase, dt)
            elif wave == 1:
                v = (1.0 if phase < pw else -1.0)
                v += _polyblep(phase, dt)
                p2 = phase - pw
                if p2 < 0:
                    p2 += 1.0
                v -= _polyblep(p2, dt)
            elif wave == 2:
                sq = (1.0 if phase < 0.5 else -1.0) + _polyblep(phase, dt)
                p2 = phase + 0.5
                if p2 >= 1.0:
                    p2 -= 1.0
                sq -= _polyblep(p2, dt)
                tri = dt * 4.0 * sq + (1.0 - dt * 0.5) * tri
                v = tri
            else:
                v = math.sin(2 * math.pi * phase)
            phase += dt
            if phase >= 1.0:
                phase -= 1.0
            out[i, 0] += v * amp * gl * 1.414
            out[i, 1] += v * amp * gr * 1.414
    return out


@njit(cache=True)
def _svf(x, cutoff, res, sr, poles, mode):
    """ZDF SVF; cutoff per sample. mode 0 LP, 1 BP, 2 HP. x (n,2)."""
    n = x.shape[0]
    y = np.empty_like(x)
    for c in range(2):
        ic1 = 0.0
        ic2 = 0.0
        ic3 = 0.0
        ic4 = 0.0
        k = 2.0 - 2.0 * res
        for i in range(n):
            fc = cutoff[i]
            if fc > sr * 0.45:
                fc = sr * 0.45
            if fc < 20.0:
                fc = 20.0
            g = math.tan(math.pi * fc / sr)
            a1 = 1.0 / (1.0 + g * (g + k))
            a2 = g * a1
            a3 = g * a2
            v0 = x[i, c]
            v3 = v0 - ic2
            v1 = a1 * ic1 + a2 * v3
            v2 = ic2 + a2 * ic1 + a3 * v3
            ic1 = 2 * v1 - ic1
            ic2 = 2 * v2 - ic2
            if mode == 0:
                o = v2
            elif mode == 1:
                o = v1
            else:
                o = v0 - k * v1 - v2
            if poles == 4:
                v3b = o - ic4
                v1b = a1 * ic3 + a2 * v3b
                v2b = ic4 + a2 * ic3 + a3 * v3b
                ic3 = 2 * v1b - ic3
                ic4 = 2 * v2b - ic4
                if mode == 0:
                    o = v2b
                elif mode == 1:
                    o = v1b
                else:
                    o = o - k * v1b - v2b
            y[i, c] = o
    return y


def midi_hz(p):
    return 440.0 * 2.0 ** ((np.asarray(p, dtype=np.float64) - 69.0) / 12.0)


DEFAULT = dict(
    layers=[dict(wave=0, uni=1, detune=0.0, pw=0.5, level=1.0, octave=0, spread=0.0)],
    noise=0.0, sub=0.0,
    a=0.002, d=0.2, s=0.8, r=0.08,
    cutoff=4000.0, res=0.1, poles=4, fenv=0.0, fa=0.001, fd=0.25, fs=0.0, fr=0.1,
    keytrack=0.5, vel_cut=0.3, drive=0.0, vib_rate=5.5, vib_depth=0.0, vib_delay=0.25,
    glide=0.0, gain=0.5, mode=0,
)


def render_note(p, pitch, dur, vel=1.0, bends=None, seed=0, prev_pitch=None):
    """Render one synth note. Returns stereo float32 (n, 2)."""
    P = dict(DEFAULT)
    P.update(p)
    sr = SR
    tail = P["r"] * 4 + 0.01
    n = int((dur + tail) * sr)
    gate_n = int(dur * sr)
    t = np.arange(n) / sr
    semis = np.full(n, float(pitch))
    if bends:
        bt = np.array([0.0] + [b[0] for b in bends])
        bv = np.array([0.0] + [b[1] for b in bends])
        semis += np.interp(t, bt, bv, right=bv[-1])
    if P["glide"] > 0 and prev_pitch is not None:
        semis += (prev_pitch - pitch) * np.exp(-t / P["glide"])
    if P["vib_depth"] > 0:
        ramp = np.clip((t - P["vib_delay"]) / 0.3, 0, 1)
        semis += P["vib_depth"] * ramp * np.sin(2 * np.pi * P["vib_rate"] * t)
    out = np.zeros((n, 2), np.float32)
    for i, L in enumerate(P["layers"]):
        f = midi_hz(semis + 12 * L.get("octave", 0) + L.get("fine", 0.0) / 100.0)
        out += L.get("level", 1.0) * _osc_bank(
            f, sr, L.get("wave", 0), L.get("uni", 1), L.get("detune", 0.0),
            L.get("pw", 0.5), (seed * 31 + i) % 100000, L.get("spread", 0.0))
    if P["sub"] > 0:
        out += P["sub"] * _osc_bank(midi_hz(semis - 12), sr, 3, 1, 0.0, 0.5, seed, 0.0)
    if P["noise"] > 0:
        rng = np.random.default_rng(seed)
        out += P["noise"] * rng.standard_normal((n, 2)).astype(np.float32) * 0.3
    fenv = _env(n, sr, P["fa"], P["fd"], P["fs"], P["fr"], gate_n)
    key = (pitch - 60) / 12.0
    cutoff = P["cutoff"] * (2.0 ** (key * P["keytrack"])) * (1 + P["vel_cut"] * (vel - 0.7))
    cut = cutoff * (2.0 ** (P["fenv"] * fenv))
    out = _svf(out.astype(np.float32), cut.astype(np.float64), P["res"], sr, P["poles"], P["mode"])
    if P["drive"] > 0:
        dg = 1 + P["drive"] * 8
        out = np.tanh(out * dg) / math.tanh(dg)
    amp = _env(n, sr, P["a"], P["d"], P["s"], P["r"], gate_n)
    out *= (amp * vel * P["gain"])[:, None]
    return out.astype(np.float32)


# ---------------------------------------------------------------- drums
def _exp(t, tau):
    return np.exp(-t / tau)


def drum(kind, vel=1.0, seed=0, flavor="modern", pitch=0.0):
    sr = SR
    rng = np.random.default_rng(seed)
    if kind == "kick":
        n = int(0.6 * sr)
        t = np.arange(n) / sr
        if flavor == "808":
            f = 48 + 120 * _exp(t, 0.03)
            body = np.sin(2 * np.pi * np.cumsum(f) / sr) * _exp(t, 0.45)
        else:
            f = 52 + 190 * _exp(t, 0.022) + 60 * _exp(t, 0.004)
            body = np.sin(2 * np.pi * np.cumsum(f) / sr) * _exp(t, 0.22)
        click = rng.standard_normal(n) * _exp(t, 0.0025) * 0.5
        click = np.convolve(click, np.ones(4) / 4, "same")
        x = np.tanh((body * 1.6 + click) * 1.4)
        return _st(x * vel * 0.9)
    if kind == "snare":
        n = int(0.45 * sr)
        t = np.arange(n) / sr
        tone = (np.sin(2 * np.pi * 185 * t) * 0.7 + np.sin(2 * np.pi * 330 * t) * 0.3) * _exp(t, 0.06)
        nz = rng.standard_normal(n)
        nz = _hp(nz, 1500) * _exp(t, 0.13 if flavor != "tight" else 0.08)
        x = np.tanh((tone * 0.9 + nz * 0.8) * 1.3)
        return _st(x * vel * 0.7, width=0.1, rng=rng)
    if kind == "clap":
        n = int(0.35 * sr)
        t = np.arange(n) / sr
        env = np.zeros(n)
        for k, off in enumerate((0.0, 0.011, 0.022, 0.031)):
            i0 = int(off * sr)
            env[i0:] += _exp(t[: n - i0], 0.008 if k < 3 else 0.11)
        nz = _bp(rng.standard_normal(n), 1100, 2.5) * env
        return _st(nz * vel * 0.8, width=0.3, rng=rng)
    if kind in ("hat", "ohat"):
        n = int((0.5 if kind == "ohat" else 0.12) * sr)
        t = np.arange(n) / sr
        freqs = [205.3, 304.4, 369.6, 522.7, 540.0, 800.0]
        x = sum(np.sign(np.sin(2 * np.pi * f * 1.7 * t + rng.random() * 6)) for f in freqs)
        x = x / 6 + rng.standard_normal(n) * 0.6
        x = _hp(x, 7000)
        x *= _exp(t, 0.15 if kind == "ohat" else 0.025)
        return _st(x * vel * 0.45, width=0.2, rng=rng)
    if kind == "tom":
        n = int(0.5 * sr)
        t = np.arange(n) / sr
        base = 110 * 2 ** (pitch / 12.0)
        f = base + base * 0.6 * _exp(t, 0.03)
        x = np.sin(2 * np.pi * np.cumsum(f) / sr) * _exp(t, 0.2)
        x += _bp(rng.standard_normal(n), base * 3, 1.0) * _exp(t, 0.02) * 0.3
        return _st(np.tanh(x * 1.3) * vel * 0.7)
    if kind == "crash":
        n = int(2.2 * sr)
        t = np.arange(n) / sr
        x = _hp(rng.standard_normal(n), 4000) * _exp(t, 0.7)
        x += _hp(rng.standard_normal(n), 9000) * _exp(t, 0.25) * 0.5
        return _st(x * vel * 0.3, width=0.6, rng=rng)
    if kind == "shaker":
        n = int(0.09 * sr)
        t = np.arange(n) / sr
        env = np.minimum(t / 0.015, 1) * _exp(t, 0.03)
        x = _bp(rng.standard_normal(n), 7000, 1.2) * env
        return _st(x * vel * 0.35, width=0.4, rng=rng)
    raise ValueError(kind)


def _st(x, width=0.0, rng=None):
    x = np.asarray(x, np.float32)
    if width and rng is not None:
        d = int(rng.integers(8, 40))
        r = np.concatenate([np.zeros(d, np.float32), x[:-d]])
        return np.stack([x, (1 - width) * x + width * r], 1)
    return np.stack([x, x], 1)


def _hp(x, fc):
    from scipy.signal import butter, sosfilt
    return sosfilt(butter(2, fc, "hp", fs=SR, output="sos"), x)


def _bp(x, fc, q):
    from scipy.signal import butter, sosfilt
    bw = fc / q
    return sosfilt(butter(2, [max(20, fc - bw / 2), min(SR / 2 - 100, fc + bw / 2)], "bp", fs=SR, output="sos"), x)
