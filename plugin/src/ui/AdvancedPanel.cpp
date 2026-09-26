#include "Panels.h"
#include "../net/CompanionClient.h"

namespace nova::ui
{

namespace
{
const char* shortModuleName (Module m)
{
    switch (m)
    {
        case Module::Global: return "Global";
        case Module::Level: return "Level";
        case Module::EQ: return "EQ";
        case Module::ToneMatch: return "Tone Match";
        case Module::DynEQ: return "Dyn EQ";
        case Module::Comp: return "Comp";
        case Module::DeEss: return "De-ess";
        case Module::Color: return "Color";
        case Module::Space: return "Space";
        case Module::Motion: return "Motion";
        case Module::Image: return "Image";
        case Module::Limiter: return "Limiter";
        case Module::Count: break;
    }
    return "";
}

class ValueSlider : public juce::Slider
{
public:
    explicit ValueSlider (int idx) : index (idx)
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::TextBoxBelow, false, 86, 18);
        setColour (juce::Slider::textBoxTextColourId, Colours::text);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setTooltip (juce::String (kParams[(size_t) idx].description));
    }
    int index;
};
} // namespace

AdvancedPanel::AdvancedPanel (NovaAudioProcessor& p) : proc (p), rackPanel (p)
{
    moduleOrder = { Module::Level, Module::EQ, Module::ToneMatch, Module::DynEQ, Module::Comp, Module::DeEss, Module::Color,
                    Module::Space, Module::Motion, Module::Image, Module::Limiter, Module::Global };
    for (auto m : moduleOrder)
    {
        auto b = std::make_unique<NovaButton> (shortModuleName (m), std::nullopt, NovaButton::Style::Segment);
        b->setTextHeight (13.f);
        b->onClick = [this, m] { showModule (m); };
        addAndMakeVisible (*b);
        moduleTabs.push_back (std::move (b));
    }
    rackTab.setTextHeight (13.f);
    rackTab.setTooltip ("Third-party VST3/AU plugins running after NOVA's chain");
    rackTab.onClick = [this]
    {
        showingRack = true;
        for (auto& b : moduleTabs) b->setToggleState (false, juce::dontSendNotification);
        rackTab.setToggleState (true, juce::dontSendNotification);
        for (auto& c : controls) c.comp->setVisible (false);
        rackPanel.refreshChoices();
        rackPanel.setVisible (true);
        repaint();
    };
    addAndMakeVisible (rackTab);
    addChildComponent (rackPanel);
    back.setTooltip ("Back to the simple AI chain view");
    back.onClick = [this] { if (onBack) onBack(); };
    addAndMakeVisible (back);
    showModule (Module::Level);
}

void AdvancedPanel::showModule (Module m)
{
    current = m;
    showingRack = false;
    rackTab.setToggleState (false, juce::dontSendNotification);
    rackPanel.setVisible (false);
    for (size_t i = 0; i < moduleOrder.size(); ++i)
        moduleTabs[i]->setToggleState (moduleOrder[i] == m, juce::dontSendNotification);
    rebuild();
}

void AdvancedPanel::rebuild()
{
    for (auto& c : controls) removeChildComponent (c.comp.get());
    controls.clear();
    auto& apvts = proc.getAPVTS();
    for (int i = 0; i < P::Count; ++i)
    {
        const auto& spec = kParams[(size_t) i];
        if (spec.module != current || i == P::Bypass || i == P::MonitorA || i == P::Delta) continue;
        Control c;
        c.index = i;
        if (spec.kind == ParamKind::Float)
        {
            auto s = std::make_unique<ValueSlider> (i);
            c.sAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, spec.id, *s);
            c.comp = std::move (s);
        }
        else if (spec.kind == ParamKind::Bool)
        {
            auto b = std::make_unique<juce::ToggleButton> ("");
            b->setTooltip (juce::String (spec.name));
            c.bAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, spec.id, *b);
            c.comp = std::move (b);
        }
        else
        {
            auto cb = std::make_unique<juce::ComboBox>();
            juce::StringArray ch; ch.addTokens (spec.choices, "|", "");
            cb->addItemList (ch, 1);
            c.cAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, spec.id, *cb);
            c.comp = std::move (cb);
        }
        addAndMakeVisible (*c.comp);
        controls.push_back (std::move (c));
    }
    resized();
    repaint();
}

