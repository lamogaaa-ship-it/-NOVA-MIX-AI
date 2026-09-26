#include "AnalysisEngine.h"
#include "Analyzer.h"
#include "../core/PluginProcessor.h"

namespace nova::analysis
{

static constexpr double kRingSeconds = 20.0;
static constexpr double kListenMaxSeconds = 30.0;
static constexpr int kPullFrames = 2048;
static constexpr int kSpecOrder = 12;

AnalysisEngine::AnalysisEngine (NovaAudioProcessor& p) : juce::Thread ("NOVA Analysis"), processor (p)
{
    fft = std::make_unique<juce::dsp::FFT> (kSpecOrder);
    const int N = 1 << kSpecOrder;
    fftBuf.assign ((size_t) N * 2, 0.f);
    fftWindow.resize ((size_t) N);
    for (int i = 0; i < N; ++i) fftWindow[(size_t) i] = 0.5f - 0.5f * std::cos (2.f * juce::MathConstants<float>::pi * (float) i / (float) N);
    pullBuffer.assign ((size_t) kPullFrames * AudioCapture::kStride, 0.f);
    specDry.fill (-100.f);
    specWet.fill (-100.f);
}

AnalysisEngine::~AnalysisEngine() { stopEngine(); }

void AnalysisEngine::startEngine()
{
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);
}

void AnalysisEngine::stopEngine() { stopThread (2000); }

void AnalysisEngine::audioPrepared (double sr) { pendingRate.store (sr); }

float AnalysisEngine::spectrumBinFrequency (int bin)
{
    return 20.f * std::pow (1000.f, (float) bin / (float) (kSpectrumBins - 1));   // 20 Hz .. 20 kHz
}

juce::int64 AnalysisEngine::getDroppedFrames() const noexcept
{
    return processor.getCapture().droppedFrames();
}

bool AnalysisEngine::isReceivingAudio() const noexcept
{
    std::lock_guard<std::mutex> g (ringLock);
    return juce::Time::currentTimeMillis() - lastActiveCaptureMs < 400;
}

void AnalysisEngine::reallocate (double sr)
{
    std::lock_guard<std::mutex> g (ringLock);
    sampleRate = sr;
    ringSize = (int) (sr * kRingSeconds);
    ringDry.setSize (2, ringSize, false, true, false);
    ringWet.setSize (2, ringSize, false, true, false);
    ringDry.clear(); ringWet.clear();
    ringWrite = 0;
    totalCaptured = 0;
    const int listenMax = (int) (std::min (sr, 96000.0) * kListenMaxSeconds);
    listenDry.setSize (2, listenMax, false, true, false);
    listenWet.setSize (2, listenMax, false, true, false);
    listenWrite = 0;
    waveSamplesPerPoint = std::max (32, (int) (sr * 4.0 / kWaveformPoints));
}

//==============================================================================
void AnalysisEngine::beginListening (double target)
{
    listenTarget.store (std::clamp (target, 3.0, kListenMaxSeconds));
    cancelRequested.store (false);
    finishRequested.store (false);
    startRequested.store (true);
    listenState.store (ListenState::Listening);
}

void AnalysisEngine::finishListening() { finishRequested.store (true); }

void AnalysisEngine::cancelListening()
{
    cancelRequested.store (true);
}

std::shared_ptr<const AnalysisResult> AnalysisEngine::getLatestResult() const
{
    std::lock_guard<std::mutex> g (resultLock);
    return latest;
}

void AnalysisEngine::setResultCallback (std::function<void (std::shared_ptr<const AnalysisResult>)> cb)
{
    std::lock_guard<std::mutex> g (resultLock);
    resultCallback = std::move (cb);
}

