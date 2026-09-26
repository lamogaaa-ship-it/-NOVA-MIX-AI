#pragma once

// Structured, machine-readable description of what a piece of audio measurably is.
// Every value here comes from an actual measurement of the audio buffer (see Analyzer.cpp).
// Values that could not be measured reliably carry an explicit confidence of 0 and are
// reported as unknown to the AI - never guessed.

#include <array>
#include <string>
#include <vector>

namespace nova::analysis
{

enum class SourceType { Unknown, LeadVocal, BackingVocal, FullMix, Drums, Bass, Instrument, Speech };
const char* sourceTypeName (SourceType t);

struct TimedEvent
{
    double start = 0, end = 0;    // seconds
    float freqHz = 0;             // dominant frequency of the event (if applicable)
    float severityDb = 0;         // how far above the "normal" level of that band it rose
    float levelDb = 0;            // absolute level of the band during the event
};

struct Segment
{
    double start = 0, end = 0;
    float loudnessDb = -100;      // K-weighted mean level (LUFS-like)
    float peakDb = -100;
    float centroidHz = 0;
    int harshEvents = 0, sibilantEvents = 0;
    std::string label;
};

struct SpectralPeak { float freqHz = 0; float prominenceDb = 0; };

constexpr int kThirdOctaveBands = 31;
extern const std::array<float, kThirdOctaveBands> kThirdOctaveCentres;

struct AudioFeatures
{
    // meta
    double sampleRate = 0, durationSec = 0, activeSec = 0;
    int numChannels = 0;
    float silenceRatio = 1;
    bool valid = false;

    // level / loudness (BS.1770-4, EBU R128 / Tech 3342)
    float peakDb = -120, truePeakDb = -120, rmsDb = -120, crestDb = 0, dcOffset = 0;
    int clippedSamples = 0;
    float integratedLufs = -70, momentaryMaxLufs = -70, shortTermMaxLufs = -70, lra = 0, plr = 0;
    std::vector<float> shortTermLufs;     // 1 value per second
    float noiseFloorDb = -120, dynamicRangeDb = 0, phraseLevelStdDb = 0, shortTermStdDb = 0;
    float microDynamicsDb = 0;            // median 10 ms level swing inside phrases

    // spectrum (long-term average over active audio)
    std::array<float, kThirdOctaveBands> thirdOctaveDb {};   // band power relative to total (dB)
    float centroidHz = 0, centroidStdHz = 0, rolloffHz = 0, flux = 0, flatness = 0, tiltDbPerOct = 0;
    // broad balance, dB relative to total energy
    float subDb = -100, lowDb = -100, lowMidDb = -100, midDb = -100, upperMidDb = -100, presenceDb = -100, sibilanceBandDb = -100, airDb = -100;
    std::vector<SpectralPeak> resonances;

    // transients
    float onsetRate = 0;                  // onsets per active second
    float attackSharpness = 0;            // average dB rise per ms at onsets
    float transientCrestDb = 0;

    // pitch / voice (valid when voicedRatio is meaningful)
    float voicedRatio = 0, f0MedianHz = 0, f0MinHz = 0, f0MaxHz = 0, pitchJitterCents = 0, hnrDb = 0;
    float f1Hz = 0, f2Hz = 0, f3Hz = 0, presencePeakHz = 0;
    float formantConfidence = 0;

    // events
    std::vector<TimedEvent> sibilantEvents, harshEvents, plosiveEvents, clickEvents, breathEvents;
    float sibilanceCenterHz = 0, sibilanceSeverityDb = 0;
    float harshCenterHz = 0, harshSeverityDb = 0, harshBandwidthOct = 0;

    // stereo
    bool isStereo = false, isEffectivelyMono = true;
    float correlation = 1, correlationLow = 1, sideToMidDb = -100, balanceDb = 0, monoLossDb = 0, interChannelDelayMs = 0;

    // space
    float decayTimeSec = 0, tailToDirectDb = -100, spaceConfidence = 0;

    // structure
    std::vector<Segment> phrases, sections;

    // source guess
    SourceType source = SourceType::Unknown;
    float sourceConfidence = 0;

    // processing info
    double analysisMs = 0;
};

struct AnalysisOptions
{
    bool pitch = true;         // YIN + formants (skipped for fast closed-loop previews)
    bool events = true;
    bool structure = true;
    bool stereo = true;
    bool space = true;
    SourceType hint = SourceType::Unknown;
};

} // namespace nova::analysis
