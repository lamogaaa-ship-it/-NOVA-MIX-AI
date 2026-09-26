#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "SyntheticAudio.h"
#include "core/NovaEngine.h"
#include "core/PluginProcessor.h"

#include <atomic>
#include <cstdlib>
#include <new>

using namespace nova;

//==============================================================================
// Real-time safety probe: count heap allocations made on a thread while it is flagged as the
// audio thread. Replacing the global allocator affects the whole test binary.
namespace rtprobe
{
thread_local bool inAudioCallback = false;
std::atomic<int> allocations { 0 };
} // namespace rtprobe

void* operator new (std::size_t n)
{
    if (rtprobe::inAudioCallback) rtprobe::allocations.fetch_add (1);
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n)
{
    if (rtprobe::inAudioCallback) rtprobe::allocations.fetch_add (1);
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
void processAll (NovaAudioProcessor& proc, const juce::AudioBuffer<float>& src, int block, bool probe = false)
{
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (int off = 0; off + block <= src.getNumSamples(); off += block)
    {
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, src, c, off, block);
        rtprobe::inAudioCallback = probe;
        proc.processBlock (buf, midi);
        rtprobe::inAudioCallback = false;
    }
}

void pump (int ms)
{
    // Parameter atomics and APVTS::copyState() are synchronous; nothing here needs a running
    // message loop (the plugin is built with JUCE_MODAL_LOOPS_PERMITTED=0), so just yield.
    juce::Thread::sleep (ms);
}
} // namespace

TEST_CASE ("processBlock is real-time safe: no heap allocation with every module enabled", "[rt][processor]")
{
    NovaAudioProcessor proc;
    proc.prepareToPlay (48000.0, 256);
    ChainSettings s;
    for (int i = 0; i < P::Count; ++i)
        if (kParams[(size_t) i].kind == ParamKind::Bool && i != P::Bypass && i != P::MonitorA && i != P::Delta)
            s[i] = 1.f;
    s[P::Delta] = 0; s[P::MonitorA] = 0;
    proc.applySettings (s);
    auto voice = test::makeVoice ({});
    // warm up (first-time lazy init outside the probe)
    processAll (proc, voice, 256, false);
    rtprobe::allocations.store (0);
    processAll (proc, voice, 256, true);
    CHECK (rtprobe::allocations.load() == 0);
    // host-bypass path too
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    rtprobe::inAudioCallback = true;
    proc.processBlockBypassed (buf, midi);
    rtprobe::inAudioCallback = false;
    CHECK (rtprobe::allocations.load() == 0);
}

TEST_CASE ("Processor: neutral passthrough, reported latency and A/B loudness match", "[processor]")
{
    NovaAudioProcessor proc;
    proc.prepareToPlay (48000.0, 480);
    const int latency = proc.getLatencySamples();
    REQUIRE (latency == proc.getChainLatency());
    auto voice = test::makeVoice ({});
    juce::AudioBuffer<float> out (2, voice.getNumSamples());
    juce::AudioBuffer<float> buf (2, 480);
    juce::MidiBuffer midi;
    for (int off = 0; off + 480 <= voice.getNumSamples(); off += 480)
    {
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, voice, c, off, 480);
        proc.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, off, buf, c, 0, 480);
    }
    double maxErr = 0;
    for (int i = latency; i < out.getNumSamples() - 480; ++i)
        maxErr = std::max (maxErr, (double) std::abs (out.getSample (0, i) - voice.getSample (0, i - latency)));
    CHECK (maxErr < 1e-6);

    // B is 6 dB louder; with loudness match, monitoring A must come out at B's loudness
    ChainSettings s = proc.getCurrentSettings();
    s[P::OutGain] = 6.f;
    proc.applySettings (s);
    proc.setParameterValue (P::MonitorA, 1.f);
    processAll (proc, voice, 480);
    const float match = proc.getMeters().matchGainDb.load();
    CHECK_THAT (match, Catch::Matchers::WithinAbs (6.0, 0.5));
}