void AdvancedPanel::resized()
{
    auto r = getLocalBounds().reduced (18, 12);
    back.setBounds (r.removeFromTop (30).removeFromRight (120).reduced (0, 1));
    auto tabs = r.removeFromTop (30);
    const int tw = tabs.getWidth() / ((int) moduleTabs.size() + 1);
    for (auto& b : moduleTabs) b->setBounds (tabs.removeFromLeft (tw).reduced (2, 1));
    rackTab.setBounds (tabs.reduced (2, 1));
    r.removeFromTop (10);
    rackPanel.setBounds (r);
    const int cellW = 104, cellH = std::min (112, r.getHeight() / 2);
    const int cols = std::max (1, r.getWidth() / cellW);
    int i = 0;
    for (auto& c : controls)
    {
        const int col = i % cols, row = i / cols;
        auto cell = juce::Rectangle<int> (r.getX() + col * cellW, r.getY() + row * cellH, cellW, cellH).reduced (6, 2);
        cell.removeFromTop (18);   // label
        const auto& spec = kParams[(size_t) c.index];
        if (spec.kind == ParamKind::Float) c.comp->setBounds (cell);
        else if (spec.kind == ParamKind::Bool) c.comp->setBounds (cell.withSizeKeepingCentre (48, 26));
        else c.comp->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 30));
        ++i;
    }
}

void AdvancedPanel::tick() {}

void AdvancedPanel::paint (juce::Graphics& g)
{
    paintGlassPanel (g, getLocalBounds().toFloat().reduced (1.f), 16.f);
    auto r = getLocalBounds().toFloat().reduced (18.f, 12.f);
    paintSectionTitle (g, "AI MIX CHAIN  /  CUSTOM", r.removeFromTop (30.f));
    if (showingRack) return;
    g.setFont (Fonts::medium (12.5f));
    for (auto& c : controls)
    {
        const auto b = c.comp->getBounds().toFloat();
        const auto& spec = kParams[(size_t) c.index];
        juce::String name (spec.name);
        const juce::String modPrefix (shortModuleName (current));
        name = name.replace ("Compressor ", "").replace ("Comp ", "").replace ("De-ess ", "").replace ("Color ", "").replace ("Reverb ", "Rev ")
                   .replace ("Limiter ", "").replace ("Rider ", "").replace ("Gate ", "");
        auto labelArea = juce::Rectangle<float> (b.getX() - 30.f, (float) (c.comp->getY()) - 18.f, b.getWidth() + 60.f, 16.f);
        if (spec.kind != ParamKind::Float)
            labelArea = labelArea.withY (b.getY() - (spec.kind == ParamKind::Bool ? 26.f : 24.f));
        g.setColour (Colours::textDim);
        g.drawText (name, labelArea, juce::Justification::centred, true);
    }
}

