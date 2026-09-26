#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Panels.h"

namespace nova
{
class NovaAudioProcessor;

class NovaEditor final : public juce::AudioProcessorEditor
{
public:
    static constexpr int kBaseW = 1440, kBaseH = 1080;

    explicit NovaEditor (NovaAudioProcessor& p);
    ~NovaEditor() override;
    void paint (juce::Graphics& g) override;
    void resized() override;

    // For the screenshot tool / tests: run one animation step without a display vblank
    void advanceAnimation (double dt);

private:
    class Content;
    NovaAudioProcessor& processor;
    ui::NovaLookAndFeel lnf;
    std::unique_ptr<Content> content;
    juce::TooltipWindow tooltips { this, 600 };
    juce::VBlankAttachment vblank;
    double lastVBlank = 0;
};

} // namespace nova