TEST_CASE ("End to end: realtime audio -> LISTEN -> request -> applied processing -> undo -> state restore", "[processor][integration]")
{
    auto proc = std::make_unique<NovaAudioProcessor>();
    auto& engine = proc->getEngine();
    engine.setApplyDirectly (true);
    auto settings = engine.getSettings();
    settings.provider = "offline";
    engine.setSettings (settings);
    proc->prepareToPlay (48000.0, 512);
    pump (100);

    // play the vocal through the plugin while NOVA listens (real capture path)
    test::VoiceSpec spec;
    auto voice = test::makeVoice (spec);
    engine.getAnalysis().beginListening (8.0);
    REQUIRE (engine.getAnalysis().getListenState() == analysis::AnalysisEngine::ListenState::Listening);
    for (int rep = 0; rep < 3 && engine.getAnalysis().getListenState() != analysis::AnalysisEngine::ListenState::Ready; ++rep)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int off = 0; off + 512 <= voice.getNumSamples(); off += 512)
        {
            for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, voice, c, off, 512);
            proc->processBlock (buf, midi);
            if ((off / 512) % 8 == 0) juce::Thread::sleep (2);   // give the analysis thread time, like a real-time host
        }
    }
    for (int i = 0; i < 300 && engine.getAnalysis().getListenState() != analysis::AnalysisEngine::ListenState::Ready; ++i)
        pump (20);
    REQUIRE (engine.getAnalysis().getListenState() == analysis::AnalysisEngine::ListenState::Ready);
    auto result = engine.getAnalysis().getLatestResult();
    REQUIRE (result != nullptr);
    CHECK (result->inputFeatures.valid);
    CHECK (result->inputFeatures.activeSec > 5.0);
    CHECK (result->semantic.find ("inconsistent_level") != nullptr);

    const auto before = proc->getCurrentSettings();
    engine.submitRequest ("Make this vocal more consistent and remove the harshness without making it dull.");
    REQUIRE (engine.waitUntilIdle (120000));
    pump (200);
    const auto after = proc->getCurrentSettings();
    CHECK (after.on (P::CompOn));
    CHECK (after.on (P::LvlOn));
    CHECK (after.on (P::DeqB1On));
    auto msgs = engine.getConversation().messages();
    REQUIRE (msgs.size() >= 2);
    const auto& reply = msgs.back();
    CHECK (reply.role == ai::ChatMessage::Role::Assistant);
    CHECK (reply.canUndo);
    CHECK (reply.changes.size() > 3);
    CHECK (engine.getMemory().all().size() == 1);

    // state save / restore (params + session memory + chat + snapshots)
    juce::MemoryBlock state;
    proc->getStateInformation (state);
    {
        auto proc2 = std::make_unique<NovaAudioProcessor>();
        proc2->setStateInformation (state.getData(), (int) state.getSize());
        pump (50);
        const auto restored = proc2->getCurrentSettings();
        for (int i = 0; i < P::Count; ++i)
            CHECK_THAT (restored[i], Catch::Matchers::WithinAbs (after[i], 1e-4 * std::max (1.0f, std::abs (after[i]))));
        CHECK (proc2->getEngine().getMemory().all().size() == 1);
        CHECK (proc2->getEngine().getConversation().messages().size() >= 2);
        CHECK (proc2->getEngine().canUndo());
    }

    // undo returns to the exact previous state
    REQUIRE (engine.undo());
    const auto undone = proc->getCurrentSettings();
    for (int i = 0; i < P::Count; ++i)   // normalised-parameter round trip: relative tolerance
        CHECK_THAT (undone[i], Catch::Matchers::WithinAbs (before[i], 1e-4 * std::max (1.0f, std::abs (before[i]))));
    CHECK (engine.getMemory().all().front().undone);
    CHECK (engine.redo());
    CHECK (proc->getCurrentSettings().on (P::CompOn));
}
