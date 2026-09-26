#include "Panels.h"
#include "../dsp/NovaChain.h"

namespace nova::ui
{

namespace
{
struct CardInfo { const char* name; const char* sub; Icon icon; Module module; };
const CardInfo kCards[] = {
    { "Level", "Auto Gain", Icon::Level, Module::Level },
    { "EQ", "Balance", Icon::Eq, Module::EQ },
    { "Comp", "Control", Icon::Comp, Module::Comp },
    { "De-ess", "Clarity", Icon::DeEss, Module::DeEss },
    { "Color", "Character", Icon::Color, Module::Color },
    { "Space", "Depth", Icon::Space, Module::Space },
};
} // namespace

ChainView::ChainView (NovaAudioProcessor& p) : proc (p)
{
    aiSeg.setClickingTogglesState (false);
    customSeg.setClickingTogglesState (false);
    aiSeg.setToggleState (true, juce::dontSendNotification);
    aiSeg.onClick = [this] { aiSeg.setToggleState (true, juce::dontSendNotification); customSeg.setToggleState (false, juce::dontSendNotification); if (onModeChange) onModeChange (false, -1); };
    customSeg.onClick = [this] { customSeg.setToggleState (true, juce::dontSendNotification); aiSeg.setToggleState (false, juce::dontSendNotification); if (onModeChange) onModeChange (true, -1); };
    aiSeg.setTooltip ("Simple view: the chain NOVA built, with live activity");
    customSeg.setTooltip ("Advanced view: every parameter, fully editable");
    addAndMakeVisible (aiSeg);
    addAndMakeVisible (customSeg);

    aBtn.setTooltip ("A: the original input (latency aligned, loudness matched to B when Match is on)");
    bBtn.setTooltip ("B: NOVA's processed signal");
    aBtn.onClick = [this] { proc.setParameterValue (P::MonitorA, 1.f); };
    bBtn.onClick = [this] { proc.setParameterValue (P::MonitorA, 0.f); };
    aBtn.setTextHeight (17.f);
    bBtn.setTextHeight (17.f);
    addAndMakeVisible (aBtn);
    addAndMakeVisible (bBtn);
    deltaToggle.setTooltip ("Delta: hear only what NOVA changed (B minus A)");
    lmToggle.setTooltip ("Loudness-matched A/B so louder never wins");
    addAndMakeVisible (deltaToggle);
    addAndMakeVisible (lmToggle);
    deltaAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.getAPVTS(), kParams[(size_t) P::Delta].id, deltaToggle);
    lmAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.getAPVTS(), kParams[(size_t) P::LoudMatch].id, lmToggle);
}

int ChainView::cardOnParam (int i) const
{
    switch (i)
    {
        case Level: return P::LvlOn;
        case Eq:    return P::EqOn;
        case Comp:  return P::CompOn;
        case DeEss: return P::DessOn;
        case Color: return P::ColOn;
        case Space: return P::SpcOn;
        default:    return P::EqOn;
    }
}

void ChainView::tick (double dt)
{
    t += dt;
    s = proc.getCurrentSettings();
    const auto& m = proc.getMeters();
    gr[Level] = m.riderGainDb.load();
    gr[Comp] = m.compGrDb.load();
    gr[DeEss] = m.dessGrDb.load();
    gr[Eq] = std::max (m.dynCutDb[0].load(), m.dynCutDb[1].load());
    const bool monA = s.on (P::MonitorA);
    aBtn.setToggleState (monA, juce::dontSendNotification);
    bBtn.setToggleState (! monA, juce::dontSendNotification);

    // "AI" badge only on modules the last (not undone) AI action actually changed
    aiTouched.fill (false);
    if (auto last = proc.getEngine().getMemory().last())
        for (auto& c : last->changes)
        {
            const auto mod = kParams[(size_t) c.index].module;
            for (int i = 0; i < NumCards; ++i)
                if (kCards[i].module == mod || (i == Eq && (mod == Module::DynEQ || mod == Module::ToneMatch))) aiTouched[(size_t) i] = true;
        }
    repaint();
}

