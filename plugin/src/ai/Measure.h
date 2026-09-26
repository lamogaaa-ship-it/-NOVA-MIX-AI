#pragma once

// Closed-loop measurement: render candidate settings offline through the exact realtime chain
// and measure, on the *same time regions* of input and output, whether a problem got better
// and whether anything we care about got worse. All level comparisons are loudness matched
// (values are expressed relative to integrated loudness) so "louder" never reads as "better".

#include <juce_audio_basics/juce_audio_basics.h>

#include "../analysis/Features.h"
#include "../core/Parameters.h"
#include "../core/PluginProcessor.h"
#include "../dsp/NovaChain.h"

#include <memory>
#include <vector>

namespace nova::ai
{

struct TimeWindow { double start = 0, end = 0; };

struct PreviewResult
{
    std::shared_ptr<juce::AudioBuffer<float>> audio;   // latency-aligned with the input
    dsp::ChainStats stats;
    double sampleRate = 0;
    double renderMs = 0;
};

// Render `input` through a fresh NovaChain with the given settings (latency compensated).
PreviewResult renderPreview (const juce::AudioBuffer<float>& input, double sampleRate, const ChainSettings& settings,
                             const ChainOrder& order, const TransportSnapshot& transport);

// Measurement plan derived from the analysis of the ORIGINAL input. Re-used unchanged for
// every candidate so the comparison is apples to apples.
struct Probe
{
    std::vector<TimeWindow> active, harsh, sibilant, calm;   // calm = active minus events
    float harshLo = 2500, harshHi = 4000, sibLo = 5000, sibHi = 10000;
    double sampleRate = 48000;

    static Probe fromFeatures (const analysis::AudioFeatures& f);
};

struct Metrics
{
    bool valid = false;
    float integratedLufs = -70, truePeakDb = -120, crestDb = 0, lra = 0;
    float phraseStdDb = 0, shortTermStdDb = 0, macroRangeDb = 0;
    // loudness-matched band levels (dB relative to integrated loudness)
    float harshOnEvents = -100, harshElsewhere = -100;        // harsh band on harsh moments / elsewhere
    float sibOnEvents = -100, sibElsewhere = -100;            // sibilant band on 's' / elsewhere
    float presence = -100, air = -100, body = -100, lowMid = -100, low = -100, sub = -100;
    float centroidHz = 0, tailToDirectDb = -100, sideToMidDb = -100, monoLossDb = 0;
    // processing load
    float compAvgGr = 0, compMaxGr = 0, riderAvgAbs = 0, dessAvgGr = 0, dessMaxGr = 0, limAvgGr = 0, limMaxGr = 0;
    float dynCutAvg[kNumDynBands] {}, dynCutMax[kNumDynBands] {};
};

Metrics measure (const juce::AudioBuffer<float>& audio, double sampleRate, const Probe& probe, const dsp::ChainStats* stats = nullptr,
                 bool withSpaceAndStereo = true);

// Convert metrics / diffs to compact JSON for the AI.
juce::var metricsToJson (const Metrics& m);
juce::var metricsDeltaJson (const Metrics& before, const Metrics& after);

} // namespace nova::ai
