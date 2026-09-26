#pragma once

#include "Modules.h"
#include "SpaceModule.h"

namespace nova::dsp
{

// The complete NOVA processing chain. The realtime processor and the offline preview renderer
// use this exact class so an AI preview is bit-for-bit what the plugin will output.
//
// Signal flow:
//   in trim -> [reorderable inserts: level, eq, tone match, dyn eq, comp, de-ess, color, motion]
//           -> space (parallel returns) -> image -> limiter -> out gain -> global mix (aligned dry)
class NovaChain
{
public:
    static constexpr int kControlBlock = 32;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    int getLatencySamples() const noexcept { return latency; }
    double getSampleRate() const noexcept { return sr; }

    // Processes `n` samples of `numCh` (1 or 2) channels in place.
    void process (float* const* ch, int numCh, int n, const ChainSettings& s, const ChainOrder& order, ProcessContext& ctx) noexcept;

    ChainStats& getStats() noexcept { return stats; }
    const ChainInstant& getInstant() const noexcept { return instant; }

    // Combined static magnitude response of the tonal modules (EQ + tone match + colour warmth)
    static double staticResponseDb (const ChainSettings& s, double sr, double freq);

private:
    double sr = 48000.0;
    int latency = 0;
    LevelRider rider;
    EqModule eq;
    ToneMatchModule toneMatch;
    DynamicEq dynEq;
    Compressor comp;
    DeEsser deess;
    ColorModule color;
    RhythmGate gate;
    SpaceModule space;
    ImageModule image;
    Limiter limiter;

    DelayLine mixDry;
    Smoother inGain, outGain, mix;
    ChainStats stats;
    ChainInstant instant;
    bool firstBlock = true;
};

} // namespace nova::dsp
