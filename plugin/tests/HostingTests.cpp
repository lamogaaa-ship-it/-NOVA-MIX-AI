#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "SyntheticAudio.h"
#include "ai/AgentTools.h"
#include "ai/CloudEngineer.h"
#include "core/NovaEngine.h"
#include "core/PluginProcessor.h"

using namespace nova;

#ifdef NOVA_TEST_VST3_PATH
namespace
{
std::optional<juce::PluginDescription> hostedTestPlugin (juce::AudioPluginFormatManager& fm)
{
    const juce::File bundle (NOVA_TEST_VST3_PATH);
    if (! bundle.exists()) return std::nullopt;
    for (int i = 0; i < fm.getNumFormats(); ++i)
        if (fm.getFormat (i)->getName() == "VST3")
        {
            juce::OwnedArray<juce::PluginDescription> types;
            fm.getFormat (i)->findAllTypesForFile (types, bundle.getFullPathName());
            if (! types.isEmpty()) return *types[0];
        }
    return std::nullopt;
}

juce::AudioBuffer<float> runThrough (NovaAudioProcessor& p, const juce::AudioBuffer<float>& src, int block)
{
    juce::AudioBuffer<float> out (2, src.getNumSamples()), buf (2, block);
    out.clear();
    juce::MidiBuffer midi;
    for (int off = 0; off + block <= src.getNumSamples(); off += block)
    {
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, src, c, off, block);
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, off, buf, c, 0, block);
    }
    return out;
}

// max |out[i] - gain * src[i - delay]| after 'settle' samples
double maxDelayedError (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& src, int delay, float gain, int settle)
{
    double e = 0;
    for (int c = 0; c < 2; ++c)
        for (int i = std::max (delay, settle); i < out.getNumSamples() - 1024; ++i)
            e = std::max (e, (double) std::abs (out.getSample (c, i) - gain * src.getSample (c, i - delay)));
    return e;
}

int findParam (hosting::HostedRack& rack, int slot, const juce::String& name)
{
    for (auto& p : rack.getParameters (slot, 4096))
        if (p.name == name) return p.index;
    return -1;
}

std::unique_ptr<NovaAudioProcessor> processorWithHostedNova (int& outGainIndex)
{
    auto proc = std::make_unique<NovaAudioProcessor>();
    proc->prepareToPlay (48000.0, 512);
    auto& rack = proc->getHostedRack();
    const auto desc = hostedTestPlugin (rack.getFormatManager());
    REQUIRE (desc.has_value());
    juce::String err;
    const bool ok = rack.loadSync (0, *desc, "VST3:test-nova", err);
    INFO (err);
    REQUIRE (ok);
    outGainIndex = findParam (rack, 0, kParams[(size_t) P::OutGain].name);
    REQUIRE (outGainIndex >= 0);
    return proc;
}
} // namespace

TEST_CASE ("Hosted rack: a real VST3 runs after the chain with exact latency compensation", "[hosting]")
{
    int outIdx = -1;
    auto proc = processorWithHostedNova (outIdx);
    auto& rack = proc->getHostedRack();
    const int hosted = rack.getLatencySamples();
    CHECK (hosted > 0);
    CHECK (proc->getTotalLatency() == proc->getChainLatency() + hosted);
    CHECK (proc->getLatencySamples() == proc->getTotalLatency());   // the host is told about the hosted latency
    REQUIRE (rack.getSlots().size() == 1);
    CHECK (rack.getSlots()[0].latencySamples == hosted);

    const auto voice = test::makeVoice ({});
    const int total = proc->getTotalLatency();

    // neutral NOVA chain + neutral hosted plugin = the input, delayed by exactly the reported latency
    auto out = runThrough (*proc, voice, 480);
    CHECK (maxDelayedError (out, voice, total, 1.f, 0) < 1e-5);

    // +6 dB inside the hosted plugin (set through its own parameter object)
    const float sixDb = normalise (kParams[(size_t) P::OutGain], 6.f);
    REQUIRE (rack.setParameter (0, outIdx, sixDb));
    out = runThrough (*proc, voice, 480);
    CHECK (maxDelayedError (out, voice, total, juce::Decibels::decibelsToGain (6.f), 48000) < 2e-3);

    // bypassing the slot crossfades to a latency-matched copy: timing never changes
    rack.setBypassed (0, true);
    out = runThrough (*proc, voice, 480);
    CHECK (maxDelayedError (out, voice, total, 1.f, 24000) < 1e-4);
    CHECK (proc->getLatencySamples() == total);

    // A/B: monitoring A (loudness match off) is the original aligned to the full latency
    rack.setBypassed (0, false);
    proc->setParameterValue (P::LoudMatch, 0.f);
    proc->setParameterValue (P::MonitorA, 1.f);
    out = runThrough (*proc, voice, 480);
    CHECK (maxDelayedError (out, voice, total, 1.f, 24000) < 1e-4);
    proc->setParameterValue (P::MonitorA, 0.f);

    // removing the plugin restores NOVA's own latency
    rack.remove (0);
    CHECK (rack.getLatencySamples() == 0);
    CHECK (proc->getLatencySamples() == proc->getChainLatency());
    out = runThrough (*proc, voice, 480);
    CHECK (maxDelayedError (out, voice, proc->getChainLatency(), 1.f, 24000) < 1e-5);
}

