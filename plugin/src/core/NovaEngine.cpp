#include "NovaEngine.h"
#include "PluginProcessor.h"

namespace nova
{

NovaEngine::NovaEngine (NovaAudioProcessor& p) : processor (p), analysisEngine (p)
{
    analysisEngine.startEngine();
}

NovaEngine::~NovaEngine()
{
    analysisEngine.stopEngine();
}

void NovaEngine::audioPrepared (double sampleRate, int)
{
    analysisEngine.audioPrepared (sampleRate);
}

juce::ValueTree NovaEngine::saveState() const { return juce::ValueTree ("SESSION"); }

void NovaEngine::restoreState (const juce::ValueTree&) {}

} // namespace nova
