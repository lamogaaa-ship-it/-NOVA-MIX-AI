#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../analysis/AnalysisEngine.h"

namespace nova
{

class NovaAudioProcessor;

// Owns every non-realtime subsystem (analysis, AI engineer, reference matching, memory,
// plugin hosting). Lives as long as the processor; never touched by the audio thread.
class NovaEngine
{
public:
    explicit NovaEngine (NovaAudioProcessor& p);
    ~NovaEngine();

    void audioPrepared (double sampleRate, int blockSize);
    juce::ValueTree saveState() const;
    void restoreState (const juce::ValueTree& session);

    analysis::AnalysisEngine& getAnalysis() noexcept { return analysisEngine; }

    NovaAudioProcessor& processor;

private:
    analysis::AnalysisEngine analysisEngine;
};

} // namespace nova
