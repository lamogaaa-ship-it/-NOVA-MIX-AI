#include "Widgets.h"

namespace nova::ui
{

juce::String formatHz (float hz)
{
    if (hz <= 0) return "--";
    return hz >= 1000.f ? juce::String (hz / 1000.f, hz >= 10000.f ? 1 : 1) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
}

juce::String formatDb (float db, int decimals)
{
    return (db > 0 ? "+" : "") + juce::String (db, decimals) + " dB";
}

//==============================================================================
NovaButton::NovaButton (const juce::String& text, std::optional<Icon> i, Style s) : juce::Button (text), icon (i), style (s)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void NovaButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.f);
    const bool on = getToggleState();
    const bool en = isEnabled();
    const float alpha = en ? 1.f : 0.38f;
    auto textCol = (on ? Colours::text : (over && en ? Colours::text : Colours::textDim)).withMultipliedAlpha (alpha);

    auto drawContent = [&] (juce::Rectangle<float> area, float th, juce::Justification just)
    {
        const auto text = getButtonText();
        if (icon.has_value() && text.isEmpty())
        {
            const float s = std::min (area.getWidth(), area.getHeight()) * 0.58f;
            drawIcon (g, *icon, area.withSizeKeepingCentre (s, s), textCol);
            return;
        }
        g.setFont (style == Style::Primary ? Fonts::title (th, 0.22f) : Fonts::medium (th));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
        const float iconS = icon.has_value() ? th * 1.35f : 0.f;
        const float gap = icon.has_value() ? th * 0.6f : 0.f;
        float x = just == juce::Justification::centred ? area.getCentreX() - (iconS + gap + tw) * 0.5f : area.getX() + 12.f;
        if (icon.has_value())
        {
            drawIcon (g, *icon, { x, area.getCentreY() - iconS * 0.5f, iconS, iconS }, on || style == Style::Primary ? Colours::cyan.withMultipliedAlpha (alpha) : textCol);
            x += iconS + gap;
        }
        g.setColour (textCol);
        g.drawText (text, juce::Rectangle<float> (x, area.getY(), tw + 4, area.getHeight()), juce::Justification::centredLeft, false);
    };

    switch (style)
    {
        case Style::Tab:
        {
            g.setColour ((on ? Colours::blue.withAlpha (0.20f) : Colours::panelHi.withAlpha (over ? 0.8f : 0.55f)).withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (r, 12.f);
            if (on)
            {
                g.setColour (Colours::cyan.withAlpha (0.16f));
                g.drawRoundedRectangle (r.expanded (2.f), 14.f, 4.f);
                g.setGradientFill (accentGradient (r.getTopLeft(), r.getBottomRight()));
                g.drawRoundedRectangle (r, 12.f, 1.6f);
            }
            else
            {
                g.setColour (over ? Colours::borderHi : Colours::border);
                g.drawRoundedRectangle (r, 12.f, 1.f);
            }
            if (down) { g.setColour (juce::Colours::black.withAlpha (0.15f)); g.fillRoundedRectangle (r, 12.f); }
            drawContent (r, textHeight > 0 ? textHeight : 17.f, juce::Justification::centred);
            break;
        }
        case Style::Primary:
        {
            const float rad = r.getHeight() * 0.5f;
            g.setColour (Colours::cyan.withAlpha ((0.10f + 0.12f * pulse) * alpha));
            g.fillRoundedRectangle (r.expanded (5.f), rad + 5.f);
            juce::ColourGradient fill (Colours::panelHi.brighter (0.1f), r.getX(), r.getY(), Colours::bgDeep, r.getX(), r.getBottom(), false);
            g.setGradientFill (fill);
            g.fillRoundedRectangle (r, rad);
            if (progress >= 0.f)
            {
                g.saveState();
                juce::Path clip; clip.addRoundedRectangle (r, rad);
                g.reduceClipRegion (clip);
                g.setGradientFill (accentGradient (r.getTopLeft(), r.getTopRight(), 0.35f));
                g.fillRect (r.withWidth (r.getWidth() * juce::jlimit (0.f, 1.f, progress)));
                g.restoreState();
            }
            g.setGradientFill (accentGradient (r.getTopLeft(), r.getBottomRight(), (over ? 1.f : 0.85f) * alpha));
            g.drawRoundedRectangle (r, rad, over ? 2.2f : 1.8f);
            if (down) { g.setColour (juce::Colours::black.withAlpha (0.2f)); g.fillRoundedRectangle (r, rad); }
            drawContent (subText.isNotEmpty() ? r.withTrimmedBottom (r.getHeight() * 0.28f) : r, textHeight > 0 ? textHeight : r.getHeight() * 0.3f, juce::Justification::centred);
            if (subText.isNotEmpty())
            {
                g.setColour (Colours::textDim.withMultipliedAlpha (alpha));
                g.setFont (Fonts::medium (12.f));
                g.drawText (subText, r.withTrimmedTop (r.getHeight() * 0.62f).withTrimmedBottom (6), juce::Justification::centred, false);
            }
            break;
        }
        case Style::Round:
        {
            const float d = std::min (r.getWidth(), r.getHeight());
            auto c = r.withSizeKeepingCentre (d, d);
            g.setColour (Colours::cyan.withAlpha ((on ? 0.25f : 0.08f + 0.1f * pulse) * alpha));
            g.fillEllipse (c.expanded (4));
            g.setColour (Colours::bgDeep);
            g.fillEllipse (c);
            g.setGradientFill (accentGradient (c.getTopLeft(), c.getBottomRight(), (over || on ? 1.f : 0.75f) * alpha));
            g.drawEllipse (c.reduced (1), on ? 2.4f : 1.8f);
            if (down) { g.setColour (juce::Colours::black.withAlpha (0.2f)); g.fillEllipse (c); }
            if (icon.has_value())
                drawIcon (g, *icon, c.withSizeKeepingCentre (d * 0.46f, d * 0.46f), (on ? Colours::cyan : Colours::text).withMultipliedAlpha (alpha), 1.1f);
            break;
        }
        case Style::Chip:
        {
            g.setColour (Colours::panelHi.withAlpha ((over ? 0.95f : 0.7f) * alpha));
            g.fillRoundedRectangle (r, 9.f);
            g.setColour ((over ? Colours::borderHi : Colours::border).withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (r, 9.f, 1.f);
            if (down) { g.setColour (juce::Colours::black.withAlpha (0.15f)); g.fillRoundedRectangle (r, 9.f); }
            juce::AttributedString as;
            appendMixed (as, juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x9c")) + getButtonText() + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x9d")),
                         Fonts::body (textHeight > 0 ? textHeight : 14.f), (over ? Colours::text : Colours::textDim).withMultipliedAlpha (alpha));
            as.setJustification (juce::Justification::centredLeft);
            as.setWordWrap (juce::AttributedString::none);
            juce::TextLayout tl;
            tl.createLayout (as, 2000.f);
            tl.draw (g, r.reduced (12.f, 0));
            break;
        }
        case Style::Segment:
        {
            g.setColour ((on ? Colours::blue.withAlpha (0.35f) : juce::Colours::transparentBlack).withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            if (on)
            {
                g.setGradientFill (accentGradient (r.getTopLeft(), r.getBottomRight(), alpha));
                g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.3f);
            }
            else if (over) { g.setColour (Colours::borderHi); g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.f); }
            drawContent (r, textHeight > 0 ? textHeight : 13.f, juce::Justification::centred);
            break;
        }
        case Style::IconOnly:
        {
            if (over || on || down)
            {
                g.setColour ((on ? Colours::blue.withAlpha (0.25f) : Colours::panelHi.withAlpha (down ? 1.f : 0.8f)).withMultipliedAlpha (alpha));
                g.fillRoundedRectangle (r, 9.f);
            }
            g.setColour ((on ? Colours::cyan.withAlpha (0.6f) : Colours::border).withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (r, 9.f, 1.f);
            drawContent (r, 14.f, juce::Justification::centred);
            break;
        }
        case Style::Pill:
        default:
        {
            const float rad = std::min (12.f, r.getHeight() * 0.5f);
            g.setColour ((on ? Colours::blue.withAlpha (0.3f) : Colours::panelHi.withAlpha (over ? 0.95f : 0.7f)).withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (r, rad);
            if (on)
            {
                g.setColour (Colours::cyan.withAlpha (0.14f));
                g.drawRoundedRectangle (r.expanded (1.5f), rad + 1.5f, 3.f);
                g.setGradientFill (accentGradient (r.getTopLeft(), r.getBottomRight(), alpha));
                g.drawRoundedRectangle (r, rad, 1.4f);
            }
            else
            {
                g.setColour ((over ? Colours::borderHi : Colours::border).withMultipliedAlpha (alpha));
                g.drawRoundedRectangle (r, rad, 1.f);
            }
            if (down) { g.setColour (juce::Colours::black.withAlpha (0.18f)); g.fillRoundedRectangle (r, rad); }
            drawContent (r, textHeight > 0 ? textHeight : 14.f, juce::Justification::centred);
            break;
        }
    }
}