TEST_CASE ("Hosted rack: session state restores the plugin, its settings and bypass", "[hosting]")
{
    int outIdx = -1;
    auto proc = processorWithHostedNova (outIdx);
    auto& rack = proc->getHostedRack();
    REQUIRE (rack.setParameter (0, outIdx, 0.8f));
    rack.setBypassed (0, true);
    juce::MemoryBlock state;
    proc->getStateInformation (state);

    NovaAudioProcessor restored;
    restored.prepareToPlay (48000.0, 512);
    restored.setStateInformation (state.getData(), (int) state.getSize());   // message thread: restores synchronously
    auto& r2 = restored.getHostedRack();
    for (auto& m : restored.getEngine().getConversation().messages()) UNSCOPED_INFO (m.text);
    REQUIRE (r2.getSlots().size() == 1);
    CHECK (r2.getSlots()[0].entryId == "VST3:test-nova");
    CHECK (r2.isBypassed (0));
    const auto params = r2.getParameters (0, 4096);
    REQUIRE ((int) params.size() > outIdx);
    CHECK_THAT (params[(size_t) outIdx].value, Catch::Matchers::WithinAbs (0.8, 1e-3));
    CHECK (restored.getLatencySamples() == restored.getTotalLatency());
}

TEST_CASE ("Rack tools: the cloud engineer edits a hosted plugin with validation, and undo restores it", "[hosting][agent]")
{
    int outIdx = -1;
    auto proc = processorWithHostedNova (outIdx);
    auto& engine = proc->getEngine();
    engine.setApplyDirectly (true);
    auto settings = engine.getSettings();
    settings.provider = "anthropic";
    settings.anthropicApiKey = "test-key";
    engine.setSettings (settings);

    // let NOVA listen to the vocal through the real capture path
    test::VoiceSpec spec;
    auto voice = test::makeVoice (spec);
    engine.getAnalysis().beginListening (8.0);
    for (int rep = 0; rep < 3 && engine.getAnalysis().getListenState() != analysis::AnalysisEngine::ListenState::Ready; ++rep)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int off = 0; off + 512 <= voice.getNumSamples(); off += 512)
        {
            for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, voice, c, off, 512);
            proc->processBlock (buf, midi);
            if ((off / 512) % 8 == 0) juce::Thread::sleep (2);
        }
    }
    for (int i = 0; i < 300 && engine.getAnalysis().getListenState() != analysis::AnalysisEngine::ListenState::Ready; ++i)
        juce::Thread::sleep (20);
    REQUIRE (engine.getAnalysis().getListenState() == analysis::AnalysisEngine::ListenState::Ready);

    const float before = proc->getHostedRack().getParameters (0, 4096)[(size_t) outIdx].value;
    auto transport = std::make_shared<ai::ScriptedTransport>();
    auto toolUse = [] (const juce::String& id, const juce::String& name, const juce::String& input)
    {
        return R"({"id":"msg_x","type":"message","role":"assistant","model":"claude-fable-5-1","stop_reason":"tool_use",
                  "content":[{"type":"tool_use","id":")" + id + R"(","name":")" + name + R"(","input":)" + input + "}]}";
    };
    transport->responses = {
        toolUse ("toolu_1", "get_plugin_rack", R"({"query":"Output"})"),
        toolUse ("toolu_2", "set_plugin_rack_parameters",
                 R"({"slot":0,"changes":[{"index":)" + juce::String (outIdx) + R"(,"value":1.0},{"index":99999,"value":0.5}],"reason":"user asked the rack plugin to be louder"})"),
        R"({"id":"msg_f","type":"message","role":"assistant","model":"claude-fable-5-1","stop_reason":"end_turn",
            "content":[{"type":"text","text":"<simple>I raised the output of the plugin in slot 1. Listen again to check it.</simple><engineer>Output gain +0.25 normalised.</engineer>"}]})"
    };
    engine.setTransportOverride (transport);
    engine.submitRequest ("Turn up the plugin in my rack a little");
    REQUIRE (engine.waitUntilIdle (60000));

    // the rack tools were offered and the tool results reached the model
    REQUIRE (transport->requests.size() == 3);
    const auto r1 = juce::JSON::parse (transport->requests[0]);
    bool offered = false;
    for (auto& t : *r1["tools"].getArray()) offered = offered || t["name"].toString() == "set_plugin_rack_parameters";
    CHECK (offered);
    CHECK (r1["messages"][0]["content"][0]["text"].toString().contains ("plugin_rack_after_chain"));
    const auto r3 = juce::JSON::parse (transport->requests[2]);
    const auto msgs = r3["messages"];
    const auto setResult = juce::JSON::parse (msgs[msgs.size() - 1]["content"][0]["content"].toString());
    CHECK (setResult["results"][0]["status"].toString().startsWith ("clamped"));        // 0.25 safety step
    CHECK (setResult["results"][1]["status"].toString().startsWith ("rejected"));       // unknown index

    // applied to the live plugin, limited to one safety step, and undoable
    const float after = proc->getHostedRack().getParameters (0, 4096)[(size_t) outIdx].value;
    CHECK_THAT (after, Catch::Matchers::WithinAbs (std::min (1.f, before + ai::AgentToolbox::kMaxRackStep), 1e-3));
    const auto reply = engine.getConversation().messages().back();
    CHECK (reply.canUndo);
    CHECK (reply.changes.joinIntoString ("|").contains ("Output Gain"));
    REQUIRE (engine.undo());
    CHECK_THAT (proc->getHostedRack().getParameters (0, 4096)[(size_t) outIdx].value, Catch::Matchers::WithinAbs (before, 1e-3));
    REQUIRE (engine.redo());
    CHECK_THAT (proc->getHostedRack().getParameters (0, 4096)[(size_t) outIdx].value, Catch::Matchers::WithinAbs (after, 1e-3));
}
#endif

