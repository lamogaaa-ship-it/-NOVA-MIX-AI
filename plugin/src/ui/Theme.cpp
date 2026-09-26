#include "Theme.h"

#include "NovaBinaryData.h"

namespace nova::ui
{

namespace
{
struct FaceCache
{
    juce::Typeface::Ptr michroma, interRegular, interMedium, interSemi, interBold, arabicRegular, arabicMedium;

    FaceCache()
    {
        auto load = [] (const char* data, int size) { return juce::Typeface::createSystemTypefaceFor (data, (size_t) size); };
        michroma = load (NovaBinary::MichromaRegular_ttf, NovaBinary::MichromaRegular_ttfSize);
        interRegular = load (NovaBinary::InterRegular_ttf, NovaBinary::InterRegular_ttfSize);
        interMedium = load (NovaBinary::InterMedium_ttf, NovaBinary::InterMedium_ttfSize);
        interSemi = load (NovaBinary::InterSemiBold_ttf, NovaBinary::InterSemiBold_ttfSize);
        interBold = load (NovaBinary::InterBold_ttf, NovaBinary::InterBold_ttfSize);
        arabicRegular = load (NovaBinary::IBMPlexSansArabicRegular_ttf, NovaBinary::IBMPlexSansArabicRegular_ttfSize);
        arabicMedium = load (NovaBinary::IBMPlexSansArabicMedium_ttf, NovaBinary::IBMPlexSansArabicMedium_ttfSize);
    }
    static FaceCache& get() { static FaceCache c; return c; }
};

juce::Font make (const juce::Typeface::Ptr& tf, float h, float tracking = 0.f)
{
    // Arabic falls back to the embedded IBM Plex Sans Arabic (registered by family name above)
    auto opts = juce::FontOptions (tf).withHeight (h).withKerningFactor (tracking)
                    .withFallbacks ({ "IBM Plex Sans Arabic" }).withFallbackEnabled (true);
    return juce::Font (opts);
}
} // namespace

juce::Font Fonts::brand (float h) { return make (FaceCache::get().michroma, h, 0.08f); }
juce::Font Fonts::title (float h, float tracking) { return make (FaceCache::get().michroma, h, tracking); }
juce::Font Fonts::body (float h) { return make (FaceCache::get().interRegular, h); }
juce::Font Fonts::medium (float h) { return make (FaceCache::get().interMedium, h); }
juce::Font Fonts::semi (float h) { return make (FaceCache::get().interSemi, h); }
juce::Font Fonts::arabic (float h) { return make (FaceCache::get().arabicRegular, h); }
juce::Typeface::Ptr Fonts::arabicTypeface() { return FaceCache::get().arabicRegular; }

bool containsArabic (const juce::String& s)
{
    for (auto p = s.getCharPointer(); ! p.isEmpty(); ++p)
        if ((*p >= 0x0600 && *p <= 0x06FF) || (*p >= 0x0750 && *p <= 0x077F) || (*p >= 0xFB50 && *p <= 0xFEFF)) return true;
    return false;
}

void appendMixed (juce::AttributedString& as, const juce::String& text, const juce::Font& latin, juce::Colour colour)
{
    if (! containsArabic (text)) { as.append (text, latin, colour); return; }
    const auto arabicFont = Fonts::arabic (latin.getHeight() * 1.04f);
    juce::String run;
    bool runArabic = false, first = true;
    auto flush = [&] { if (run.isNotEmpty()) as.append (run, runArabic ? arabicFont : latin, colour); run.clear(); };
    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;
        const bool isAr = (c >= 0x0600 && c <= 0x06FF) || (c >= 0x0750 && c <= 0x077F) || (c >= 0xFB50 && c <= 0xFEFF);
        const bool neutral = c == ' ' || c == ',' || c == '.' || c == '-' || c == ':' || juce::CharacterFunctions::isDigit (c);
        if (first) { runArabic = isAr; first = false; }
        if (! neutral && isAr != runArabic) { flush(); runArabic = isAr; }
        run += juce::String::charToString (c);
    }
    flush();
}

//==============================================================================
juce::ColourGradient accentGradient (juce::Point<float> a, juce::Point<float> b, float alpha)
{
    juce::ColourGradient g (Colours::cyan.withMultipliedAlpha (alpha), a, Colours::purple.withMultipliedAlpha (alpha), b, false);
    g.addColour (0.5, Colours::blue.withMultipliedAlpha (alpha));
    return g;
}

void strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float glow)
{
    if (glow > 0.f)
    {
        g.setColour (c.withMultipliedAlpha (0.10f * glow));
        g.strokePath (p, juce::PathStrokeType (width * 5.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withMultipliedAlpha (0.22f * glow));
        g.strokePath (p, juce::PathStrokeType (width * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void paintGlassPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius, float glow)
{
    // soft shadow
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0, 3).expanded (1), radius);
    juce::ColourGradient fill (Colours::panelHi.withAlpha (0.92f), r.getX(), r.getY(), Colours::panel.withAlpha (0.94f), r.getX(), r.getBottom(), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (r, radius);
    // top sheen
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.045f), r.getX(), r.getY(), juce::Colours::transparentWhite, r.getX(), r.getY() + 60.f, false);
    g.setGradientFill (sheen);
    g.fillRoundedRectangle (r, radius);
    g.setColour (Colours::border);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.f);
    if (glow > 0.f)
    {
        g.setColour (Colours::cyan.withAlpha (0.18f * glow));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.4f);
    }
}

