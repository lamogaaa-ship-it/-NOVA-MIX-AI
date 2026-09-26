#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace nova::ui
{

// Design tokens taken from the NOVA MIX AI concept: deep near-black / midnight navy surfaces,
// electric cyan -> blue -> violet accents, glass panels with hairline borders.
namespace Colours
{
    inline const juce::Colour bg        { 0xff04060d };
    inline const juce::Colour bgDeep    { 0xff02030a };
    inline const juce::Colour panel     { 0xff0a0f1f };
    inline const juce::Colour panelHi   { 0xff111831 };
    inline const juce::Colour border    { 0x2e7c93ff };
    inline const juce::Colour borderHi  { 0x5a6fe3ff };
    inline const juce::Colour text      { 0xffe7edf8 };
    inline const juce::Colour textDim   { 0xff9aa5c4 };
    inline const juce::Colour textMute  { 0xff5d6788 };
    inline const juce::Colour cyan      { 0xff2fe3ff };
    inline const juce::Colour cyanSoft  { 0xff1aa7d6 };
    inline const juce::Colour blue      { 0xff3b82f6 };
    inline const juce::Colour violet    { 0xff8b5cf6 };
    inline const juce::Colour purple    { 0xffb45cf6 };
    inline const juce::Colour magenta   { 0xffd946ef };
    inline const juce::Colour warm      { 0xffff9a57 };
    inline const juce::Colour good      { 0xff34d399 };
    inline const juce::Colour warn      { 0xfffbbf24 };
    inline const juce::Colour bad       { 0xfff87171 };
}

struct Fonts
{
    static juce::Font brand (float h);                 // Michroma, wide geometric
    static juce::Font title (float h, float tracking = 0.18f);   // section titles (Michroma, tracked)
    static juce::Font body (float h);                  // Inter regular
    static juce::Font medium (float h);                // Inter medium
    static juce::Font semi (float h);                  // Inter semibold
    static juce::Font arabic (float h);                // IBM Plex Sans Arabic
    static juce::Typeface::Ptr arabicTypeface();
};

// Append text to an AttributedString, switching to the Arabic face for Arabic-script runs so
// mixed English/Arabic messages render correctly (layout handles bidi).
void appendMixed (juce::AttributedString& as, const juce::String& text, const juce::Font& latin, juce::Colour colour);
bool containsArabic (const juce::String& s);

class NovaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NovaLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                            bool hasSubMenu, const juce::String& text, const juce::String& shortcut, const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbPos, int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 8; }
};

// Shared painting helpers
void paintGlassPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 14.f, float glow = 0.f);
void paintSectionTitle (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Colour c = Colours::textDim);
juce::ColourGradient accentGradient (juce::Point<float> a, juce::Point<float> b, float alpha = 1.f);
void strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float glowAmount = 1.f);

} // namespace nova::ui
