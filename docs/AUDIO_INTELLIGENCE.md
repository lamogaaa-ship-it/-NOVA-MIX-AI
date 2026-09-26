# Audio Intelligence Engine

This is native C++ analysis on a worker thread (`analysis/AnalysisEngine`). It is fed by a lock-free capture FIFO that receives both the dry input and the processed output. Nothing is guessed: every value comes from these measurements.

## LISTEN state machine

```
Idle → Listening (captures up to N s of real audio, gaps ≤ 0.6 s kept so phrasing survives)
     → Analyzing → Ready (AnalysisResult published)
```

The orb and the LISTEN button display exactly this state. If no audio arrives, NOVA says so ("press play in your DAW") and nothing is invented. The live meters, spectrum and waveform come from the same capture path.

## Measurements (`analysis/Features.h`)

| Area | Features |
|---|---|
| Level | peak, true peak (4× oversampled), RMS, crest, integrated / momentary / short-term LUFS (BS.1770-4), LRA, PLR, noise floor, dynamic range, phrase-level spread, micro-dynamics |
| Spectrum | 1/3-octave balance, centroid (+ spread), roll-off, flux, flatness, tilt, band energies (sub … air), resonant peaks |
| Transients | onset rate, attack sharpness, transient crest |
| Voice | voiced ratio, f0 median / range (YIN), jitter, HNR, formants F1–F3 (LPC), presence peak |
| Events | sibilants, harsh moments (localised 2–5 kHz), plosives, clicks, breaths, each with time and frequency; sibilance and harshness centre / severity |
| Stereo | correlation (full and low band), side/mid, balance, mono loss, inter-channel delay |
| Space | decay time, tail-to-direct ratio (envelope-offset detection), confidence |
| Structure | phrases and sections |
| Source | vocal / instrument / mix / master classification, with confidence |

Event detection is relative to each recording's own statistics, so a naturally bright voice is not flagged as "harsh" everywhere.

## Semantic layer (`analysis/Semantic`)

The semantic layer turns features into engineering problems. Each problem carries a confidence and its evidence, for example:

- `harshness`
- `sibilance`
- `inconsistent_level`
- `word_level_spikes`
- `low_mid_congestion`
- `boxiness`
- `lack_of_presence`
- `lack_of_air`
- `rumble`
- `plosives`
- `roomy_recording`
- `true_peak_over`
- `over_limited`

These problems drive the suggestion chips and are the evidence the engineer must cite.

## Accuracy notes

Everything is validated on synthetic signals with known ground truth: tones, noise, a synthetic voice with phrases, sibilants and harsh bursts, and reverberant tails. It has not yet been evaluated on a corpus of real recordings; see `ROADMAP.md`.
