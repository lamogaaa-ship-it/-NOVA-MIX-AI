#include "PluginEditor.h"
#include "../core/PluginProcessor.h"

namespace nova
{

using namespace ui;

//==============================================================================
// Everything is laid out on a 1440 x 1080 design canvas (the concept's proportions) and scaled
// with a transform, so text and vectors stay crisp at any window size / Retina density.
class NovaEditor::Content : public juce::Component
{
public:
    explicit Content (NovaAudioProcessor& p)
        : proc (p), engine (p.getEngine()), reference (p), assistant (p), center (p), chain (p), analysis (p),
          inMeter (p, true), outMeter (p, false), advanced (p), settings (p)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &reference, &assistant, &center, &chain, &analysis, &inMeter, &outMeter })
            addAndMakeVisible (c);
        addChildComponent (advanced);
        addChildComponent (settings);

        // header controls
        for (auto* b : { &vocalTab, &mixTab, &masterTab })
        {
            b->setClickingTogglesState (false);
            b->setTextHeight (18.f);
            addAndMakeVisible (*b);
        }
        vocalTab.onClick = [this] { setMode (analysis::WorkMode::Vocal); };
        mixTab.onClick = [this] { setMode (analysis::WorkMode::Mix); };
        masterTab.onClick = [this] { setMode (analysis::WorkMode::Master); };
        vocalTab.setTooltip ("Vocal mode: lead/backing vocal processing");
        mixTab.setTooltip ("Mix mode: instruments, buses and full mixes");
        masterTab.setTooltip ("Master mode: loudness, tone and dynamics of the whole song");

        styleBox.addItemList ({ "Modern Pop Vocal", "Rap / Hip-Hop Vocal", "R&B / Soul Vocal", "Rock Vocal", "Acoustic / Singer-Songwriter",
                                "Mahraganat / Shaabi", "Podcast / Speech", "Full Mix", "Master - Streaming", "Master - Loud / Club" }, 1);
        styleBox.setTooltip ("Context for NOVA's decisions (a style hint, never a fixed preset)");
        styleBox.onChange = [this]
        {
            static const char* ids[] = { "modern_pop", "rap", "rnb", "rock", "acoustic", "mahraganat", "speech", "mix", "master_streaming", "master_loud" };
            const int i = styleBox.getSelectedItemIndex();
            if (i >= 0 && i < 10) engine.setStyle (ids[i]);
        };
        addAndMakeVisible (styleBox);
        menuBtn.setTooltip ("More");
        menuBtn.onClick = [this] { showMenu(); };
        addAndMakeVisible (menuBtn);
        gearBtn.setTooltip ("Settings: AI engine, privacy, learning, voice, plugins");
        gearBtn.onClick = [this] { settings.refresh(); settings.setVisible (true); settings.toFront (true); };
        addAndMakeVisible (gearBtn);
        settings.onClose = [this] { settings.setVisible (false); };

        chain.onModeChange = [this] (bool custom, int moduleIndex)
        {
            advanced.setVisible (custom);
            chain.setVisible (! custom);
            analysis.setVisible (! custom);
            if (custom && moduleIndex >= 0) advanced.showModule ((Module) moduleIndex);
        };
        advanced.onBack = [this] { chain.onModeChange (false, -1); };

        const auto style = engine.getStyle();
        static const char* ids[] = { "modern_pop", "rap", "rnb", "rock", "acoustic", "mahraganat", "speech", "mix", "master_streaming", "master_loud" };
        int sel = 0;
        for (int i = 0; i < 10; ++i) if (style == ids[i]) sel = i;
        styleBox.setSelectedItemIndex (sel, juce::dontSendNotification);
        syncModeTabs();
    }

    void setMode (analysis::WorkMode m)
    {
        engine.setWorkMode (m);
        syncModeTabs();
    }

    void syncModeTabs()
    {
        const auto m = engine.getWorkMode();
        vocalTab.setToggleState (m == analysis::WorkMode::Vocal, juce::dontSendNotification);
        mixTab.setToggleState (m == analysis::WorkMode::Mix, juce::dontSendNotification);
        masterTab.setToggleState (m == analysis::WorkMode::Master, juce::dontSendNotification);
    }

    void showMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Undo last AI change", engine.canUndo());
        m.addItem (2, "Redo", engine.canRedo());
        m.addSeparator();
        m.addItem (3, "Listen again (20 s)");
        m.addItem (4, "Clear conversation");
        m.addItem (5, "Stop current task", engine.isBusy());
        m.addSeparator();
        m.addItem (6, "About NOVA MIX AI");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuBtn), [this] (int r)
        {
            if (r == 1) engine.undo();
            else if (r == 2) engine.redo();
            else if (r == 3) engine.getAnalysis().beginListening (20.0);
            else if (r == 4) engine.getConversation().clear();
            else if (r == 5) engine.cancelCurrent();
            else if (r == 6)
                juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("NOVA MIX AI " NOVA_VERSION_STRING)
                                                  .withMessage ("AI mixing & mastering engineer.\nEngine: " + engine.engineLabel()
                                                                + "\nLatency: " + juce::String (proc.getChainLatency()) + " samples")
                                                  .withButton ("OK"), nullptr);
        });
    }

    void resized() override
    {
        // coordinates on the 1440 x 1080 design canvas
        vocalTab.setBounds (470, 38, 166, 58);
        mixTab.setBounds (648, 38, 166, 58);
        masterTab.setBounds (826, 38, 166, 58);
        styleBox.setBounds (1052, 42, 212, 50);
        menuBtn.setBounds (1276, 42, 50, 50);
        gearBtn.setBounds (1334, 42, 50, 50);

        reference.setBounds (32, 124, 358, 432);
        assistant.setBounds (1050, 124, 358, 432);
        center.setBounds (404, 112, 632, 452);
        chain.setBounds (32, 566, 1376, 212);
        inMeter.setBounds (32, 790, 176, 262);
        analysis.setBounds (220, 790, 1000, 262);
        outMeter.setBounds (1232, 790, 176, 262);
        advanced.setBounds (220, 566, 1000, 486);
        settings.setBounds (getLocalBounds());
        background = {};
    }

    void paint (juce::Graphics& g) override
    {
        const auto t = getTransform();
        const float scale = std::sqrt (std::abs (t.getDeterminant())) * juce::Component::getApproximateScaleFactorForComponent (this);
        if (background.isNull() || std::abs (bgScale - scale) > 0.01f)
        {
            bgScale = std::max (1.f, scale);
            background = juce::Image (juce::Image::ARGB, (int) (kBaseW * bgScale), (int) (kBaseH * bgScale), true);
            juce::Graphics bg (background);
            bg.addTransform (juce::AffineTransform::scale (bgScale));
            paintBackground (bg);
        }
        g.drawImage (background, getLocalBounds().toFloat());
        paintBrand (g);
    }

    void paintBackground (juce::Graphics& g)
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) kBaseW, (float) kBaseH);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff070a18), 0, 0, juce::Colour (0xff02030a), 0, r.getBottom(), false));
        g.fillRect (r);
        // atmospheric glows + light streaks like the concept (subtle, behind the glass)
        auto glow = [&] (float x, float y, float rad, juce::Colour c)
        {
            g.setGradientFill (juce::ColourGradient (c, x, y, juce::Colours::transparentBlack, x + rad, y, true));
            g.fillEllipse (x - rad, y - rad, rad * 2, rad * 2);
        };
        glow (120, 60, 520, juce::Colour (0xff3b1d8f).withAlpha (0.35f));
        glow (1380, 1020, 560, juce::Colour (0xff1d3b8f).withAlpha (0.32f));
        glow (720, 330, 420, juce::Colour (0xff1a2f7a).withAlpha (0.25f));
        for (int i = 0; i < 9; ++i)
        {
            juce::Path streak;
            const float o = (float) i * 26.f;
            streak.startNewSubPath (-40 + o, 200 - o * 0.3f);
            streak.lineTo (260 + o, -20 - o * 0.3f);
            g.setColour ((i % 2 ? Colours::violet : Colours::blue).withAlpha (0.05f));
            g.strokePath (streak, juce::PathStrokeType (1.2f));
            juce::Path s2;
            s2.startNewSubPath (1180 + o, 1120 - o * 0.3f);
            s2.lineTo (1480 + o, 880 - o * 0.3f);
            g.strokePath (s2, juce::PathStrokeType (1.2f));
        }
        // outer frame
        const auto outer = r.reduced (14.f);
        g.setColour (Colours::border.withAlpha (0.5f));
        g.drawRoundedRectangle (outer, 22.f, 1.2f);
    }

    void paintBrand (juce::Graphics& g)
    {
        auto brand = juce::Rectangle<float> (42, 40, 390, 44);
        g.setFont (Fonts::brand (38.f));
        const juce::String nova ("NOVA "), mix ("MIX "), ai ("AI");
        float x = brand.getX();
        const float wNova = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), nova);
        const float wMix = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), mix);
        g.setColour (Colours::text);
        g.drawText (nova, juce::Rectangle<float> (x, brand.getY(), wNova + 4, brand.getHeight()), juce::Justification::centredLeft, false);
        x += wNova;
        g.setColour (Colours::textDim);
        g.drawText (mix, juce::Rectangle<float> (x, brand.getY(), wMix + 4, brand.getHeight()), juce::Justification::centredLeft, false);
        x += wMix;
        g.setGradientFill (juce::ColourGradient (Colours::cyan, x, brand.getY(), Colours::blue, x + 70, brand.getBottom(), false));
        g.drawText (ai, juce::Rectangle<float> (x, brand.getY(), 90, brand.getHeight()), juce::Justification::centredLeft, false);
        g.setColour (Colours::textMute);
        g.setFont (Fonts::title (9.5f, 0.32f));
        g.drawText (juce::CharPointer_UTF8 ("LISTEN  \xc2\xb7  UNDERSTAND  \xc2\xb7  MIX  \xc2\xb7  ELEVATE"), juce::Rectangle<float> (44, 86, 420, 16), juce::Justification::centredLeft, false);
        // "N" mark
        auto mark = juce::Rectangle<float> (1392, 50, 24, 34);
        juce::Path n;
        n.startNewSubPath (mark.getX() + 3, mark.getBottom());
        n.lineTo (mark.getX() + 3, mark.getY());
        n.lineTo (mark.getRight() - 3, mark.getBottom());
        n.lineTo (mark.getRight() - 3, mark.getY());
        strokeGlow (g, n, Colours::cyan, 2.4f, 0.8f);
    }

    void tick (double dt)
    {
        ++frame;
        center.tick (dt);
        inMeter.tick (dt);
        outMeter.tick (dt);
        if (frame % 2 == 0)
        {
            if (chain.isVisible()) chain.tick (dt * 2);
            if (analysis.isVisible()) analysis.tick (dt * 2);
        }
        if (frame % 6 == 0)
        {
            reference.tick();
            assistant.tick (dt * 6);
            syncModeTabs();
            if (settings.isVisible()) { settings.refresh(); settings.repaint(); }
        }
    }

