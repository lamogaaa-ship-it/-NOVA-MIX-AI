#pragma once

#include "../dsp/Modules.h"

namespace nova
{

// A/B, Delta and host-bypass monitoring. Runs after the chain on the audio thread.
//
//   A      = original input, latency aligned, and (optionally) gain-matched to B's loudness
//   B      = processed chain output (never altered by loudness matching, so bounces are exact)
//   Delta  = B - A (with A gain-matched when loudness match is on): what the chain changed
//   Bypass = aligned original with no matching (host "true bypass" semantics)
//
// Loudness matching uses K-weighted mean-square energy with a slow (~1.5 s) integration and
// freezes during silence, so switching A/B never rewards "louder".
class MonitorStage
{
public:
    void prepare (double sr, int latencySamples, int maxBlock);
    void reset();

    struct Flags { bool monitorA = false, loudnessMatch = true, delta = false, bypass = false; };

    // wet: in/out buffer (chain output). dry: undelayed chain input for the same samples.
    void process (float* const* wet, const float* const* dry, int numCh, int n, Flags flags) noexcept;

    float getMatchGainDb() const noexcept { return matchDb; }
    float getDryLoudnessDb() const noexcept { return 10.f * std::log10 (msDry + 1e-12f); }
    float getWetLoudnessDb() const noexcept { return 10.f * std::log10 (msWet + 1e-12f); }

private:
    double sr = 48000;
    int latency = 0;
    dsp::DelayLine dryDelay;
    dsp::KWeighting kDry, kWet;
    float msDry = 0.f, msWet = 0.f, msCoeff = 0.f;
    float matchDb = 0.f;
    dsp::Smoother aMix, deltaMix, bypassMix, matchGain;
};

} // namespace nova
