#include "Panels.h"

namespace nova::ui
{

using analysis::AnalysisEngine;

AnalysisView::AnalysisView (NovaAudioProcessor& p) : proc (p), engine (p.getEngine())
{
    const char* names[] = { "Spectral", "Loudness", "Formant", "Width", "AI Focus" };
    for (int i = 0; i < 5; ++i)
    {
        tabs[(size_t) i] = std::make_unique<NovaButton> (names[i], std::nullopt, NovaButton::Style::Segment);
        tabs[(size_t) i]->setTextHeight (13.f);
        tabs[(size_t) i]->onClick = [this, i] { tab = i; for (int k = 0; k < 5; ++k) tabs[(size_t) k]->setToggleState (k == i, juce::dontSendNotification); repaint(); };
        addAndMakeVisible (*tabs[(size_t) i]);
    }
    tabs[0]->setToggleState (true, juce::dontSendNotification);
    specDry.fill (-100.f);
    specWet.fill (-100.f);
}

void AnalysisView::resized()
{
    auto r = getLocalBounds().toFloat().reduced (18.f, 12.f);
    auto top = r.removeFromTop (32.f);
    top.removeFromLeft (220.f);
    const float tw[] = { 84, 90, 84, 72, 86 };
    for (int i = 0; i < 5; ++i)
    {
        tabs[(size_t) i]->setBounds (top.removeFromLeft (tw[i]).reduced (2, 2).toNearestInt());
        top.removeFromLeft (6.f);
    }
    r.removeFromTop (8.f);
    side = r.removeFromRight (240.f);
    r.removeFromRight (14.f);
    plot = r;
}

void AnalysisView::tick (double dt)
{
    auto& a = engine.getAnalysis();
    a.getSpectra (specDry, specWet);
    loudTimer += dt;
    if (loudTimer >= 0.1)
    {
        loudTimer = 0;
        loudHistory.push_back (proc.getMeters().wetLoudnessDb.load());
        loudHistoryDry.push_back (proc.getMeters().dryLoudnessDb.load());
        if (loudHistory.size() > 300) { loudHistory.erase (loudHistory.begin()); loudHistoryDry.erase (loudHistoryDry.begin()); }
        live = a.getLiveInfo();
    }
    if (tab == 3)
        a.copyRecent (0.04, gonioDry, gonioWet, gonioSr);
    repaint();
}

float AnalysisView::xForFreq (float f, juce::Rectangle<float> r) const
{
    return r.getX() + r.getWidth() * std::log (juce::jlimit (20.f, 20000.f, f) / 20.f) / std::log (1000.f);
}

void AnalysisView::paint (juce::Graphics& g)
{
    paintGlassPanel (g, getLocalBounds().toFloat().reduced (1.f), 16.f);
    auto r = getLocalBounds().toFloat().reduced (18.f, 12.f);
    paintSectionTitle (g, "REAL-TIME ANALYSIS", r.removeFromTop (32.f).withWidth (220.f));
    switch (tab)
    {
        case 0: paintSpectrum (g, plot); break;
        case 1: paintLoudness (g, plot); break;
        case 2: paintFormant (g, plot); break;
        case 3: paintWidth (g, plot); break;
        case 4: paintFocus (g, plot); break;
        default: break;
    }
    paintSideCard (g, side);
}

static juce::Rectangle<float> gridArea (juce::Rectangle<float> r) { return r.withTrimmedBottom (22.f).withTrimmedLeft (4.f); }

void AnalysisView::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto a = gridArea (r);
    g.setColour (Colours::bgDeep.withAlpha (0.5f));
    g.fillRoundedRectangle (a, 8.f);
    g.setFont (Fonts::medium (12.f));
    for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
    {
        const float x = xForFreq (f, a);
        g.setColour (Colours::border.withAlpha (0.35f));
        g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        g.setColour (Colours::textMute);
        g.drawText (f >= 1000.f ? juce::String ((int) (f / 1000.f)) + "k" : juce::String ((int) f), juce::Rectangle<float> (x - 20, a.getBottom() + 4, 40, 16), juce::Justification::centred, false);
    }
    for (float db : { -24.f, -48.f, -72.f })
    {
        const float y = a.getY() + (-db / 96.f) * a.getHeight();
        g.setColour (Colours::border.withAlpha (0.25f));
        g.drawHorizontalLine ((int) y, a.getX(), a.getRight());
    }
    auto buildPath = [&] (const std::array<float, AnalysisEngine::kSpectrumBins>& spec, bool closed)
    {
        juce::Path p;
        for (int b = 0; b < AnalysisEngine::kSpectrumBins; ++b)
        {
            const float x = xForFreq (AnalysisEngine::spectrumBinFrequency (b), a);
            // tilt by +3 dB/oct so a balanced mix reads roughly flat, like pro analyzers
            const float tilt = 3.f * std::log2 (AnalysisEngine::spectrumBinFrequency (b) / 1000.f);
            const float db = juce::jlimit (-96.f, 0.f, spec[(size_t) b] + tilt);
            const float y = a.getY() + (-db / 96.f) * a.getHeight();
            if (b == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        if (closed) { p.lineTo (a.getRight(), a.getBottom()); p.lineTo (a.getX(), a.getBottom()); p.closeSubPath(); }
        return p;
    };
    // A (input) behind, B (processed) in front
    g.setColour (Colours::blue.withAlpha (0.18f));
    g.fillPath (buildPath (specDry, true));
    g.setColour (Colours::blue.withAlpha (0.45f));
    g.strokePath (buildPath (specDry, false), juce::PathStrokeType (1.f));
    juce::ColourGradient fill (Colours::cyan.withAlpha (0.55f), a.getX(), 0, Colours::purple.withAlpha (0.55f), a.getRight(), 0, false);
    fill.addColour (0.5, Colours::blue.withAlpha (0.5f));
    g.setGradientFill (fill);
    g.fillPath (buildPath (specWet, true));
    juce::ColourGradient line (Colours::cyan, a.getX(), 0, Colours::magenta, a.getRight(), 0, false);
    g.setGradientFill (line);
    g.strokePath (buildPath (specWet, false), juce::PathStrokeType (1.4f));

    // reference tonal balance (shape only), aligned to the processed spectrum between 200 Hz - 5 kHz
    if (auto ref = engine.getReferences().get(); ref != nullptr && engine.getReferences().getState() == reference::ReferenceManager::State::Ready)
    {
        double sumW = 0, sumR = 0; int nw = 0, nr = 0;
        for (int b = 0; b < AnalysisEngine::kSpectrumBins; ++b)
        {
            const float f = AnalysisEngine::spectrumBinFrequency (b);
            if (f > 200 && f < 5000) { sumW += specWet[(size_t) b] + 3.f * std::log2 (f / 1000.f); ++nw; }
        }
        for (int k = 0; k < analysis::kThirdOctaveBands; ++k)
        {
            const float f = analysis::kThirdOctaveCentres[(size_t) k];
            if (f > 200 && f < 5000) { sumR += ref->features.thirdOctaveDb[(size_t) k]; ++nr; }
        }
        if (nw > 0 && nr > 0 && sumW / nw > -90)
        {
            const float offset = (float) (sumW / nw - sumR / nr);
            juce::Path rp;
            bool started = false;
            for (int k = 0; k < analysis::kThirdOctaveBands; ++k)
            {
                const float f = analysis::kThirdOctaveCentres[(size_t) k];
                if (f < 30 || f > 18000 || ref->features.thirdOctaveDb[(size_t) k] < -80) continue;
                const float db = juce::jlimit (-96.f, 0.f, ref->features.thirdOctaveDb[(size_t) k] + offset);
                const auto pt = juce::Point<float> (xForFreq (f, a), a.getY() + (-db / 96.f) * a.getHeight());
                if (! started) { rp.startNewSubPath (pt); started = true; } else rp.lineTo (pt);
            }
            juce::Path dashed;
            const float dl[] = { 6.f, 4.f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, rp, dl, 2);
            g.setColour (Colours::warm.withAlpha (0.85f));
            g.fillPath (dashed);
            g.setFont (Fonts::body (12.f));
            g.drawText ("reference tone", a.reduced (8).removeFromTop (16), juce::Justification::topRight, false);
        }
    }
    g.setColour (Colours::textMute);
    g.setFont (Fonts::body (12.f));
    g.drawText ("A input", a.reduced (8).removeFromTop (16).translated (0, 18), juce::Justification::topRight, false);
    g.setColour (Colours::cyan.withAlpha (0.8f));
    g.drawText ("B processed", a.reduced (8).removeFromTop (16).translated (0, 34), juce::Justification::topRight, false);
}

void AnalysisView::paintLoudness (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto a = gridArea (r).withTrimmedRight (180.f);
    g.setColour (Colours::bgDeep.withAlpha (0.5f));
    g.fillRoundedRectangle (a, 8.f);
    auto yFor = [&] (float db) { return a.getY() + (-(juce::jlimit (-60.f, 0.f, db)) / 60.f) * a.getHeight(); };
    g.setFont (Fonts::medium (12.f));
    for (float db : { -6.f, -14.f, -23.f, -36.f, -48.f })
    {
        g.setColour (Colours::border.withAlpha (0.3f));
        g.drawHorizontalLine ((int) yFor (db), a.getX(), a.getRight());
        g.setColour (Colours::textMute);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (a.getX() + 4, yFor (db) - 14, 40, 14), juce::Justification::centredLeft, false);
    }
    auto draw = [&] (const std::vector<float>& h, juce::Colour c)
    {
        if (h.size() < 2) return;
        juce::Path p;
        for (size_t i = 0; i < h.size(); ++i)
        {
            const float x = a.getRight() - (float) (h.size() - 1 - i) / 300.f * a.getWidth();
            if (i == 0) p.startNewSubPath (x, yFor (h[i])); else p.lineTo (x, yFor (h[i]));
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.6f));
    };
    draw (loudHistoryDry, Colours::blue.withAlpha (0.5f));
    draw (loudHistory, Colours::cyan);
    auto info = gridArea (r).removeFromRight (170.f);
    const auto latest = engine.getAnalysis().getLatestResult();
    auto row = [&] (const juce::String& k, const juce::String& v)
    {
        auto rr = info.removeFromTop (26.f);
        g.setColour (Colours::textDim);
        g.setFont (Fonts::body (13.f));
        g.drawText (k, rr, juce::Justification::centredLeft, false);
        g.setColour (Colours::text);
        g.setFont (Fonts::semi (13.f));
        g.drawText (v, rr, juce::Justification::centredRight, false);
    };
    row ("Now (B, ~1.5 s)", juce::String (proc.getMeters().wetLoudnessDb.load(), 1) + " LUFS");
    row ("Now (A input)", juce::String (proc.getMeters().dryLoudnessDb.load(), 1) + " LUFS");
    if (latest != nullptr && latest->inputFeatures.valid)
    {
        row ("Listened integrated", juce::String (latest->inputFeatures.integratedLufs, 1) + " LUFS");
        row ("Loudness range", juce::String (latest->inputFeatures.lra, 1) + " LU");
        row ("True peak", juce::String (latest->inputFeatures.truePeakDb, 1) + " dBTP");
        row ("Crest factor", juce::String (latest->inputFeatures.crestDb, 1) + " dB");
    }
    else row ("Listened", "press LISTEN");
    if (proc.getCurrentSettings().on (P::LoudMatch))
        row ("A/B match", formatDb (proc.getMeters().matchGainDb.load()));
}