private:
    NovaAudioProcessor& proc;
    NovaEngine& engine;
    ReferencePanel reference;
    AssistantPanel assistant;
    CenterView center;
    ChainView chain;
    AnalysisView analysis;
    MeterPanel inMeter, outMeter;
    AdvancedPanel advanced;
    SettingsOverlay settings;
    NovaButton vocalTab { "Vocal", Icon::Mic, NovaButton::Style::Tab }, mixTab { "Mix", Icon::Pulse, NovaButton::Style::Tab },
               masterTab { "Master", Icon::Bars, NovaButton::Style::Tab };
    NovaButton menuBtn { {}, Icon::Dots, NovaButton::Style::IconOnly }, gearBtn { {}, Icon::Gear, NovaButton::Style::IconOnly };
    juce::ComboBox styleBox;
    juce::Image background;
    float bgScale = 0.f;
    juce::int64 frame = 0;
};

//==============================================================================
NovaEditor::NovaEditor (NovaAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      vblank (this, [this] (double ts)
      {
          const double dt = lastVBlank > 0 ? std::clamp (ts - lastVBlank, 0.0, 0.1) : 1.0 / 60.0;
          lastVBlank = ts;
          advanceAnimation (dt);
      })
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    content = std::make_unique<Content> (p);
    content->setBounds (0, 0, kBaseW, kBaseH);
    addAndMakeVisible (*content);

    const float uiScale = SettingsStore::shared().get().uiScale;
    setResizable (true, true);
    setResizeLimits ((int) (kBaseW * 0.55f), (int) (kBaseH * 0.55f), (int) (kBaseW * 1.6f), (int) (kBaseH * 1.6f));
    getConstrainer()->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setSize ((int) (kBaseW * 0.84f * uiScale), (int) (kBaseH * 0.84f * uiScale));
}

NovaEditor::~NovaEditor()
{
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void NovaEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bgDeep);
}

void NovaEditor::resized()
{
    const float s = (float) getWidth() / (float) kBaseW;
    content->setTransform (juce::AffineTransform::scale (s));
}

void NovaEditor::advanceAnimation (double dt)
{
    content->tick (dt);
}

} // namespace nova
