#include "PluginEditor.h"
#include "../core/PluginProcessor.h"

namespace nova
{
NovaEditor::NovaEditor (NovaAudioProcessor& p) : AudioProcessorEditor (p), processor (p), generic (p)
{
    addAndMakeVisible (generic);
    setSize (900, 700);
}
NovaEditor::~NovaEditor() = default;
void NovaEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colours::black); }
void NovaEditor::resized() { generic.setBounds (getLocalBounds()); }
} // namespace nova