void AnalysisView::paintFormant (juce::Graphics& g, juce::Rectangle<float> r)
{
    paintSpectrum (g, r);
    auto a = gridArea (r);
    const auto latest = engine.getAnalysis().getLatestResult();
    auto marker = [&] (float f, const juce::String& label, juce::Colour c, int row)
    {
        if (f <= 20.f) return;
        const float x = xForFreq (f, a);
        g.setColour (c.withAlpha (0.8f));
        g.drawVerticalLine ((int) x, a.getY() + 20, a.getBottom());
        g.setFont (Fonts::semi (12.f));
        g.drawText (label + " " + formatHz (f), juce::Rectangle<float> (x + 4, a.getY() + 6 + row * 16, 130, 16), juce::Justification::centredLeft, false);
    };
    marker (live.fundamentalHz, "f0", Colours::good, 0);
    if (latest != nullptr && latest->inputFeatures.formantConfidence > 0.2f)
    {
        marker (latest->inputFeatures.f1Hz, "F1", Colours::warm, 1);
        marker (latest->inputFeatures.f2Hz, "F2", Colours::warm, 2);
        marker (latest->inputFeatures.f3Hz, "F3", Colours::warm, 3);
    }
    marker (live.sibilanceHz, "S", Colours::magenta, 4);
}

