#pragma once

#include "DspCommon.h"
#include "../core/Parameters.h"

#include <cstdint>

namespace nova::dsp
{

struct ProcessContext
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    double ppqAtBlockStart = 0.0;   // musical position (quarter notes) of the first sample
    bool hasPpq = false;
    bool isPlaying = false;
    int64_t freeRunSample = 0;      // internal clock when the host does not provide a position
};

// Running statistics of what each module did during a block/render. Used by the AI
// closed loop (e.g. "is the compressor working too hard?") and by the UI meters.
struct ModuleStats
{
    double sum = 0;      // sum of per-sample values (dB of gain change)
    double activeSum = 0;
    double maxv = 0;
    int64_t count = 0;
    int64_t activeCount = 0; // samples where |value| > 0.5 dB

    void add (double v) noexcept
    {
        sum += v; ++count;
        maxv = std::max (maxv, v);
        if (v > 0.5) { ++activeCount; activeSum += v; }
    }
    double mean() const noexcept { return count > 0 ? sum / (double) count : 0.0; }
    // mean over the samples where the module was actually working (> 0.5 dB)
    double activeMean() const noexcept { return activeCount > 0 ? activeSum / (double) activeCount : 0.0; }
    double activeRatio() const noexcept { return count > 0 ? (double) activeCount / (double) count : 0.0; }
    void reset() noexcept { *this = {}; }
};

struct ChainStats
{
    ModuleStats riderAbsGain, compGr, dessGr, limGr;
    std::array<ModuleStats, kNumDynBands> dynCut;
    void reset() noexcept
    {
        riderAbsGain.reset(); compGr.reset(); dessGr.reset(); limGr.reset();
        for (auto& d : dynCut) d.reset();
    }
};

// Instantaneous values for meters (written by the owning thread, read by UI through atomics
// in the processor).
struct ChainInstant
{
    float riderGainDb = 0, compGrDb = 0, dessGrDb = 0, limGrDb = 0;
    std::array<float, kNumDynBands> dynCutDb {};
};

//==============================================================================
// BS.1770 K-weighting (pre-filter shelf + RLB high-pass), exact for any sample rate.
struct KWeighting
{
    Biquad shelf, hp;
    void prepare (double sr);
    void reset() { shelf.reset(); hp.reset(); }
    inline float process (float x, int ch) noexcept { return hp.process (shelf.process (x, ch), ch); }
    static BiquadCoeffs shelfCoeffs (double sr);
    static BiquadCoeffs highpassCoeffs (double sr);
};

//==============================================================================
class LevelRider
{
public:
    void prepare (double sr);
    void reset();
    void process (float* const* ch, int numCh, int n, const ChainSettings& s, ChainStats& stats, ChainInstant& inst) noexcept;

private:
    double sr = 48000;
    KWeighting kw;
    float msState = 0.f, msCoeff = 0.f;
    float gainDb = 0.f, currentLin = 1.f;
    bool active = false;
};

//==============================================================================
class EqModule
{
public:
    void prepare (double sr);
    void reset();
    void process (float* const* ch, int numCh, int n, const ChainSettings& s) noexcept;

    // Static magnitude response (dB) of the EQ described by `s` - used for UI curves & AI.
    static double responseDb (const ChainSettings& s, double sr, double freq);

private:
    double sr = 48000;
    Biquad hp1, hp2, lp;
    Smoother hpFreqLog, lpFreqLog;
    std::array<Biquad, kNumEqBands> bands;
    std::array<Smoother, kNumEqBands> fLog, gain, qLog;
    std::array<int, kNumEqBands> lastType {};
    std::array<float, kNumEqBands> lastF {}, lastG {}, lastQ {};
    float lastHp = -1, lastLp = -1;
    int lastSlope = -1;
    bool first = true;

    void updateControl (const ChainSettings& s, int n);
};

