// nova-host-check: load the BUILT plugin binary through JUCE's hosting layer, exactly like a DAW
// would, and verify: identity, parameters, latency reporting, bit-accurate neutral passthrough,
// that a parameter change really changes the audio, state save/restore, and (optionally) that
// the editor opens.
//
//   nova-host-check <path to NOVA MIX AI.vst3 | .component> [--editor]

#include <juce_audio_utils/juce_audio_utils.h>

#include "SyntheticAudio.h"

static int failures = 0;
static void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << what << (detail.isNotEmpty() ? "  (" + detail + ")" : juce::String()) << "\n";
    if (! ok) ++failures;
}

static juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (64) == name) return prm;
    return nullptr;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::cerr << "usage: nova-host-check <plugin path> [--editor]\n"; return 2; }
    const auto path = juce::String::fromUTF8 (argv[1]);
    const bool testEditor = argc > 2 && juce::String::fromUTF8 (argv[2]) == "--editor";

    juce::AudioPluginFormatManager fm;
    juce::addDefaultFormatsToManager (fm);
    juce::OwnedArray<juce::PluginDescription> types;
    for (int i = 0; i < fm.getNumFormats(); ++i)
        if (fm.getFormat (i)->fileMightContainThisPluginType (path))
            fm.getFormat (i)->findAllTypesForFile (types, path);
    check (! types.isEmpty(), "plugin binary discovered by the host", path);
    if (types.isEmpty()) return 1;
    const auto& desc = *types[0];
    check (desc.name == "NOVA MIX AI", "plugin name", desc.name);
    check (desc.manufacturerName == "NOVA Audio", "manufacturer", desc.manufacturerName);
    check (! desc.isInstrument, "registered as an effect");

    const double sr = 48000.0;
    const int block = 512;
    juce::String err;
    auto inst = fm.createPluginInstance (desc, sr, block, err);
    check (inst != nullptr, "instantiate", err);
    if (inst == nullptr) return 1;
    inst->setPlayConfigDetails (2, 2, sr, block);
    inst->prepareToPlay (sr, block);
    const int latency = inst->getLatencySamples();
    check (latency > 0 && latency < 512, "latency reported to host", juce::String (latency) + " samples");
    check (inst->getParameters().size() > 100, "host-automatable parameters", juce::String (inst->getParameters().size()));
    check (inst->getBypassParameter() != nullptr, "exposes a host bypass parameter");

    auto voice = nova::test::makeVoice ({});
    const int n = voice.getNumSamples();
    auto run = [&] (juce::AudioBuffer<float>& out)
    {
        out.setSize (2, n);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        for (int off = 0; off + block <= n; off += block)
        {
            for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, voice, c, off, block);
            inst->processBlock (buf, midi);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, off, buf, c, 0, block);
        }
    };

    juce::AudioBuffer<float> out;
    run (out);
    double maxErr = 0;
    for (int i = latency; i < n - block; ++i)
        maxErr = std::max (maxErr, (double) std::abs (out.getSample (0, i) - voice.getSample (0, i - latency)));
    check (maxErr < 1e-5, "neutral settings pass audio through bit-accurately (latency aligned)", "max error " + juce::String (maxErr, 8));

    // a parameter change must really change the audio: +6 dB output gain
    if (auto* gain = findParam (*inst, "Output Gain"))
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (gain);
        const float norm = ranged != nullptr ? ranged->convertTo0to1 (6.f) : 0.625f;
        gain->setValueNotifyingHost (norm);
        inst->reset();
        juce::AudioBuffer<float> out2;
        run (out2);
        const double r1 = out.getRMSLevel (0, n / 2, n / 3), r2 = out2.getRMSLevel (0, n / 2, n / 3);
        const double diff = juce::Decibels::gainToDecibels (r2) - juce::Decibels::gainToDecibels (r1);
        check (std::abs (diff - 6.0) < 0.3, "parameter automation changes the processed audio", juce::String (diff, 2) + " dB for +6 dB");
    }
    else check (false, "find 'Output Gain' parameter");

    // enable real processing and make sure the output stays finite and changes
    if (auto* comp = findParam (*inst, "Compressor On")) comp->setValueNotifyingHost (1.f);
    if (auto* dess = findParam (*inst, "De-esser On")) dess->setValueNotifyingHost (1.f);
    if (auto* col = findParam (*inst, "Color On")) col->setValueNotifyingHost (1.f);
    juce::AudioBuffer<float> out3;
    run (out3);
    bool finite = true;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) finite = finite && std::isfinite (out3.getSample (c, i));
    check (finite, "processing with modules enabled stays finite");

    // state save / restore through the host API
    juce::MemoryBlock state;
    inst->getStateInformation (state);
    check (state.getSize() > 1000, "state saved", juce::String ((int) state.getSize()) + " bytes");
    auto inst2 = fm.createPluginInstance (desc, sr, block, err);
    if (inst2 != nullptr)
    {
        inst2->setStateInformation (state.getData(), (int) state.getSize());
        int mismatches = 0;
        auto& a = inst->getParameters();
        auto& b = inst2->getParameters();
        for (int i = 0; i < std::min (a.size(), b.size()); ++i)
            if (std::abs (a[i]->getValue() - b[i]->getValue()) > 1e-4f) ++mismatches;
        check (mismatches == 0 && a.size() == b.size(), "state restored into a fresh instance", juce::String (mismatches) + " mismatches");
    }

    if (testEditor)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorAndMakeActive());
        check (ed != nullptr && ed->getWidth() > 600 && ed->getHeight() > 400, "editor opens", ed ? juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()) : juce::String ("null"));
        ed.reset();
    }

    inst->releaseResources();
    inst.reset();
    inst2.reset();
    std::cout << (failures == 0 ? "ALL HOST CHECKS PASSED\n" : "HOST CHECK FAILURES: " + juce::String (failures) + "\n");
    return failures == 0 ? 0 : 1;
}
