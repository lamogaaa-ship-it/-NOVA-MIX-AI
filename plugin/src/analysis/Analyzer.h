#pragma once

#include "Features.h"

#include <vector>

namespace nova::analysis
{

// Deterministic, thread-agnostic audio analysis. Used on the analysis thread for captured
// audio, on the agent thread for rendered previews, and for reference files.
class Analyzer
{
public:
    static AudioFeatures analyze (const float* const* channels, int numChannels, int numSamples,
                                  double sampleRate, const AnalysisOptions& options = {});

    // Exposed building blocks (also unit-tested individually)
    struct Loudness
    {
        float integrated = -70, momentaryMax = -70, shortTermMax = -70, lra = 0;
        std::vector<float> shortTerm100ms;   // 3 s windows at 10 Hz
        std::vector<float> shortTerm1s;      // 3 s windows at 1 Hz
        std::vector<float> momentary100ms;   // 400 ms windows at 10 Hz
    };
    static Loudness measureLoudness (const float* const* channels, int numChannels, int numSamples, double sampleRate);
    static float truePeakDb (const float* const* channels, int numChannels, int numSamples);

    struct PitchTrack
    {
        double frameRate = 0;
        std::vector<float> f0;          // 0 = unvoiced
        std::vector<float> aperiodicity;
        std::vector<float> levelDb;
    };
    static PitchTrack trackPitch (const float* mono, int numSamples, double sampleRate, float minHz = 60.f, float maxHz = 1000.f);

    // Long-term average spectrum in third-octave bands (dB relative to total) - also used for
    // reference matching.
    static std::array<float, kThirdOctaveBands> thirdOctaveFromPowerSpectrum (const std::vector<double>& binPower, double sampleRate, int fftSize);
};

double percentile (std::vector<float> values, double p);   // p in [0, 100]
float median (std::vector<float> values);

} // namespace nova::analysis
