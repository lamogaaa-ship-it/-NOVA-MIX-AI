#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "AudioCapture.h"
#include "MonitorStage.h"
#include "../dsp/NovaChain.h"

#include <atomic>
#include <memory>

namespace nova
{

class NovaEngine;

// Level meters published by the audio thread (relaxed atomics, UI applies ballistics).
struct RealtimeMeters
{
    std::atomic<float> inPeak[2] {}, inRms[2] {}, outPeak[2] {}, outRms[2] {};
    std::atomic<float> riderGainDb { 0 }, compGrDb { 0 }, dessGrDb { 0 }, limGrDb { 0 };
    std::atomic<float> dynCutDb[kNumDynBands] {};
    std::atomic<float> matchGainDb { 0 };
    std::atomic<int64_t> blocksProcessed { 0 };
    std::atomic<float> callbackLoad { 0 };     // fraction of the real-time budget used (0..1+)
    std::atomic<float> maxCallbackLoad { 0 };
};

struct TransportSnapshot
{
    double bpm = 120.0;
    double ppq = 0.0;
    bool hasPpq = false;
    bool isPlaying = false;
    bool hostProvidesTempo = false;
    int timeSigNum = 4, timeSigDen = 4;
};

class NovaAudioProcessor final : public juce::AudioProcessor
{
public:
    NovaAudioProcessor();
    ~NovaAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==============================================================================
    // Non-realtime API used by the engine / UI (message or worker threads)
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    ChainSettings getCurrentSettings() const noexcept;
    // Writes parameter values through the host-notifying path (call on the message thread).
    void applySettings (const ChainSettings& s);
    void setParameterValue (int index, float value);
    ChainOrder getChainOrder() const noexcept { return unpackChainOrder (chainOrderPacked.load()); }
    void setChainOrder (const ChainOrder& order) noexcept { chainOrderPacked.store (packChainOrder (order)); }

    AudioCapture& getCapture() noexcept { return capture; }
    const RealtimeMeters& getMeters() const noexcept { return meters; }
    TransportSnapshot getTransport() const noexcept;
    double getPreparedSampleRate() const noexcept { return preparedRate.load(); }
    int getChainLatency() const noexcept { return chainLatency.load(); }
    NovaEngine& getEngine() noexcept { return *engine; }
    bool isPrepared() const noexcept { return preparedRate.load() > 0.0; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static constexpr int kMaxInternalBlock = 512;

private:
    juce::AudioProcessorValueTreeState apvts;
    std::array<std::atomic<float>*, P::Count> paramPtrs {};
    std::array<juce::RangedAudioParameter*, P::Count> paramObjects {};

    dsp::NovaChain chain;
    MonitorStage monitor;
    AudioCapture capture;
    RealtimeMeters meters;
    dsp::ProcessContext ctx;
    juce::AudioBuffer<float> dryScratch;
    std::atomic<uint64_t> chainOrderPacked { packChainOrder (defaultChainOrder()) };
    std::atomic<double> preparedRate { 0.0 };
    std::atomic<int> chainLatency { 0 };

    std::atomic<double> tBpm { 120.0 }, tPpq { 0.0 };
    std::atomic<bool> tPlaying { false }, tHasPpq { false }, tHostTempo { false };
    std::atomic<int> tSigNum { 4 }, tSigDen { 4 };

    std::unique_ptr<NovaEngine> engine;

    void processInternal (juce::AudioBuffer<float>& buffer, bool forceBypass) noexcept;
    void updateTransport() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NovaAudioProcessor)
};

} // namespace nova