void AnalysisView::paintWidth (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto a = gridArea (r);
    const float d = std::min (a.getWidth() * 0.5f, a.getHeight());
    auto gon = juce::Rectangle<float> (d, d).withCentre ({ a.getX() + d * 0.6f, a.getCentreY() });
    g.setColour (Colours::bgDeep.withAlpha (0.5f));
    g.fillEllipse (gon);
    g.setColour (Colours::border);
    g.drawEllipse (gon, 1.f);
    g.drawLine (gon.getCentreX(), gon.getY(), gon.getCentreX(), gon.getBottom(), 1.f);
    g.drawLine (gon.getX(), gon.getCentreY(), gon.getRight(), gon.getCentreY(), 1.f);
    double sLL = 0, sRR = 0, sLR = 0;
    const int n = gonioWet.getNumSamples();
    if (n > 0 && gonioWet.getNumChannels() >= 2)
    {
        float mx = 1e-4f;
        for (int i = 0; i < n; ++i) mx = std::max ({ mx, std::abs (gonioWet.getSample (0, i)), std::abs (gonioWet.getSample (1, i)) });
        const float k = 0.45f * d / mx;
        g.setColour (Colours::cyan.withAlpha (0.55f));
        for (int i = 0; i < n; i += 2)
        {
            const float L = gonioWet.getSample (0, i), R = gonioWet.getSample (1, i);
            sLL += L * L; sRR += R * R; sLR += L * R;
            const float x = (L - R) * 0.7071f * k, y = (L + R) * 0.7071f * k;
            g.fillRect (gon.getCentreX() + x, gon.getCentreY() - y, 1.6f, 1.6f);
        }
    }
    const float corr = (sLL > 0 && sRR > 0) ? (float) (sLR / std::sqrt (sLL * sRR)) : 1.f;
    auto info = a.withTrimmedLeft (d * 1.3f);
    g.setColour (Colours::textDim);
    g.setFont (Fonts::medium (14.f));
    g.drawText ("Correlation", info.removeFromTop (24.f), juce::Justification::centredLeft, false);
    auto bar = info.removeFromTop (14.f).withWidth (std::min (260.f, info.getWidth()));
    g.setColour (Colours::bgDeep);
    g.fillRoundedRectangle (bar, 7.f);
    const float x = bar.getX() + (corr + 1.f) * 0.5f * bar.getWidth();
    g.setColour (corr < 0 ? Colours::bad : Colours::cyan);
    g.fillEllipse (x - 6, bar.getCentreY() - 6, 12, 12);
    g.setColour (Colours::text);
    g.setFont (Fonts::semi (14.f));
    g.drawText (juce::String (corr, 2), info.removeFromTop (26.f), juce::Justification::centredLeft, false);
    const auto latest = engine.getAnalysis().getLatestResult();
    if (latest != nullptr && latest->inputFeatures.valid)
    {
        g.setColour (Colours::textDim);
        g.setFont (Fonts::body (13.f));
        g.drawFittedText ("Listened input: side/mid " + juce::String (latest->inputFeatures.sideToMidDb, 1) + " dB, mono sum "
                          + formatDb (latest->inputFeatures.monoLossDb) + (latest->inputFeatures.isEffectivelyMono ? " (mono source)" : ""),
                          info.removeFromTop (40.f).toNearestInt(), juce::Justification::topLeft, 2);
    }
}