//==============================================================================
SettingsOverlay::SettingsOverlay (NovaAudioProcessor& p) : proc (p), engine (p.getEngine())
{
    provider.addItemList ({ "Automatic", "Offline engineer (no network)", "Claude API (developer key)", "NOVA Cloud" }, 1);
    effort.addItemList ({ "low", "medium", "high" }, 1);
    explanation.addItemList ({ "Simple explanations", "Engineer explanations" }, 1);
    for (auto* c : { &provider, &effort, &explanation }) addAndMakeVisible (*c);
    apiKey.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    for (auto* e : { &apiKey, &model, &cloudUrl, &cloudToken, &companionUrl })
    {
        e->setFont (Fonts::body (14.f));
        e->setIndents (10, 8);
        addAndMakeVisible (*e);
    }
    cloudToken.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    apiKey.setTextToShowWhenEmpty ("sk-ant-... (or set ANTHROPIC_API_KEY)", Colours::textMute);
    model.setTextToShowWhenEmpty ("claude-fable-5-1", Colours::textMute);
    cloudUrl.setTextToShowWhenEmpty ("https://api.your-nova-backend.com", Colours::textMute);
    for (auto* t : { &allowCloud, &learning, &experience, &fallbacks, &diagnostics }) addAndMakeVisible (*t);
    save.onClick = [this]
    {
        auto s = engine.getSettings();
        const int pi = provider.getSelectedItemIndex();
        s.provider = pi == 1 ? "offline" : pi == 2 ? "anthropic" : pi == 3 ? "nova_cloud" : "auto";
        s.anthropicApiKey = apiKey.getText().trim();
        s.model = model.getText().trim().isEmpty() ? juce::String ("claude-fable-5-1") : model.getText().trim();
        s.effort = effort.getText().isEmpty() ? juce::String ("medium") : effort.getText();
        s.cloudUrl = cloudUrl.getText().trim();
        s.cloudToken = cloudToken.getText().trim();
        s.companionUrl = companionUrl.getText().trim().isEmpty() ? juce::String ("http://127.0.0.1:47800") : companionUrl.getText().trim();
        s.allowCloudAnalysis = allowCloud.getToggleState();
        s.learningEnabled = learning.getToggleState();
        s.experienceEnabled = experience.getToggleState();
        s.useServerFallbacks = fallbacks.getToggleState();
        s.diagnosticsVisible = diagnostics.getToggleState();
        s.explanationMode = explanation.getSelectedItemIndex() == 1 ? "engineer" : "simple";
        engine.setSettings (s);
        statusText = "Saved. Engine: " + engine.engineLabel();
        repaint();
    };
    addAndMakeVisible (save);
    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (close);
    clearLearning.onClick = [this]
    {
        engine.getPreferences().clear();
        engine.getExperiences().clear();
        statusText = "Learned taste profile and experience history deleted from this computer.";
        repaint();
    };
    addAndMakeVisible (clearLearning);
    scanPlugins.setTooltip ("Scan installed VST3/AU plugins in a separate process (a crashing plugin cannot take the DAW down)");
    scanPlugins.onClick = [this]
    {
        if (scanning) return;
        // The scanner is a separate executable next to the plugin binary / in the app data folder.
        const auto exeName = juce::String ("nova-plugin-scanner")
                           #if JUCE_WINDOWS
                             + ".exe"
                           #endif
            ;
        const auto binDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        juce::Array<juce::File> candidates {
            binDir.getSiblingFile ("Helpers").getChildFile (exeName),   // macOS bundles: Contents/Helpers (installer)
            binDir.getChildFile (exeName),
            novaUserDataDirectory().getChildFile ("bin").getChildFile (exeName),
            juce::File ("/usr/local/bin").getChildFile (exeName),
            juce::File ("/Library/Application Support/NOVA MIX AI/bin").getChildFile (exeName) };
        for (auto& f : candidates)
            if (f.existsAsFile())
            {
                const auto db = engine.getPluginCatalog().getFile();
                scanning = scanner.start (juce::StringArray { f.getFullPathName(), "--output", db.getFullPathName() });
                statusText = scanning ? "Scanning plugins in a separate process..." : "Could not start the scanner.";
                repaint();
                return;
            }
        statusText = "Plugin scanner not installed (nova-plugin-scanner). NOVA's built-in modules remain fully available.";
        repaint();
    };
    addAndMakeVisible (scanPlugins);
    refresh();
}

void SettingsOverlay::refresh()
{
    const auto s = engine.getSettings();
    provider.setSelectedItemIndex (s.provider == "offline" ? 1 : s.provider == "anthropic" ? 2 : s.provider == "nova_cloud" ? 3 : 0, juce::dontSendNotification);
    apiKey.setText (s.anthropicApiKey, false);
    model.setText (s.model, false);
    effort.setText (s.effort, juce::dontSendNotification);
    cloudUrl.setText (s.cloudUrl, false);
    cloudToken.setText (s.cloudToken, false);
    companionUrl.setText (s.companionUrl, false);
    allowCloud.setToggleState (s.allowCloudAnalysis, juce::dontSendNotification);
    learning.setToggleState (s.learningEnabled, juce::dontSendNotification);
    experience.setToggleState (s.experienceEnabled, juce::dontSendNotification);
    fallbacks.setToggleState (s.useServerFallbacks, juce::dontSendNotification);
    diagnostics.setToggleState (s.diagnosticsVisible, juce::dontSendNotification);
    explanation.setSelectedItemIndex (s.explanationMode == "engineer" ? 1 : 0, juce::dontSendNotification);
    if (scanning && ! scanner.isRunning())
    {
        scanning = false;
        engine.getPluginCatalog().reload();
        statusText = "Plugin scan finished: " + juce::String (engine.getPluginCatalog().size()) + " plugins in the database.";
    }
}

