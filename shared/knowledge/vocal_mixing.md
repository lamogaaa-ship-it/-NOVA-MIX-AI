# Vocal mixing

## Order of operations
Clean up first (high-pass, plosives, clicks), then control level (clip gain or a level rider for phrase-to-phrase differences, then compression for syllable-level dynamics), then tone (subtractive EQ before additive EQ), then protection (dynamic EQ for harsh notes, de-essing after any presence/air boosts), then colour (saturation), then space (reverb/delay on returns). Leveling before compression matters: a compressor asked to fix 10 dB of phrase-level difference has to work too hard on loud phrases and pumps.

## Level consistency
Phrase-level variation (macro dynamics) is best fixed by riding or clip-gain style automation with a slow time constant (150-400 ms) so words keep their natural shape. Syllable-level spikes (micro dynamics) are the compressor's job: 2:1-4:1, attack 5-20 ms to keep consonant definition, release 60-150 ms. If average gain reduction on a vocal exceeds ~6 dB it usually starts to sound over-compressed; crest factor falling below ~8 dB is a warning sign.

## Harshness vs sibilance
Harshness is upper-mid energy (roughly 2-5 kHz, voice dependent) that jumps out on loud or belted notes. Sibilance is the 's', 'sh', 't', 'ch' consonant energy (roughly 4.5-11 kHz). They need different tools: harshness wants a dynamic EQ band centred on the measured harsh frequency that only acts on the loud notes; sibilance wants a de-esser centred on the voice's own sibilance frequency. Static cuts for either make the vocal dull everywhere to fix a few moments.

## Muffled vocals
"Muffled" has several possible causes: low-mid build-up (200-500 Hz), lack of presence (2-4 kHz), lack of air (10 kHz+), over-de-essing, heavy compression, reverb masking the direct sound, or a poor recording (distance, room, low-pass microphones). Verify which one applies before boosting treble - boosting 8 kHz on a vocal whose problem is 300 Hz mud adds hiss and sibilance without clearing it.

## Frequency areas are voice dependent
Do not use fixed frequencies. Body sits around 1.2-2x the fundamental; the nasal region sits roughly 700-1500 Hz; presence and harshness depend on the singer's formants; sibilance frequency varies from about 4.5 kHz (low voices, lisps) to 10 kHz (bright voices). Always centre treatments on measured frequencies.

## Proximity and distance
Closer vocals have more low-mid body (proximity effect), more direct sound, less reverb, a little more density (compression) and clear presence. Distant vocals have more diffuse sound (reverb), less presence and less low end. Pre-delay (20-40 ms) keeps the voice intelligible while adding space. Reverb baked into a recording cannot be removed by EQ or compression.

## Modern pop vocal conventions
Controlled level (small phrase-to-phrase spread), clear presence, smooth but present air, well-controlled sibilance, a short plate or room plus tempo-synced delay throws that are ducked under the dry vocal, and saturation for density. These are conventions, not rules: the audio decides.
