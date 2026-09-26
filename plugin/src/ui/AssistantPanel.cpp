#include "Panels.h"
#include "../net/CompanionClient.h"

namespace nova::ui
{

//==============================================================================
class MessageView : public juce::Component
{
public:
    MessageView (NovaEngine& e, ai::ChatMessage m) : engine (e), msg (std::move (m))
    {
        const bool actionable = msg.role == ai::ChatMessage::Role::Assistant && msg.actionId > 0;
        if (msg.engineerText.isNotEmpty() || ! msg.changes.isEmpty())
        {
            addAndMakeVisible (details);
            details.setClickingTogglesState (true);
            details.onClick = [this] { if (auto* p = getParentComponent()) if (auto* pp = p->getParentComponent()) pp->resized(); if (onResize) onResize(); };
            details.setTooltip ("Show the engineering details and every parameter change");
        }
        if (actionable)
        {
            for (auto* b : { &up, &down })
            {
                addAndMakeVisible (*b);
                b->setClickingTogglesState (false);
            }
            up.setTooltip ("This helped - NOVA learns your taste from this");
            down.setTooltip ("Not what I wanted - NOVA learns from this");
            up.setToggleState (msg.feedback > 0, juce::dontSendNotification);
            down.setToggleState (msg.feedback < 0, juce::dontSendNotification);
            up.onClick = [this] { engine.giveFeedback (msg.id, +1); up.setToggleState (true, juce::dontSendNotification); down.setToggleState (false, juce::dontSendNotification); };
            down.onClick = [this] { engine.giveFeedback (msg.id, -1); down.setToggleState (true, juce::dontSendNotification); up.setToggleState (false, juce::dontSendNotification); };
            if (msg.canUndo)
            {
                addAndMakeVisible (undo);
                undo.setTooltip ("Undo this change (restores the previous settings exactly)");
                undo.onClick = [this] { engine.undo(); undo.setEnabled (false); };
            }
        }
    }

    std::function<void()> onResize;

    int heightFor (int width)
    {
        layoutText (width);
        int h = (int) std::ceil (bubbleH) + 6;
        if (msg.role == ai::ChatMessage::Role::Assistant && (details.isVisible() || up.isVisible())) h += 30;
        return h;
    }

    void paint (juce::Graphics& g) override
    {
        const bool user = msg.role == ai::ChatMessage::Role::User;
        const bool status = msg.role == ai::ChatMessage::Role::Status;
        const bool err = msg.role == ai::ChatMessage::Role::Error;
        if (status)
        {
            g.setColour (Colours::textMute);
            textLayout.draw (g, bubble.reduced (10.f, 4.f));
            return;
        }
        if (! user)
        {
            // assistant avatar: a miniature of the orb
            const auto av = juce::Rectangle<float> (4, 6, 30, 30);
            juce::ColourGradient cg (juce::Colour (0xff0b1340), av.getCentreX(), av.getCentreY(), juce::Colour (0xff3a2a9e), av.getRight(), av.getCentreY(), true);
            g.setGradientFill (cg);
            g.fillEllipse (av);
            g.setGradientFill (accentGradient (av.getTopLeft(), av.getBottomRight()));
            g.drawEllipse (av, 1.2f);
            g.setColour (Colours::text);
            g.setFont (Fonts::brand (9.f));
            g.drawText ("AI", av, juce::Justification::centred, false);
        }
        g.setColour (user ? Colours::blue.withAlpha (0.22f) : (err ? Colours::bad.withAlpha (0.12f) : Colours::panelHi.withAlpha (0.9f)));
        g.fillRoundedRectangle (bubble, 12.f);
        g.setColour (user ? Colours::blue.withAlpha (0.45f) : Colours::border);
        g.drawRoundedRectangle (bubble, 12.f, 1.f);
        textLayout.draw (g, bubble.reduced (12.f, 9.f));
        if (msg.engine.isNotEmpty() && ! user && msg.engine != "analysis")
        {
            g.setColour (Colours::textMute);
            g.setFont (Fonts::body (11.f));
            const auto label = msg.engine == "offline" ? juce::String ("offline engineer") : msg.engine;
            // only where it fits between the details toggle and the feedback buttons
            const float left = bubble.getX() + (details.isVisible() ? 128.f : 0.f);
            const float right = bubble.getRight() - (undo.isVisible() ? 96.f : 66.f);
            if (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 4.f <= right - left)
                g.drawText (label, juce::Rectangle<float> (left, bubble.getBottom() + 4, right - left, 20), juce::Justification::centredRight, false);
        }
    }

