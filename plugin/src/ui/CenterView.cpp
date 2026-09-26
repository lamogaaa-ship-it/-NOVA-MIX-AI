#include "Panels.h"
#include "../net/CompanionClient.h"

namespace nova::ui
{

using LS = analysis::AnalysisEngine::ListenState;

CenterView::CenterView (NovaAudioProcessor& p) : proc (p), engine (p.getEngine())
{
    addAndMakeVisible (orb);
    listen.setTextHeight (20.f);
    listen.setTooltip ("LISTEN: NOVA captures and measures what your track is playing (it needs the DAW to play). Click again to finish early.");
    listen.onClick = [this]
    {
        auto& a = engine.getAnalysis();
        switch (a.getListenState())
        {
            case LS::Listening:
                if (a.getListenProgressSeconds() >= 3.0) a.finishListening(); else a.cancelListening();
                break;
            case LS::Analyzing: break;
            case LS::Idle:
            case LS::Ready:
            default: a.beginListening (20.0); break;
        }
    };
    addAndMakeVisible (listen);
    talk.setTooltip ("Hold to speak (English / Arabic). Needs the NOVA Companion's speech-to-text.");
    talk.onStateChange = [this]
    {
        auto* comp = engine.getCompanion();
        if (comp == nullptr || ! comp->lastHealth().stt) return;
        if (talk.getState() == juce::Button::buttonDown && ! talking)
        {
            talking = true;
            talk.setToggleState (true, juce::dontSendNotification);
            juce::Thread::launch ([comp, lang = SettingsStore::shared().get().voiceLanguage] { juce::String e; comp->startListening (lang, e); });
        }
        else if (talk.getState() != juce::Button::buttonDown && talking)
        {
            talking = false;
            talk.setToggleState (false, juce::dontSendNotification);
            juce::Component::SafePointer<CenterView> safe (this);
            juce::Thread::launch ([comp, safe]
            {
                juce::String lang, err;
                const auto text = comp->stopListening (lang, err);
                juce::MessageManager::callAsync ([safe, text]
                {
                    if (safe != nullptr && text.isNotEmpty()) safe->engine.submitRequest (text, NovaEngine::Source::Voice);
                });
            });
        }
    };
    addAndMakeVisible (talk);
}

void CenterView::resized()
{
    auto r = getLocalBounds().toFloat();
    orb.setBounds (r.withHeight (r.getHeight() * 0.64f).toNearestInt());
    auto controls = juce::Rectangle<float> (r.getX(), r.getY() + r.getHeight() * 0.62f, r.getWidth(), 76.f);
    const float lw = 256.f;
    listen.setBounds (juce::Rectangle<float> (lw, 62.f).withCentre ({ controls.getCentreX() - 64.f, controls.getCentreY() }).toNearestInt());
    talk.setBounds (juce::Rectangle<float> (64.f, 64.f).withCentre ({ (float) listen.getRight() + 52.f, controls.getCentreY() }).toNearestInt());
    waveArea = juce::Rectangle<float> (r.getX() - 24.f, controls.getBottom() + 2.f, r.getWidth() + 48.f, r.getBottom() - controls.getBottom() - 2.f);
}

void CenterView::tick (double dt)
{
    t += dt;
    auto& a = engine.getAnalysis();
    const auto ls = a.getListenState();
    const auto& m = proc.getMeters();
    const float lvl = std::max (m.inPeak[0].load(), m.inPeak[1].load());

    OrbView::Inputs in;
    in.phase = engine.getPhase();
    in.listening = ls == LS::Listening;
    in.analyzing = ls == LS::Analyzing;
    in.level = juce::jlimit (0.f, 1.f, (juce::Decibels::gainToDecibels (lvl, -80.f) + 60.f) / 60.f);
    in.listenProgress = ls == LS::Listening ? (float) (a.getListenProgressSeconds() / a.getListenTargetSeconds()) : -1.f;
    in.referenceLoaded = engine.getReferences().getState() == reference::ReferenceManager::State::Ready;
    orb.update (in, dt);

    switch (ls)
    {
        case LS::Listening:
            listen.setButtonText ("LISTENING");
            listen.setProgress (in.listenProgress);
            listen.setSubText (a.isReceivingAudio() ? juce::String (a.getListenProgressSeconds(), 1) + " / " + juce::String ((int) a.getListenTargetSeconds()) + " s  -  click to finish"
                                                    : juce::String ("waiting for audio - press play"));
            listen.setEnabled (true);
            break;
        case LS::Analyzing:
            listen.setButtonText ("ANALYZING");
            listen.setProgress (1.f);
            listen.setSubText ("measuring what I heard");
            listen.setEnabled (false);
            break;
        case LS::Ready:
        {
            listen.setButtonText ("LISTEN");
            listen.setProgress (-1.f);
            const auto r = a.getLatestResult();
            listen.setSubText (r != nullptr ? "READY  -  heard " + juce::String (juce::roundToInt (r->inputFeatures.activeSec)) + " s" : juce::String());
            listen.setEnabled (! engine.isBusy());
            break;
        }
        case LS::Idle:
        default:
            listen.setButtonText ("LISTEN");
            listen.setProgress (-1.f);
            listen.setSubText ({});
            listen.setEnabled (! engine.isBusy());
            break;
    }
    listen.setPulse (ls == LS::Listening ? 0.5f + 0.5f * std::sin ((float) t * 5.f) : 0.f);
    auto* comp = engine.getCompanion();
    const bool voice = comp != nullptr && comp->lastHealth().stt;
    talk.setEnabled (voice);
    talkHint = voice ? "Hold to speak" : "Voice off";
    talk.setPulse (talking ? 1.f : 0.f);

    a.getWaveform (wave, waveWrite);
    repaint (waveArea.toNearestInt().expanded (2));
    repaint (talk.getBounds().withWidth (140).translated (70, 0));
}

void CenterView::paint (juce::Graphics& g)
{
    // Live processed waveform (real audio from the capture path), newest at the right.
    // Dense thin bars with a gentle amplitude curve so dynamics stay visible.
    const auto a = waveArea;
    const int n = analysis::AnalysisEngine::kWaveformPoints;
    constexpr int kGroup = 4;                       // 720 points -> 180 visible bars (peak of each group)
    const int numBars = n / kGroup;
    const float step = a.getWidth() / (float) numBars;
    const float cy = a.getCentreY();
    juce::Path bars;
    for (int b = 0; b < numBars; ++b)
    {
        float v = 0.f;
        for (int k = 0; k < kGroup; ++k)
            v = std::max (v, wave[(size_t) ((waveWrite + b * kGroup + k) % n)]);
        const float amp = std::pow (juce::jlimit (0.f, 1.f, v * 1.4f), 0.7f);
        const float edge = std::min (1.f, std::min (b, numBars - 1 - b) / 22.f);
        const float h = amp * a.getHeight() * 0.47f * edge;
        const float w = std::max (1.f, step * 0.45f);
        const float x = a.getX() + b * step + (step - w) * 0.5f;
        bars.addRoundedRectangle (x, cy - h - 0.5f, w, 2.f * h + 1.f, w * 0.5f);
    }
    juce::ColourGradient grad (Colours::cyan, a.getX(), cy, Colours::purple, a.getRight(), cy, false);
    grad.addColour (0.45, Colours::blue);
    grad.addColour (0.75, Colours::violet);
    g.setGradientFill (grad);
    g.setOpacity (0.22f);
    g.fillPath (bars, juce::AffineTransform::scale (1.f, 1.35f, a.getCentreX(), cy));
    g.setOpacity (1.f);
    g.fillPath (bars);
    g.setColour (Colours::cyan.withAlpha (0.18f));
    g.drawHorizontalLine ((int) cy, a.getX(), a.getRight());

    // talk hint
    g.setColour (talk.isEnabled() ? Colours::textDim : Colours::textMute);
    g.setFont (Fonts::medium (14.f));
    g.drawText (talkHint, juce::Rectangle<float> ((float) talk.getRight() + 12.f, (float) talk.getY(), 120.f, (float) talk.getHeight()), juce::Justification::centredLeft, false);
}

//==============================================================================
MeterPanel::MeterPanel (NovaAudioProcessor& p, bool isInput) : proc (p), input (isInput)
{
    meter.source = [this]
    {
        auto& m = proc.getMetersForUi();
        auto* peaks = input ? m.inPeak : m.outPeak;
        auto* rmss = input ? m.inRms : m.outRms;
        // take-and-reset peak: the audio thread keeps the max since the last UI frame
        return std::array<float, 4> { peaks[0].exchange (0.f), peaks[1].exchange (0.f), rmss[0].load(), rmss[1].load() };
    };
    addAndMakeVisible (meter);
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.setTooltip (input ? "Input trim" : "Output gain");
    addAndMakeVisible (knob);
    attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.getAPVTS(), kParams[(size_t) (input ? P::InTrim : P::OutGain)].id, knob);
}

void MeterPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    r.removeFromTop (30);
    auto bottom = r.removeFromBottom (58);
    knob.setBounds (bottom.removeFromRight (58).reduced (4));
    meter.setBounds (r.withTrimmedRight (6));
}

void MeterPanel::paint (juce::Graphics& g)
{
    paintGlassPanel (g, getLocalBounds().toFloat().reduced (1.f), 16.f);
    auto r = getLocalBounds().toFloat().reduced (18.f, 12.f);
    paintSectionTitle (g, input ? "INPUT" : "OUTPUT", r.removeFromTop (30.f));
    g.setColour (Colours::textDim);
    g.setFont (Fonts::medium (12.5f));
    const float v = (float) knob.getValue();
    g.drawText (formatDb (v), juce::Rectangle<float> ((float) knob.getX() - 70.f, (float) knob.getBottom() - 18.f, 66.f, 16.f), juce::Justification::centredRight, false);
}

} // namespace nova::ui