//==============================================================================
class ToneMatchModule
{
public:
    void prepare (double sr);
    void reset();
    void process (float* const* ch, int numCh, int n, const ChainSettings& s) noexcept;
    static BiquadCoeffs bandCoeffs (double sr, int band, double gainDb);
    static double responseDb (const ChainSettings& s, double sr, double freq);
    static double responseDbForGains (const std::array<float, kNumToneMatchBands>& gainsDb, double sr, double freq);

private:
    double sr = 48000;
    std::array<Biquad, kNumToneMatchBands> bands;
    std::array<Smoother, kNumToneMatchBands> g;
    std::array<float, kNumToneMatchBands> lastG {};
    bool first = true;
};

//==============================================================================
class DynamicEq
{
public:
    void prepare (double sr);
    void reset();
    void process (float* const* ch, int numCh, int n, const ChainSettings& s, ChainStats& stats, ChainInstant& inst) noexcept;

private:
    struct Band
    {
        Biquad bp;
        EnvelopeFollower env;
        float lastF = -1, lastQ = -1, lastA = -1, lastR = -1;
        float cutDb = 0;
        Smoother enable;
    };
    double sr = 48000;
    std::array<Band, kNumDynBands> bands;
};

//==============================================================================
class Compressor
{
public:
    void prepare (double sr);
    void reset();
    void process (float* const* ch, int numCh, int n, const ChainSettings& s, ChainStats& stats, ChainInstant& inst) noexcept;

    // Static gain computer (dB in -> dB of gain reduction, positive)
    static float gainReductionDb (float inDb, float thresh, float ratio, float knee) noexcept;

private:
    double sr = 48000;
    float rmsState = 0.f, rmsCoeff = 0.f;
    float grSmoothed = 0.f;
    Smoother makeup, mix, enable;
};

//==============================================================================
class DeEsser
{
public:
    void prepare (double sr);
    void reset();
    void process (float* const* ch, int numCh, int n, const ChainSettings& s, ChainStats& stats, ChainInstant& inst) noexcept;

private:
    double sr = 48000;
    Biquad detect, band;
    EnvelopeFollower env;
    float lastF = -1;
    float grDb = 0;
    Smoother enable;
};

//==============================================================================
class ColorModule
{
public:
    void prepare (double sr, int maxBlock);
    void reset();
    void process (float* const* ch, int numCh, int n, const ChainSettings& s) noexcept;
    static constexpr int latencySamples() { return Halfband2x::kLatency; }

    static float shape (int type, float x, float drive) noexcept;

private:
    double sr = 48000;
    Halfband2x os;
    DelayLine dryDelay;
    Biquad dcBlock, warmLow, warmHigh;
    Smoother drive, blend, warmth;
    float lastWarm = 1e9f;
};

//==============================================================================
class RhythmGate
{
public:
    void prepare (double sr);
    void reset();
    void process (float* const* ch, int numCh, int n, const ChainSettings& s, const ProcessContext& ctx) noexcept;
    static double stepLengthBeats (int division);
    static bool stepOpen (int pattern, int stepIndex, double phaseInStep);

private:
    double sr = 48000;
    float gain = 1.f, coeff = 0.f;
    Smoother enable;
};

//==============================================================================
class ImageModule
{
public:
    void prepare (double sr);
    void reset();
    void process (float* const* ch, int numCh, int n, const ChainSettings& s) noexcept;

private:
    double sr = 48000;
    Biquad sideHp;
    Smoother width, monoBlend;
    float lastF = -1;
};

//==============================================================================
class Limiter
{
public:
    void prepare (double sr);
    void reset();
    void process (float* const* ch, int numCh, int n, const ChainSettings& s, ChainStats& stats, ChainInstant& inst) noexcept;
    int latencySamples() const noexcept { return lookahead; }

private:
    double sr = 48000;
    int lookahead = 64;
    DelayLine audioDelay;
    // sliding-window minimum (monotonic deque in a fixed ring)
    std::vector<float> dqVal;
    std::vector<int64_t> dqIdx;
    int dqHead = 0, dqTail = 0, dqCap = 0;
    int64_t sampleIndex = 0;
    // box filter for attack smoothing
    std::vector<float> boxBuf;
    double boxSum = 0;
    int boxPos = 0;
    float relGain = 1.f;
    Smoother drive, ceiling, enable;
};

} // namespace nova::dsp
