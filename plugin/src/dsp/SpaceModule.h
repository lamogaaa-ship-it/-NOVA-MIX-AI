#pragma once

#include "Modules.h"

namespace nova::dsp
{

// Parallel "send/return" space: an 8-line feedback delay network reverb and a tempo-synced
// stereo delay, both fed from the chain signal and summed back in. Allocation only in prepare().
class SpaceModule
{
public:
    void prepare (double sr, int maxBlock);
    void reset();
    void process (float* const* ch, int numCh, int n, const ChainSettings& s, const ProcessContext& ctx) noexcept;

    static double delayTimeMs (const ChainSettings& s, double bpm);
    static double divisionBeats (int division);

private:
    static constexpr int kLines = 8;
    double sr = 48000;

    // reverb
    DelayLine preDelay;
    Biquad lowCut;
    std::array<DelayLine, 4> diffusers;
    std::array<int, 4> diffLen {};
    std::array<DelayLine, kLines> lines;
    std::array<float, kLines> baseLenMs {};
    std::array<float, kLines> lineGain {};
    std::array<float, kLines> dampState {};
    std::array<float, kLines> lineLen {};
    float dampCoeff = 0.f;
    float lastDecay = -1, lastSize = -1, lastDamp = -1, lastLowCut = -1;
    double lfoPhase = 0.0;

    // delay
    DelayLine echo;
    std::array<float, kMaxChannels> echoLp {}, echoHpState {};
    Smoother echoTime;
    float echoLpCoeff = 0.f;

    // ducking + returns
    EnvelopeFollower duckEnv;
    Smoother revGain, dlyGain, active;
    bool wasActive = false;

    void updateReverb (const ChainSettings& s);
};

} // namespace nova::dsp
