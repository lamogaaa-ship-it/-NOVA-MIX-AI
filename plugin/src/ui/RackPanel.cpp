#include "Panels.h"

namespace nova::ui
{

// A plugin's own editor in a separate window. Owned by the RackPanel; always closed before the
// plugin instance it shows is replaced or destroyed (HostedRack::Listener::rackSlotWillChange).
class RackPanel::EditorWindow : public juce::DocumentWindow
{
public:
    EditorWindow (juce::AudioProcessorEditor* ed, const juce::String& title, std::function<void()> closed)
        : juce::DocumentWindow (title, juce::Colour (0xff0b1026), juce::DocumentWindow::closeButton), onClosed (std::move (closed))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (ed, true);
        setResizable (ed->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }
    void closeButtonPressed() override { if (onClosed) onClosed(); }

private:
    std::function<void()> onClosed;
};

RackPanel::RackPanel (NovaAudioProcessor& p) : proc (p), rack (p.getHostedRack())
{
    pluginChoice.setTextWhenNothingSelected ("Choose a scanned plugin...");
    pluginChoice.setTextWhenNoChoicesAvailable ("No scanned plugins - use Settings > Scan plugins");
    addAndMakeVisible (pluginChoice);
    add.setTooltip ("Load the chosen plugin into the first free rack slot");
    add.onClick = [this]
    {
        const int idx = pluginChoice.getSelectedItemIndex();
        const int slot = rack.firstFreeSlot();
        if (idx < 0 || idx >= (int) choices.size()) { status = "Choose a plugin first."; repaint(); return; }
        if (slot < 0) { status = "The rack is full (4 plugins). Remove one first."; repaint(); return; }
        loading = true;
        status = "Loading " + choices[(size_t) idx].name + "...";
        add.setEnabled (false);
        juce::Component::SafePointer<RackPanel> safe (this);
        const auto name = choices[(size_t) idx].name;
        rack.loadAsync (slot, choices[(size_t) idx], [safe, name] (bool ok, const juce::String& err)
        {
            if (safe == nullptr) return;
            safe->loading = false;
            safe->add.setEnabled (true);
            safe->status = ok ? name + " loaded. It runs after NOVA's chain, latency compensated."
                              : "Could not load " + name + ": " + err;
            safe->updateRows();
            safe->repaint();
        });
        repaint();
    };
    addAndMakeVisible (add);
    for (int i = 0; i < hosting::HostedRack::kMaxSlots; ++i)
    {
        auto& r = rows[(size_t) i];
        r.bypass.setClickingTogglesState (false);
        r.bypass.setTooltip ("Bypass (latency compensated)");
        r.bypass.onClick = [this, i] { rack.setBypassed (i, ! rack.isBypassed (i)); };
        r.edit.setTooltip ("Open the plugin's own window");
        r.edit.onClick = [this, i] { openEditor (i); };
        r.remove.setTooltip ("Remove from the rack");
        r.remove.onClick = [this, i] { rack.remove (i); };
        for (auto* b : { &r.bypass, &r.edit, &r.remove }) addChildComponent (*b);
    }
    rack.addListener (this);
    updateRows();
}

RackPanel::~RackPanel()
{
    rack.removeListener (this);
    for (auto& e : editors) e.reset();
}

void RackPanel::refreshChoices()
{
    auto& catalog = proc.getEngine().getPluginCatalog();
    catalog.reload();
    const auto stamp = catalog.lastScanMs();
    if (stamp == catalogStamp && ! choices.empty()) return;
    catalogStamp = stamp;
    choices.clear();
    pluginChoice.clear (juce::dontSendNotification);
    for (auto& e : catalog.search ({}, {}, 1000))
    {
        if (e.isInstrument || ! e.loadedOk || e.numOutputs <= 0) continue;   // only effects that loaded cleanly in the scanner
        choices.push_back (e);
        pluginChoice.addItem (e.name + "  -  " + e.manufacturer + "  (" + e.format + ")", (int) choices.size());
    }
    if (catalog.size() == 0) status = "No plugins scanned yet. Open Settings (gear) and press \"Scan plugins\".";
    repaint();
}

void RackPanel::rackSlotWillChange (int slot)
{
    editors[(size_t) slot].reset();
}

void RackPanel::rackChanged()
{
    updateRows();
    repaint();
}

void RackPanel::updateRows()
{
    const auto slots = rack.getSlots();
    for (int i = 0; i < hosting::HostedRack::kMaxSlots; ++i)
    {
        auto& r = rows[(size_t) i];
        const auto it = std::find_if (slots.begin(), slots.end(), [i] (auto& s) { return s.slot == i; });
        const bool used = it != slots.end();
        r.bypass.setVisible (used);
        r.remove.setVisible (used);
        r.edit.setVisible (used && it->hasEditor);
        if (used) r.bypass.setToggleState (! it->bypassed, juce::dontSendNotification);
    }
    add.setEnabled (! loading && rack.firstFreeSlot() >= 0);
    resized();
}

void RackPanel::openEditor (int slot)
{
    if (auto& w = editors[(size_t) slot]; w != nullptr) { w->toFront (true); return; }
    auto* inst = rack.getInstance (slot);
    if (inst == nullptr) return;
    auto* ed = inst->createEditorAndMakeActive();
    if (ed == nullptr) { status = "This plugin has no editor window."; repaint(); return; }
    juce::Component::SafePointer<RackPanel> safe (this);
    editors[(size_t) slot] = std::make_unique<EditorWindow> (ed, inst->getName() + "  -  NOVA rack slot " + juce::String (slot + 1),
                                                            [safe, slot] { if (safe != nullptr) safe->editors[(size_t) slot].reset(); });
}

void RackPanel::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (36);
    add.setBounds (top.removeFromRight (150).reduced (0, 2));
    top.removeFromRight (10);
    pluginChoice.setBounds (top.reduced (0, 3));
    r.removeFromTop (12);
    r.removeFromBottom (26);
    const int rowH = std::min (64, r.getHeight() / hosting::HostedRack::kMaxSlots);
    for (auto& row : rows)
    {
        row.area = r.removeFromTop (rowH).reduced (0, 4);
        auto a = row.area.reduced (12, 0);
        row.remove.setBounds (a.removeFromRight (30).withSizeKeepingCentre (30, 30));
        a.removeFromRight (8);
        row.edit.setBounds (a.removeFromRight (90).withSizeKeepingCentre (90, 32));
        a.removeFromRight (8);
        row.bypass.setBounds (a.removeFromRight (30).withSizeKeepingCentre (30, 30));
    }
}

