// nova-screenshot: drive the real processor with audio, let NOVA listen / act, render the real
// editor to PNG. Used for visual verification against the design reference.
//
//   nova-screenshot out.png [--wav input.wav] [--request "text"] [--reference ref.wav]
//                   [--scale 1.0] [--mode vocal|mix|master] [--custom] [--rack] [--host-plugin path.vst3]

#include <juce_gui_extra/juce_gui_extra.h>

#include "SyntheticAudio.h"
#include "core/NovaEngine.h"
#include "core/PluginProcessor.h"
#include "ui/PluginEditor.h"

using namespace nova;

static juce::AudioBuffer<float> loadWav (const juce::File& f, double& sr)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) return {};
    sr = r->sampleRate;
    juce::AudioBuffer<float> b (2, (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (sr * 60)));
    r->read (&b, 0, b.getNumSamples(), 0, true, true);
    if (r->numChannels == 1) b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
    return b;
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24)))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    if (args.isEmpty()) { std::cout << "usage: nova-screenshot out.png [--wav f] [--request text] [--reference f] [--scale s] [--mode m] [--custom]\n"; return 1; }
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (args[0]));
    auto opt = [&] (const juce::String& k) { const int i = args.indexOf (k); return i >= 0 && i + 1 < args.size() ? args[i + 1] : juce::String(); };

    double sr = 48000.0;
    juce::AudioBuffer<float> audio;
    if (opt ("--wav").isNotEmpty()) audio = loadWav (juce::File (opt ("--wav")), sr);
    if (audio.getNumSamples() == 0) audio = test::makeVoice ({});

    auto proc = std::make_unique<NovaAudioProcessor>();
    auto& engine = proc->getEngine();
    engine.setApplyDirectly (true);
    auto st = engine.getSettings();
    st.provider = "offline";
    st.companionEnabled = false;
    engine.setSettings (st);
    const auto mode = opt ("--mode");
    if (mode == "mix") engine.setWorkMode (analysis::WorkMode::Mix);
    else if (mode == "master") engine.setWorkMode (analysis::WorkMode::Master);
    proc->prepareToPlay (sr, 512);

    // reference
    if (opt ("--reference") == "synthetic")
    {
        test::VoiceSpec rs;
        rs.f0 = 220.f;
        rs.roomDecaySec = 1.4f;
        rs.phraseLevelsDb = { -16, -17, -15, -16, -17, -15 };
        auto ra = test::makeVoice (rs);
        const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nova_reference_vocal.wav");
        writeWav (f, ra, rs.sampleRate);
        engine.loadReference (f);
    }
    else if (opt ("--reference").isNotEmpty())
        engine.loadReference (juce::File (opt ("--reference")));

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditorAndMakeActive());
    auto* ed = dynamic_cast<NovaEditor*> (editor.get());
    const float scale = opt ("--scale").isNotEmpty() ? opt ("--scale").getFloatValue() : 1.f;
    editor->setSize ((int) (NovaEditor::kBaseW * scale), (int) (NovaEditor::kBaseH * scale));

    // play audio in (roughly) real time while NOVA listens
    engine.getAnalysis().beginListening (8.0);
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    int pos = 0;
    auto playFor = [&] (double seconds)
    {
        const int blocks = (int) (seconds * sr / 512);
        for (int b = 0; b < blocks; ++b)
        {
            for (int c = 0; c < 2; ++c)
            {
                const int avail = std::min (512, audio.getNumSamples() - pos);
                block.copyFrom (c, 0, audio, c, pos, avail);
                if (avail < 512) block.clear (c, avail, 512 - avail);
            }
            pos = (pos + 512) % std::max (512, audio.getNumSamples() - 512);
            proc->processBlock (block, midi);
            if (b % 3 == 0)
            {
                juce::Thread::sleep (8);
                if (ed != nullptr) ed->advanceAnimation (1.0 / 60.0);
            }
        }
    };
    playFor (10.0);
    for (int i = 0; i < 400 && engine.getAnalysis().getListenState() != analysis::AnalysisEngine::ListenState::Ready; ++i)
    {
        playFor (0.05);
    }
    if (opt ("--request").isNotEmpty())
    {
        engine.submitRequest (opt ("--request"));
        for (int i = 0; i < 1200 && (engine.isBusy()); ++i) playFor (0.05);
    }
    playFor (2.5);   // settle meters / spectrum / animation with processed audio
    if (opt ("--host-plugin").isNotEmpty())
    {
        // load a real plug-in into rack slot 1 (e.g. NOVA's own VST3) to show the rack with content
        auto& rack = proc->getHostedRack();
        juce::OwnedArray<juce::PluginDescription> types;
        for (int i = 0; i < rack.getFormatManager().getNumFormats(); ++i)
            rack.getFormatManager().getFormat (i)->findAllTypesForFile (types, opt ("--host-plugin"));
        juce::String err;
        if (types.isEmpty() || ! rack.loadSync (0, *types[0], "VST3:screenshot", err))
            std::cerr << "could not host " << opt ("--host-plugin") << ": " << err << "\n";
        playFor (0.5);
    }
    if (args.contains ("--custom") || args.contains ("--rack"))
        if (ed != nullptr) ed->showCustomView (args.contains ("--rack"));

    for (int i = 0; i < 30; ++i) if (ed != nullptr) ed->advanceAnimation (1.0 / 60.0);
    auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.f);
    juce::PNGImageFormat png;
    out.deleteFile();
    if (auto os = out.createOutputStream())
    {
        png.writeImageToStream (img, *os);
        std::cout << "wrote " << out.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";
    }
    editor.reset();
    proc.reset();
    return 0;
}