void SettingsOverlay::resized()
{
    auto r = getLocalBounds().reduced (60, 50).reduced (28, 24);
    close.setBounds (r.removeFromTop (34).removeFromRight (34));
    r.removeFromTop (6);
    auto col1 = r.removeFromLeft (r.getWidth() / 2 - 20);
    r.removeFromLeft (40);
    auto col2 = r;
    auto row = [] (juce::Rectangle<int>& col, int h) { auto x = col.removeFromTop (h); col.removeFromTop (8); return x; };
    row (col1, 18); provider.setBounds (row (col1, 36));
    row (col1, 18); model.setBounds (row (col1, 36));
    row (col1, 18); effort.setBounds (row (col1, 36));
    row (col1, 18); apiKey.setBounds (row (col1, 36));
    row (col1, 18); cloudUrl.setBounds (row (col1, 36));
    row (col1, 18); cloudToken.setBounds (row (col1, 36));
    row (col2, 18); explanation.setBounds (row (col2, 36));
    row (col2, 18); companionUrl.setBounds (row (col2, 36));
    allowCloud.setBounds (row (col2, 30));
    fallbacks.setBounds (row (col2, 30));
    learning.setBounds (row (col2, 30));
    experience.setBounds (row (col2, 30));
    diagnostics.setBounds (row (col2, 30));
    auto buttons = row (col2, 38);
    clearLearning.setBounds (buttons.removeFromLeft (190));
    buttons.removeFromLeft (10);
    scanPlugins.setBounds (buttons.removeFromLeft (150));
    save.setBounds (row (col2, 40).removeFromRight (130));
}

void SettingsOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.6f));
    auto panel = getLocalBounds().reduced (60, 50).toFloat();
    paintGlassPanel (g, panel, 18.f, 0.6f);
    auto r = panel.reduced (28.f, 24.f);
    paintSectionTitle (g, "SETTINGS", r.removeFromTop (34.f), Colours::text);
    g.setFont (Fonts::medium (13.f));
    g.setColour (Colours::textDim);
    auto label = [&] (juce::Component& c, const juce::String& text) { g.drawText (text, c.getBounds().toFloat().translated (0, -24).withHeight (18), juce::Justification::centredLeft, false); };
    label (provider, "AI engineer");
    label (model, "Model");
    label (effort, "Reasoning effort (lower = faster replies)");
    label (apiKey, "Claude API key (developer builds)");
    label (cloudUrl, "NOVA Cloud URL");
    label (cloudToken, "NOVA Cloud token");
    label (explanation, "Explanations");
    label (companionUrl, "Companion (voice, neural models) URL");
    // honest status lines
    auto* comp = engine.getCompanion();
    const auto h = comp != nullptr ? comp->lastHealth() : CompanionClient::Health {};
    g.setFont (Fonts::body (13.f));
    auto status = r.removeFromBottom (86.f);
    g.setColour (Colours::textDim);
    g.drawText ("Engine: " + engine.engineLabel() + (engine.isCloudEngineActive() ? "  (analysis numbers + your text are sent; audio never is)" : "  (runs locally)"),
                status.removeFromTop (20.f), juce::Justification::centredLeft, true);
    g.drawText ("Companion: " + (h.reachable ? "running v" + h.version + (h.stt ? ", voice " + h.sttEngine : ", no voice") + (h.embeddings ? ", embeddings " + h.embeddingModel : "")
                                             : juce::String ("not running (voice and neural embeddings unavailable)")),
                status.removeFromTop (20.f), juce::Justification::centredLeft, true);
    g.drawText ("Learning: " + juce::String (engine.getPreferences().totalObservations()) + " taste observations, " + juce::String (engine.getExperiences().size())
                    + " past situations stored locally at " + novaUserDataDirectory().getFullPathName(),
                status.removeFromTop (20.f), juce::Justification::centredLeft, true);
    g.setColour (Colours::cyan);
    g.drawText (statusText, status.removeFromTop (20.f), juce::Justification::centredLeft, true);
    if (engine.getSettings().diagnosticsVisible)
    {
        const auto& m = proc.getMeters();
        g.setColour (Colours::textMute);
        g.drawText ("DSP load " + juce::String (m.callbackLoad.load() * 100.f, 1) + "% (max " + juce::String (m.maxCallbackLoad.load() * 100.f, 1) + "%), latency "
                        + juce::String (proc.getChainLatency()) + " samples, dropped capture frames " + juce::String (engine.getAnalysis().getDroppedFrames())
                        + ", last analysis " + juce::String (engine.getAnalysis().getLastAnalysisMs(), 0) + " ms, plugins in DB " + juce::String (engine.getPluginCatalog().size()),
                    juce::Rectangle<float> (panel.getX() + 28, panel.getBottom() - 26, panel.getWidth() - 56, 18), juce::Justification::centredLeft, true);
    }
}

} // namespace nova::ui
