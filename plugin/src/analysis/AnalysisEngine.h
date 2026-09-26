#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "Features.h"
#include "Semantic.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace nova
{
class NovaAudioProcessor;

namespace analysis
{

// Result of a completed LISTEN session (or any explicit analysis of captured audio).
struct AnalysisResult
{
    double sampleRate = 0;
    std::shared_ptr<juce::AudioBuffer<float>> input;       // captured chain input (dry)
    std::shared_ptr<juce::AudioBuffer<float>> processed;   // captured chain output at the time
    AudioFeatures inputFeatures, processedFeatures;
    SemanticProfile semantic;
    WorkMode mode = WorkMode::Vocal;
    juce::int64 timestampMs = 0;
    int sessionId = 0;
};

// Small, frequently refreshed facts for the live analysis panel.
struct LiveInfo
{
    bool hasSignal = false;
    bool vocalDetected = false;
    float sourceConfidence = 0;
    float fundamentalHz = 0, presencePeakHz = 0, sibilanceHz = 0, dynamicRangeDb = 0;
    float shortTermLufs = -70, integratedLufs = -70;
    juce::String sourceName;
    juce::int64 updatedMs = 0;
};

// Background analysis thread. Consumes the lock-free capture FIFO, never touches the audio
// thread's data structures directly and never blocks it.
class AnalysisEngine : private juce::Thread
{
public:
    enum class ListenState { Idle, Listening, Analyzing, Ready };
    static constexpr int kSpectrumBins = 160;
    static constexpr int kWaveformPoints = 720;

    explicit AnalysisEngine (NovaAudioProcessor& p);
    ~AnalysisEngine() override;

    void startEngine();
    void stopEngine();
    void audioPrepared (double sampleRate);

    // LISTEN session control (message thread)
    void beginListening (double targetActiveSeconds = 20.0);
    void finishListening();         // analyse what has been collected so far (>= 3 s)
    void cancelListening();
    ListenState getListenState() const noexcept { return listenState.load(); }
    double getListenProgressSeconds() const noexcept { return listenCollected.load(); }
    double getListenTargetSeconds() const noexcept { return listenTarget.load(); }
    bool isReceivingAudio() const noexcept;

    void setWorkMode (WorkMode m) noexcept { mode.store ((int) m); }
    WorkMode getWorkMode() const noexcept { return (WorkMode) mode.load(); }

    std::shared_ptr<const AnalysisResult> getLatestResult() const;
    void setResultCallback (std::function<void (std::shared_ptr<const AnalysisResult>)> cb);

    // Copy the most recent `seconds` of captured audio (dry + processed). Returns false if not
    // enough audio has been captured yet.
    bool copyRecent (double seconds, juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& wet, double& sampleRate) const;

    // UI feeds (message thread)
    void getSpectra (std::array<float, kSpectrumBins>& dry, std::array<float, kSpectrumBins>& wet) const;
    void getWaveform (std::array<float, kWaveformPoints>& out, int& writeIndex) const;
    LiveInfo getLiveInfo() const;
    static float spectrumBinFrequency (int bin);

    // Diagnostics
    double getLastAnalysisMs() const noexcept { return lastAnalysisMs.load(); }
    juce::int64 getDroppedFrames() const noexcept;

    // Run a synchronous analysis on arbitrary audio (used by tests and tools)
    static std::shared_ptr<AnalysisResult> analyseBuffers (const juce::AudioBuffer<float>& input, const juce::AudioBuffer<float>* processed,
                                                           double sampleRate, WorkMode mode, int sessionId);

private:
    void run() override;
    void reallocate (double sr);
    void consume (const float* frames, int numFrames);
    void updateSpectrum();
    void updateLiveInfo();
    void runListenAnalysis();

    NovaAudioProcessor& processor;
    std::atomic<double> pendingRate { 0.0 };
    double sampleRate = 0.0;

    // rolling capture (analysis-thread owned; copied out under ringLock)
    mutable std::mutex ringLock;
    juce::AudioBuffer<float> ringDry, ringWet;
    int ringSize = 0, ringWrite = 0;
    juce::int64 totalCaptured = 0;
    juce::int64 lastActiveCaptureMs = 0;

    // listen session
    std::atomic<ListenState> listenState { ListenState::Idle };
    std::atomic<double> listenCollected { 0.0 }, listenTarget { 20.0 };
    std::atomic<bool> finishRequested { false }, cancelRequested { false }, startRequested { false };
    juce::AudioBuffer<float> listenDry, listenWet;
    int listenWrite = 0;
    int sessionCounter = 0;

    // results
    mutable std::mutex resultLock;
    std::shared_ptr<const AnalysisResult> latest;
    std::function<void (std::shared_ptr<const AnalysisResult>)> resultCallback;

    // UI feeds
    mutable std::mutex uiLock;
    std::array<float, kSpectrumBins> specDry {}, specWet {};
    std::array<float, kWaveformPoints> waveform {};
    int waveWrite = 0;
    float wavePeakAccum = 0.f;
    int waveAccumCount = 0, waveSamplesPerPoint = 256;
    LiveInfo live;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> fftBuf, fftWindow;
    std::atomic<int> mode { 0 };
    std::atomic<double> lastAnalysisMs { 0 };
    juce::int64 lastSpectrumMs = 0, lastLiveMs = 0;
    std::vector<float> pullBuffer;
};

} // namespace analysis
} // namespace nova
