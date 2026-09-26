#pragma once

// Deterministic synthetic test material. These are not "real music" - they are controlled
// signals with known ground truth (known phrase levels, known harsh notes, known sibilants),
// which is what unit tests need. Real-material evaluation lives in docs/TESTING.md.

#include "TestUtils.h"
#include "dsp/DspCommon.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace nova::test
{

struct VoiceSpec
{
    double sampleRate = 48000.0;
    int phrases = 6;
    double phraseSec = 1.6;
    double gapSec = 0.45;
    float f0 = 196.f;                                    // G3 like the concept UI
    std::vector<float> phraseLevelsDb { -18, -26, -14, -24, -16, -28 };
    std::vector<bool> harshPhrase { false, false, true, false, true, false };
    float harshFreq = 3200.f, harshBoostDb = 14.f;
    bool sibilants = true;
    float sibilantFreq = 6800.f, sibilantLevelDb = -14.f;
    float roomDecaySec = 0.f;                           // > 0 adds a synthetic reverb tail
    unsigned seed = 42;
};

// Glottal-pulse source through three formant resonators, phrased with smooth envelopes.
inline juce::AudioBuffer<float> makeVoice (const VoiceSpec& spec)
{
    const double sr = spec.sampleRate;
    const int phraseN = (int) (spec.phraseSec * sr), gapN = (int) (spec.gapSec * sr);
    const int total = spec.phrases * (phraseN + gapN) + gapN;
    std::vector<float> out ((size_t) total, 0.f);

    std::mt19937 rng (spec.seed);
    std::normal_distribution<float> jitter (0.f, 0.004f);
    std::uniform_real_distribution<float> uni (-1.f, 1.f);

    dsp::Biquad f1, f2, f3, harsh, sib, air;
    f1.setCoeffs (dsp::BiquadCoeffs::bandPass (sr, 650.0, 5.0));
    f2.setCoeffs (dsp::BiquadCoeffs::bandPass (sr, 1150.0, 7.0));
    f3.setCoeffs (dsp::BiquadCoeffs::bandPass (sr, 2600.0, 9.0));
    harsh.setCoeffs (dsp::BiquadCoeffs::peak (sr, spec.harshFreq, 2.5, spec.harshBoostDb));
    sib.setCoeffs (dsp::BiquadCoeffs::bandPass (sr, spec.sibilantFreq, 2.0));

    double phase = 0.0;
    for (int p = 0; p < spec.phrases; ++p)
    {
        const int start = gapN + p * (phraseN + gapN);
        const float lvl = dsp::dbToGain (spec.phraseLevelsDb[(size_t) p % spec.phraseLevelsDb.size()]);
        const bool isHarsh = spec.harshPhrase[(size_t) p % spec.harshPhrase.size()];
        const float noteF0 = spec.f0 * (1.f + 0.12f * (float) (p % 3));   // small melody
        harsh.reset();
        for (int i = 0; i < phraseN; ++i)
        {
            const double t = (double) i / sr;
            const float env = (float) (std::min ({ 1.0, t / 0.05, (spec.phraseSec - t) / 0.08 }));
            const float vib = 1.f + 0.006f * (float) std::sin (2 * kPi * 5.5 * t);
            phase += noteF0 * vib * (1.f + jitter (rng)) / sr;
            if (phase >= 1.0) phase -= 1.0;
            const float glottal = (float) (phase < 0.6 ? std::sin (kPi * phase / 0.6) : 0.0) - 0.38f;   // pulse, ~zero mean
            float v = 2.2f * f1.process (glottal, 0) + 1.6f * f2.process (glottal, 0) + 0.9f * f3.process (glottal, 0)
                      + 0.15f * glottal;
            if (isHarsh) v = harsh.process (v, 0);
            out[(size_t) (start + i)] = v * env * lvl * 4.f;
        }
        // an 's' at the end of each phrase
        if (spec.sibilants)
        {
            const int sibStart = start + phraseN - (int) (0.12 * sr);
            const int sibLen = (int) (0.11 * sr);
            const float sl = dsp::dbToGain (spec.sibilantLevelDb);
            for (int i = 0; i < sibLen; ++i)
            {
                const float env = (float) std::sin (kPi * i / sibLen);
                out[(size_t) (sibStart + i)] += sl * 3.f * env * sib.process (uni (rng), 0);
            }
        }
    }

    if (spec.roomDecaySec > 0.f)
    {
        // exponentially decaying noise impulse response, 20 % wet
        const int irLen = (int) (spec.roomDecaySec * sr);
        std::vector<float> ir ((size_t) irLen);
        for (int i = 0; i < irLen; ++i)
            ir[(size_t) i] = uni (rng) * (float) std::pow (10.0, -3.0 * i / (spec.roomDecaySec * sr));
        std::vector<float> wet (out.size(), 0.f);
        // sparse convolution (every 4th tap) keeps the test fast and still diffuse
        for (int k = 0; k < irLen; k += 4)
        {
            const float h = ir[(size_t) k] * 0.06f;
            for (size_t i = 0; i + (size_t) k < out.size(); ++i)
                wet[i + (size_t) k] += h * out[i];
        }
        for (size_t i = 0; i < out.size(); ++i) out[i] += wet[i];
    }

    juce::AudioBuffer<float> buf (2, total);
    for (int c = 0; c < 2; ++c)
        buf.copyFrom (c, 0, out.data(), total);
    return buf;
}

inline juce::AudioBuffer<float> toBuffer (const std::vector<float>& l, const std::vector<float>& r)
{
    juce::AudioBuffer<float> b (2, (int) l.size());
    b.copyFrom (0, 0, l.data(), (int) l.size());
    b.copyFrom (1, 0, r.data(), (int) r.size());
    return b;
}

} // namespace nova::test