    void resized() override
    {
        layoutText (getWidth());
        const float y = bubble.getBottom() + 4;
        int x = (int) bubble.getX();
        if (details.isVisible()) { details.setBounds (x, (int) y, 118, 22); x += 124; }
        int rx = (int) bubble.getRight();
        if (undo.isVisible()) { undo.setBounds (rx - 26, (int) y, 26, 22); rx -= 30; }
        if (down.isVisible()) { down.setBounds (rx - 26, (int) y, 26, 22); rx -= 30; }
        if (up.isVisible()) { up.setBounds (rx - 26, (int) y, 26, 22); }
    }

private:
    NovaEngine& engine;
    ai::ChatMessage msg;
    juce::TextLayout textLayout;
    juce::Rectangle<float> bubble;
    float bubbleH = 0;
    NovaButton details { "Engineer details", Icon::ChevronRight, NovaButton::Style::Segment };
    NovaButton up { {}, Icon::ThumbUp, NovaButton::Style::IconOnly }, down { {}, Icon::ThumbDown, NovaButton::Style::IconOnly },
               undo { {}, Icon::Undo, NovaButton::Style::IconOnly };

    void layoutText (int width)
    {
        const bool user = msg.role == ai::ChatMessage::Role::User;
        const bool status = msg.role == ai::ChatMessage::Role::Status;
        juce::AttributedString as;
        as.setWordWrap (juce::AttributedString::byWord);
        const auto base = status ? Fonts::body (12.5f) : Fonts::body (14.5f);
        appendMixed (as, msg.text, base, status ? Colours::textMute : Colours::text);
        if (details.getToggleState())
        {
            if (msg.engine.isNotEmpty() && msg.engine != "analysis")
                as.append ("\n\nEngine: " + (msg.engine == "offline" ? juce::String ("offline engineer (local rules)") : msg.engine),
                           Fonts::body (12.f), Colours::textMute);
            if (msg.engineerText.isNotEmpty())
                appendMixed (as, "\n\n" + msg.engineerText, Fonts::body (13.f), Colours::textDim);
            if (! msg.changes.isEmpty())
            {
                as.append ("\n\nChanges:", Fonts::semi (12.5f), Colours::cyan);
                for (auto& c : msg.changes) as.append ("\n" + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2 ")) + c, Fonts::body (12.5f), Colours::textDim);
            }
            if (! msg.warnings.isEmpty())
                for (auto& w : msg.warnings) as.append ("\n! " + w, Fonts::body (12.5f), Colours::warn);
        }
        if (containsArabic (msg.text)) as.setJustification (juce::Justification::topRight);
        const float avatar = user || status ? 0.f : 40.f;
        const float maxW = (float) width - avatar - (user ? 28.f : 8.f);
        textLayout.createLayout (as, maxW - 24.f);
        const float tw = std::min (maxW, textLayout.getWidth() + 26.f);
        bubbleH = textLayout.getHeight() + 20.f;
        bubble = user ? juce::Rectangle<float> ((float) width - tw - 2.f, 2.f, tw, bubbleH)
                      : juce::Rectangle<float> (avatar, 2.f, status ? maxW : std::max (tw, 170.f), bubbleH);
        if (status) bubbleH = textLayout.getHeight() + 8.f;
    }
};

//==============================================================================
AssistantPanel::AssistantPanel (NovaAudioProcessor& p) : proc (p), engine (p.getEngine())
{
    list = std::make_unique<juce::Component>();
    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    input.setTextToShowWhenEmpty ("Type a request or hold the mic...", Colours::textMute);
    input.setFont (Fonts::body (15.f));
    input.setIndents (12, 10);
    input.setReturnKeyStartsNewLine (false);
    input.onReturnKey = [this] { submit(); };
    addAndMakeVisible (input);
    send.onClick = [this] { submit(); };
    send.setTooltip ("Send");
    addAndMakeVisible (send);
    mic.setTooltip ("Hold to speak (English / Arabic) - needs the NOVA Companion running");
    mic.onStateChange = [this]
    {
        if (mic.getState() == juce::Button::buttonDown && ! voiceActive) startVoice();
        else if (mic.getState() != juce::Button::buttonDown && voiceActive) stopVoice();
    };
    addAndMakeVisible (mic);
    engine.getConversation().addChangeListener (this);
    rebuildMessages();
    rebuildChips();
}

AssistantPanel::~AssistantPanel() { engine.getConversation().removeChangeListener (this); }

void AssistantPanel::changeListenerCallback (juce::ChangeBroadcaster*) { rebuildMessages(); }

void AssistantPanel::submit()
{
    const auto t = input.getText().trim();
    if (t.isEmpty()) return;
    engine.submitRequest (t, NovaEngine::Source::Typed);
    input.clear();
}

void AssistantPanel::startVoice()
{
    auto* comp = engine.getCompanion();
    if (comp == nullptr || ! comp->lastHealth().stt)
    {
        ai::ChatMessage m;
        m.role = ai::ChatMessage::Role::Status;
        m.text = "Voice needs the NOVA Companion with speech-to-text running (see docs). You can type your request instead.";
        engine.getConversation().add (m);
        return;
    }
    voiceActive = true;
    mic.setToggleState (true, juce::dontSendNotification);
    juce::Thread::launch ([comp]
    {
        juce::String err;
        comp->startListening (SettingsStore::shared().get().voiceLanguage, err);
    });
}

void AssistantPanel::stopVoice()
{
    voiceActive = false;
    mic.setToggleState (false, juce::dontSendNotification);
    auto* comp = engine.getCompanion();
    juce::Component::SafePointer<AssistantPanel> safe (this);
    juce::Thread::launch ([comp, safe]
    {
        juce::String lang, err;
        const auto text = comp->stopListening (lang, err);
        juce::MessageManager::callAsync ([safe, text, err]
        {
            if (safe == nullptr) return;
            if (text.isNotEmpty()) safe->engine.submitRequest (text, NovaEngine::Source::Voice);
            else
            {
                ai::ChatMessage m;
                m.role = ai::ChatMessage::Role::Status;
                m.text = err.isNotEmpty() ? "Voice: " + err : "I didn't catch that - hold the mic while you speak.";
                safe->engine.getConversation().add (m);
            }
        });
    });
}

void AssistantPanel::rebuildMessages()
{
    const auto msgs = engine.getConversation().messages();
    views.clear();
    list->removeAllChildren();
    // welcome message (static text, not a claim about the audio)
    if (msgs.empty())
    {
        ai::ChatMessage w;
        w.role = ai::ChatMessage::Role::Assistant;
        w.text = "Hi! I'm Nova, your AI mixing assistant. Play your track and press LISTEN, then tell me what you want - or try a suggestion below.";
        views.push_back (std::make_unique<MessageView> (engine, w));
    }
    for (auto& m : msgs)
        views.push_back (std::make_unique<MessageView> (engine, m));
    for (auto& v : views)
    {
        v->onResize = [this] { layoutMessages(); };
        list->addAndMakeVisible (*v);
    }
    const bool grew = (int) msgs.size() != lastMessageCount;
    lastMessageCount = (int) msgs.size();
    layoutMessages();
    if (grew) viewport.setViewPosition (0, std::max (0, list->getHeight() - viewport.getHeight()));
    rebuildChips();
}

void AssistantPanel::layoutMessages()
{
    const int w = viewport.getWidth() - 10;
    if (w <= 0) return;
    int y = 4;
    for (auto& v : views)
    {
        const int h = v->heightFor (w);
        v->setBounds (0, y, w, h);
        y += h + 8;
    }
    list->setSize (w, std::max (y, viewport.getHeight()));
}

void AssistantPanel::rebuildChips()
{
    const auto s = engine.suggestions();
    if (s == lastSuggestions && ! chips.empty()) return;
    lastSuggestions = s;
    for (auto& c : chips) removeChildComponent (c.get());
    chips.clear();
    for (auto& text : s)
    {
        auto b = std::make_unique<NovaButton> (text, std::nullopt, NovaButton::Style::Chip);
        b->onClick = [this, text] { engine.submitRequest (text, NovaEngine::Source::Button); };
        addAndMakeVisible (*b);
        chips.push_back (std::move (b));
    }
    resized();
}

void AssistantPanel::tick (double dt)
{
    chipTimer += dt;
    if (chipTimer > 1.0) { chipTimer = 0; rebuildChips(); }
    // never miss a message even if a change notification was coalesced
    if ((int) engine.getConversation().messages().size() != lastMessageCount) rebuildMessages();
    const bool busy = engine.isBusy();
    for (auto& c : chips) c->setEnabled (! busy);
    auto* comp = engine.getCompanion();
    mic.setEnabled (comp != nullptr && comp->lastHealth().stt);
    repaint (getLocalBounds().removeFromTop (48));
}

void AssistantPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    r.removeFromTop (48);   // title row + engine label
    auto inputRow = r.removeFromBottom (48);
    send.setBounds (inputRow.removeFromRight (48).reduced (2));
    inputRow.removeFromRight (6);
    input.setBounds (inputRow);
    mic.setBounds (inputRow.removeFromRight (40).reduced (6).translated (-4, 0));
    r.removeFromBottom (10);
    const int chipH = 36, gap = 7;
    for (int i = (int) chips.size() - 1; i >= 0; --i)
    {
        auto row = r.removeFromBottom (chipH);
        const float tw = juce::GlyphArrangement::getStringWidth (Fonts::body (14.f), chips[(size_t) i]->getButtonText()) + 44.f;
        chips[(size_t) i]->setBounds (row.removeFromRight (juce::jlimit (140, row.getWidth(), (int) tw)));
        r.removeFromBottom (gap);
    }
    r.removeFromBottom (4);
    viewport.setBounds (r);
    layoutMessages();
}

void AssistantPanel::paint (juce::Graphics& g)
{
    paintGlassPanel (g, getLocalBounds().toFloat().reduced (1.f), 16.f);
    auto r = getLocalBounds().toFloat().reduced (18.f, 14.f);
    auto top = r.removeFromTop (26.f);
    paintSectionTitle (g, "AI ASSISTANT", top);
    // honest engine status
    const bool cloud = engine.isCloudEngineActive();
    const bool busy = engine.isBusy();
    const auto col = busy ? Colours::cyan : (cloud ? Colours::good : Colours::warn);
    const juce::String label = busy ? juce::String (ai::agentPhaseName (engine.getPhase())) : (cloud ? juce::String ("ONLINE") : juce::String ("LOCAL"));
    g.setFont (Fonts::title (10.5f, 0.22f));
    const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 4.f;
    auto st = top.removeFromRight (w + 18.f);
    g.setColour (col.withAlpha (0.25f));
    g.fillEllipse (st.getX() - 2, st.getCentreY() - 6, 12, 12);
    g.setColour (col);
    g.fillEllipse (st.getX() + 1, st.getCentreY() - 3, 6, 6);
    g.setColour (Colours::textDim);
    g.drawText (label, st.withTrimmedLeft (16.f), juce::Justification::centredLeft, false);
    // engine label under the title
    g.setColour (Colours::textMute);
    g.setFont (Fonts::body (12.f));
    g.drawText (engine.engineLabel(), r.removeFromTop (14.f), juce::Justification::centredLeft, true);
}

} // namespace nova::ui