void RackPanel::paint (juce::Graphics& g)
{
    const auto slots = rack.getSlots();
    for (int i = 0; i < hosting::HostedRack::kMaxSlots; ++i)
    {
        const auto& row = rows[(size_t) i];
        const auto a = row.area.toFloat();
        const auto it = std::find_if (slots.begin(), slots.end(), [i] (auto& s) { return s.slot == i; });
        const bool used = it != slots.end();
        g.setColour (Colours::panelHi.withAlpha (used ? 0.85f : 0.35f));
        g.fillRoundedRectangle (a, 10.f);
        g.setColour (used && ! it->bypassed ? Colours::cyan.withAlpha (0.45f) : Colours::border);
        g.drawRoundedRectangle (a.reduced (0.5f), 10.f, 1.f);
        auto t = a.reduced (16.f, 6.f);
        g.setColour (Colours::textMute);
        g.setFont (Fonts::title (10.f, 0.2f));
        g.drawText ("SLOT " + juce::String (i + 1), t.removeFromLeft (70.f), juce::Justification::centredLeft, false);
        if (! used)
        {
            g.setFont (Fonts::body (13.f));
            g.drawText ("Empty", t, juce::Justification::centredLeft, false);
            continue;
        }
        g.setColour (it->bypassed ? Colours::textDim : Colours::text);
        g.setFont (Fonts::semi (15.f));
        g.drawText (it->name, t.removeFromTop (t.getHeight() * 0.55f), juce::Justification::bottomLeft, true);
        g.setColour (Colours::textMute);
        g.setFont (Fonts::body (12.f));
        g.drawText (it->manufacturer + "  |  " + it->format + "  |  latency " + juce::String (it->latencySamples) + " samples"
                        + (it->bypassed ? "  |  bypassed" : ""),
                    t, juce::Justification::topLeft, true);
    }
    auto foot = getLocalBounds().removeFromBottom (24).toFloat();
    g.setFont (Fonts::body (12.5f));
    g.setColour (Colours::cyan);
    g.drawText (status, foot, juce::Justification::centredLeft, true);
    g.setColour (Colours::textMute);
    g.drawText ("Total added latency: " + juce::String (rack.getLatencySamples()) + " samples"
                    + (rack.latencyWasClamped() ? " (clamped)" : ""),
                foot, juce::Justification::centredRight, true);
}

} // namespace nova::ui
