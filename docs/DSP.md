# DSP

All processing runs in `dsp::NovaChain`. The real-time processor uses it, and so do the AI's offline previews, so a preview is bit-for-bit the plug-in's output.

## Signal flow

```
in trim
 → reorderable inserts (default order): Level rider → EQ → Tone Match → Dynamic EQ → Compressor → De-esser → Color → Motion (rhythm gate)
 → Space (parallel reverb + delay returns, ducking)
 → Image (width, mono bass)
 → Limiter (true-peak aware, 1.5 ms look-ahead)
 → out gain
 → global mix (against a latency-aligned dry signal)
 → hosted plug-in rack (optional, see PLUGIN_HOSTING.md)
 → MonitorStage (A/B, Delta, bypass, loudness match)
```

The chain has 116 parameters, all defined once in the X-macro table `core/Parameters.h`. That table feeds:

- the APVTS / host automation;
- `ChainSettings`;
- the AI's validation (ranges, units, per-action safety steps `maxAiStep`);
- the UI.

## Modules

| Module | What it does |
|---|---|
| Level rider | Phrase-aware gain riding towards a target level, with range, speed and an activity gate. It evens phrases before any compression. |
| EQ | 5 bands (bell / shelves / cuts), smoothed coefficient changes. |
| Tone Match | 8 fixed bands whose gains come from the reference-matching fit. `Amount` (0–100 %) is the live reference-influence control. |
| Dynamic EQ | 2 bands that cut only while their band exceeds a threshold. Used for harsh moments and resonances "only when they happen". |
| Compressor | Feed-forward, soft knee, RMS or peak detector, parallel mix, auto-reported gain reduction. |
| De-esser | Band-pass (bell, Q 0.75) detection with split-band or wide reduction modes. |
| Color | Tape / tube / soft-clip saturation with warmth tilt and mix. Oversampled through the half-band stage. |
| Space | 8-line feedback-delay-network reverb plus a tempo-synced stereo delay with ping-pong and tone. Parallel returns with ducking. |
| Motion | Tempo-synced rhythmic gate with 5 patterns (straight, off-beat, stutter 3-3-2, broken, half-time). |
| Image | Mid/side width and mono bass below a frequency. |
| Limiter | Look-ahead peak limiter with ceiling and release. The drive stage is used for loudness targets. |

## Real-time guarantees

- **No allocation:** allocation happens only in `prepare()`. `ProcessorTests` fails if `processBlock` allocates with every module enabled.
- **Control rate:** parameters are read from atomics once per block. Coefficient and gain changes are smoothed every 32-sample control block. Smoothers keep double-precision state, because a float state stalls short of its target with long time constants.
- **Constant latency:** 31-sample half-band FIR plus 1.5 ms limiter look-ahead. It is reported to the host, and the dry path used by the global mix and the monitor stage is delayed by exactly the same amount (tested sample-exact).
- **Blocks:** host blocks are split into ≤ 512-sample chunks.
- **Denormals and bad input:** denormals are flushed (`ScopedNoDenormals`). Non-finite output from hosted plug-ins is replaced with silence.

## Monitoring (MonitorStage)

- **A:** the original, latency-aligned. With loudness match on, it is gain-matched to B using K-weighted energy with about 1.5 s of integration, frozen during silence. A/B therefore never rewards "louder".
- **B:** the processed output. It is never altered by loudness matching, so bounces are exact.
- **Delta:** B − A, i.e. what the processing changed.
- **Bypass:** the aligned original with no matching, i.e. host true-bypass semantics.
