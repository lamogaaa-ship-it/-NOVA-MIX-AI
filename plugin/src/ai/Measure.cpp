#include "Measure.h"
#include "../analysis/Analyzer.h"

#include <juce_dsp/juce_dsp.h>

#include <chrono>

namespace nova::ai
{

PreviewResult renderPreview (const juce::AudioBuffer<float>& input, double sr, const ChainSettings& settings,
                             const ChainOrder& order, const TransportSnapshot& transport)
{
    const auto t0 = std::chrono::steady_clock::now();
    PreviewResult r;
    r.sampleRate = sr;
    constexpr int kBlock = 512;
    dsp::NovaChain chain;
    chain.prepare (sr, kBlock);
    const int latency = chain.getLatencySamples();
    const int n = input.getNumSamples();
    const int numCh = std::min (2, input.getNumChannels());
    juce::AudioBuffer<float> work (2, n + latency);
    work.clear();
    for (int c = 0; c < 2; ++c)
        work.copyFrom (c, 0, input, std::min (c, numCh - 1), 0, n);

    ChainSettings s = settings;
    s[P::Bypass] = 0; s[P::MonitorA] = 0; s[P::Delta] = 0;   // previews always render B
    dsp::ProcessContext ctx;
    ctx.sampleRate = sr;
    ctx.bpm = transport.bpm > 0 ? transport.bpm : 120.0;
    ctx.isPlaying = true;
    ctx.hasPpq = true;
    ctx.ppqAtBlockStart = 0.0;
    const double beatsPerSample = ctx.bpm / 60.0 / sr;
    for (int off = 0; off < work.getNumSamples(); off += kBlock)
    {
        const int len = std::min (kBlock, work.getNumSamples() - off);
        float* ch[2] = { work.getWritePointer (0, off), work.getWritePointer (1, off) };
        chain.process (ch, 2, len, s, order, ctx);
        ctx.ppqAtBlockStart += len * beatsPerSample;
    }
    r.audio = std::make_shared<juce::AudioBuffer<float>> (2, n);
    for (int c = 0; c < 2; ++c)
        r.audio->copyFrom (c, 0, work, c, latency, n);
    r.stats = chain.getStats();
    r.renderMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    return r;
}

//==============================================================================
Probe Probe::fromFeatures (const analysis::AudioFeatures& f)
{
    Probe p;
    p.sampleRate = f.sampleRate;
    for (auto& ph : f.phrases) p.active.push_back ({ ph.start, ph.end });
    if (p.active.empty()) p.active.push_back ({ 0.0, f.durationSec });
    for (auto& e : f.harshEvents) p.harsh.push_back ({ e.start, e.end });
    for (auto& e : f.sibilantEvents) p.sibilant.push_back ({ e.start - 0.01, e.end + 0.01 });
    if (f.harshCenterHz > 0)
    {
        p.harshLo = f.harshCenterHz / 1.35f;
        p.harshHi = f.harshCenterHz * 1.35f;
    }
    if (f.sibilanceCenterHz > 0)
    {
        p.sibLo = std::max (3500.f, f.sibilanceCenterHz / 1.5f);
        p.sibHi = std::min ((float) f.sampleRate * 0.45f, f.sibilanceCenterHz * 1.5f);
    }
    // calm = active phrases minus a margin around every event
    for (auto& a : p.active)
    {
        std::vector<TimeWindow> pieces { a };
        auto cut = [&] (const TimeWindow& e)
        {
            std::vector<TimeWindow> next;
            for (auto& w : pieces)
            {
                const double s = e.start - 0.05, en = e.end + 0.05;
                if (en <= w.start || s >= w.end) { next.push_back (w); continue; }
                if (s > w.start) next.push_back ({ w.start, s });
                if (en < w.end) next.push_back ({ en, w.end });
            }
            pieces.swap (next);
        };
        for (auto& e : p.harsh) cut (e);
        for (auto& e : p.sibilant) cut (e);
        for (auto& w : pieces) if (w.end - w.start > 0.05) p.calm.push_back (w);
    }
    return p;
}

//==============================================================================
namespace
{
bool inAny (double t, const std::vector<TimeWindow>& ws)
{
    for (auto& w : ws) if (t >= w.start && t < w.end) return true;
    return false;
}
float db (double p) { return (float) (10.0 * std::log10 (p + 1e-20)); }
} // namespace

Metrics measure (const juce::AudioBuffer<float>& audio, double sr, const Probe& probe, const dsp::ChainStats* stats, bool withSpaceAndStereo)
{
    Metrics m;
    const int n = audio.getNumSamples();
    const int numCh = std::min (2, audio.getNumChannels());
    if (n < (int) (0.5 * sr)) return m;
    const auto* const* ch = audio.getArrayOfReadPointers();

    const auto loud = analysis::Analyzer::measureLoudness (ch, numCh, n, sr);
    m.integratedLufs = loud.integrated;
    m.lra = loud.lra;
    m.truePeakDb = analysis::Analyzer::truePeakDb (ch, numCh, n);
    {
        double sq = 0; float pk = 0;
        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i) { sq += (double) ch[c][i] * ch[c][i]; pk = std::max (pk, std::abs (ch[c][i])); }
        m.crestDb = dsp::gainToDb (pk) - db (sq / ((double) n * numCh));
    }
    {
        std::vector<float> st;
        for (float v : loud.shortTerm100ms) if (v > m.integratedLufs - 20.f) st.push_back (v);
        if (st.size() > 1)
        {
            double mean = 0; for (float v : st) mean += v; mean /= (double) st.size();
            double var = 0; for (float v : st) var += (v - mean) * (v - mean);
            m.shortTermStdDb = (float) std::sqrt (var / (double) (st.size() - 1));
        }
    }

