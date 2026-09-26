#include "Panels.h"

namespace nova::ui
{

ReferencePanel::ReferencePanel (NovaAudioProcessor& p) : proc (p), engine (p.getEngine())
{
    strength.setSliderStyle (juce::Slider::LinearHorizontal);
    strength.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    strength.setTooltip ("Reference Influence: how far the tone match moves toward the reference (live). Other dimensions use it when you press MATCH.");
    addAndMakeVisible (strength);
    strengthAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.getAPVTS(), kParams[(size_t) P::TmAmount].id, strength);

    for (auto* b : { &tone, &dynamics, &space, &width, &full })
    {
        b->setClickingTogglesState (true);
        b->setTextHeight (14.f);
        b->onClick = [this, b]
        {
            if (b == &full && full.getToggleState())
                for (auto* o : { &tone, &dynamics, &space, &width }) o->setToggleState (true, juce::dontSendNotification);
            if (b != &full && ! b->getToggleState()) full.setToggleState (false, juce::dontSendNotification);
            pushDims();
        };
        addAndMakeVisible (*b);
    }
    tone.setTooltip ("Match the tonal balance (loudness-independent, smoothed spectrum)");
    dynamics.setTooltip ("Match density / crest factor");
    space.setTooltip ("Match the amount of reverb/ambience");
    width.setTooltip ("Match stereo width (the lead stays centred)");
    full.setTooltip ("Tone + dynamics + space + width");
    match.setTextHeight (14.f);
    match.onClick = [this] { engine.matchReferenceNow(); };
    match.setTooltip ("Run the iterative reference match on the selected dimensions");
    addAndMakeVisible (match);
    clear.onClick = [this] { engine.clearReference(); repaint(); };
    clear.setTooltip ("Remove reference");
    addChildComponent (clear);
    help.setTooltip ("Drop a reference vocal, beat, mix or master. NOVA measures its tone, dynamics, space and width and moves your audio toward "
                     "the dimensions you pick - loudness-matched, iteratively verified, and never copying the audio itself.");
    addAndMakeVisible (help);
    syncDims();
}

void ReferencePanel::syncDims()
{
    const auto d = engine.getReferences().getDimensions();
    tone.setToggleState (d.tone, juce::dontSendNotification);
    dynamics.setToggleState (d.dynamics, juce::dontSendNotification);
    space.setToggleState (d.space, juce::dontSendNotification);
    width.setToggleState (d.width, juce::dontSendNotification);
    full.setToggleState (d.tone && d.dynamics && d.space && d.width, juce::dontSendNotification);
}

void ReferencePanel::pushDims()
{
    reference::MatchDimensions d;
    d.tone = tone.getToggleState();
    d.dynamics = dynamics.getToggleState();
    d.space = space.getToggleState();
    d.width = width.getToggleState();
    if (! (d.tone || d.dynamics || d.space || d.width)) { d.tone = true; tone.setToggleState (true, juce::dontSendNotification); }
    engine.getReferences().setDimensions (d);
}

bool ReferencePanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a;caf")) return true;
    return false;
}

void ReferencePanel::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a;caf")) { engine.loadReference (juce::File (f)); break; }
    repaint();
}

void ReferencePanel::mouseUp (const juce::MouseEvent& e)
{
    if (! dropZone.contains (e.position)) return;
    chooser = std::make_unique<juce::FileChooser> ("Choose a reference", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile()) engine.loadReference (fc.getResult());
    });
}

void ReferencePanel::tick()
{
    const auto st = engine.getReferences().getState();
    match.setEnabled (st == reference::ReferenceManager::State::Ready && ! engine.isBusy());
    clear.setVisible (st == reference::ReferenceManager::State::Ready || st == reference::ReferenceManager::State::Error);
    if (st != lastState) { lastState = st; repaint(); }
    if (st == reference::ReferenceManager::State::Loading) repaint (dropZone.toNearestInt());
}