void AnalysisView::paintFocus (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto a = gridArea (r);
    const auto latest = engine.getAnalysis().getLatestResult();
    if (latest == nullptr || ! latest->inputFeatures.valid)
    {
        g.setColour (Colours::textMute);
        g.setFont (Fonts::medium (15.f));
        g.drawText ("Press LISTEN - NOVA's focus appears here once it has measured your audio.", a, juce::Justification::centred, true);
        return;
    }
    int shown = 0;
    for (auto& p : latest->semantic.problems)
    {
        if (shown >= 5) break;
        auto row = a.removeFromTop (34.f);
        a.removeFromTop (4.f);
        g.setColour (Colours::text);
        g.setFont (Fonts::semi (14.f));
        g.drawText (juce::String (p.title), row.removeFromTop (18.f).withTrimmedRight (200.f), juce::Justification::centredLeft, true);
        g.setColour (Colours::textMute);
        g.setFont (Fonts::body (12.f));
        g.drawText (juce::String (p.evidence), row.withTrimmedRight (200.f), juce::Justification::centredLeft, true);
        auto bars = juce::Rectangle<float> (a.getRight() - 190.f, row.getY() - 14.f, 190.f, 12.f);
        paintActivityBar (g, bars.withWidth (90.f), p.severity, Colours::warm.withAlpha (0.9f));
        paintActivityBar (g, bars.withX (bars.getX() + 100.f).withWidth (90.f), p.confidence, Colours::cyan.withAlpha (0.9f));
        ++shown;
    }
    if (shown == 0)
    {
        g.setColour (Colours::textDim);
        g.setFont (Fonts::medium (15.f));
        g.drawText ("Nothing measurable stands out. Tell NOVA what you'd like to change.", a, juce::Justification::centred, true);
    }
    else
    {
        g.setColour (Colours::textMute);
        g.setFont (Fonts::body (11.5f));
        g.drawText ("severity    confidence", juce::Rectangle<float> (r.getRight() - 190.f, r.getBottom() - 18.f, 190.f, 16.f), juce::Justification::centredLeft, false);
    }
}

