#pragma once

#include "Widgets.h"
#include "OrbView.h"
#include "../core/NovaEngine.h"
#include "../core/PluginProcessor.h"

namespace nova::ui
{

//==============================================================================
class ReferencePanel : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    ReferencePanel (NovaAudioProcessor& p);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void tick();

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    NovaAudioProcessor& proc;
    NovaEngine& engine;
    juce::Slider strength;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> strengthAttach;
    NovaButton tone { "Tone", {}, NovaButton::Style::Pill }, dynamics { "Dynamics", {}, NovaButton::Style::Pill },
               space { "Space", {}, NovaButton::Style::Pill }, width { "Width", {}, NovaButton::Style::Pill },
               full { "Full", {}, NovaButton::Style::Pill }, match { "MATCH", Icon::Sparkle, NovaButton::Style::Pill },
               clear { {}, Icon::Close, NovaButton::Style::IconOnly }, help { {}, Icon::Help, NovaButton::Style::IconOnly };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<float> dropZone, card;
    bool dragOver = false;
    reference::ReferenceManager::State lastState = reference::ReferenceManager::State::Empty;
    void syncDims();
    void pushDims();
};

//==============================================================================
class MessageView;
class AssistantPanel : public juce::Component, private juce::ChangeListener
{
public:
    AssistantPanel (NovaAudioProcessor& p);
    ~AssistantPanel() override;
    void paint (juce::Graphics& g) override;
    void resized() override;
    void tick (double dt);

private:
    NovaAudioProcessor& proc;
    NovaEngine& engine;
    juce::Viewport viewport;
    std::unique_ptr<juce::Component> list;
    std::vector<std::unique_ptr<MessageView>> views;
    juce::TextEditor input;
    NovaButton send { {}, Icon::Send, NovaButton::Style::IconOnly }, mic { {}, Icon::Mic, NovaButton::Style::IconOnly };
    std::vector<std::unique_ptr<NovaButton>> chips;
    juce::StringArray lastSuggestions;
    int lastMessageCount = -1;
    bool voiceActive = false;
    double chipTimer = 0;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildMessages();
    void layoutMessages();
    void rebuildChips();
    void submit();
    void startVoice();
    void stopVoice();
};

//==============================================================================
class ChainView : public juce::Component
{
public:
    ChainView (NovaAudioProcessor& p);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }
    void tick (double dt);
    std::function<void (bool custom, int moduleIndex)> onModeChange;

    enum Card { Level, Eq, Comp, DeEss, Color, Space, NumCards };

private:
    NovaAudioProcessor& proc;
    NovaButton aiSeg { "AI", {}, NovaButton::Style::Segment }, customSeg { "Custom", {}, NovaButton::Style::Segment };
    NovaButton aBtn { "A", {}, NovaButton::Style::Pill }, bBtn { "B", {}, NovaButton::Style::Pill };
    juce::ToggleButton deltaToggle { "Delta" }, lmToggle { "Match" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> deltaAttach, lmAttach;
    std::array<juce::Rectangle<float>, NumCards> cards;
    std::array<bool, NumCards> aiTouched {};
    int hovered = -1;
    double t = 0;
    ChainSettings s;
    float gr[NumCards] {};
    void paintCard (juce::Graphics& g, int i, juce::Rectangle<float> r);
    int cardOnParam (int i) const;
};

//==============================================================================
class AnalysisView : public juce::Component
{
public:
    AnalysisView (NovaAudioProcessor& p);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void tick (double dt);

private:
    NovaAudioProcessor& proc;
    NovaEngine& engine;
    std::array<std::unique_ptr<NovaButton>, 5> tabs;
    int tab = 0;
    std::array<float, analysis::AnalysisEngine::kSpectrumBins> specDry {}, specWet {};
    std::vector<float> loudHistory, loudHistoryDry;
    double loudTimer = 0;
    juce::AudioBuffer<float> gonioDry, gonioWet;
    double gonioSr = 48000;
    analysis::LiveInfo live;
    juce::Rectangle<float> plot, side;
    void paintSpectrum (juce::Graphics& g, juce::Rectangle<float> r);
    void paintLoudness (juce::Graphics& g, juce::Rectangle<float> r);
    void paintFormant (juce::Graphics& g, juce::Rectangle<float> r);
    void paintWidth (juce::Graphics& g, juce::Rectangle<float> r);
    void paintFocus (juce::Graphics& g, juce::Rectangle<float> r);
    void paintSideCard (juce::Graphics& g, juce::Rectangle<float> r);
    float xForFreq (float f, juce::Rectangle<float> r) const;
};

