#pragma once

#include "Theme.h"
#include "Icons.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <optional>

namespace nova::ui
{

// Button with the full state set: normal / hover / pressed / disabled / active (toggled).
class NovaButton : public juce::Button
{
public:
    enum class Style { Pill, Tab, IconOnly, Chip, Primary, Round, Segment };

    NovaButton (const juce::String& text, std::optional<Icon> icon, Style style);
    void paintButton (juce::Graphics& g, bool over, bool down) override;
    void setIcon (std::optional<Icon> i) { icon = i; repaint(); }
    void setProgress (float p) { progress = p; repaint(); }       // Primary style: fills while listening
    void setPulse (float p) { pulse = p; repaint(); }
    void setSubText (const juce::String& s) { subText = s; repaint(); }
    void setTextHeight (float h) { textHeight = h; }

private:
    std::optional<Icon> icon;
    Style style;
    float progress = -1.f, pulse = 0.f, textHeight = 0.f;
    juce::String subText;
};

// Vertical L/R meter with dBFS scale, peak hold and RMS body (ballistics applied on the UI side).
class LevelMeter : public juce::Component
{
public:
    std::function<std::array<float, 4>()> source;   // peakL, peakR, rmsL, rmsR (linear)
    void tick (double dt);
    void paint (juce::Graphics& g) override;

private:
    float peak[2] { 0, 0 }, rms[2] { 0, 0 }, hold[2] { -100, -100 };
    double holdAge[2] { 0, 0 };
    static float toY (float db, float top, float bottom);
};

// Horizontal gain-reduction style activity bar
void paintActivityBar (juce::Graphics& g, juce::Rectangle<float> r, float amount01, juce::Colour c);

juce::String formatHz (float hz);
juce::String formatDb (float db, int decimals = 1);

} // namespace nova::ui