bool AnalysisEngine::copyRecent (double seconds, juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& wet, double& sr) const
{
    std::lock_guard<std::mutex> g (ringLock);
    if (ringSize <= 0 || sampleRate <= 0) return false;
    const int want = std::min ((int) (seconds * sampleRate), (int) std::min<juce::int64> (totalCaptured, ringSize));
    if (want < (int) (0.5 * sampleRate)) return false;
    sr = sampleRate;
    dry.setSize (2, want, false, true, false);
    wet.setSize (2, want, false, true, false);
    const int start = (ringWrite - want + ringSize) % ringSize;
    for (int c = 0; c < 2; ++c)
    {
        const int first = std::min (want, ringSize - start);
        dry.copyFrom (c, 0, ringDry, c, start, first);
        wet.copyFrom (c, 0, ringWet, c, start, first);
        if (first < want)
        {
            dry.copyFrom (c, first, ringDry, c, 0, want - first);
            wet.copyFrom (c, first, ringWet, c, 0, want - first);
        }
    }
    return true;
}

void AnalysisEngine::getSpectra (std::array<float, kSpectrumBins>& d, std::array<float, kSpectrumBins>& w) const
{
    std::lock_guard<std::mutex> g (uiLock);
    d = specDry; w = specWet;
}

void AnalysisEngine::getWaveform (std::array<float, kWaveformPoints>& out, int& writeIndex) const
{
    std::lock_guard<std::mutex> g (uiLock);
    out = waveform;
    writeIndex = waveWrite;
}

LiveInfo AnalysisEngine::getLiveInfo() const
{
    std::lock_guard<std::mutex> g (uiLock);
    return live;
}

//==============================================================================
void AnalysisEngine::run()
{
    while (! threadShouldExit())
    {
        const double pr = pendingRate.load();
        if (pr > 0 && std::abs (pr - sampleRate) > 0.5)
            reallocate (pr);

        if (sampleRate > 0)
        {
            auto& cap = processor.getCapture();
            int guard = 0;
            while (cap.available() > 0 && ++guard < 64)
            {
                const int got = cap.pull (pullBuffer.data(), kPullFrames);
                if (got <= 0) break;
                consume (pullBuffer.data(), got);
            }

            const auto now = juce::Time::currentTimeMillis();
            if (now - lastSpectrumMs >= 30) { updateSpectrum(); lastSpectrumMs = now; }
            if (now - lastLiveMs >= 2000) { updateLiveInfo(); lastLiveMs = now; }

            if (cancelRequested.exchange (false))
            {
                listenWrite = 0;
                listenCollected.store (0);
                listenState.store (ListenState::Idle);
            }
            if (startRequested.exchange (false))
            {
                listenWrite = 0;
                listenSilentRun = 0;
                listenCollected.store (0);
            }
            if (listenState.load() == ListenState::Listening)
            {
                const double collected = listenWrite / sampleRate;
                const bool finish = finishRequested.load() && collected >= 3.0;
                if (collected >= listenTarget.load() || finish || listenWrite >= listenDry.getNumSamples())
                {
                    finishRequested.store (false);
                    listenState.store (ListenState::Analyzing);
                    runListenAnalysis();
                }
            }
        }
        wait (8);
    }
}