//==============================================================================
class MeterPanel : public juce::Component
{
public:
    MeterPanel (NovaAudioProcessor& p, bool isInput);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void tick (double dt) { meter.tick (dt); }

private:
    NovaAudioProcessor& proc;
    bool input;
    LevelMeter meter;
    juce::Slider knob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
};

//==============================================================================
class CenterView : public juce::Component
{
public:
    CenterView (NovaAudioProcessor& p);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void tick (double dt);

private:
    NovaAudioProcessor& proc;
    NovaEngine& engine;
    OrbView orb;
    NovaButton listen { "LISTEN", Icon::Wave, NovaButton::Style::Primary };
    NovaButton talk { {}, Icon::Mic, NovaButton::Style::Round };
    std::array<float, analysis::AnalysisEngine::kWaveformPoints> wave {};
    int waveWrite = 0;
    juce::Rectangle<float> waveArea;
    double t = 0;
    bool talking = false;
    juce::String talkHint;
};

//==============================================================================
// Third-party plugin rack: pick scanned plugins, bypass, open their own editors, remove.
class RackPanel : public juce::Component, private hosting::HostedRack::Listener
{
public:
    explicit RackPanel (NovaAudioProcessor& p);
    ~RackPanel() override;
    void paint (juce::Graphics& g) override;
    void resized() override;
    void refreshChoices();

private:
    struct SlotRow
    {
        NovaButton bypass { {}, Icon::Power, NovaButton::Style::IconOnly },
                   edit { "Open", {}, NovaButton::Style::Pill },
                   remove { {}, Icon::Close, NovaButton::Style::IconOnly };
        juce::Rectangle<int> area;
    };
    class EditorWindow;

    NovaAudioProcessor& proc;
    hosting::HostedRack& rack;
    juce::ComboBox pluginChoice;
    NovaButton add { "Add to rack", Icon::Plus, NovaButton::Style::Pill };
    std::array<SlotRow, hosting::HostedRack::kMaxSlots> rows;
    std::vector<hosting::PluginEntry> choices;
    std::array<std::unique_ptr<juce::DocumentWindow>, hosting::HostedRack::kMaxSlots> editors;
    juce::String status;
    bool loading = false;
    juce::int64 catalogStamp = -1;

    void rackSlotWillChange (int slot) override;
    void rackChanged() override;
    void updateRows();
    void openEditor (int slot);
};

//==============================================================================
class AdvancedPanel : public juce::Component
{
public:
    AdvancedPanel (NovaAudioProcessor& p);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void showModule (Module m);
    void tick();
    std::function<void()> onBack;

private:
    NovaAudioProcessor& proc;
    NovaButton back { "AI view", Icon::Sparkle, NovaButton::Style::Pill };
    std::vector<std::unique_ptr<NovaButton>> moduleTabs;
    std::vector<Module> moduleOrder;
    Module current = Module::Level;
    struct Control
    {
        int index;
        std::unique_ptr<juce::Component> comp;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAttach;
    };
    std::vector<Control> controls;
    juce::Rectangle<float> curveArea;
    NovaButton rackTab { "Plugins", {}, NovaButton::Style::Segment };
    RackPanel rackPanel;
    bool showingRack = false;
    void rebuild();
};

//==============================================================================
class SettingsOverlay : public juce::Component
{
public:
    SettingsOverlay (NovaAudioProcessor& p);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void refresh();
    std::function<void()> onClose;

private:
    NovaAudioProcessor& proc;
    NovaEngine& engine;
    juce::ComboBox provider, effort, explanation;
    juce::TextEditor apiKey, model, cloudUrl, cloudToken, companionUrl;
    juce::ToggleButton allowCloud { "Allow cloud engineer (analysis numbers + text only, never audio)" },
                       learning { "Learn my taste (stored on this computer)" },
                       experience { "Remember past situations (features only)" },
                       fallbacks { "Server-side refusal fallbacks" },
                       diagnostics { "Show developer diagnostics" },
                       speak { "Speak replies to voice requests (companion)" };
    NovaButton save { "Save", Icon::Check, NovaButton::Style::Pill }, close { {}, Icon::Close, NovaButton::Style::IconOnly },
               clearLearning { "Delete learned data", {}, NovaButton::Style::Pill },
               scanPlugins { "Scan plugins", {}, NovaButton::Style::Pill };
    juce::String statusText;
    juce::ChildProcess scanner;
    bool scanning = false;
};

} // namespace nova::ui