    // phrase loudness spread on the input's phrase windows (K-weighted)
    {
        std::vector<float> levels;
        for (auto& w : probe.active)
        {
            const int s0 = std::clamp ((int) (w.start * sr), 0, n), s1 = std::clamp ((int) (w.end * sr), 0, n);
            if (s1 - s0 < (int) (0.1 * sr)) continue;
            double e = 0;
            for (int c = 0; c < numCh; ++c)
            {
                dsp::KWeighting kw; kw.prepare (sr);
                // pre-roll 20 ms to settle the filters
                for (int i = std::max (0, s0 - (int) (0.02 * sr)); i < s0; ++i) kw.process (ch[c][i], 0);
                for (int i = s0; i < s1; ++i) { const double y = kw.process (ch[c][i], 0); e += y * y; }
            }
            levels.push_back ((float) (-0.691 + 10.0 * std::log10 (e / (s1 - s0) + 1e-20)));
        }
        if (levels.size() > 1)
        {
            double mean = 0; for (float v : levels) mean += v; mean /= (double) levels.size();
            double var = 0; for (float v : levels) var += (v - mean) * (v - mean);
            m.phraseStdDb = (float) std::sqrt (var / (double) (levels.size() - 1));
            m.macroRangeDb = *std::max_element (levels.begin(), levels.end()) - *std::min_element (levels.begin(), levels.end());
        }
    }

