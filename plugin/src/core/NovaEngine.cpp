#include "NovaEngine.h"
#include "PluginProcessor.h"

namespace nova
{

NovaEngine::NovaEngine (NovaAudioProcessor& p) : processor (p) {}
NovaEngine::~NovaEngine() = default;

void NovaEngine::audioPrepared (double, int) {}

juce::ValueTree NovaEngine::saveState() const { return juce::ValueTree ("SESSION"); }

void NovaEngine::restoreState (const juce::ValueTree&) {}

} // namespace nova