//==============================================================================
float LevelMeter::toY (float db, float top, float bottom)
{
    // piecewise scale like the concept: 0, -6, -12, -24, -36, -48
    const float t = juce::jlimit (0.f, 1.f, (float) std::pow ((std::clamp (db, -60.f, 0.f) + 60.f) / 60.f, 1.6f));
    return bottom - t * (bottom - top);
}

void LevelMeter::tick (double dt)
{
    if (! source) return;
    const auto v = source();
    for (int c = 0; c < 2; ++c)
    {
        const float pk = juce::Decibels::gainToDecibels (v[(size_t) c], -100.f);
        const float rm = juce::Decibels::gainToDecibels (v[(size_t) c + 2], -100.f);
        peak[c] = pk > peak[c] ? pk : std::max (pk, peak[c] - (float) (26.0 * dt));   // 26 dB/s fall
        rms[c] = rms[c] + (float) std::min (1.0, dt * 8.0) * (rm - rms[c]);
        holdAge[c] += dt;
        if (pk >= hold[c]) { hold[c] = pk; holdAge[c] = 0; }
        else if (holdAge[c] > 1.5) hold[c] = std::max (peak[c], hold[c] - (float) (20.0 * dt));
    }
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float top = r.getY() + 8, bottom = r.getBottom() - 6;
    const float scaleW = 26.f;
    g.setFont (Fonts::medium (12.f));
    for (float db : { 0.f, -6.f, -12.f, -24.f, -36.f, -48.f })
    {
        const float y = toY (db, top, bottom);
        g.setColour (Colours::textMute);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (r.getX(), y - 7, scaleW - 4, 14), juce::Justification::centredRight, false);
        g.setColour (Colours::border.withAlpha (0.25f));
        g.drawHorizontalLine ((int) y, r.getX() + scaleW, r.getRight());
    }
    const float barW = std::min (14.f, (r.getWidth() - scaleW - 10) / 2.f - 4);
    for (int c = 0; c < 2; ++c)
    {
        const float x = r.getX() + scaleW + 6 + c * (barW + 10);
        auto bar = juce::Rectangle<float> (x, top, barW, bottom - top);
        g.setColour (Colours::bgDeep);
        g.fillRoundedRectangle (bar, 3.f);
        const float yP = toY (peak[c], top, bottom), yR = toY (rms[c], top, bottom);
        juce::ColourGradient grad (Colours::purple, x, top, Colours::cyan, x, bottom, false);
        grad.addColour (0.08, Colours::bad);
        grad.addColour (0.16, Colours::violet);
        g.setGradientFill (grad);
        g.setOpacity (0.45f);
        g.fillRoundedRectangle (bar.withTop (yP), 3.f);
        g.setOpacity (1.f);
        g.fillRoundedRectangle (bar.withTop (yR), 3.f);
        const float yH = toY (hold[c], top, bottom);
        g.setColour (hold[c] > -0.5f ? Colours::bad : Colours::text.withAlpha (0.85f));
        g.fillRect (bar.getX(), yH - 1, barW, 2.f);
        g.setColour (Colours::textMute);
        g.setFont (Fonts::medium (12.f));
        g.drawText (c == 0 ? "L" : "R", juce::Rectangle<float> (x - 4, bottom + 2, barW + 8, 14), juce::Justification::centred, false);
    }
}

void paintActivityBar (juce::Graphics& g, juce::Rectangle<float> r, float amount01, juce::Colour c)
{
    g.setColour (Colours::bgDeep);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (c);
    g.fillRoundedRectangle (r.withWidth (r.getWidth() * juce::jlimit (0.f, 1.f, amount01)), r.getHeight() * 0.5f);
}

} // namespace nova::ui