    // STFT band powers on the mono sum
    {
        constexpr int order = 11, N = 1 << order, hop = N / 4;
        juce::dsp::FFT fft (order);
        std::vector<float> win ((size_t) N), buf ((size_t) N * 2);
        double wSq = 0;
        for (int i = 0; i < N; ++i) { win[(size_t) i] = 0.5f - 0.5f * std::cos (2.f * (float) dsp::kPi * i / N); wSq += (double) win[(size_t) i] * win[(size_t) i]; }
        const double scale = 2.0 / ((double) N * wSq), binHz = sr / N;
        auto bin = [&] (double hz) { return std::clamp ((int) std::lround (hz / binHz), 1, N / 2); };
        struct Band { int a, b; };
        const Band bHarsh { bin (probe.harshLo), bin (probe.harshHi) }, bSib { bin (probe.sibLo), bin (probe.sibHi) };
        const Band bPres { bin (1500), bin (4000) }, bAir { bin (10000), bin (std::min (20000.0, sr * 0.49)) };
        const Band bBody { bin (150), bin (500) }, bLowMid { bin (250), bin (500) }, bLow { bin (60), bin (250) }, bSub { bin (20), bin (60) };
        auto sumBand = [&] (const Band& b) { double s = 0; for (int k = b.a; k <= b.b; ++k) s += (double) buf[(size_t) k] * buf[(size_t) k]; return s * scale; };

        double hOn = 0, hElse = 0, sOn = 0, sElse = 0, pres = 0, air = 0, body = 0, lowMid = 0, low = 0, sub = 0, cW = 0, cT = 0;
        int nHOn = 0, nCalm = 0, nSOn = 0, nAct = 0;
        for (int start = 0; start + N <= n; start += hop)
        {
            const double t = (start + N * 0.5) / sr;
            const bool act = inAny (t, probe.active);
            const bool hEv = inAny (t, probe.harsh), sEv = inAny (t, probe.sibilant), calm = inAny (t, probe.calm);
            if (! act && ! hEv && ! sEv) continue;
            std::fill (buf.begin(), buf.end(), 0.f);
            for (int i = 0; i < N; ++i)
            {
                float v = 0;
                for (int c = 0; c < numCh; ++c) v += ch[c][start + i];
                buf[(size_t) i] = v / (float) numCh * win[(size_t) i];
            }
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);
            if (hEv) { hOn += sumBand (bHarsh); ++nHOn; }
            if (sEv) { sOn += sumBand (bSib); ++nSOn; }
            if (calm)
            {
                hElse += sumBand (bHarsh); sElse += sumBand (bSib);
                pres += sumBand (bPres); air += sumBand (bAir);
                ++nCalm;
            }
            if (act)
            {
                body += sumBand (bBody); lowMid += sumBand (bLowMid); low += sumBand (bLow); sub += sumBand (bSub);
                for (int k = 1; k <= N / 2; ++k) { const double p = (double) buf[(size_t) k] * buf[(size_t) k]; cW += p * k * binHz; cT += p; }
                ++nAct;
            }
        }
        const float ref = m.integratedLufs;
        if (nHOn > 0) m.harshOnEvents = db (hOn / nHOn) - ref;
        if (nSOn > 0) m.sibOnEvents = db (sOn / nSOn) - ref;
        if (nCalm > 0)
        {
            m.harshElsewhere = db (hElse / nCalm) - ref;
            m.sibElsewhere = db (sElse / nCalm) - ref;
            m.presence = db (pres / nCalm) - ref;
            m.air = db (air / nCalm) - ref;
        }
        if (nAct > 0)
        {
            m.body = db (body / nAct) - ref;
            m.lowMid = db (lowMid / nAct) - ref;
            m.low = db (low / nAct) - ref;
            m.sub = db (sub / nAct) - ref;
            m.centroidHz = cT > 0 ? (float) (cW / cT) : 0.f;
        }
    }

    // space + stereo (no pitch / events: fast)
    if (withSpaceAndStereo)
    {
        analysis::AnalysisOptions o;
        o.pitch = false; o.events = false; o.structure = false;
        const auto f = analysis::Analyzer::analyze (ch, numCh, n, sr, o);
        m.tailToDirectDb = f.spaceConfidence > 0 ? f.tailToDirectDb : -100.f;
        m.sideToMidDb = f.sideToMidDb;
        m.monoLossDb = f.monoLossDb;
    }

    if (stats != nullptr)
    {
        m.compAvgGr = (float) stats->compGr.activeMean(); m.compMaxGr = (float) stats->compGr.maxv;
        m.riderAvgAbs = (float) stats->riderAbsGain.mean();
        m.dessAvgGr = (float) stats->dessGr.activeMean(); m.dessMaxGr = (float) stats->dessGr.maxv;
        m.limAvgGr = (float) stats->limGr.activeMean(); m.limMaxGr = (float) stats->limGr.maxv;
        for (int b = 0; b < kNumDynBands; ++b)
        {
            m.dynCutAvg[b] = (float) stats->dynCut[(size_t) b].activeMean();
            m.dynCutMax[b] = (float) stats->dynCut[(size_t) b].maxv;
        }
    }
    m.valid = true;
    return m;
}

//==============================================================================
static double r1 (double v) { return std::round (v * 10.0) / 10.0; }