void AnalysisView::paintSideCard (juce::Graphics& g, juce::Rectangle<float> r)
{
    paintGlassPanel (g, r, 12.f);
    auto c = r.reduced (16.f, 14.f);
    auto head = c.removeFromTop (30.f);
    juce::String title = "No signal";
    if (live.hasSignal)
        title = live.vocalDetected ? "Vocal Detected" : (live.sourceName == "full_mix" ? "Full Mix Detected" : "Signal: " + live.sourceName.replace ("_", " "));
    else if (engine.getAnalysis().isReceivingAudio())
        title = "Listening...";
    drawIcon (g, Icon::Wave, head.removeFromLeft (26.f), live.hasSignal ? Colours::good : Colours::textMute);
    head.removeFromLeft (8.f);
    g.setColour (Colours::text);
    g.setFont (Fonts::semi (16.f));
    g.drawText (title, head, juce::Justification::centredLeft, true);
    c.removeFromTop (10.f);
    const auto latest = engine.getAnalysis().getLatestResult();
    auto row = [&] (const juce::String& k, const juce::String& v)
    {
        auto rr = c.removeFromTop (30.f);
        g.setColour (Colours::textDim);
        g.setFont (Fonts::body (14.f));
        g.drawText (k, rr, juce::Justification::centredLeft, false);
        g.setColour (Colours::text);
        g.setFont (Fonts::semi (14.f));
        g.drawText (v, rr, juce::Justification::centredRight, false);
    };
    row ("Fundamental", live.fundamentalHz > 0 ? formatHz (live.fundamentalHz) : "--");
    if (latest != nullptr && latest->inputFeatures.formantConfidence > 0.3f)
        row ("Formants", formatHz (latest->inputFeatures.f1Hz) + " / " + formatHz (latest->inputFeatures.f2Hz));
    else
        row ("Presence peak", live.presencePeakHz > 0 ? formatHz (live.presencePeakHz) : "--");
    row ("Sibilance", live.sibilanceHz > 0 ? formatHz (live.sibilanceHz) : "--");
    row ("Dynamic Range", live.hasSignal ? juce::String (live.dynamicRangeDb, 1) + " dB" : "--");
}

} // namespace nova::ui