void AnalysisEngine::consume (const float* frames, int n)
{
    // activity of this chunk (dry)
    double sq = 0;
    for (int i = 0; i < n; ++i)
    {
        const float* f = frames + (size_t) i * AudioCapture::kStride;
        sq += 0.5 * ((double) f[0] * f[0] + (double) f[1] * f[1]);
    }
    const bool active = sq / std::max (1, n) > 3.0e-7;   // ~ -65 dBFS

    {
        std::lock_guard<std::mutex> g (ringLock);
        for (int i = 0; i < n; ++i)
        {
            const float* f = frames + (size_t) i * AudioCapture::kStride;
            ringDry.setSample (0, ringWrite, f[0]); ringDry.setSample (1, ringWrite, f[1]);
            ringWet.setSample (0, ringWrite, f[2]); ringWet.setSample (1, ringWrite, f[3]);
            ringWrite = (ringWrite + 1) % ringSize;
        }
        totalCaptured += n;
        if (active) lastActiveCaptureMs = juce::Time::currentTimeMillis();
    }

    // While listening keep the natural phrasing: gaps up to 0.6 s are kept (they define phrases,
    // breaths and reverb tails); longer silences (transport stopped, empty bars) are skipped.
    if (listenState.load() == ListenState::Listening)
    {
        if (active) listenSilentRun = 0;
        else listenSilentRun += n;
    }
    const bool keepForListen = listenState.load() == ListenState::Listening
                               && (active || (listenWrite > 0 && listenSilentRun <= (int) (0.6 * sampleRate)));
    if (keepForListen)
    {
        const int space = listenDry.getNumSamples() - listenWrite;
        const int take = std::min (space, n);
        for (int i = 0; i < take; ++i)
        {
            const float* f = frames + (size_t) i * AudioCapture::kStride;
            listenDry.setSample (0, listenWrite + i, f[0]); listenDry.setSample (1, listenWrite + i, f[1]);
            listenWet.setSample (0, listenWrite + i, f[2]); listenWet.setSample (1, listenWrite + i, f[3]);
        }
        listenWrite += take;
        listenCollected.store (listenWrite / sampleRate);
    }

    // waveform envelope of the processed signal
    std::lock_guard<std::mutex> g (uiLock);
    for (int i = 0; i < n; ++i)
    {
        const float* f = frames + (size_t) i * AudioCapture::kStride;
        wavePeakAccum = std::max (wavePeakAccum, std::abs (0.5f * (f[2] + f[3])));
        if (++waveAccumCount >= waveSamplesPerPoint)
        {
            waveform[(size_t) waveWrite] = wavePeakAccum;
            waveWrite = (waveWrite + 1) % kWaveformPoints;
            wavePeakAccum = 0.f;
            waveAccumCount = 0;
        }
    }
}

void AnalysisEngine::updateSpectrum()
{
    const int N = 1 << kSpecOrder;
    std::array<float, kSpectrumBins> newDry {}, newWet {};
    {
        std::lock_guard<std::mutex> g (ringLock);
        if (totalCaptured < N) return;
        for (int pass = 0; pass < 2; ++pass)
        {
            auto& src = pass == 0 ? ringDry : ringWet;
            auto& dst = pass == 0 ? newDry : newWet;
            std::fill (fftBuf.begin(), fftBuf.end(), 0.f);
            const int start = (ringWrite - N + ringSize) % ringSize;
            for (int i = 0; i < N; ++i)
            {
                const int k = (start + i) % ringSize;
                fftBuf[(size_t) i] = 0.5f * (src.getSample (0, k) + src.getSample (1, k)) * fftWindow[(size_t) i];
            }
            fft->performFrequencyOnlyForwardTransform (fftBuf.data(), true);
            const float norm = 2.f / (0.5f * (float) N);   // full-scale sine -> 0 dB
            const double binHz = sampleRate / N;
            for (int b = 0; b < kSpectrumBins; ++b)
            {
                const float fLo = spectrumBinFrequency (b) * std::pow (1000.f, -0.5f / (kSpectrumBins - 1));
                const float fHi = spectrumBinFrequency (b) * std::pow (1000.f, 0.5f / (kSpectrumBins - 1));
                int k0 = (int) std::floor (fLo / binHz), k1 = (int) std::ceil (fHi / binHz);
                k0 = std::clamp (k0, 1, N / 2 - 1); k1 = std::clamp (k1, k0, N / 2 - 1);
                float mx = 0.f;
                for (int k = k0; k <= k1; ++k) mx = std::max (mx, fftBuf[(size_t) k]);
                dst[(size_t) b] = juce::Decibels::gainToDecibels (mx * norm, -100.f);
            }
        }
    }
    std::lock_guard<std::mutex> g (uiLock);
    for (int b = 0; b < kSpectrumBins; ++b)
    {
        specDry[(size_t) b] = std::max (newDry[(size_t) b], specDry[(size_t) b] - 1.5f);
        specWet[(size_t) b] = std::max (newWet[(size_t) b], specWet[(size_t) b] - 1.5f);
    }
}

