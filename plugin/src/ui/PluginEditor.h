#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace nova
{
class NovaAudioProcessor;

class NovaEditor final : public juce::AudioProcessorEditor
{
public:
    explicit NovaEditor (NovaAudioProcessor& p);
    ~NovaEditor() override;
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    NovaAudioProcessor& processor;
    juce::GenericAudioProcessorEditor generic;
};
} // namespace nova