void ChainView::resized()
{
    auto r = getLocalBounds().toFloat().reduced (18.f, 12.f);
    auto top = r.removeFromTop (30.f);
    auto seg = top.removeFromRight (150.f).withSizeKeepingCentre (150.f, 28.f);
    aiSeg.setBounds (seg.removeFromLeft (75.f).toNearestInt());
    customSeg.setBounds (seg.toNearestInt());
    r.removeFromTop (10.f);
    auto ab = r.removeFromRight (150.f);
    ab.removeFromTop (6.f);
    auto abTitle = ab.removeFromTop (26.f);
    juce::ignoreUnused (abTitle);
    auto abRow = ab.removeFromTop (46.f);
    aBtn.setBounds (abRow.removeFromLeft (66.f).toNearestInt());
    abRow.removeFromLeft (10.f);
    bBtn.setBounds (abRow.removeFromLeft (66.f).toNearestInt());
    ab.removeFromTop (12.f);
    deltaToggle.setBounds (ab.removeFromTop (28.f).toNearestInt());
    ab.removeFromTop (4.f);
    lmToggle.setBounds (ab.removeFromTop (28.f).toNearestInt());
    r.removeFromRight (24.f);
    const float gap = 36.f;
    const float cw = (r.getWidth() - gap * (NumCards - 1)) / NumCards;
    for (int i = 0; i < NumCards; ++i)
        cards[(size_t) i] = juce::Rectangle<float> (r.getX() + i * (cw + gap), r.getY(), cw, r.getHeight());
}

void ChainView::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < NumCards; ++i) if (cards[(size_t) i].contains (e.position)) h = i;
    if (h != hovered) { hovered = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void ChainView::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < NumCards; ++i)
    {
        const auto r = cards[(size_t) i];
        if (! r.contains (e.position)) continue;
        const auto power = juce::Rectangle<float> (r.getRight() - 34, r.getY() + 10, 24, 24);
        if (power.contains (e.position))
        {
            const int param = cardOnParam (i);
            proc.setParameterValue (param, s.on (param) ? 0.f : 1.f);
        }
        else if (onModeChange)
        {
            aiSeg.setToggleState (false, juce::dontSendNotification);
            customSeg.setToggleState (true, juce::dontSendNotification);
            onModeChange (true, (int) kCards[i].module);
        }
        return;
    }
}

void ChainView::paint (juce::Graphics& g)
{
    paintGlassPanel (g, getLocalBounds().toFloat().reduced (1.f), 16.f);
    auto r = getLocalBounds().toFloat().reduced (18.f, 12.f);
    paintSectionTitle (g, "AI MIX CHAIN", r.removeFromTop (30.f));

    // connectors (energy flows while the engine is processing)
    const bool flowing = proc.getEngine().getPhase() == ai::AgentPhase::Processing;
    for (int i = 0; i + 1 < NumCards; ++i)
    {
        const auto a = cards[(size_t) i], b = cards[(size_t) i + 1];
        const float y = a.getCentreY();
        juce::Path link;
        link.startNewSubPath (a.getRight() + 2, y);
        link.lineTo (b.getX() - 2, y);
        strokeGlow (g, link, Colours::cyan.withAlpha (0.8f), 1.5f, 0.7f);
        for (auto x : { a.getRight() + 4.f, b.getX() - 4.f })
        {
            g.setColour (Colours::cyan.withAlpha (0.3f));
            g.fillEllipse (x - 5, y - 5, 10, 10);
            g.setColour (Colours::cyan);
            g.fillEllipse (x - 2.5f, y - 2.5f, 5, 5);
        }
        if (flowing)
        {
            const float ph = (float) std::fmod (t * 1.5 + i * 0.2, 1.0);
            const float x = a.getRight() + ph * (b.getX() - a.getRight());
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillEllipse (x - 2, y - 2, 4, 4);
        }
    }
    for (int i = 0; i < NumCards; ++i) paintCard (g, i, cards[(size_t) i]);

    // A/B title + matching readout
    const auto abX = (float) aBtn.getX();
    g.setColour (Colours::textDim);
    g.setFont (Fonts::title (12.f, 0.3f));
    g.drawText ("A / B", juce::Rectangle<float> (abX, (float) aBtn.getY() - 30, 142, 24), juce::Justification::centred, false);
    if (s.on (P::LoudMatch) && s.on (P::MonitorA))
    {
        g.setFont (Fonts::body (12.f));
        g.setColour (Colours::textMute);
        g.drawText ("A matched " + formatDb (proc.getMeters().matchGainDb.load()), juce::Rectangle<float> (abX - 10, (float) lmToggle.getBottom() + 2, 170, 18),
                    juce::Justification::centredLeft, false);
    }
}