void AnalysisEngine::updateLiveInfo()
{
    juce::AudioBuffer<float> dry, wet;
    double sr = 0;
    if (! isReceivingAudio() || ! copyRecent (6.0, dry, wet, sr))
    {
        std::lock_guard<std::mutex> g (uiLock);
        live.hasSignal = false;
        return;
    }
    AnalysisOptions opt;
    opt.structure = false; opt.space = false; opt.stereo = false;
    opt.hint = getWorkMode() == WorkMode::Vocal ? SourceType::LeadVocal : SourceType::FullMix;
    const auto f = Analyzer::analyze (dry.getArrayOfReadPointers(), 2, dry.getNumSamples(), sr, opt);
    const auto fw = Analyzer::measureLoudness (wet.getArrayOfReadPointers(), 2, wet.getNumSamples(), sr);

    std::lock_guard<std::mutex> g (uiLock);
    live.hasSignal = f.valid;
    live.vocalDetected = f.valid && (f.source == SourceType::LeadVocal || f.source == SourceType::Speech) && f.sourceConfidence > 0.35f;
    live.sourceConfidence = f.sourceConfidence;
    live.sourceName = sourceTypeName (f.source);
    live.fundamentalHz = f.voicedRatio > 0.2f ? f.f0MedianHz : 0.f;
    live.presencePeakHz = f.presencePeakHz;
    live.sibilanceHz = f.sibilantEvents.size() >= 2 ? f.sibilanceCenterHz : 0.f;
    live.dynamicRangeDb = f.dynamicRangeDb;
    live.shortTermLufs = fw.shortTerm1s.empty() ? fw.integrated : fw.shortTerm1s.back();
    live.integratedLufs = fw.integrated;
    live.updatedMs = juce::Time::currentTimeMillis();
}

std::shared_ptr<AnalysisResult> AnalysisEngine::analyseBuffers (const juce::AudioBuffer<float>& input, const juce::AudioBuffer<float>* processed,
                                                                double sr, WorkMode m, int sessionId)
{
    auto r = std::make_shared<AnalysisResult>();
    r->sampleRate = sr;
    r->mode = m;
    r->sessionId = sessionId;
    r->timestampMs = juce::Time::currentTimeMillis();
    r->input = std::make_shared<juce::AudioBuffer<float>> (input);
    if (processed != nullptr) r->processed = std::make_shared<juce::AudioBuffer<float>> (*processed);

    AnalysisOptions opt;
    opt.hint = m == WorkMode::Vocal ? SourceType::LeadVocal : SourceType::FullMix;
    r->inputFeatures = Analyzer::analyze (input.getArrayOfReadPointers(), input.getNumChannels(), input.getNumSamples(), sr, opt);
    if (processed != nullptr)
    {
        AnalysisOptions fast = opt;
        fast.pitch = false;
        r->processedFeatures = Analyzer::analyze (processed->getArrayOfReadPointers(), processed->getNumChannels(), processed->getNumSamples(), sr, fast);
    }
    r->semantic = describe (r->inputFeatures, m);
    return r;
}

void AnalysisEngine::runListenAnalysis()
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const int n = listenWrite;
    juce::AudioBuffer<float> dry (2, n), wet (2, n);
    for (int c = 0; c < 2; ++c)
    {
        dry.copyFrom (c, 0, listenDry, c, 0, n);
        wet.copyFrom (c, 0, listenWet, c, 0, n);
    }
    auto result = analyseBuffers (dry, &wet, sampleRate, getWorkMode(), ++sessionCounter);
    lastAnalysisMs.store (juce::Time::getMillisecondCounterHiRes() - t0);

    std::function<void (std::shared_ptr<const AnalysisResult>)> cb;
    {
        std::lock_guard<std::mutex> g (resultLock);
        latest = result;
        cb = resultCallback;
    }
    listenState.store (ListenState::Ready);
    if (cb) cb (result);
}

} // namespace nova::analysis
