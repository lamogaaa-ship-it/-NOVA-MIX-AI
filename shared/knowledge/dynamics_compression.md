# Dynamics and compression

## Detector and gain computer
Feed-forward compressors measure the level (peak or RMS), compare it to the threshold and apply gain reduction according to the ratio. A soft knee (4-10 dB) makes the onset gradual. Log-domain smoothing with separate attack and release avoids distortion from fast gain changes.

## Attack and release
Short attack (0.5-5 ms) catches transients and dulls them; medium (10-30 ms) lets the transient through and controls the body, which is how compression adds punch. Release that is too short causes distortion on low frequencies; too long causes the compressor to never recover (pumping or constant squash).

## Parallel compression
Blending a heavily compressed copy with the dry signal raises low-level detail while keeping transients. It is a good way to add density without losing punch.

## Level riding vs compression
A level rider applies slow gain changes toward a target level and should hold (not boost) during silence, breaths and noise. It solves phrase-to-phrase level differences without the side effects of heavy compression.

## Limiting
A lookahead limiter prevents peaks exceeding a ceiling. True-peak (inter-sample) overs can still occur after conversion, so masters are typically limited to -1 dBTP. Excessive limiting lowers the peak-to-loudness ratio (PLR) and crest factor and flattens transients.
