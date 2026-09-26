"""Audio embeddings for similarity (experience retrieval, reference comparison).

The default backend is a transparent DSP descriptor ("nova-dsp-embed-v1"), NOT a neural network:
log-mel band statistics, spectral shape and dynamics, L2-normalised. It is fully commercially
usable and deterministic. A neural backend can replace it behind the same interface as long as
its licence allows commercial use (research-only models such as MERT are deliberately excluded).
"""

from __future__ import annotations

from typing import Protocol

import numpy as np


class Embedder(Protocol):
    available: bool
    name: str
    neural: bool

    def embed(self, audio: np.ndarray, sample_rate: int) -> np.ndarray: ...


def _mel_filterbank(sr: int, n_fft: int, n_mels: int, fmin: float = 40.0, fmax: float | None = None) -> np.ndarray:
    fmax = fmax or sr / 2
    mel = lambda f: 2595.0 * np.log10(1.0 + f / 700.0)
    inv = lambda m: 700.0 * (10 ** (m / 2595.0) - 1.0)
    pts = inv(np.linspace(mel(fmin), mel(fmax), n_mels + 2))
    bins = np.floor((n_fft + 1) * pts / sr).astype(int)
    fb = np.zeros((n_mels, n_fft // 2 + 1), dtype=np.float32)
    for m in range(1, n_mels + 1):
        a, b, c = bins[m - 1], bins[m], bins[m + 1]
        if b > a:
            fb[m - 1, a:b] = (np.arange(a, b) - a) / (b - a)
        if c > b:
            fb[m - 1, b:c] = (c - np.arange(b, c)) / (c - b)
    return fb


class DspEmbedder:
    name = "nova-dsp-embed-v1"
    available = True
    neural = False
    n_mels = 40

    def embed(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        x = np.asarray(audio, dtype=np.float32)
        if x.ndim == 2:
            x = x.mean(axis=0 if x.shape[0] <= 8 else 1)
        if x.size < sample_rate // 4:
            raise ValueError("need at least 0.25 s of audio")
        n_fft, hop = 2048, 512
        frames = 1 + (x.size - n_fft) // hop if x.size >= n_fft else 1
        x = np.pad(x, (0, max(0, n_fft - x.size)))
        idx = np.arange(n_fft)[None, :] + hop * np.arange(frames)[:, None]
        spec = np.abs(np.fft.rfft(x[idx] * np.hanning(n_fft).astype(np.float32), axis=1)) ** 2 + 1e-12
        fb = _mel_filterbank(sample_rate, n_fft, self.n_mels)
        logmel = 10.0 * np.log10(spec @ fb.T + 1e-10)
        power = spec.sum(axis=1)
        active = power > power.max() * 1e-4           # ignore silence
        if active.sum() < 2:
            active[:] = True
        lm = logmel[active]
        lm_rel = lm - lm.mean(axis=1, keepdims=True)  # tone balance independent of level
        freqs = np.fft.rfftfreq(n_fft, 1.0 / sample_rate)
        s = spec[active]
        centroid = (s * freqs).sum(axis=1) / s.sum(axis=1)
        cum = np.cumsum(s, axis=1)
        rolloff = freqs[np.argmax(cum >= 0.85 * cum[:, -1:], axis=1)]
        flatness = np.exp(np.log(s).mean(axis=1)) / s.mean(axis=1)
        flux = np.sqrt((np.diff(lm, axis=0).clip(min=0) ** 2).sum(axis=1)) if lm.shape[0] > 1 else np.zeros(1)
        rms_db = 10.0 * np.log10(power[active] / n_fft)
        feats = np.concatenate([
            lm_rel.mean(axis=0) / 20.0, lm_rel.std(axis=0) / 10.0,
            [np.log2(centroid.mean() / 1000.0), centroid.std() / 2000.0, np.log2(rolloff.mean() / 1000.0),
             flatness.mean() * 10.0, flatness.std() * 10.0, flux.mean() / 20.0, flux.std() / 20.0,
             (np.percentile(rms_db, 95) - np.percentile(rms_db, 10)) / 20.0, rms_db.std() / 10.0],
        ]).astype(np.float32)
        feats = np.nan_to_num(feats)
        n = np.linalg.norm(feats)
        return feats / n if n > 0 else feats


def default_embedder() -> Embedder:
    return DspEmbedder()