juce::var metricsToJson (const Metrics& m)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("integrated_lufs", r1 (m.integratedLufs));
    o->setProperty ("true_peak_dbtp", r1 (m.truePeakDb));
    o->setProperty ("crest_db", r1 (m.crestDb));
    o->setProperty ("lra_lu", r1 (m.lra));
    o->setProperty ("phrase_level_std_db", r1 (m.phraseStdDb));
    o->setProperty ("short_term_std_db", r1 (m.shortTermStdDb));
    auto* b = new juce::DynamicObject();
    if (m.harshOnEvents > -99) b->setProperty ("harsh_band_on_harsh_moments", r1 (m.harshOnEvents));
    if (m.harshElsewhere > -99) b->setProperty ("harsh_band_elsewhere", r1 (m.harshElsewhere));
    if (m.sibOnEvents > -99) b->setProperty ("sibilant_band_on_esses", r1 (m.sibOnEvents));
    if (m.sibElsewhere > -99) b->setProperty ("sibilant_band_elsewhere", r1 (m.sibElsewhere));
    b->setProperty ("presence_1k5_4k", r1 (m.presence));
    b->setProperty ("air_10k_20k", r1 (m.air));
    b->setProperty ("body_150_500", r1 (m.body));
    b->setProperty ("low_60_250", r1 (m.low));
    b->setProperty ("sub_20_60", r1 (m.sub));
    o->setProperty ("band_levels_db_re_loudness", juce::var (b));
    o->setProperty ("centroid_hz", juce::roundToInt (m.centroidHz));
    if (m.tailToDirectDb > -99) o->setProperty ("tail_80ms_db", r1 (m.tailToDirectDb));
    auto* g = new juce::DynamicObject();
    g->setProperty ("comp_avg_gr_db", r1 (m.compAvgGr));
    g->setProperty ("comp_max_gr_db", r1 (m.compMaxGr));
    g->setProperty ("rider_avg_abs_db", r1 (m.riderAvgAbs));
    g->setProperty ("deesser_avg_gr_db", r1 (m.dessAvgGr));
    g->setProperty ("deesser_max_gr_db", r1 (m.dessMaxGr));
    g->setProperty ("limiter_avg_gr_db", r1 (m.limAvgGr));
    g->setProperty ("limiter_max_gr_db", r1 (m.limMaxGr));
    g->setProperty ("dyn_eq1_max_cut_db", r1 (m.dynCutMax[0]));
    g->setProperty ("dyn_eq2_max_cut_db", r1 (m.dynCutMax[1]));
    o->setProperty ("processing", juce::var (g));
    return juce::var (o);
}

juce::var metricsDeltaJson (const Metrics& a, const Metrics& b)
{
    auto* o = new juce::DynamicObject();
    auto d = [&] (const char* k, float x, float y) { if (x > -99 && y > -99) o->setProperty (k, r1 (y - x)); };
    d ("integrated_lufs", a.integratedLufs, b.integratedLufs);
    d ("true_peak_db", a.truePeakDb, b.truePeakDb);
    d ("crest_db", a.crestDb, b.crestDb);
    d ("phrase_level_std_db", a.phraseStdDb, b.phraseStdDb);
    d ("short_term_std_db", a.shortTermStdDb, b.shortTermStdDb);
    d ("harsh_band_on_harsh_moments", a.harshOnEvents, b.harshOnEvents);
    d ("harsh_band_elsewhere", a.harshElsewhere, b.harshElsewhere);
    d ("sibilant_band_on_esses", a.sibOnEvents, b.sibOnEvents);
    d ("sibilant_band_elsewhere", a.sibElsewhere, b.sibElsewhere);
    d ("presence", a.presence, b.presence);
    d ("air", a.air, b.air);
    d ("body", a.body, b.body);
    d ("low", a.low, b.low);
    d ("sub", a.sub, b.sub);
    d ("tail_80ms_db", a.tailToDirectDb, b.tailToDirectDb);
    o->setProperty ("note", "after minus before; band values are loudness matched (relative to integrated loudness)");
    return juce::var (o);
}

} // namespace nova::ai