void ReferencePanel::resized()
{
    auto r = getLocalBounds().toFloat().reduced (18.f, 14.f);
    help.setBounds (juce::Rectangle<int> ((int) r.getRight() - 26, (int) r.getY(), 26, 26));
    r.removeFromTop (36.f);
    dropZone = r.removeFromTop (118.f);
    r.removeFromTop (12.f);
    card = r.removeFromTop (66.f);
    clear.setBounds (juce::Rectangle<int> ((int) card.getRight() - 34, (int) card.getCentreY() - 13, 26, 26));
    r.removeFromTop (14.f);
    auto sl = r.removeFromTop (40.f);
    strength.setBounds (sl.withTrimmedTop (18.f).toNearestInt());
    r.removeFromTop (10.f);
    auto row = r.removeFromTop (34.f);
    const float bw = (row.getWidth() - 3 * 8.f) / 4.f;
    tone.setBounds (row.removeFromLeft (bw).toNearestInt()); row.removeFromLeft (8.f);
    dynamics.setBounds (row.removeFromLeft (bw).toNearestInt()); row.removeFromLeft (8.f);
    space.setBounds (row.removeFromLeft (bw).toNearestInt()); row.removeFromLeft (8.f);
    width.setBounds (row.toNearestInt());
    r.removeFromTop (8.f);
    auto row2 = r.removeFromTop (34.f);
    full.setBounds (row2.removeFromLeft (bw).toNearestInt());
    row2.removeFromLeft (8.f);
    match.setBounds (row2.toNearestInt());
}

void ReferencePanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    paintGlassPanel (g, b.reduced (1.f), 16.f);
    auto r = b.reduced (18.f, 14.f);
    paintSectionTitle (g, "REFERENCE MATCH", r.removeFromTop (26.f));

    const auto& refs = engine.getReferences();
    const auto st = refs.getState();

    // drop zone
    {
        juce::Path dash;
        dash.addRoundedRectangle (dropZone, 12.f);
        juce::Path dashed;
        const float dl[] = { 5.f, 4.f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, dash, dl, 2);
        g.setColour (dragOver ? Colours::cyan.withAlpha (0.12f) : Colours::bgDeep.withAlpha (0.45f));
        g.fillRoundedRectangle (dropZone, 12.f);
        g.setColour (dragOver ? Colours::cyan : Colours::border.brighter (0.3f));
        g.fillPath (dashed);
        auto z = dropZone.reduced (10.f);
        if (st == reference::ReferenceManager::State::Loading)
        {
            const float ph = (float) std::fmod (juce::Time::getMillisecondCounterHiRes() / 1000.0, 1.0);
            drawIcon (g, Icon::Wave, z.removeFromTop (46.f).withSizeKeepingCentre (34, 34), Colours::cyan.withAlpha (0.5f + 0.5f * std::sin (ph * 6.28f)));
            g.setColour (Colours::text);
            g.setFont (Fonts::medium (15.f));
            g.drawText ("Analyzing reference...", z.removeFromTop (24.f), juce::Justification::centred, false);
        }
        else
        {
            drawIcon (g, Icon::Wave, z.removeFromTop (46.f).withSizeKeepingCentre (34, 34), Colours::textDim);
            g.setColour (Colours::textDim);
            g.setFont (Fonts::medium (15.f));
            g.drawText ("Drop a reference vocal or track", z.removeFromTop (24.f), juce::Justification::centred, false);
            g.setColour (Colours::textMute);
            g.setFont (Fonts::title (10.f, 0.2f));
            g.drawText (juce::CharPointer_UTF8 ("WAV  \xc2\xb7  MP3  \xc2\xb7  FLAC  \xc2\xb7  AIFF"), z.removeFromTop (20.f), juce::Justification::centred, false);
        }
    }

    // reference card: thumbnail = the reference's measured loudness contour (real data)
    g.setColour (Colours::bgDeep.withAlpha (0.55f));
    g.fillRoundedRectangle (card, 10.f);
    g.setColour (Colours::border);
    g.drawRoundedRectangle (card, 10.f, 1.f);
    auto cr = card.reduced (8.f);
    auto thumb = cr.removeFromLeft (58.f);
    juce::ColourGradient tg (juce::Colour (0xff1e1b4b), thumb.getX(), thumb.getY(), juce::Colour (0xff0e7490), thumb.getRight(), thumb.getBottom(), false);
    g.setGradientFill (tg);
    g.fillRoundedRectangle (thumb, 8.f);
    if (auto ref = refs.get(); ref != nullptr && st == reference::ReferenceManager::State::Ready)
    {
        const auto& lu = ref->features.shortTermLufs;
        if (lu.size() > 2)
        {
            juce::Path p;
            const float lo = -40.f, hi = -4.f;
            for (size_t i = 0; i < lu.size(); ++i)
            {
                const float x = thumb.getX() + 4 + (thumb.getWidth() - 8) * (float) i / (float) (lu.size() - 1);
                const float h = juce::jlimit (0.f, 1.f, (lu[i] - lo) / (hi - lo)) * (thumb.getHeight() * 0.42f);
                p.addRectangle (x, thumb.getCentreY() - h, 1.2f, 2 * h + 0.5f);
            }
            g.setColour (Colours::cyan.withAlpha (0.85f));
            g.fillPath (p);
        }
        cr.removeFromLeft (12.f);
        g.setColour (Colours::text);
        g.setFont (Fonts::semi (15.f));
        g.drawText (juce::String (ref->name), cr.removeFromTop (cr.getHeight() * 0.52f).withTrimmedRight (34.f), juce::Justification::bottomLeft, true);
        const int secs = (int) ref->durationSec;
        g.setColour (Colours::textDim);
        g.setFont (Fonts::body (13.f));
        const juce::String kind = ref->isFullMix ? "Full mix" : juce::String (analysis::sourceTypeName (ref->features.source)).replace ("_", " ");
        const juce::String dot (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
        g.drawText (kind.substring (0, 1).toUpperCase() + kind.substring (1) + dot
                        + juce::String (ref->features.integratedLufs, 1) + " LUFS" + dot
                        + juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2),
                    cr.withTrimmedRight (34.f), juce::Justification::topLeft, true);
    }
    else
    {
        drawIcon (g, Icon::Wave, thumb.withSizeKeepingCentre (26, 26), Colours::textMute);
        cr.removeFromLeft (12.f);
        g.setColour (st == reference::ReferenceManager::State::Error ? Colours::bad : Colours::textMute);
        g.setFont (Fonts::body (14.f));
        g.drawFittedText (st == reference::ReferenceManager::State::Error ? refs.getError() : juce::String ("No reference loaded"),
                          cr.withTrimmedRight (30.f).toNearestInt(), juce::Justification::centredLeft, 2);
    }

    // strength label + value
    auto sl = strength.getBounds().toFloat().withY (strength.getY() - 18.f).withHeight (18.f);
    g.setColour (Colours::textDim);
    g.setFont (Fonts::medium (14.f));
    g.drawText ("Match Strength", sl, juce::Justification::centredLeft, false);
    g.setColour (Colours::text);
    g.drawText (juce::String (juce::roundToInt (strength.getValue())) + "%", sl, juce::Justification::centredRight, false);

    // last match result (only exists after a real comparison ran)
    if (auto m = refs.getLastMatch(); m != nullptr && ! m->reports.empty())
    {
        juce::String s;
        for (auto& rep : m->reports)
            if (rep.attempted) s << (s.isEmpty() ? "" : "  ") << juce::String (rep.dimension) << (rep.improved ? juce::String (juce::CharPointer_UTF8 (" \xe2\x9c\x93")) : juce::String (" -"));
        g.setColour (Colours::textMute);
        g.setFont (Fonts::body (12.5f));
        g.drawText ("Matched: " + s, juce::Rectangle<float> (r.getX(), b.getBottom() - 30.f, r.getWidth(), 18.f), juce::Justification::centredLeft, true);
    }
}

} // namespace nova::ui
