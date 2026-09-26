#include "Analyzer.h"
#include "../dsp/Modules.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

namespace nova::analysis
{

using dsp::Biquad;
using dsp::BiquadCoeffs;
using dsp::kPi;

const std::array<float, kThirdOctaveBands> kThirdOctaveCentres {
    20.f, 25.f, 31.5f, 40.f, 50.f, 63.f, 80.f, 100.f, 125.f, 160.f, 200.f, 250.f, 315.f, 400.f, 500.f, 630.f,
    800.f, 1000.f, 1250.f, 1600.f, 2000.f, 2500.f, 3150.f, 4000.f, 5000.f, 6300.f, 8000.f, 10000.f, 12500.f, 16000.f, 20000.f
};

const char* sourceTypeName (SourceType t)
{
    switch (t)
    {
        case SourceType::LeadVocal:    return "lead_vocal";
        case SourceType::BackingVocal: return "backing_vocal";
        case SourceType::FullMix:      return "full_mix";
        case SourceType::Drums:        return "drums";
        case SourceType::Bass:         return "bass";
        case SourceType::Instrument:   return "instrument";
        case SourceType::Speech:       return "speech";
        case SourceType::Unknown:      break;
    }
    return "unknown";
}

double percentile (std::vector<float> v, double p)
{
    if (v.empty()) return 0.0;
    std::sort (v.begin(), v.end());
    const double pos = std::clamp (p, 0.0, 100.0) / 100.0 * (double) (v.size() - 1);
    const auto lo = (size_t) std::floor (pos);
    const auto hi = std::min (v.size() - 1, lo + 1);
    const double f = pos - (double) lo;
    return (double) v[lo] * (1.0 - f) + (double) v[hi] * f;
}

float median (std::vector<float> v) { return (float) percentile (std::move (v), 50.0); }

static inline float powDb (double p) { return (float) (10.0 * std::log10 (p + 1e-20)); }

static float stddev (const std::vector<float>& v)
{
    if (v.size() < 2) return 0.f;
    const double m = std::accumulate (v.begin(), v.end(), 0.0) / (double) v.size();
    double s = 0;
    for (float x : v) s += (x - m) * (x - m);
    return (float) std::sqrt (s / (double) (v.size() - 1));
}

//==============================================================================
// Loudness (ITU-R BS.1770-4, gating per EBU R128; LRA per EBU Tech 3342)
Analyzer::Loudness Analyzer::measureLoudness (const float* const* ch, int numCh, int n, double sr)
{
    Loudness out;
    const int hop = (int) std::lround (sr * 0.1);
    if (n < hop || numCh <= 0) return out;
    const int numHops = n / hop;
    std::vector<double> e100 ((size_t) numHops, 0.0);   // sum over channels of mean-square per 100 ms

    for (int c = 0; c < numCh; ++c)
    {
        dsp::KWeighting kw;
        kw.prepare (sr);
        for (int h = 0; h < numHops; ++h)
        {
            double acc = 0;
            const float* x = ch[c] + (size_t) h * (size_t) hop;
            for (int i = 0; i < hop; ++i)
            {
                const double y = kw.process (x[i], 0);
                acc += y * y;
            }
            e100[(size_t) h] += acc / hop;
        }
    }

    auto lufs = [] (double ms) { return (float) (-0.691 + 10.0 * std::log10 (ms + 1e-20)); };

    // 400 ms blocks with 75 % overlap
    std::vector<double> blockMs;
    for (int h = 3; h < numHops; ++h)
    {
        const double ms = (e100[(size_t) h] + e100[(size_t) h - 1] + e100[(size_t) h - 2] + e100[(size_t) h - 3]) * 0.25;
        blockMs.push_back (ms);
        out.momentary100ms.push_back (lufs (ms));
        out.momentaryMax = std::max (out.momentaryMax, lufs (ms));
    }
    // integrated: absolute gate -70, relative gate -10 LU
    {
        double sum = 0; int cnt = 0;
        for (double ms : blockMs) if (lufs (ms) > -70.f) { sum += ms; ++cnt; }
        if (cnt > 0)
        {
            const float rel = lufs (sum / cnt) - 10.f;
            double sum2 = 0; int cnt2 = 0;
            for (double ms : blockMs) if (lufs (ms) > -70.f && lufs (ms) > rel) { sum2 += ms; ++cnt2; }
            if (cnt2 > 0) out.integrated = lufs (sum2 / cnt2);
        }
    }
    // short-term (3 s) at 10 Hz and 1 Hz
    std::vector<float> st;
    for (int h = 29; h < numHops; ++h)
    {
        double ms = 0;
        for (int k = 0; k < 30; ++k) ms += e100[(size_t) (h - k)];
        const float l = lufs (ms / 30.0);
        st.push_back (l);
        out.shortTermMax = std::max (out.shortTermMax, l);
        if ((h - 29) % 10 == 0) out.shortTerm1s.push_back (l);
    }
    out.shortTerm100ms = st;
    // LRA: absolute gate -70, relative gate -20 LU, 10th..95th percentile
    {
        std::vector<float> gated;
        double sum = 0; int cnt = 0;
        for (float l : st) if (l > -70.f) { sum += std::pow (10.0, (l + 0.691) / 10.0); ++cnt; }
        if (cnt > 0)
        {
            const float rel = lufs (sum / cnt) - 20.f;
            for (float l : st) if (l > -70.f && l > rel) gated.push_back (l);
            if (gated.size() >= 2)
                out.lra = (float) (percentile (gated, 95.0) - percentile (gated, 10.0));
        }
    }
    return out;
}

//==============================================================================
// True peak: 4x oversampled (windowed-sinc polyphase interpolator, BS.1770-4 Annex 2 style)
float Analyzer::truePeakDb (const float* const* ch, int numCh, int n)
{
    constexpr int kPhases = 4, kTapsPerPhase = 12, kTaps = kPhases * kTapsPerPhase;
    static const auto coeffs = []
    {
        std::array<float, kTaps> h {};
        const double centre = (kTaps - 1) / 2.0;
        for (int i = 0; i < kTaps; ++i)
        {
            const double t = (i - centre) / kPhases;
            const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (kPi * t) / (kPi * t);
            const double w = 0.5 * (1 - std::cos (2 * kPi * (i + 0.5) / kTaps));   // Hann
            h[(size_t) i] = (float) (sinc * w);
        }
        return h;
    }();

    float maxAbs = 0.f;
    for (int c = 0; c < numCh; ++c)
    {
        const float* x = ch[c];
        for (int i = 0; i < n; ++i) maxAbs = std::max (maxAbs, std::abs (x[i]));
        for (int i = kTapsPerPhase; i < n; ++i)
            for (int p = 0; p < kPhases; ++p)
            {
                float acc = 0.f;
                for (int k = 0; k < kTapsPerPhase; ++k)
                    acc += coeffs[(size_t) (k * kPhases + p)] * x[i - k];
                maxAbs = std::max (maxAbs, std::abs (acc));
            }
    }
    return dsp::gainToDb (maxAbs);
}

//==============================================================================
// YIN pitch tracking on a decimated (~16 kHz) copy
Analyzer::PitchTrack Analyzer::trackPitch (const float* mono, int n, double sr, float minHz, float maxHz)
{
    PitchTrack out;
    const int D = std::max (1, (int) std::lround (sr / 16000.0));
    const double sd = sr / D;
    Biquad a1, a2;
    a1.setCoeffs (BiquadCoeffs::lowPass (sr, 0.42 * sd, 0.54119610));
    a2.setCoeffs (BiquadCoeffs::lowPass (sr, 0.42 * sd, 1.30656296));
    std::vector<float> y;
    y.reserve ((size_t) (n / D + 1));
    for (int i = 0; i < n; ++i)
    {
        const float v = a2.process (a1.process (mono[i], 0), 0);
        if (i % D == 0) y.push_back (v);
    }

    const int W = 512, hop = 256;
    const int maxTau = std::min (W - 2, (int) (sd / minHz));
    const int minTau = std::max (2, (int) (sd / maxHz));
    out.frameRate = sd / hop;
    std::vector<float> d ((size_t) maxTau + 1), cmnd ((size_t) maxTau + 1);

    for (int start = 0; start + W + maxTau < (int) y.size(); start += hop)
    {
        const float* x = y.data() + start;
        double energy = 0;
        for (int j = 0; j < W; ++j) energy += (double) x[j] * x[j];
        const float lvl = powDb (energy / W);
        out.levelDb.push_back (lvl);
        if (lvl < -60.f) { out.f0.push_back (0.f); out.aperiodicity.push_back (1.f); continue; }

        d[0] = 0.f;
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            double s = 0;
            for (int j = 0; j < W; ++j)
            {
                const double diff = (double) x[j] - x[j + tau];
                s += diff * diff;
            }
            d[(size_t) tau] = (float) s;
        }
        cmnd[0] = 1.f;
        double running = 0;
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            running += d[(size_t) tau];
            cmnd[(size_t) tau] = running > 0 ? (float) (d[(size_t) tau] * tau / running) : 1.f;
        }
        int best = -1;
        for (int tau = minTau; tau <= maxTau; ++tau)
        {
            if (cmnd[(size_t) tau] < 0.15f)
            {
                while (tau + 1 <= maxTau && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {   // no dip below threshold: take global minimum but mark as weakly periodic
            best = minTau;
            for (int tau = minTau; tau <= maxTau; ++tau)
                if (cmnd[(size_t) tau] < cmnd[(size_t) best]) best = tau;
        }
        const float ap = cmnd[(size_t) best];
        double tauF = best;
        if (best > minTau && best < maxTau)
        {
            const double s0 = cmnd[(size_t) best - 1], s1 = cmnd[(size_t) best], s2 = cmnd[(size_t) best + 1];
            const double den = s0 + s2 - 2 * s1;
            if (std::abs (den) > 1e-12) tauF = best + 0.5 * (s0 - s2) / den;
        }
        const bool voiced = ap < 0.25f;
        out.f0.push_back (voiced ? (float) (sd / tauF) : 0.f);
        out.aperiodicity.push_back (ap);
    }
    return out;
}

//==============================================================================
std::array<float, kThirdOctaveBands> Analyzer::thirdOctaveFromPowerSpectrum (const std::vector<double>& p, double sr, int N)
{
    std::array<float, kThirdOctaveBands> out {};
    const double binHz = sr / N;
    double total = 0;
    for (size_t k = 1; k < p.size(); ++k) if (k * binHz >= 18.0) total += p[k];
    for (int b = 0; b < kThirdOctaveBands; ++b)
    {
        const double fc = kThirdOctaveCentres[(size_t) b];
        const double lo = fc * std::pow (2.0, -1.0 / 6.0), hi = fc * std::pow (2.0, 1.0 / 6.0);
        double sum = 0;
        for (size_t k = 1; k < p.size(); ++k)
        {
            const double f = k * binHz;
            if (f >= lo && f < hi) sum += p[k];
        }
        // bands narrower than a bin: interpolate from the nearest bin to avoid holes
        if (sum <= 0 && fc < sr * 0.5)
        {
            const auto k = (size_t) std::clamp ((int) std::lround (fc / binHz), 1, (int) p.size() - 1);
            sum = p[k] * ((hi - lo) / binHz);
        }
        out[(size_t) b] = fc < sr * 0.5 ? powDb (sum / (total + 1e-30)) : -100.f;
    }
    return out;
}

//==============================================================================
namespace
{
struct FrameInfo
{
    double t = 0;
    float levelDb = -120;      // mean-square of the frame (dBFS)
    float centroid = 0, rolloff = 0, flatness = 0, flux = 0;
    double pTotal = 0, pSib = 0, pHarsh = 0, pLow = 0, pAir = 0;
    float sibPeakHz = 0, harshPeakHz = 0;
    bool active = false;
};

// group consecutive flagged frames into events
std::vector<std::pair<int, int>> groupRuns (const std::vector<bool>& flags, int maxGap, int minLen)
{
    std::vector<std::pair<int, int>> runs;
    int start = -1, lastOn = -1;
    for (int i = 0; i < (int) flags.size(); ++i)
    {
        if (flags[(size_t) i])
        {
            if (start < 0) start = i;
            lastOn = i;
        }
        else if (start >= 0 && i - lastOn > maxGap)
        {
            if (lastOn - start + 1 >= minLen) runs.emplace_back (start, lastOn);
            start = -1;
        }
    }
    if (start >= 0 && lastOn - start + 1 >= minLen) runs.emplace_back (start, lastOn);
    return runs;
}
} // namespace

AudioFeatures Analyzer::analyze (const float* const* ch, int numCh, int n, double sr, const AnalysisOptions& opt)
{
    const auto t0 = std::chrono::steady_clock::now();
    AudioFeatures f;
    f.sampleRate = sr;
    f.numChannels = numCh;
    f.durationSec = n / sr;
    if (numCh <= 0 || n < (int) (0.5 * sr))
        return f;
    numCh = std::min (numCh, 2);
    const float* L = ch[0];
    const float* R = numCh > 1 ? ch[1] : ch[0];

    //--------------------------------------------------------------------------
    // Basic sample statistics
    double sumSq = 0, sumDc = 0;
    float pk = 0.f;
    int clipRun = 0;
    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < n; ++i)
        {
            const float v = ch[c][i];
            pk = std::max (pk, std::abs (v));
            sumSq += (double) v * v;
            sumDc += v;
            if (std::abs (v) >= 0.9999f) { if (++clipRun == 3) f.clippedSamples += 3; else if (clipRun > 3) ++f.clippedSamples; }
            else clipRun = 0;
        }
    f.peakDb = dsp::gainToDb (pk);
    f.rmsDb = powDb (sumSq / ((double) n * numCh));
    f.crestDb = f.peakDb - f.rmsDb;
    f.dcOffset = (float) (sumDc / ((double) n * numCh));
    f.truePeakDb = truePeakDb (ch, numCh, n);

    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (L[i] + R[i]);

    //--------------------------------------------------------------------------
    // Loudness
    const auto loud = measureLoudness (ch, numCh, n, sr);
    f.integratedLufs = loud.integrated;
    f.momentaryMaxLufs = loud.momentaryMax;
    f.shortTermMaxLufs = loud.shortTermMax;
    f.lra = loud.lra;
    f.shortTermLufs = loud.shortTerm1s;
    f.plr = f.truePeakDb - f.integratedLufs;

    //--------------------------------------------------------------------------
    // 10 ms K-weighted envelope (dB, channels summed like BS.1770)
    const int hop10 = std::max (1, (int) std::lround (sr * 0.01));
    const int numEnv = n / hop10;
    std::vector<float> env ((size_t) numEnv, -120.f);
    {
        std::vector<double> e ((size_t) numEnv, 0.0);
        for (int c = 0; c < numCh; ++c)
        {
            dsp::KWeighting kw;
            kw.prepare (sr);
            for (int h = 0; h < numEnv; ++h)
            {
                double acc = 0;
                for (int i = 0; i < hop10; ++i)
                {
                    const double y = kw.process (ch[c][(size_t) h * (size_t) hop10 + (size_t) i], 0);
                    acc += y * y;
                }
                e[(size_t) h] += acc / hop10;
            }
        }
        for (int h = 0; h < numEnv; ++h) env[(size_t) h] = (float) (-0.691 + 10.0 * std::log10 (e[(size_t) h] + 1e-20));
    }

    const float envMax = percentile (env, 99.5) > -200 ? (float) percentile (env, 99.5) : -120.f;
    std::vector<float> nonDigitalSilence;
    for (float v : env) if (v > -110.f) nonDigitalSilence.push_back (v);
    // The quietest frames are only a noise floor if they sit well below the programme; for
    // continuous material (a mix, sustained noise) they are signal and the floor is unknown.
    const float quietest = nonDigitalSilence.empty() ? -120.f : (float) percentile (nonDigitalSilence, 5.0);
    const bool floorMeasurable = quietest < envMax - 35.f;
    f.noiseFloorDb = floorMeasurable ? quietest : -120.f;
    const float activeThr = std::max ({ -60.f, envMax - 42.f, floorMeasurable ? f.noiseFloorDb + 10.f : -120.f });
    std::vector<bool> activeEnv ((size_t) numEnv);
    int activeCount = 0;
    for (int h = 0; h < numEnv; ++h)
    {
        activeEnv[(size_t) h] = env[(size_t) h] > activeThr;
        activeCount += activeEnv[(size_t) h] ? 1 : 0;
    }
    f.activeSec = activeCount * 0.01;
    f.silenceRatio = numEnv > 0 ? 1.f - (float) activeCount / (float) numEnv : 1.f;
    if (activeCount < 20)
    {
        f.valid = false;   // not enough signal to say anything meaningful
        f.analysisMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        return f;
    }
    f.valid = true;

    // 50 ms levels over active audio -> macro dynamic range
    {
        std::vector<float> lv50;
        for (int h = 0; h + 5 <= numEnv; h += 5)
        {
            double e = 0; int a = 0;
            for (int k = 0; k < 5; ++k) { e += std::pow (10.0, (env[(size_t) (h + k)] + 0.691) / 10.0); a += activeEnv[(size_t) (h + k)] ? 1 : 0; }
            if (a >= 3) lv50.push_back ((float) (-0.691 + 10.0 * std::log10 (e / 5.0)));
        }
        if (lv50.size() > 4)
            f.dynamicRangeDb = (float) (percentile (lv50, 95.0) - percentile (lv50, 10.0));
    }
    {
        std::vector<float> stActive;
        for (float v : loud.shortTerm100ms) if (v > activeThr) stActive.push_back (v);
        f.shortTermStdDb = stddev (stActive);
    }

    //--------------------------------------------------------------------------
    // Phrases: active regions, gaps < 200 ms merged, min 250 ms
    {
        auto runs = groupRuns (activeEnv, 20, 25);
        std::vector<float> phraseLevels, micro;
        for (auto [a, b] : runs)
        {
            Segment s;
            s.start = a * 0.01; s.end = (b + 1) * 0.01;
            double e = 0; float p = -120.f;
            std::vector<float> inside;
            for (int h = a; h <= b; ++h)
            {
                e += std::pow (10.0, (env[(size_t) h] + 0.691) / 10.0);
                p = std::max (p, env[(size_t) h]);
                if (activeEnv[(size_t) h]) inside.push_back (env[(size_t) h]);
            }
            s.loudnessDb = (float) (-0.691 + 10.0 * std::log10 (e / (b - a + 1)));
            s.peakDb = p;
            f.phrases.push_back (s);
            phraseLevels.push_back (s.loudnessDb);
            if (inside.size() > 10) micro.push_back ((float) (percentile (inside, 90) - percentile (inside, 10)));
        }
        f.phraseLevelStdDb = stddev (phraseLevels);
        f.microDynamicsDb = micro.empty() ? 0.f : median (micro);
    }

    //--------------------------------------------------------------------------
    // STFT analysis of the mono sum
    int order = 10;
    while ((1 << order) < (int) (sr * 0.04)) ++order;
    const int N = 1 << order;
    const int hop = N / 4;
    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) N), buf ((size_t) N * 2);
    double wSq = 0;
    for (int i = 0; i < N; ++i)
    {
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2 * kPi * i / N));
        wSq += (double) window[(size_t) i] * window[(size_t) i];
    }
    const double binHz = sr / N;
    const int nBins = N / 2 + 1;
    const double scale = 2.0 / ((double) N * wSq);
    auto binOf = [&] (double hz) { return std::clamp ((int) std::lround (hz / binHz), 1, nBins - 1); };
    const double nyq = sr * 0.5;
    const int bSib0 = binOf (4500), bSib1 = binOf (std::min (11000.0, nyq * 0.95));
    const int bHar0 = binOf (2000), bHar1 = binOf (5000);
    const int bLow1 = binOf (150), bAir0 = binOf (std::min (10000.0, nyq * 0.8));
    const int bFlat0 = binOf (100);

    std::vector<FrameInfo> frames;
    std::vector<double> ltas ((size_t) nBins, 0.0);
    std::vector<float> prevMag ((size_t) nBins, 0.f), mag ((size_t) nBins);
    int ltasFrames = 0;
    std::vector<float> centroids;

    for (int start = 0; start + N <= n; start += hop)
    {
        std::fill (buf.begin(), buf.end(), 0.f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = mono[(size_t) (start + i)] * window[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);

        FrameInfo fi;
        fi.t = (start + N * 0.5) / sr;
        double total = 0, wsum = 0, logSum = 0; int flatCount = 0;
        double flux = 0;
        float sibPk = 0, harPk = 0; int sibPkBin = bSib0, harPkBin = bHar0;
        for (int k = 1; k < nBins; ++k)
        {
            const float m = buf[(size_t) k];
            mag[(size_t) k] = m;
            const double p = (double) m * m * scale;
            total += p;
            wsum += p * k * binHz;
            if (k >= bFlat0) { logSum += std::log (p + 1e-20); ++flatCount; }
            const float d = m - prevMag[(size_t) k];
            if (d > 0) flux += d;
            if (k >= bSib0 && k <= bSib1) { fi.pSib += p; if (m > sibPk) { sibPk = m; sibPkBin = k; } }
            if (k >= bHar0 && k <= bHar1) { fi.pHarsh += p; if (m > harPk) { harPk = m; harPkBin = k; } }
            if (k <= bLow1) fi.pLow += p;
            if (k >= bAir0) fi.pAir += p;
        }
        fi.pTotal = total;
        fi.levelDb = powDb (total);
        fi.centroid = total > 0 ? (float) (wsum / total) : 0.f;
        double cum = 0;
        for (int k = 1; k < nBins; ++k)
        {
            cum += (double) mag[(size_t) k] * mag[(size_t) k] * scale;
            if (cum >= 0.85 * total) { fi.rolloff = (float) (k * binHz); break; }
        }
        double arith = 0;
        for (int k = bFlat0; k < nBins; ++k) arith += (double) mag[(size_t) k] * mag[(size_t) k] * scale;
        arith /= std::max (1, flatCount);
        fi.flatness = arith > 0 ? (float) (std::exp (logSum / std::max (1, flatCount)) / arith) : 0.f;
        fi.flux = (float) flux;
        fi.sibPeakHz = (float) (sibPkBin * binHz);
        fi.harshPeakHz = (float) (harPkBin * binHz);
        const int envIdx = std::clamp ((int) (fi.t / 0.01), 0, numEnv - 1);
        fi.active = activeEnv[(size_t) envIdx] && fi.levelDb > activeThr - 6.f;
        if (fi.active)
        {
            for (int k = 1; k < nBins; ++k) ltas[(size_t) k] += (double) mag[(size_t) k] * mag[(size_t) k] * scale;
            ++ltasFrames;
            centroids.push_back (fi.centroid);
        }
        std::copy (mag.begin(), mag.end(), prevMag.begin());
        frames.push_back (fi);
    }
    if (ltasFrames > 0)
        for (auto& v : ltas) v /= ltasFrames;

    //--------------------------------------------------------------------------
    // Long-term spectrum descriptors
    {
        f.thirdOctaveDb = thirdOctaveFromPowerSpectrum (ltas, sr, N);
        double total = 0, wsum = 0;
        for (int k = 1; k < nBins; ++k) { total += ltas[(size_t) k]; wsum += ltas[(size_t) k] * k * binHz; }
        auto band = [&] (double lo, double hi)
        {
            if (lo >= nyq) return -100.f;
            double s = 0;
            for (int k = binOf (lo); k <= binOf (std::min (hi, nyq)); ++k) s += ltas[(size_t) k];
            return powDb (s / (total + 1e-30));
        };
        f.subDb = band (20, 60);
        f.lowDb = band (60, 250);
        f.lowMidDb = band (250, 500);
        f.midDb = band (500, 2000);
        f.upperMidDb = band (2000, 5000);
        f.presenceDb = band (1500, 4000);
        f.sibilanceBandDb = band (5000, 10000);
        f.airDb = band (10000, 20000);
        f.centroidHz = total > 0 ? (float) (wsum / total) : 0.f;
        f.centroidStdHz = stddev (centroids);
        double cum = 0;
        for (int k = 1; k < nBins; ++k) { cum += ltas[(size_t) k]; if (cum >= 0.85 * total) { f.rolloffHz = (float) (k * binHz); break; } }

        // tilt: regression of third-octave level vs log2(f) between 100 Hz and 10 kHz
        double sx = 0, sy = 0, sxx = 0, sxy = 0; int cnt = 0;
        for (int b = 0; b < kThirdOctaveBands; ++b)
        {
            const double fc = kThirdOctaveCentres[(size_t) b];
            if (fc < 100 || fc > 10000 || fc > nyq) continue;
            const double x = std::log2 (fc), y = f.thirdOctaveDb[(size_t) b];
            sx += x; sy += y; sxx += x * x; sxy += x * y; ++cnt;
        }
        if (cnt > 3) f.tiltDbPerOct = (float) ((cnt * sxy - sx * sy) / (cnt * sxx - sx * sx));

        // resonances: fine (1/12 oct) vs coarse (2/3 oct) smoothed spectra
        auto smoothAt = [&] (int k, double octaves)
        {
            const double fc = k * binHz;
            const int a = binOf (fc * std::pow (2.0, -octaves / 2)), b = binOf (fc * std::pow (2.0, octaves / 2));
            double s = 0; int c = 0;
            for (int j = a; j <= b; ++j) { s += ltas[(size_t) j]; ++c; }
            return powDb (s / std::max (1, c));
        };
        std::vector<std::pair<float, float>> cand;   // freq, prominence
        float prevProm = 0, prevPrevProm = 0; float prevF = 0;
        for (int k = binOf (120); k <= binOf (std::min (9000.0, nyq * 0.9)); ++k)
        {
            const float prom = smoothAt (k, 1.0 / 12.0) - smoothAt (k, 2.0 / 3.0);
            if (prevProm > prevPrevProm && prevProm >= prom && prevProm > 3.0f)
                cand.emplace_back (prevF, prevProm);
            prevPrevProm = prevProm; prevProm = prom; prevF = (float) (k * binHz);
        }
        std::sort (cand.begin(), cand.end(), [] (auto& a, auto& b) { return a.second > b.second; });
        for (auto& c : cand)
        {
            bool near = false;
            for (auto& r : f.resonances) near = near || std::abs (std::log2 (r.freqHz / c.first)) < 0.25;
            if (! near) f.resonances.push_back ({ c.first, c.second });
            if (f.resonances.size() >= 6) break;
        }
    }

    //--------------------------------------------------------------------------
    // Transients: spectral-flux onsets
    {
        std::vector<float> flux;
        for (auto& fr : frames) flux.push_back (fr.flux);
        const double frameRate = sr / hop;
        const int win = std::max (3, (int) (frameRate * 0.5));
        std::vector<int> onsets;
        int lastOnset = -1000;
        for (int i = 1; i + 1 < (int) flux.size(); ++i)
        {
            if (! frames[(size_t) i].active) continue;
            const int a = std::max (0, i - win), b = std::min ((int) flux.size() - 1, i + win);
            std::vector<float> local (flux.begin() + a, flux.begin() + b + 1);
            const float thr = (float) percentile (local, 50.0) * 1.6f + 1e-6f;
            if (flux[(size_t) i] > thr && flux[(size_t) i] >= flux[(size_t) i - 1] && flux[(size_t) i] >= flux[(size_t) i + 1]
                && (i - lastOnset) > frameRate * 0.05)
            {
                onsets.push_back (i);
                lastOnset = i;
            }
        }
        f.onsetRate = f.activeSec > 0 ? (float) (onsets.size() / f.activeSec) : 0.f;
        std::vector<float> slopes, crests;
        for (int o : onsets)
        {
            const int e = std::clamp ((int) (frames[(size_t) o].t / 0.01), 5, numEnv - 4);
            float lo = 1e9f, hi = -1e9f; int hiIdx = e;
            for (int k = e - 5; k <= e; ++k) lo = std::min (lo, env[(size_t) k]);
            for (int k = e - 2; k <= e + 3; ++k) if (env[(size_t) k] > hi) { hi = env[(size_t) k]; hiIdx = k; }
            const int loIdx = e - 5;
            const float riseMs = std::max (10.f, (float) (hiIdx - loIdx) * 10.f);
            if (hi - lo > 3.f) slopes.push_back ((hi - lo) / riseMs);
            // transient crest: peak sample around the onset vs 100 ms RMS
            const auto s0 = (size_t) std::max (0, (int) (frames[(size_t) o].t * sr) - (int) (0.05 * sr));
            const auto s1 = std::min ((size_t) n, s0 + (size_t) (0.1 * sr));
            double sq = 0; float p2 = 0;
            for (size_t k = s0; k < s1; ++k) { sq += (double) mono[k] * mono[k]; p2 = std::max (p2, std::abs (mono[k])); }
            if (s1 > s0) crests.push_back (dsp::gainToDb (p2) - powDb (sq / (double) (s1 - s0)));
        }
        f.attackSharpness = slopes.empty() ? 0.f : median (slopes);
        f.transientCrestDb = crests.empty() ? 0.f : median (crests);
    }

    //--------------------------------------------------------------------------
    // Pitch, voicing, HNR, formants
    PitchTrack pitch;
    if (opt.pitch)
    {
        pitch = trackPitch (mono.data(), n, sr);
        std::vector<float> f0s, aps; int activeFrames = 0; std::vector<float> jumps;
        float prev = 0;
        for (size_t i = 0; i < pitch.f0.size(); ++i)
        {
            const double t = i / pitch.frameRate;
            const int e = std::clamp ((int) (t / 0.01), 0, numEnv - 1);
            if (! activeEnv[(size_t) e]) { prev = 0; continue; }
            ++activeFrames;
            if (pitch.f0[i] > 0)
            {
                f0s.push_back (pitch.f0[i]);
                aps.push_back (pitch.aperiodicity[i]);
                if (prev > 0)
                {
                    const float c = 1200.f * std::log2 (pitch.f0[i] / prev);
                    if (std::abs (c) < 300.f) jumps.push_back (std::abs (c));   // ignore note changes / octave errors
                }
                prev = pitch.f0[i];
            }
            else prev = 0;
        }
        f.voicedRatio = activeFrames > 0 ? (float) f0s.size() / (float) activeFrames : 0.f;
        if (f0s.size() > 5)
        {
            f.f0MedianHz = median (f0s);
            f.f0MinHz = (float) percentile (f0s, 5);
            f.f0MaxHz = (float) percentile (f0s, 95);
            f.pitchJitterCents = jumps.empty() ? 0.f : median (jumps);
            const float ap = std::max (0.005f, median (aps));
            f.hnrDb = 10.f * std::log10 ((1.f - ap) / ap);
        }

        // LPC formants on the loudest voiced frames (decimated to ~16 kHz)
        if (f0s.size() > 10)
        {
            const int D = std::max (1, (int) std::lround (sr / 16000.0));
            const double sd = sr / D;
            constexpr int P = 18, W = 512;
            std::vector<std::pair<float, size_t>> voicedFrames;
            for (size_t i = 0; i < pitch.f0.size(); ++i)
                if (pitch.f0[i] > 0) voicedFrames.emplace_back (pitch.levelDb[i], i);
            std::sort (voicedFrames.begin(), voicedFrames.end(), [] (auto& a, auto& b) { return a.first > b.first; });
            voicedFrames.resize (std::min<size_t> (voicedFrames.size(), 80));
            std::vector<float> F1, F2, F3; int complete = 0;
            Biquad a1; a1.setCoeffs (BiquadCoeffs::lowPass (sr, 0.42 * sd, 0.7071));
            std::vector<float> seg ((size_t) W);
            for (auto& vf : voicedFrames)
            {
                const auto centreSample = (size_t) ((double) vf.second / pitch.frameRate * sr);
                const size_t startSample = centreSample > (size_t) (W * D / 2) ? centreSample - (size_t) (W * D / 2) : 0;
                if (startSample + (size_t) (W * D) >= (size_t) n) continue;
                a1.reset();
                float prevS = 0;
                for (int i = 0; i < W; ++i)
                {
                    float v = 0;
                    for (int k = 0; k < D; ++k) v = a1.process (mono[startSample + (size_t) (i * D + k)], 0);
                    const float pre = v - 0.97f * prevS;
                    prevS = v;
                    seg[(size_t) i] = pre * (float) (0.54 - 0.46 * std::cos (2 * kPi * i / (W - 1)));
                }
                std::array<double, P + 1> r {};
                for (int lag = 0; lag <= P; ++lag)
                    for (int i = lag; i < W; ++i) r[(size_t) lag] += (double) seg[(size_t) i] * seg[(size_t) (i - lag)];
                if (r[0] <= 1e-12) continue;
                r[0] *= 1.0001;   // lag-window regularisation
                std::array<double, P + 1> a {}, tmp {};
                a[0] = 1; double err = r[0];
                for (int i = 1; i <= P; ++i)
                {
                    double acc = r[(size_t) i];
                    for (int j = 1; j < i; ++j) acc += a[(size_t) j] * r[(size_t) (i - j)];
                    const double k = -acc / err;
                    tmp = a;
                    for (int j = 1; j < i; ++j) a[(size_t) j] = tmp[(size_t) j] + k * tmp[(size_t) (i - j)];
                    a[(size_t) i] = k;
                    err *= (1 - k * k);
                    if (err <= 0) break;
                }
                // envelope peaks
                std::vector<float> peaks;
                constexpr int G = 400;
                double prevV = 0, prevPrevV = 0;
                for (int g = 0; g <= G; ++g)
                {
                    const double fr = 90.0 + (4000.0 - 90.0) * g / G;
                    const double w = 2 * kPi * fr / sd;
                    std::complex<double> A (1.0, 0.0);
                    for (int k = 1; k <= P; ++k) A += a[(size_t) k] * std::polar (1.0, -w * k);
                    const double v = -20 * std::log10 (std::abs (A) + 1e-12);
                    if (g >= 2 && prevV > prevPrevV && prevV >= v)
                        peaks.push_back ((float) (90.0 + (4000.0 - 90.0) * (g - 1) / G));
                    prevPrevV = prevV; prevV = v;
                }
                float p1 = 0, p2 = 0, p3 = 0;
                for (float p : peaks)
                {
                    if (p1 <= 0.f) { if (p >= 250 && p <= 1000) p1 = p; }
                    else if (p2 <= 0.f) { if (p >= std::max (700.f, p1 + 200) && p <= 2800) p2 = p; }
                    else if (p3 <= 0.f) { if (p >= std::max (1800.f, p2 + 300) && p <= 3800) p3 = p; }
                }
                if (p1 > 0) F1.push_back (p1);
                if (p2 > 0) F2.push_back (p2);
                if (p3 > 0) F3.push_back (p3);
                if (p1 > 0 && p2 > 0 && p3 > 0) ++complete;
            }
            if (! F1.empty()) f.f1Hz = median (F1);
            if (! F2.empty()) f.f2Hz = median (F2);
            if (! F3.empty()) f.f3Hz = median (F3);
            f.formantConfidence = voicedFrames.empty() ? 0.f : (float) complete / (float) voicedFrames.size();
        }
    }
    // presence peak: strongest smoothed LTAS point in 1.8 - 5 kHz
    {
        float best = -1e9f;
        for (int k = binOf (1800); k <= binOf (std::min (5000.0, nyq * 0.9)); ++k)
        {
            double s = 0; int c = 0;
            const double fc = k * binHz;
            for (int j = binOf (fc * 0.9); j <= binOf (fc * 1.1); ++j) { s += ltas[(size_t) j]; ++c; }
            const float v = powDb (s / std::max (1, c));
            if (v > best) { best = v; f.presencePeakHz = (float) fc; }
        }
    }

    //--------------------------------------------------------------------------
    // Events
    if (opt.events)
    {
        const double frameDur = (double) hop / sr;
        // body level: median full-band level of active non-sibilant frames
        std::vector<float> bodyLevels, harshRatios, lowLevels;
        for (auto& fr : frames)
        {
            if (! fr.active) continue;
            const float sibRatio = powDb (fr.pSib / (fr.pTotal + 1e-30));
            if (sibRatio < -6.f) bodyLevels.push_back (fr.levelDb);
            harshRatios.push_back (powDb (fr.pHarsh / (fr.pTotal + 1e-30)));
            lowLevels.push_back (powDb (fr.pLow));
        }
        const float bodyDb = bodyLevels.empty() ? -60.f : median (bodyLevels);
        const float harshMedian = harshRatios.empty() ? -20.f : median (harshRatios);
        std::vector<float> sibRatios;
        for (auto& fr : frames) if (fr.active) sibRatios.push_back (powDb (fr.pSib / (fr.pTotal + 1e-30)));
        const float sibMedian = sibRatios.empty() ? -30.f : median (sibRatios);
        std::vector<float> activeLevels;
        for (auto& fr : frames) if (fr.active) activeLevels.push_back (fr.levelDb);
        const float loudHalf = activeLevels.empty() ? -60.f : (float) percentile (activeLevels, 50.0);

        // Sibilance: frames dominated by the 4.5-11 kHz band with a high centroid
        std::vector<bool> sibFlags (frames.size());
        for (size_t i = 0; i < frames.size(); ++i)
        {
            const auto& fr = frames[i];
            const float ratio = powDb (fr.pSib / (fr.pTotal + 1e-30));
            sibFlags[i] = fr.active && ratio > -4.5f && fr.centroid > 3500.f && powDb (fr.pSib) > bodyDb - 30.f;
        }
        const int minSib = std::max (1, (int) (0.02 / frameDur));
        std::vector<float> sevs, sibFreqs;
        for (auto [a, b] : groupRuns (sibFlags, 1, minSib))
        {
            if ((b - a + 1) * frameDur > 0.45) continue;   // too long for an 's' (noise / hi-hat bed)
            TimedEvent e;
            e.start = frames[(size_t) a].t - frameDur * 0.5;
            e.end = frames[(size_t) b].t + frameDur * 0.5;
            size_t loudest = (size_t) a;
            for (int k = a; k <= b; ++k) if (frames[(size_t) k].pSib > frames[loudest].pSib) loudest = (size_t) k;
            e.freqHz = frames[loudest].sibPeakHz;
            e.levelDb = powDb (frames[loudest].pSib);
            e.severityDb = e.levelDb - bodyDb;
            f.sibilantEvents.push_back (e);
            sevs.push_back (e.severityDb);
            sibFreqs.push_back (e.freqHz);
        }
        if (! sevs.empty())
        {
            f.sibilanceSeverityDb = (float) percentile (sevs, 90.0);
            f.sibilanceCenterHz = median (sibFreqs);
        }

        // Harshness: loud frames whose 2-5 kHz share jumps well above this voice's own norm
        std::vector<bool> harshFlags (frames.size());
        for (size_t i = 0; i < frames.size(); ++i)
        {
            const auto& fr = frames[i];
            const float ratio = powDb (fr.pHarsh / (fr.pTotal + 1e-30));
            const bool nearSibilant = sibFlags[i] || (i > 0 && sibFlags[i - 1]) || (i + 1 < frames.size() && sibFlags[i + 1]);
            const bool sibEnergy = powDb (fr.pSib / (fr.pTotal + 1e-30)) > -12.f;   // 's' spilling into the band
            // harshness is a *localised* upper-mid excess: its peak is inside the band (not at the
            // top edge, which means energy spilling down from above) and it rises more than the
            // band above 5 kHz does in the same frame
            const float sibExcess = powDb (fr.pSib / (fr.pTotal + 1e-30)) - sibMedian;
            const bool localised = fr.harshPeakHz < 4700.f && (ratio - harshMedian) > sibExcess + 3.f;
            harshFlags[i] = fr.active && ! nearSibilant && ! sibEnergy && localised && fr.levelDb >= loudHalf && ratio > harshMedian + 4.0f;
        }
        std::vector<float> hsev, hfreq;
        for (auto [a, b] : groupRuns (harshFlags, 1, std::max (1, (int) (0.03 / frameDur))))
        {
            TimedEvent e;
            e.start = frames[(size_t) a].t - frameDur * 0.5;
            e.end = frames[(size_t) b].t + frameDur * 0.5;
            size_t worst = (size_t) a; float worstR = -1e9f;
            for (int k = a; k <= b; ++k)
            {
                const float r = powDb (frames[(size_t) k].pHarsh / (frames[(size_t) k].pTotal + 1e-30));
                if (r > worstR) { worstR = r; worst = (size_t) k; }
            }
            e.freqHz = frames[worst].harshPeakHz;
            e.severityDb = worstR - harshMedian;
            e.levelDb = powDb (frames[worst].pHarsh);
            f.harshEvents.push_back (e);
            hsev.push_back (e.severityDb);
            hfreq.push_back (e.freqHz);
        }
        if (! hsev.empty())
        {
            f.harshSeverityDb = (float) percentile (hsev, 90.0);
            f.harshCenterHz = median (hfreq);
            f.harshBandwidthOct = hfreq.size() > 2 ? (float) (std::log2 (percentile (hfreq, 85) / std::max (1.0, percentile (hfreq, 15)))) : 0.3f;
        }

        // Plosives: sudden low-frequency bursts
        const float lowMed = lowLevels.empty() ? -80.f : median (lowLevels);
        std::vector<bool> plosFlags (frames.size());
        for (size_t i = 1; i < frames.size(); ++i)
        {
            const auto& fr = frames[i];
            const float lowDb = powDb (fr.pLow);
            plosFlags[i] = fr.active && lowDb > lowMed + 12.f && fr.pLow > 0.5 * fr.pTotal
                           && lowDb - powDb (frames[i - 1].pLow) > 8.f;
        }
        for (auto [a, b] : groupRuns (plosFlags, 2, 1))
        {
            if ((b - a + 1) * frameDur > 0.12) continue;
            TimedEvent e;
            e.start = frames[(size_t) a].t - frameDur * 0.5;
            e.end = frames[(size_t) b].t + frameDur;
            e.freqHz = 80.f;
            e.levelDb = powDb (frames[(size_t) a].pLow);
            e.severityDb = e.levelDb - lowMed;
            f.plosiveEvents.push_back (e);
        }

        // Breaths: quiet, noisy, unvoiced stretches between phrases
        if (opt.pitch && ! pitch.f0.empty())
        {
            std::vector<bool> breathFlags (frames.size());
            for (size_t i = 0; i < frames.size(); ++i)
            {
                const auto& fr = frames[i];
                const auto pi = std::min (pitch.f0.size() - 1, (size_t) (fr.t * pitch.frameRate));
                breathFlags[i] = fr.levelDb < bodyDb - 12.f && fr.levelDb > bodyDb - 40.f && fr.levelDb > f.noiseFloorDb + 8.f
                                 && fr.flatness > 0.25f && pitch.f0[pi] <= 0.f;
            }
            for (auto [a, b] : groupRuns (breathFlags, 2, std::max (1, (int) (0.15 / frameDur))))
            {
                const double dur = (b - a + 1) * frameDur;
                if (dur > 1.0) continue;
                TimedEvent e;
                e.start = frames[(size_t) a].t; e.end = frames[(size_t) b].t;
                e.levelDb = frames[(size_t) a].levelDb;
                e.severityDb = e.levelDb - bodyDb;
                f.breathEvents.push_back (e);
            }
        }

        // Clicks: isolated second-difference spikes far above the local signal
        {
            double localMs = 1e-6;
            const float c = 0.999f;
            int lastClick = -100000;
            for (int i = 2; i < n; ++i)
            {
                const float d2 = mono[(size_t) i] - 2 * mono[(size_t) i - 1] + mono[(size_t) i - 2];
                const bool spike = (double) d2 * d2 > 400.0 * localMs && std::abs (d2) > 0.05f;
                localMs = c * localMs + (1 - c) * (double) d2 * d2;
                if (spike && i - lastClick > (int) (0.02 * sr))
                {
                    TimedEvent e; e.start = i / sr; e.end = e.start + 0.001;
                    e.levelDb = dsp::gainToDb (std::abs (d2));
                    f.clickEvents.push_back (e);
                    lastClick = i;
                    if (f.clickEvents.size() > 200) break;
                }
            }
        }

        // attach event counts to phrases
        for (auto& ph : f.phrases)
        {
            for (auto& e : f.harshEvents) if (e.start >= ph.start && e.start < ph.end) ++ph.harshEvents;
            for (auto& e : f.sibilantEvents) if (e.start >= ph.start && e.start < ph.end) ++ph.sibilantEvents;
            std::vector<float> cs;
            for (auto& fr : frames) if (fr.active && fr.t >= ph.start && fr.t < ph.end) cs.push_back (fr.centroid);
            ph.centroidHz = cs.empty() ? 0.f : median (cs);
        }
    }

    //--------------------------------------------------------------------------
    // Stereo
    f.isStereo = numCh > 1;
    if (f.isStereo && opt.stereo)
    {
        const int fl = (int) (0.05 * sr);
        double sLL = 0, sRR = 0, sLR = 0, sM = 0, sS = 0, sSum = 0, sEach = 0;
        std::vector<float> corrs;
        for (int start = 0; start + fl <= n; start += fl)
        {
            double ll = 0, rr = 0, lr = 0;
            for (int i = start; i < start + fl; ++i)
            {
                ll += (double) L[i] * L[i]; rr += (double) R[i] * R[i]; lr += (double) L[i] * R[i];
                const double m = 0.5 * (L[i] + R[i]), s = 0.5 * (L[i] - R[i]);
                sM += m * m; sS += s * s;
                sSum += (double) (L[i] + R[i]) * (L[i] + R[i]);
                sEach += (double) L[i] * L[i] + (double) R[i] * R[i];
            }
            sLL += ll; sRR += rr; sLR += lr;
            const int e = std::clamp ((int) ((start + fl / 2) / sr / 0.01), 0, numEnv - 1);
            if (activeEnv[(size_t) e] && ll > 1e-10 && rr > 1e-10)
                corrs.push_back ((float) (lr / std::sqrt (ll * rr)));
        }
        f.correlation = (sLL > 0 && sRR > 0) ? (float) (sLR / std::sqrt (sLL * sRR)) : 1.f;
        f.correlationLow = corrs.empty() ? f.correlation : (float) percentile (corrs, 5.0);
        f.sideToMidDb = powDb (sS / (sM + 1e-30));
        f.balanceDb = powDb (sLL / (sRR + 1e-30));
        f.monoLossDb = powDb (sSum / (2.0 * sEach + 1e-30));
        f.isEffectivelyMono = f.correlation > 0.998f && f.sideToMidDb < -40.f;

        // inter-channel delay (+/- 1 ms) around the loudest region
        const int win = std::min (n, (int) (0.2 * sr));
        int bestStart = 0; float bestLvl = -1e9f;
        for (int h = 0; h + win / hop10 < numEnv; h += 10)
            if (env[(size_t) h] > bestLvl) { bestLvl = env[(size_t) h]; bestStart = h * hop10; }
        bestStart = std::min (bestStart, n - win);
        const int maxLag = (int) (0.001 * sr);
        double bestC = -1e30; int bestLag = 0;
        for (int lag = -maxLag; lag <= maxLag; ++lag)
        {
            double c = 0;
            for (int i = bestStart + maxLag; i < bestStart + win - maxLag; ++i) c += (double) L[i] * R[i + lag];
            if (c > bestC) { bestC = c; bestLag = lag; }
        }
        f.interChannelDelayMs = (float) (bestLag * 1000.0 / sr);
    }

    //--------------------------------------------------------------------------
    // Space: decay after sharp level drops (phrase offsets detected directly on the envelope,
    // so reverb tails that fill the gaps do not hide them)
    if (opt.space && numEnv > 100)
    {
        std::vector<float> t60s, tails;
        for (int i = 20; i + 20 < numEnv; ++i)
        {
            double e = 0;
            for (int k = i - 20; k < i; ++k) e += std::pow (10.0, (env[(size_t) k] + 0.691) / 10.0);
            const float pre = (float) (-0.691 + 10.0 * std::log10 (e / 20.0));
            if (pre < activeThr + 6.f) continue;
            if (env[(size_t) i] > pre - 4.f) continue;                 // not dropping yet
            if (env[(size_t) (i + 12)] > pre - 12.f) continue;         // not a real offset (just a dip)
            // regression from pre-5 dB down to pre-40 dB while the level keeps falling
            const float stopDb = f.noiseFloorDb > -110.f ? std::max (pre - 40.f, f.noiseFloorDb + 6.f) : pre - 40.f;
            double sx = 0, sy = 0, sxx = 0, sxy = 0; int cnt = 0;
            float prev = env[(size_t) i];
            int k = i;
            for (; k < std::min (numEnv, i + 200); ++k)
            {
                const float v = env[(size_t) k];
                if (v > prev + 3.f) break;                             // a new onset: stop
                prev = std::min (prev, v);
                if (v > pre - 5.f) continue;
                if (v < stopDb) break;
                const double x = (k - i) * 0.01;
                sx += x; sy += v; sxx += x * x; sxy += x * v; ++cnt;
            }
            if (cnt >= 4)
            {
                const double slope = (cnt * sxy - sx * sy) / (cnt * sxx - sx * sx);   // dB per second
                if (slope < -1.0) t60s.push_back ((float) std::clamp (-60.0 / slope, 0.05, 12.0));
            }
            else
                t60s.push_back (0.08f);                                // fell away almost instantly: very dry
            const float at80 = env[(size_t) std::min (numEnv - 1, i + 8)];
            tails.push_back (std::max (at80, std::max (f.noiseFloorDb, pre - 80.f)) - pre);
            i = k + 10;                                                // skip past this tail
        }
        if (! t60s.empty())
        {
            f.decayTimeSec = median (t60s);
            f.tailToDirectDb = median (tails);
            f.spaceConfidence = std::min (1.f, (float) t60s.size() / 5.f);
        }
    }

    //--------------------------------------------------------------------------
    // Sections (for longer material): novelty on 1 s loudness
    if (opt.structure && loud.shortTerm1s.size() >= 20)
    {
        const auto& st = loud.shortTerm1s;
        const int W = 4;
        std::vector<int> bounds { 0 };
        for (int t = W; t + W < (int) st.size(); ++t)
        {
            double a = 0, b = 0;
            for (int k = 0; k < W; ++k) { a += st[(size_t) (t - 1 - k)]; b += st[(size_t) (t + k)]; }
            const double nov = std::abs (a - b) / W;
            if (nov > 3.0 && t - bounds.back() >= 8)
                bounds.push_back (t);
        }
        bounds.push_back ((int) st.size() + 2);
        const float med = median (st);
        for (size_t i = 0; i + 1 < bounds.size(); ++i)
        {
            Segment s;
            s.start = bounds[i]; s.end = std::min (f.durationSec, (double) bounds[i + 1]);
            std::vector<float> part;
            for (int t = bounds[i]; t < std::min ((int) st.size(), bounds[i + 1]); ++t) part.push_back (st[(size_t) t]);
            s.loudnessDb = part.empty() ? -70.f : median (part);
            s.label = s.loudnessDb > med + 2.f ? "loud" : (s.loudnessDb < med - 2.f ? "quiet" : "medium");
            f.sections.push_back (s);
        }
    }

    //--------------------------------------------------------------------------
    // Source guess (heuristic scores; confidence reflects the margin between candidates)
    {
        auto clamp01 = [] (float v) { return std::clamp (v, 0.f, 1.f); };
        const bool f0Plausible = f.f0MedianHz > 70.f && f.f0MedianHz < 1100.f;
        const float vocal = 0.45f * clamp01 ((f.voicedRatio - 0.2f) / 0.4f)
                          + 0.15f * (f0Plausible ? 1.f : 0.f)
                          + 0.15f * (f.subDb < -25.f ? 1.f : 0.3f)
                          + 0.1f * (f.lowDb < -6.f ? 1.f : 0.4f)
                          + 0.15f * (f.isEffectivelyMono || f.correlation > 0.9f ? 1.f : 0.4f)
                          + (opt.hint == SourceType::LeadVocal ? 0.1f : 0.f);
        const float mix = 0.25f * (f.subDb > -24.f || f.lowDb > -8.f ? 1.f : 0.f)
                        + 0.2f * (f.airDb > -36.f ? 1.f : 0.3f)
                        + 0.2f * clamp01 (f.onsetRate / 2.f)
                        + 0.25f * (! f.isEffectivelyMono && f.correlation < 0.96f ? 1.f : 0.2f)
                        + 0.1f * (f.silenceRatio < 0.1f ? 1.f : 0.f)
                        + (opt.hint == SourceType::FullMix ? 0.1f : 0.f);
        const float drums = 0.45f * clamp01 ((f.onsetRate - 1.5f) / 3.f) + 0.3f * clamp01 ((0.3f - f.voicedRatio) / 0.3f)
                          + 0.25f * clamp01 (f.flatness * 3.f);
        const float lowShare = std::pow (10.f, f.subDb / 10.f) + std::pow (10.f, f.lowDb / 10.f);
        const float bass = 0.6f * clamp01 ((lowShare - 0.4f) / 0.4f) + 0.4f * (f.centroidHz < 500.f ? 1.f : 0.f);
        const float instrument = 0.35f;

        std::array<std::pair<float, SourceType>, 5> scores { {
            { vocal, SourceType::LeadVocal }, { mix, SourceType::FullMix }, { drums, SourceType::Drums },
            { bass, SourceType::Bass }, { instrument, SourceType::Instrument } } };
        std::sort (scores.begin(), scores.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        f.source = scores[0].second;
        f.sourceConfidence = std::clamp (scores[0].first * (0.55f + (scores[0].first - scores[1].first)), 0.05f, 0.95f);
    }

    f.analysisMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    return f;
}

} // namespace nova::analysis