void ChainView::paintCard (juce::Graphics& g, int i, juce::Rectangle<float> r)
{
    const auto& info = kCards[i];
    const bool on = s.on (cardOnParam (i)) && ! (i == Eq && ! s.on (P::EqOn));
    const bool hover = hovered == i;
    paintGlassPanel (g, r, 12.f, hover ? 0.8f : 0.f);
    const float alpha = on ? 1.f : 0.45f;

    auto head = r.reduced (12.f, 10.f).removeFromTop (40.f);
    auto iconR = head.removeFromLeft (34.f).withSizeKeepingCentre (34, 34);
    g.setColour (Colours::bgDeep);
    g.fillEllipse (iconR);
    g.setColour (Colours::border);
    g.drawEllipse (iconR, 1.f);
    drawIcon (g, info.icon, iconR.reduced (8), Colours::cyan.withMultipliedAlpha (alpha));
    head.removeFromLeft (10.f);
    g.setColour (Colours::text.withMultipliedAlpha (alpha));
    g.setFont (Fonts::semi (16.f));
    g.drawText (info.name, head.removeFromTop (21.f), juce::Justification::bottomLeft, false);
    g.setColour (Colours::textDim.withMultipliedAlpha (alpha));
    g.setFont (Fonts::body (13.f));
    g.drawText (info.sub, head, juce::Justification::topLeft, false);

    // power toggle
    const auto power = juce::Rectangle<float> (r.getRight() - 34, r.getY() + 10, 24, 24);
    drawIcon (g, Icon::Power, power.reduced (3), on ? Colours::cyan : Colours::textMute);

    auto viz = r.reduced (12.f, 10.f).withTrimmedTop (48.f).withTrimmedBottom (22.f);
    g.saveState();
    g.reduceClipRegion (viz.toNearestInt());
    const auto col1 = Colours::cyan.withMultipliedAlpha (alpha), col2 = Colours::purple.withMultipliedAlpha (alpha);

    switch (i)
    {
        case Level:
        {
            const float range = std::max (1.f, s[P::LvlRange]);
            const float v = juce::jlimit (-1.f, 1.f, gr[Level] / range);
            const auto c = viz.getCentre().translated (0, -6);
            const float rad = std::min (viz.getWidth(), viz.getHeight()) * 0.36f;
            const float a0 = -2.4f, a1 = 2.4f, mid = 0.f;
            juce::Path track; track.addCentredArc (c.x, c.y, rad, rad, 0, a0, a1, true);
            g.setColour (Colours::textMute.withAlpha (0.35f));
            g.strokePath (track, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path val; val.addCentredArc (c.x, c.y, rad, rad, 0, std::min (mid, v * a1), std::max (mid, v * a1), true);
            g.setGradientFill (accentGradient ({ c.x - rad, c.y }, { c.x + rad, c.y }, alpha));
            g.strokePath (val, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (Colours::bgDeep);
            g.fillEllipse (juce::Rectangle<float> (rad * 1.3f, rad * 1.3f).withCentre (c));
            const auto tip = c.getPointOnCircumference (rad * 0.55f, v * a1);
            g.setColour (col1);
            g.drawLine ({ c, tip }, 2.f);
            break;
        }
        case Eq:
        {
            const double sr = std::max (44100.0, proc.getPreparedSampleRate());
            juce::Path curve;
            for (int k = 0; k <= 96; ++k)
            {
                const float f = 20.f * std::pow (1000.f, k / 96.f);
                const float db = (float) dsp::NovaChain::staticResponseDb (s, sr, f);
                const float x = viz.getX() + viz.getWidth() * k / 96.f;
                const float y = viz.getCentreY() - juce::jlimit (-15.f, 15.f, db) / 15.f * viz.getHeight() * 0.45f;
                if (k == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
            }
            g.setColour (Colours::border);
            g.drawHorizontalLine ((int) viz.getCentreY(), viz.getX(), viz.getRight());
            juce::Path fill (curve);
            fill.lineTo (viz.getRight(), viz.getBottom()); fill.lineTo (viz.getX(), viz.getBottom()); fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (Colours::cyan.withAlpha (0.18f * alpha), viz.getX(), viz.getY(), juce::Colours::transparentBlack, viz.getX(), viz.getBottom(), false));
            g.fillPath (fill);
            juce::ColourGradient cg (col1, viz.getX(), 0, col2, viz.getRight(), 0, false);
            cg.addColour (0.6, Colours::warn.withMultipliedAlpha (alpha * 0.9f));
            g.setGradientFill (cg);
            g.strokePath (curve, juce::PathStrokeType (2.f));
            // dynamic bands: marker + live cut
            for (int b = 0; b < kNumDynBands; ++b)
            {
                if (! (s.on (P::DeqOn) && s.on (dynBandParam (b, 0)))) continue;
                const float f = s[dynBandParam (b, 1)];
                const float x = viz.getX() + viz.getWidth() * std::log (f / 20.f) / std::log (1000.f);
                const float cut = proc.getMeters().dynCutDb[b].load();
                g.setColour (Colours::purple.withAlpha (0.7f));
                g.drawVerticalLine ((int) x, viz.getCentreY(), viz.getCentreY() + cut / 15.f * viz.getHeight() * 0.45f + 1.f);
                g.fillEllipse (x - 3, viz.getCentreY() + cut / 15.f * viz.getHeight() * 0.45f - 3, 6, 6);
            }
            break;
        }
        case Comp:
        {
            const float T = s[P::CompThresh], R = s[P::CompRatio], W = s[P::CompKnee];
            auto map = [&] (float inDb, float outDb) { return juce::Point<float> (viz.getX() + (inDb + 60.f) / 60.f * viz.getWidth(), viz.getBottom() - (outDb + 60.f) / 60.f * viz.getHeight()); };
            g.setColour (Colours::border);
            g.drawLine (juce::Line<float> (map (-60, -60), map (0, 0)), 1.f);
            juce::Path p;
            for (int k = 0; k <= 60; ++k)
            {
                const float in = -60.f + k;
                const float out = in - dsp::Compressor::gainReductionDb (in, T, R, W);
                const auto pt = map (in, out);
                if (k == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            g.setGradientFill (accentGradient (viz.getBottomLeft(), viz.getTopRight(), alpha));
            g.strokePath (p, juce::PathStrokeType (2.f));
            if (on && gr[Comp] > 0.05f)
            {
                g.setColour (Colours::warn);
                g.drawText ("GR " + juce::String (gr[Comp], 1), viz.removeFromTop (16.f), juce::Justification::centredRight, false);
            }
            break;
        }
        case DeEss:
        {
            const float f = s[P::DessFreq], range = s[P::DessRange];
            const float x0 = viz.getX() + viz.getWidth() * std::log (f / 2000.f) / std::log (8.f);
            auto shape = [&] (float depthDb)
            {
                juce::Path p;
                for (int k = 0; k <= 60; ++k)
                {
                    const float x = viz.getX() + viz.getWidth() * k / 60.f;
                    const float d = (x - x0) / (viz.getWidth() * 0.18f);
                    const float y = viz.getY() + viz.getHeight() * 0.25f + depthDb / 20.f * viz.getHeight() * 0.7f * std::exp (-d * d);
                    if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                return p;
            };
            g.setColour (Colours::textMute.withAlpha (0.5f));
            g.strokePath (shape (range), juce::PathStrokeType (1.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setGradientFill (accentGradient (viz.getTopLeft(), viz.getTopRight(), alpha));
            g.strokePath (shape (std::max (0.3f, gr[DeEss])), juce::PathStrokeType (2.f));
            break;
        }
        case Color:
        {
            const int type = s.choice (P::ColType);
            const float d = dsp::dbToGain (s[P::ColDrive]);
            juce::Path p;
            for (int k = 0; k <= 60; ++k)
            {
                const float x = -1.f + 2.f * k / 60.f;
                const float y = dsp::ColorModule::shape (type, x, d) * d * 0.9f;
                const auto pt = juce::Point<float> (viz.getCentreX() + x * viz.getWidth() * 0.45f, viz.getCentreY() - juce::jlimit (-1.2f, 1.2f, y) * viz.getHeight() * 0.42f);
                if (k == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            g.setColour (Colours::border);
            g.drawLine (viz.getCentreX(), viz.getY(), viz.getCentreX(), viz.getBottom(), 1.f);
            g.setGradientFill (juce::ColourGradient (Colours::warm.withMultipliedAlpha (alpha), viz.getX(), 0, Colours::cyan.withMultipliedAlpha (alpha), viz.getRight(), 0, false));
            g.strokePath (p, juce::PathStrokeType (2.f));
            break;
        }
        case Space:
        {
            const float pre = s[P::SpcPreDelay], decay = s[P::SpcDecay], mix = s[P::SpcRevMix] * 0.01f;
            const float totalMs = std::max (600.f, decay * 1000.f * 0.7f + pre);
            juce::Path p;
            p.startNewSubPath (viz.getX(), viz.getBottom());
            for (int k = 0; k <= 80; ++k)
            {
                const float ms = totalMs * k / 80.f;
                const float env = ms < pre ? 0.f : (float) std::pow (10.0, -3.0 * (ms - pre) / (decay * 1000.0)) * (0.35f + 0.65f * mix);
                p.lineTo (viz.getX() + viz.getWidth() * k / 80.f, viz.getBottom() - env * viz.getHeight() * 0.95f);
            }
            p.lineTo (viz.getRight(), viz.getBottom());
            p.closeSubPath();
            g.setGradientFill (juce::ColourGradient (Colours::violet.withAlpha (0.55f * alpha), viz.getX(), viz.getY(), Colours::blue.withAlpha (0.05f), viz.getRight(), viz.getBottom(), false));
            g.fillPath (p);
            if (s[P::SpcDlyMix] > 0.5f)
            {
                const float dms = (float) dsp::SpaceModule::delayTimeMs (s, proc.getTransport().bpm);
                float lvl = s[P::SpcDlyMix] * 0.01f;
                for (float ms = dms; ms < totalMs; ms += dms, lvl *= s[P::SpcDlyFeedback] * 0.01f)
                {
                    const float x = viz.getX() + viz.getWidth() * ms / totalMs;
                    g.setColour (Colours::cyan.withAlpha (0.8f * alpha));
                    g.drawLine (x, viz.getBottom(), x, viz.getBottom() - lvl * viz.getHeight(), 1.5f);
                }
            }
            break;
        }
        default: break;
    }
    g.restoreState();

    // footer: AI badge (only if the AI changed this module) or value readout
    auto foot = r.reduced (12.f, 8.f).removeFromBottom (18.f);
    g.setFont (Fonts::title (10.f, 0.18f));
    if (aiTouched[(size_t) i])
    {
        g.setColour (Colours::cyan);
        g.drawText ("AI", foot, juce::Justification::centred, false);
    }
    else
    {
        g.setColour (Colours::textMute);
        juce::String v;
        switch (i)
        {
            case Level: v = on ? "RIDING" : "OFF"; break;
            case Comp: v = on ? juce::String (s[P::CompRatio], 1) + ":1" : "OFF"; break;
            case DeEss: v = on ? formatHz (s[P::DessFreq]) : "OFF"; break;
            case Color: v = on ? juce::String (choiceName (kParams[(size_t) P::ColType], s.choice (P::ColType)).data()) : "OFF"; break;
            case Space: v = on ? juce::String (s[P::SpcDecay], 1) + " s" : "OFF"; break;
            default: v = on ? "ON" : "OFF"; break;
        }
        g.drawText (v, foot, juce::Justification::centred, false);
    }
    if (i == Level)
    {
        g.setColour (Colours::text.withMultipliedAlpha (alpha));
        g.setFont (Fonts::semi (15.f));
        g.drawText (on ? formatDb (gr[Level]) : "0.0 dB", juce::Rectangle<float> (r.getX(), foot.getY() - 18.f, r.getWidth(), 18.f), juce::Justification::centred, false);
    }
}

} // namespace nova::ui
