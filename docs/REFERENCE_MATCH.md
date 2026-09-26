# Reference Match

Load a reference (WAV, AIFF, FLAC, MP3, OGG) in the Reference panel. NOVA analyses it with the same engine it uses for your audio. The reference's audio never leaves the computer.

## Dimensions

Choose any of: **Tone, Dynamics, Space, Width, Color, Loudness, Vocal (sibilance character)**, or **Full**.

The Match Strength slider sets **influence** (0–100 %). For tone it stays a live control: the Tone Match module's `Amount` scales the fitted curve in real time.

## How matching works (`reference/Reference.cpp`)

1. **Comparable measurements.** Tone is compared as loudness-independent 1/3-octave balance, smoothed over about 1.7 octaves so notes and resonances are not "matched".
2. **Tone.** An 8-band Tone Match curve is fitted by ridge-regularised least squares. Details:
   - the fundamental zone is weighted ×0.15, so pitch differences are not treated as tone;
   - the ridge term is 0.25;
   - gains are clamped to ±8 dB.

   The curve is then refined against real renders of the chain: render, re-measure, adjust.
3. **Dynamics, space, width, color, loudness, vocal.** Each moves the relevant modules (compressor / level rider, reverb, image, saturation, limiter, de-esser) towards the reference's measured value, scaled by influence.
4. **Report.** Each dimension reports before and after distances. Honest limitations are stated, for example:
   - a full mix used as a reference for a solo vocal;
   - a reverb tail that cannot be removed from the source.

Comparisons are always loudness-matched. `compare_to_reference` gives the AI (and the user, through chat) perceptual statements such as "your vocal is 2.1 dB darker above 5 kHz than the reference".
