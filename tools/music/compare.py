#!/usr/bin/env python3
"""Compare two renders of the same track (QA): did the generator make the same music?

    python3 tools/music/compare.py A.ogg B.ogg [more pairs...] [--json out.json]

Per pair: length, loop tags (LOOPSTART / LOOPLENGTH), integrated loudness and true peak, and
how alike the two are:
  waveform   correlation of the samples (1.0 only if bit-for-bit alike)
  spectrum   mean |difference| of the long-term 1/3-octave spectrum, dB
  spectrogram  mean per-frame correlation of 1/3-octave band levels (timbre over time), and
             their mean |difference| in dB
  chroma     mean per-frame cosine of the 12 pitch classes (the notes and chords)
  onsets     correlation of the onset envelopes (the rhythm), best lag within +-50 ms
Reads Ogg Vorbis and WAV (soundfile); needs numpy, scipy, soundfile, pyloudnorm.
"""
import argparse
import json
import math
import struct
import sys

import numpy as np
import soundfile as sf

SR = 48000


def vorbis_comments(path):
    """The Vorbis comment packet's tags (from the Ogg pages), as a dict."""
    data = open(path, "rb").read(1 << 20)
    packets, cur, pos = [], b"", 0
    while pos + 27 <= len(data) and data[pos:pos + 4] == b"OggS" and len(packets) < 3:
        nseg = data[pos + 26]
        lace = data[pos + 27:pos + 27 + nseg]
        body = pos + 27 + nseg
        for L in lace:
            cur += data[body:body + L]
            body += L
            if L < 255:
                packets.append(cur)
                cur = b""
        pos = body
    tags = {}
    for p in packets:
        if p[:7] == b"\x03vorbis":
            q = 7
            vlen = struct.unpack("<I", p[q:q + 4])[0]
            q += 4 + vlen
            n = struct.unpack("<I", p[q:q + 4])[0]
            q += 4
            for _ in range(n):
                ln = struct.unpack("<I", p[q:q + 4])[0]
                k, _, v = p[q + 4:q + 4 + ln].decode("utf-8", "replace").partition("=")
                tags[k.upper()] = v
                q += 4 + ln
    return tags


def load(path):
    y, sr = sf.read(path, dtype="float32", always_2d=True)
    if sr != SR:
        from scipy.signal import resample_poly
        y = resample_poly(y, SR, sr, axis=0).astype(np.float32)
    return y


def loudness(y):
    import pyloudnorm
    from scipy.signal import resample_poly
    lufs = pyloudnorm.Meter(SR).integrated_loudness(y)
    tp = 20 * math.log10(max(1e-9, float(np.abs(resample_poly(y, 4, 1, axis=0)).max())))
    return round(lufs, 2), round(tp, 2)


def bands_matrix(m, n_fft=4096, hop=2048):
    """Third-octave band powers per frame (frames x bands) and the STFT magnitudes."""
    from scipy.signal import stft
    f, _, Z = stft(m, SR, nperseg=n_fft, noverlap=n_fft - hop, boundary=None, padded=False)
    P = np.abs(Z) ** 2
    centres = 1000 * 2 ** (np.arange(-17, 14) / 3)  # 20 Hz .. 20 kHz
    rows = []
    for c in centres:
        sel = (f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))
        rows.append(P[sel].sum(0) if sel.any() else np.zeros(P.shape[1]))
    return np.array(rows).T, f, np.abs(Z)


def chroma(f, mag):
    k = (f > 55) & (f < 5000)
    pc = np.mod(np.round(12 * np.log2(f[k] / 440.0)) + 9, 12).astype(int)
    onehot = np.eye(12)[pc]  # bins x 12
    return mag[k].T @ onehot  # frames x 12


def onset_env(B):
    L = np.log10(B + 1e-10)
    return np.maximum(np.diff(L, axis=0), 0).sum(1)


def corr(a, b):
    a = a - a.mean()
    b = b - b.mean()
    d = np.sqrt((a * a).sum() * (b * b).sum())
    return float((a * b).sum() / d) if d > 0 else 0.0


def compare(pa, pb):
    ya, yb = load(pa), load(pb)
    n = min(len(ya), len(yb))
    ma, mb = ya[:n].mean(1), yb[:n].mean(1)
    Ba, f, Ma = bands_matrix(ma)
    Bb, _, Mb = bands_matrix(mb)
    la, lb = 10 * np.log10(Ba.mean(0) + 1e-12), 10 * np.log10(Bb.mean(0) + 1e-12)
    audible = (la > la.max() - 60) | (lb > lb.max() - 60)
    da, db = 10 * np.log10(Ba + 1e-10), 10 * np.log10(Bb + 1e-10)
    live = (Ba.sum(1) > 1e-8) & (Bb.sum(1) > 1e-8)
    xa, xb = da[live] - da[live].mean(1, keepdims=True), db[live] - db[live].mean(1, keepdims=True)
    frame_corr = (xa * xb).sum(1) / np.sqrt((xa * xa).sum(1) * (xb * xb).sum(1) + 1e-20)
    Ca, Cb = chroma(f, Ma), chroma(f, Mb)
    na, nb = np.linalg.norm(Ca, axis=1), np.linalg.norm(Cb, axis=1)
    ok = (na > 0) & (nb > 0)
    cos = (Ca[ok] * Cb[ok]).sum(1) / (na[ok] * nb[ok])
    oa, ob = onset_env(Ba), onset_env(Bb)
    hop_s = 2048 / SR
    lags = range(-int(0.05 / hop_s) - 1, int(0.05 / hop_s) + 2)
    best = max((corr(oa[max(0, -k):len(oa) - max(0, k)], ob[max(0, k):len(ob) - max(0, -k)]), k) for k in lags)
    ta, tb = vorbis_comments(pa) if pa.endswith(".ogg") else {}, vorbis_comments(pb) if pb.endswith(".ogg") else {}
    return {
        "a": pa, "b": pb,
        "seconds": [round(len(ya) / SR, 3), round(len(yb) / SR, 3)],
        "loop": [[ta.get("LOOPSTART"), ta.get("LOOPLENGTH")], [tb.get("LOOPSTART"), tb.get("LOOPLENGTH")]],
        "lufs_tp": [loudness(ya), loudness(yb)],
        "waveform_corr": round(corr(ma, mb), 4),
        "spectrum_mean_abs_diff_db": round(float(np.abs(la - lb)[audible].mean()), 2),
        "spectrogram_corr": round(float(np.mean(frame_corr)), 4),
        "spectrogram_mean_abs_diff_db": round(float(np.abs(da[live] - db[live])[:, audible].mean()), 2),
        "chroma_cos": round(float(np.mean(cos)), 4),
        "onset_corr": round(best[0], 4), "onset_lag_ms": round(best[1] * hop_s * 1000, 1),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+", help="pairs: A B [A B ...]")
    ap.add_argument("--json", help="also write the results here")
    a = ap.parse_args()
    if len(a.files) % 2:
        sys.exit("give the files in pairs")
    res = []
    for i in range(0, len(a.files), 2):
        r = compare(a.files[i], a.files[i + 1])
        res.append(r)
        print(json.dumps(r), flush=True)
    if a.json:
        with open(a.json, "w") as f:
            json.dump(res, f, indent=1)


if __name__ == "__main__":
    main()