TEST_CASE ("Rack tools are only offered when plugins are loaded, and reject bad input", "[hosting][agent]")
{
    const auto without = ai::AgentToolbox::toolDefinitions (false, false);
    const auto with = ai::AgentToolbox::toolDefinitions (false, true);
    CHECK (with.size() == without.size() + 3);

    ai::EngineerContext ctx;
    hosting::RackSlotView v;
    v.info.slot = 1;
    v.info.entryId = "VST3:x";
    v.info.name = "Test EQ";
    hosting::RackParamState p;
    p.index = 3; p.name = "Band 2 Gain"; p.value = 0.5f;
    v.params.push_back (p);
    hosting::RackParamState sw;
    sw.index = 4; sw.name = "Mode"; sw.value = 0.f; sw.numSteps = 3;
    v.params.push_back (sw);
    ctx.rack.push_back (v);
    auto audio = std::make_shared<const juce::AudioBuffer<float>> (2, 4800);
    ai::TreatmentSession session (audio, 48000.0, {}, analysis::WorkMode::Vocal, ChainSettings(), defaultChainOrder(), {});
    ai::AgentToolbox tools (ctx, session, ai::KnowledgeBase::shared(), nullptr);

    bool err = false;
    auto r = tools.execute ("set_plugin_rack_parameters", juce::JSON::parse (R"({"slot":1,"changes":[{"index":3,"value":0.9},{"index":4,"value":0.8}],"reason":"t"})"), err);
    CHECK_FALSE (err);
    CHECK_THAT ((double) r["results"][0]["applied"], Catch::Matchers::WithinAbs (0.75, 1e-6));   // 0.5 + one 0.25 step
    CHECK_THAT ((double) r["results"][1]["applied"], Catch::Matchers::WithinAbs (1.0, 1e-6));    // stepped: snaps (3 steps: 0, .5, 1)
    REQUIRE (tools.rackChanges.size() == 2);
    CHECK_THAT (tools.rackChanges[0].before, Catch::Matchers::WithinAbs (0.5, 1e-6));

    // a second call keeps the original 'before' and moves another step
    tools.execute ("set_plugin_rack_parameters", juce::JSON::parse (R"({"slot":1,"changes":[{"index":3,"value":0.9}],"reason":"t"})"), err);
    REQUIRE (tools.rackChanges.size() == 2);
    CHECK_THAT (tools.rackChanges[0].before, Catch::Matchers::WithinAbs (0.5, 1e-6));
    CHECK_THAT (tools.rackChanges[0].after, Catch::Matchers::WithinAbs (0.9, 1e-6));

    tools.execute ("set_plugin_rack_parameters", juce::JSON::parse (R"({"slot":7,"changes":[{"index":3,"value":0.1}],"reason":"t"})"), err);
    CHECK (err);
    tools.execute ("set_plugin_rack_parameters", juce::JSON::parse (R"({"slot":1,"changes":[{"index":3,"value":"loud"}],"reason":"t"})"), err);
    CHECK (err);
    tools.execute ("set_plugin_rack_bypass", juce::JSON::parse (R"({"slot":1,"bypassed":true})"), err);
    CHECK_FALSE (err);
    REQUIRE (tools.rackBypass.size() == 1);
    CHECK (tools.rackBypass[0].bypassed);
}