void paintSectionTitle (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Colour c)
{
    g.setColour (c);
    g.setFont (Fonts::title (12.5f, 0.32f));
    g.drawText (text, r, juce::Justification::centredLeft, false);
}

//==============================================================================
NovaLookAndFeel::NovaLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Colours::bg);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::highlightColourId, Colours::cyan.withAlpha (0.25f));
    setColour (juce::TextEditor::highlightedTextColourId, Colours::text);
    setColour (juce::CaretComponent::caretColourId, Colours::cyan);
    setColour (juce::Label::textColourId, Colours::textDim);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::blue.withAlpha (0.3f));
    setColour (juce::TooltipWindow::textColourId, Colours::text);
    setColour (juce::ScrollBar::thumbColourId, Colours::textMute);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ToggleButton::textColourId, Colours::textDim);
    setDefaultSansSerifTypeface (Fonts::body (14.f).getTypefacePtr());
}

void NovaLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const float trackW = std::max (2.f, radius * 0.12f);
    const bool enabled = s.isEnabled();

    juce::Path track;
    track.addCentredArc (c.x, c.y, radius - trackW, radius - trackW, 0.f, start, end, true);
    g.setColour (Colours::textMute.withAlpha (0.35f));
    g.strokePath (track, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc from the "zero" point for bipolar parameters
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float zeroPos = bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.f;
    const float a0 = start + zeroPos * (end - start);
    juce::Path val;
    val.addCentredArc (c.x, c.y, radius - trackW, radius - trackW, 0.f, std::min (a0, angle), std::max (a0, angle), true);
    if (enabled)
    {
        juce::ColourGradient grad (Colours::cyan, c.x - radius, c.y, Colours::purple, c.x + radius, c.y, false);
        g.setGradientFill (grad);
        g.strokePath (val, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // knob body
    const float kr = radius * 0.62f;
    juce::ColourGradient body (Colours::panelHi.brighter (0.25f), c.x, c.y - kr, Colours::bgDeep, c.x, c.y + kr, false);
    g.setGradientFill (body);
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    g.setColour (Colours::border.brighter (0.4f));
    g.drawEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2, 1.f);
    const auto tip = c.getPointOnCircumference (kr * 0.78f, angle);
    const auto base = c.getPointOnCircumference (kr * 0.25f, angle);
    g.setColour (enabled ? Colours::cyan : Colours::textMute);
    g.drawLine ({ base, tip }, 2.f);
    if (s.isMouseOverOrDragging() && enabled)
    {
        g.setColour (Colours::cyan.withAlpha (0.12f));
        g.fillEllipse (bounds.expanded (1));
    }
}

void NovaLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float cy = r.getCentreY();
    const auto track = juce::Rectangle<float> (r.getX(), cy - 2.f, r.getWidth(), 4.f);
    g.setColour (Colours::textMute.withAlpha (0.35f));
    g.fillRoundedRectangle (track, 2.f);
    auto filled = track.withRight (pos);
    g.setGradientFill (accentGradient ({ track.getX(), cy }, { track.getRight(), cy }, s.isEnabled() ? 1.f : 0.3f));
    g.fillRoundedRectangle (filled, 2.f);
    const float tr = s.isMouseOverOrDragging() ? 8.f : 7.f;
    g.setColour (Colours::cyan.withAlpha (0.25f));
    g.fillEllipse (pos - tr - 3, cy - tr - 3, (tr + 3) * 2, (tr + 3) * 2);
    juce::ColourGradient thumb (juce::Colours::white, pos, cy - tr, Colours::cyan, pos, cy + tr, false);
    g.setGradientFill (thumb);
    g.fillEllipse (pos - tr, cy - tr, tr * 2, tr * 2);
}

void NovaLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.f);
    const float rad = std::min (r.getHeight() * 0.5f, 12.f);
    const bool on = b.getToggleState();
    const bool enabled = b.isEnabled();
    juce::Colour fill = on ? Colours::blue.withAlpha (0.28f) : Colours::panelHi.withAlpha (0.7f);
    if (over && enabled) fill = fill.brighter (0.15f);
    if (down && enabled) fill = fill.darker (0.2f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, rad);
    if (on)
    {
        g.setColour (Colours::cyan.withAlpha (0.18f));
        g.drawRoundedRectangle (r.expanded (1.5f), rad + 1.5f, 3.f);
        g.setGradientFill (accentGradient (r.getTopLeft(), r.getBottomRight(), 0.95f));
        g.drawRoundedRectangle (r, rad, 1.4f);
    }
    else
    {
        g.setColour (over && enabled ? Colours::borderHi : Colours::border);
        g.drawRoundedRectangle (r, rad, 1.f);
    }
}

void NovaLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (! b.isEnabled() ? Colours::textMute : (b.getToggleState() ? Colours::text : Colours::textDim));
    g.drawText (b.getButtonText(), b.getLocalBounds().reduced (6, 0), juce::Justification::centred, true);
}

juce::Font NovaLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return Fonts::medium (std::min (15.f, h * 0.44f)); }
juce::Font NovaLookAndFeel::getLabelFont (juce::Label& l) { return Fonts::body ((float) std::min (15, std::max (11, l.getHeight() - 6))); }
juce::Font NovaLookAndFeel::getComboBoxFont (juce::ComboBox&) { return Fonts::medium (14.f); }
juce::Font NovaLookAndFeel::getPopupMenuFont() { return Fonts::body (14.f); }

void NovaLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    // pill switch + label
    auto r = b.getLocalBounds().toFloat();
    const auto sw = juce::Rectangle<float> (r.getX() + 2, r.getCentreY() - 9, 34, 18);
    const bool on = b.getToggleState();
    g.setColour (on ? Colours::blue.withAlpha (0.55f) : Colours::panelHi);
    g.fillRoundedRectangle (sw, 9.f);
    g.setColour (on ? Colours::cyan.withAlpha (0.7f) : (over ? Colours::borderHi : Colours::border));
    g.drawRoundedRectangle (sw, 9.f, 1.f);
    const float kx = on ? sw.getRight() - 16 : sw.getX() + 2;
    g.setColour (on ? juce::Colours::white : Colours::textDim);
    g.fillEllipse (kx, sw.getY() + 2, 14, 14);
    g.setColour (b.isEnabled() ? Colours::textDim : Colours::textMute);
    g.setFont (Fonts::medium (14.f));
    g.drawText (b.getButtonText(), r.withLeft (sw.getRight() + 8), juce::Justification::centredLeft, true);
}

void NovaLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (Colours::bgDeep.withAlpha (0.8f));
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 10.f);
}

void NovaLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& te)
{
    g.setColour (te.hasKeyboardFocus (true) ? Colours::cyan.withAlpha (0.55f) : Colours::border);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 10.f, 1.f);
}

void NovaLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& cb)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (Colours::panelHi.withAlpha (cb.isMouseOver() ? 0.95f : 0.75f));
    g.fillRoundedRectangle (r, 10.f);
    g.setColour (cb.isMouseOver() ? Colours::borderHi : Colours::border);
    g.drawRoundedRectangle (r, 10.f, 1.f);
    juce::Path arrow;
    const float ax = (float) w - 20.f, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 5, ay - 2.5f);
    arrow.lineTo (ax, ay + 2.5f);
    arrow.lineTo (ax + 5, ay - 2.5f);
    g.setColour (Colours::textDim);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void NovaLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (Colours::panel);
    g.setColour (Colours::border);
    g.drawRect (0, 0, w, h, 1);
}

void NovaLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                         bool isTicked, bool, const juce::String& text, const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (Colours::border);
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    if (isHighlighted && isActive)
    {
        g.setColour (Colours::blue.withAlpha (0.25f));
        g.fillRect (area);
    }
    g.setColour (isActive ? (isTicked ? Colours::cyan : Colours::text) : Colours::textMute);
    g.setFont (getPopupMenuFont());
    g.drawText ((isTicked ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93  ")) : juce::String ("     ")) + text, area.reduced (10, 0), juce::Justification::centredLeft, true);
}

void NovaLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (Colours::panelHi);
    g.setColour (Colours::borderHi);
    g.drawRect (0, 0, w, h, 1);
    juce::AttributedString as;
    appendMixed (as, text, Fonts::body (13.f), Colours::text);
    juce::TextLayout tl;
    tl.createLayout (as, (float) w - 16.f);
    tl.draw (g, juce::Rectangle<float> (8, 6, (float) w - 16, (float) h - 12));
}

juce::Rectangle<int> NovaLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    juce::AttributedString as;
    appendMixed (as, tipText, Fonts::body (13.f), Colours::text);
    juce::TextLayout tl;
    tl.createLayout (as, 320.f);
    const int w = (int) std::ceil (std::min (320.f, tl.getWidth())) + 18, h = (int) std::ceil (tl.getHeight()) + 14;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void NovaLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbPos, int thumbSize, bool over, bool)
{
    auto thumb = vertical ? juce::Rectangle<int> (x + w / 2 - 2, thumbPos, 4, thumbSize) : juce::Rectangle<int> (thumbPos, y + h / 2 - 2, thumbSize, 4);
    g.setColour ((over ? Colours::textDim : Colours::textMute).withAlpha (0.6f));
    g.fillRoundedRectangle (thumb.toFloat(), 2.f);
}

} // namespace nova::ui
