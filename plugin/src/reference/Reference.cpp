#include "Reference.h"
#include "../analysis/Analyzer.h"
#include "../dsp/NovaChain.h"

namespace nova::reference
{

using analysis::kThirdOctaveBands;
using analysis::kThirdOctaveCentres;

namespace
{
std::string f1 (float v) { return juce::String (v, 1).toStdString(); }
void setp (ChainSettings& s, int idx, float v) { s[idx] = clampToSpec (kParams[(size_t) idx], v); }

// tonal curve (third-octave, dB re total) of a rendered buffer
std::array<float, kThirdOctaveBands> toneOf (const juce::AudioBuffer<float>& b, double sr)
{
    analysis::AnalysisOptions o;
    o.pitch = false; o.events = false; o.structure = false; o.stereo = false; o.space = false;
    return analysis::Analyzer::analyze (b.getArrayOfReadPointers(), b.getNumChannels(), b.getNumSamples(), sr, o).thirdOctaveDb;
}

using Curve = std::array<float, kThirdOctaveBands>;

// ~1.7 octave triangular smoothing: tone matching is about broad balance, not note-level detail
Curve smoothOct (const Curve& c, const std::array<float, kThirdOctaveBands>& w)
{
    static constexpr float kKernel[] = { 0.25f, 0.6f, 1.f, 0.6f, 0.25f };
    Curve out {};
    for (int b = 0; b < kThirdOctaveBands; ++b)
    {
        double s = 0, ws = 0;
        for (int k = -2; k <= 2; ++k)
        {
            const int j = b + k;
            if (j < 0 || j >= kThirdOctaveBands) continue;
            const double wk = w[(size_t) j] * kKernel[k + 2];
            s += wk * c[(size_t) j]; ws += wk;
        }
        out[(size_t) b] = ws > 0 ? (float) (s / ws) : 0.f;
    }
    return out;
}

void centre (Curve& c, const Curve& w)
{
    double s = 0, ws = 0;
    for (int b = 0; b < kThirdOctaveBands; ++b) { s += w[(size_t) b] * c[(size_t) b]; ws += w[(size_t) b]; }
    const float m = ws > 0 ? (float) (s / ws) : 0.f;
    for (auto& v : c) v -= m;
}

float weightedRms (const Curve& c, const Curve& w)
{
    double s = 0, ws = 0;
    for (int b = 0; b < kThirdOctaveBands; ++b) { s += w[(size_t) b] * c[(size_t) b] * c[(size_t) b]; ws += w[(size_t) b]; }
    return ws > 0 ? (float) std::sqrt (s / ws) : 0.f;
}

// Solve (A) x = y for a small dense system (Gaussian elimination with partial pivoting)
std::array<double, kNumToneMatchBands> solve (std::array<std::array<double, kNumToneMatchBands>, kNumToneMatchBands> A, std::array<double, kNumToneMatchBands> y)
{
    constexpr int N = kNumToneMatchBands;
    for (int c = 0; c < N; ++c)
    {
        int piv = c;
        for (int r = c + 1; r < N; ++r) if (std::abs (A[(size_t) r][(size_t) c]) > std::abs (A[(size_t) piv][(size_t) c])) piv = r;
        std::swap (A[(size_t) c], A[(size_t) piv]); std::swap (y[(size_t) c], y[(size_t) piv]);
        const double d = A[(size_t) c][(size_t) c];
        if (std::abs (d) < 1e-12) continue;
        for (int r = c + 1; r < N; ++r)
        {
            const double f = A[(size_t) r][(size_t) c] / d;
            for (int k = c; k < N; ++k) A[(size_t) r][(size_t) k] -= f * A[(size_t) c][(size_t) k];
            y[(size_t) r] -= f * y[(size_t) c];
        }
    }
    std::array<double, N> x {};
    for (int r = N - 1; r >= 0; --r)
    {
        double s = y[(size_t) r];
        for (int k = r + 1; k < N; ++k) s -= A[(size_t) r][(size_t) k] * x[(size_t) k];
        x[(size_t) r] = std::abs (A[(size_t) r][(size_t) r]) > 1e-12 ? s / A[(size_t) r][(size_t) r] : 0.0;
    }
    return x;
}

// Gauss-Newton fit of the 8 tone-match gains to a target dB curve (ridge-regularised)
std::array<float, kNumToneMatchBands> fitToneGains (const Curve& target, const Curve& w, double sr)
{
    std::array<float, kNumToneMatchBands> g {};
    for (int it = 0; it < 4; ++it)
    {
        Curve resp {};
        for (int b = 0; b < kThirdOctaveBands; ++b)
            resp[(size_t) b] = (float) dsp::ToneMatchModule::responseDbForGains (g, sr, kThirdOctaveCentres[(size_t) b]);
        std::array<std::array<double, kNumToneMatchBands>, kThirdOctaveBands> J {};
        for (int i = 0; i < kNumToneMatchBands; ++i)
        {
            auto gp = g; gp[(size_t) i] += 0.5f;
            for (int b = 0; b < kThirdOctaveBands; ++b)
                J[(size_t) b][(size_t) i] = (dsp::ToneMatchModule::responseDbForGains (gp, sr, kThirdOctaveCentres[(size_t) b]) - resp[(size_t) b]) / 0.5;
        }
        std::array<std::array<double, kNumToneMatchBands>, kNumToneMatchBands> A {};
        std::array<double, kNumToneMatchBands> y {};
        for (int i = 0; i < kNumToneMatchBands; ++i)
        {
            for (int j = 0; j < kNumToneMatchBands; ++j)
            {
                double s = 0;
                for (int b = 0; b < kThirdOctaveBands; ++b) s += w[(size_t) b] * J[(size_t) b][(size_t) i] * J[(size_t) b][(size_t) j];
                A[(size_t) i][(size_t) j] = s + (i == j ? 0.25 : 0.0);
            }
            double s = 0;
            for (int b = 0; b < kThirdOctaveBands; ++b) s += w[(size_t) b] * J[(size_t) b][(size_t) i] * (target[(size_t) b] - resp[(size_t) b]);
            y[(size_t) i] = s - 0.25 * g[(size_t) i];
        }
        const auto dx = solve (A, y);
        for (int i = 0; i < kNumToneMatchBands; ++i)
            g[(size_t) i] = std::clamp (g[(size_t) i] + (float) dx[(size_t) i], -8.f, 8.f);
    }
    return g;
}

float bandGroup (const Curve& c, float lo, float hi)
{
    double s = 0;
    for (int b = 0; b < kThirdOctaveBands; ++b)
        if (kThirdOctaveCentres[(size_t) b] >= lo && kThirdOctaveCentres[(size_t) b] < hi) s += std::pow (10.0, c[(size_t) b] / 10.0);
    return (float) (10.0 * std::log10 (s + 1e-20));
}
} // namespace

//==============================================================================
std::shared_ptr<ReferenceProfile> loadReference (const juce::File& file, analysis::WorkMode mode, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Unsupported or unreadable audio file: " + file.getFileName();
        return nullptr;
    }
    const double sr = reader->sampleRate;
    if (sr < 8000 || reader->lengthInSamples <= 0)
    {
        error = "The file has no usable audio.";
        return nullptr;
    }
    const auto maxLen = (juce::int64) (sr * 480.0);   // analyse up to 8 minutes
    const int len = (int) std::min (reader->lengthInSamples, maxLen);
    juce::AudioBuffer<float> buf (2, len);
    buf.clear();
    reader->read (&buf, 0, len, 0, true, true);
    if (reader->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, len);

    auto p = std::make_shared<ReferenceProfile>();
    p->name = file.getFileName().toStdString();
    p->path = file.getFullPathName().toStdString();
    p->sampleRate = sr;
    p->durationSec = len / sr;
    analysis::AnalysisOptions o;
    o.hint = mode == analysis::WorkMode::Vocal ? analysis::SourceType::LeadVocal : analysis::SourceType::FullMix;
    p->features = analysis::Analyzer::analyze (buf.getArrayOfReadPointers(), 2, len, sr, o);
    if (! p->features.valid)
    {
        error = "The reference is silent or too short to analyse.";
        return nullptr;
    }
    p->semantic = analysis::describe (p->features, mode);
    p->isFullMix = p->features.source == analysis::SourceType::FullMix
                   || (! p->features.isEffectivelyMono && p->features.subDb > -26.f && p->features.onsetRate > 1.f);
    p->analysedAtMs = juce::Time::currentTimeMillis();
    return p;
}

MatchDimensions MatchDimensions::fromNames (const std::vector<std::string>& names)
{
    MatchDimensions d;
    d.tone = false;
    for (auto& n : names)
    {
        if (n == "tone" || n == "tonality" || n == "eq") d.tone = true;
        else if (n == "dynamics") d.dynamics = true;
        else if (n == "space") d.space = true;
        else if (n == "width") d.width = true;
        else if (n == "color" || n == "colour") d.color = true;
        else if (n == "loudness") d.loudness = true;
        else if (n == "vocal" || n == "vocal_character") d.vocal = true;
        else if (n == "full" || n == "full_mix" || n == "all") { d.tone = d.dynamics = d.space = d.width = true; }
    }
    if (! (d.tone || d.dynamics || d.space || d.width || d.color || d.loudness || d.vocal)) d.tone = true;
    return d;
}

std::vector<std::string> MatchDimensions::names() const
{
    std::vector<std::string> n;
    if (tone) n.push_back ("tone");
    if (dynamics) n.push_back ("dynamics");
    if (space) n.push_back ("space");
    if (width) n.push_back ("width");
    if (color) n.push_back ("color");
    if (loudness) n.push_back ("loudness");
    if (vocal) n.push_back ("vocal");
    return n;
}

//==============================================================================
juce::var compareToReference (const analysis::AudioFeatures& c, const ReferenceProfile& ref)
{
    const auto& r = ref.features;
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> statements;
    auto say = [&] (const std::string& s) { statements.add (juce::String (s)); };

    auto* tone = new juce::DynamicObject();
    struct G { const char* name; float lo, hi; };
    const G groups[] = { { "low_60_250", 60, 250 }, { "lowmid_250_500", 250, 500 }, { "mid_500_2k", 500, 2000 },
                         { "presence_2k_5k", 2000, 5000 }, { "brilliance_5k_10k", 5000, 10000 }, { "air_10k_20k", 10000, 20000 } };
    for (auto& g : groups)
    {
        const float d = bandGroup (r.thirdOctaveDb, g.lo, g.hi) - bandGroup (c.thirdOctaveDb, g.lo, g.hi);
        tone->setProperty (g.name, std::round (d * 10.f) / 10.f);
        if (std::abs (d) >= 1.5f)
            say ("reference has " + std::string (d > 0 ? "+" : "") + f1 (d) + " dB " + (d > 0 ? "more" : "less") + " energy in " + g.name + " (loudness-independent)");
    }
    tone->setProperty ("tilt_db_per_oct_delta", std::round ((r.tiltDbPerOct - c.tiltDbPerOct) * 100.f) / 100.f);
    const float highBalance = bandGroup (r.thirdOctaveDb, 2000, 20000) - bandGroup (c.thirdOctaveDb, 2000, 20000);
    tone->setProperty ("above_2k_balance_db", std::round (highBalance * 10.f) / 10.f);
    if (highBalance > 2.f) say ("reference is brighter (" + f1 (highBalance) + " dB more energy above 2 kHz, loudness-independent)");
    else if (highBalance < -2.f) say ("reference is darker (" + f1 (-highBalance) + " dB less energy above 2 kHz, loudness-independent)");
    else if (r.centroidHz > c.centroidHz * 1.15f) say ("reference is brighter overall (centroid " + std::to_string ((int) r.centroidHz) + " vs " + std::to_string ((int) c.centroidHz) + " Hz)");
    else if (r.centroidHz < c.centroidHz / 1.15f) say ("reference is darker overall (centroid " + std::to_string ((int) r.centroidHz) + " vs " + std::to_string ((int) c.centroidHz) + " Hz)");
    o->setProperty ("tone_delta_db", juce::var (tone));

    auto* dyn = new juce::DynamicObject();
    dyn->setProperty ("crest_db", juce::Array<juce::var> { std::round (c.crestDb * 10) / 10, std::round (r.crestDb * 10) / 10 });
    dyn->setProperty ("lra_lu", juce::Array<juce::var> { std::round (c.lra * 10) / 10, std::round (r.lra * 10) / 10 });
    dyn->setProperty ("phrase_std_db", juce::Array<juce::var> { std::round (c.phraseLevelStdDb * 10) / 10, std::round (r.phraseLevelStdDb * 10) / 10 });
    o->setProperty ("dynamics_current_vs_reference", juce::var (dyn));
    if (r.crestDb < c.crestDb - 2.f) say ("reference is more compressed/denser (crest " + f1 (r.crestDb) + " vs " + f1 (c.crestDb) + " dB)");
    else if (r.crestDb > c.crestDb + 2.f) say ("reference is more dynamic (crest " + f1 (r.crestDb) + " vs " + f1 (c.crestDb) + " dB)");

    auto* spc = new juce::DynamicObject();
    spc->setProperty ("tail_80ms_db", juce::Array<juce::var> { std::round (c.tailToDirectDb * 10) / 10, std::round (r.tailToDirectDb * 10) / 10 });
    spc->setProperty ("decay_s", juce::Array<juce::var> { std::round (c.decayTimeSec * 100) / 100, std::round (r.decayTimeSec * 100) / 100 });
    spc->setProperty ("confidence", std::round (std::min (c.spaceConfidence, r.spaceConfidence) * 100) / 100);
    o->setProperty ("space_current_vs_reference", juce::var (spc));
    if (r.spaceConfidence > 0.3f && c.spaceConfidence > 0.3f)
    {
        if (r.tailToDirectDb > c.tailToDirectDb + 4.f) say ("reference is wetter / more distant (tail " + f1 (r.tailToDirectDb) + " vs " + f1 (c.tailToDirectDb) + " dB)");
        else if (r.tailToDirectDb < c.tailToDirectDb - 4.f) say ("reference is drier / closer (tail " + f1 (r.tailToDirectDb) + " vs " + f1 (c.tailToDirectDb) + " dB)");
    }
    else say ("space comparison has low confidence (not enough clear phrase endings)");

    auto* st = new juce::DynamicObject();
    st->setProperty ("side_to_mid_db", juce::Array<juce::var> { std::round (c.sideToMidDb * 10) / 10, std::round (r.sideToMidDb * 10) / 10 });
    o->setProperty ("width_current_vs_reference", juce::var (st));
    if (r.sideToMidDb > c.sideToMidDb + 4.f) say ("reference is wider");
    else if (r.sideToMidDb < c.sideToMidDb - 4.f) say ("reference is narrower");

    o->setProperty ("loudness_lufs", juce::Array<juce::var> { std::round (c.integratedLufs * 10) / 10, std::round (r.integratedLufs * 10) / 10 });
    if (r.sibilantEvents.size() > 1 && c.sibilantEvents.size() > 1 && r.sibilanceSeverityDb < c.sibilanceSeverityDb - 3.f)
        say ("reference sibilants are tamer (" + f1 (r.sibilanceSeverityDb) + " vs " + f1 (c.sibilanceSeverityDb) + " dB re vowels)");
    o->setProperty ("reference_is_full_mix", ref.isFullMix);
    o->setProperty ("statements", statements);
    return juce::var (o);
}

//==============================================================================
MatchResult matchReference (ai::TreatmentSession& session, const ReferenceProfile& ref, const MatchDimensions& dims, float influence)
{
    MatchResult res;
    res.influence = influence = std::clamp (influence, 0.f, 1.f);
    const double sr = session.sampleRate();
    const auto& inF = session.features();
    const bool sourceIsVocal = session.mode() == analysis::WorkMode::Vocal;
    const float startLoudness = session.getCandidateMetrics().integratedLufs;
    ChainSettings s = session.candidate;
    std::vector<std::string> simpleParts, engParts;

    //--------------------------------------------------------------------------
    if (dims.tone)
    {
        DimensionReport rep;
        rep.dimension = "tone"; rep.unit = "dB rms"; rep.attempted = true;
        // spectrum of the current state without any previous tone match
        ChainSettings base = s;
        setp (base, P::TmOn, 0);
        auto pr = ai::renderPreview (*session.inputAudio(), sr, base, session.chainOrder(), session.transport());
        const Curve C0 = toneOf (*pr.audio, sr);
        const auto& R = ref.features.thirdOctaveDb;

        Curve w {};
        const float nyqLim = (float) std::min (sr, ref.sampleRate) * 0.45f;
        const bool restrict = ref.isFullMix && sourceIsVocal;
        const float fundamentalZone = 1.5f * std::max ({ 120.f, inF.f0MedianHz, ref.features.f0MedianHz });
        for (int b = 0; b < kThirdOctaveBands; ++b)
        {
            const float fc = kThirdOctaveCentres[(size_t) b];
            float wt = (fc >= 60.f && fc <= 12000.f) ? 1.f : 0.4f;
            if (fc < 40.f || fc > nyqLim) wt = 0.f;
            if (restrict && (fc < 200.f || fc > 10000.f)) wt = 0.f;
            if (C0[(size_t) b] < -60.f || R[(size_t) b] < -60.f) wt = 0.f;
            // below ~1.5 x f0 the spectrum reflects which notes are sung, not the tone
            if (sourceIsVocal && fc < fundamentalZone) wt *= 0.15f;
            w[(size_t) b] = wt;
        }
        Curve D {};
        for (int b = 0; b < kThirdOctaveBands; ++b) D[(size_t) b] = R[(size_t) b] - C0[(size_t) b];
        D = smoothOct (D, w);
        centre (D, w);
        for (int b = 0; b < kThirdOctaveBands; ++b)
        {
            float lim = 9.f;
            if (C0[(size_t) b] < -42.f) lim = 3.f;   // never boost what the source barely contains (noise)
            D[(size_t) b] = std::clamp (D[(size_t) b], -9.f, lim);
        }
        Curve target {};
        for (int b = 0; b < kThirdOctaveBands; ++b) target[(size_t) b] = C0[(size_t) b] + influence * D[(size_t) b];

        Curve before {};
        for (int b = 0; b < kThirdOctaveBands; ++b) before[(size_t) b] = target[(size_t) b] - C0[(size_t) b];
        before = smoothOct (before, w);
        centre (before, w);
        rep.distanceBefore = weightedRms (before, w);

        Curve fitTarget = D;
        std::array<float, kNumToneMatchBands> gains {};
        float bestErr = 1e9f;
        std::array<float, kNumToneMatchBands> bestGains {};
        for (int it = 0; it < 4; ++it)
        {
            gains = fitToneGains (fitTarget, w, sr);
            ChainSettings t = s;
            setp (t, P::TmOn, 1);
            setp (t, P::TmAmount, influence * 100.f);
            for (int i = 0; i < kNumToneMatchBands; ++i) setp (t, P::TmG1 + i, gains[(size_t) i]);
            auto p2 = ai::renderPreview (*session.inputAudio(), sr, t, session.chainOrder(), session.transport());
            ++res.iterations;
            const Curve Cn = toneOf (*p2.audio, sr);
            Curve E {};
            for (int b = 0; b < kThirdOctaveBands; ++b) E[(size_t) b] = target[(size_t) b] - Cn[(size_t) b];
            E = smoothOct (E, w);
            centre (E, w);
            const float err = weightedRms (E, w);
            if (err < bestErr) { bestErr = err; bestGains = gains; }
            if (err < 0.6f || influence < 0.05f) break;
            for (int b = 0; b < kThirdOctaveBands; ++b)
                fitTarget[(size_t) b] = std::clamp (fitTarget[(size_t) b] + 0.8f * E[(size_t) b] / std::max (0.1f, influence), -10.f, 10.f);
        }
        setp (s, P::TmOn, 1);
        setp (s, P::TmAmount, influence * 100.f);
        for (int i = 0; i < kNumToneMatchBands; ++i) setp (s, P::TmG1 + i, bestGains[(size_t) i]);
        res.toneGains = bestGains;
        rep.distanceAfter = bestErr;
        rep.improved = bestErr < rep.distanceBefore;
        rep.confidence = restrict ? 0.45f : 0.8f;
        rep.current = rep.distanceBefore; rep.after = bestErr;
        if (restrict)
            rep.limitation = "reference is a full mix and no vocal separation was available: matched only the 200 Hz - 10 kHz tonal balance";
        rep.summary = "tonal distance to the " + std::to_string ((int) std::lround (influence * 100)) + "% target " + f1 (rep.distanceBefore) + " -> " + f1 (bestErr) + " dB";
        simpleParts.push_back (session.say ("matched the tonal balance (" + std::to_string ((int) std::lround (influence * 100)) + "% influence)",
                                             "قرّبت التوازن الطوني منه (بتأثير " + std::to_string ((int) std::lround (influence * 100)) + "%)"));
        std::string g;
        for (int i = 0; i < kNumToneMatchBands; ++i) g += (i ? ", " : "") + f1 (bestGains[(size_t) i]);
        engParts.push_back ("Tone match: 8-band fit (" + g + " dB @50..13k) scaled by influence; weighted third-octave error " + f1 (rep.distanceBefore) + " -> " + f1 (bestErr) + " dB");
        res.reports.push_back (rep);
    }

    //--------------------------------------------------------------------------
    if (dims.dynamics)
    {
        DimensionReport rep;
        rep.dimension = "dynamics"; rep.unit = "dB crest"; rep.attempted = true;
        const auto m0 = session.evaluate (s);
        const float cur = m0.crestDb, rc = ref.features.crestDb;
        const float target = cur + influence * (rc - cur);
        rep.current = cur; rep.reference = rc; rep.distanceBefore = std::abs (target - cur);
        ChainSettings t = s;
        float after = cur;
        if (target < cur - 0.7f)
        {
            setp (t, P::CompOn, 1);
            setp (t, P::CompRatio, std::max (t[P::CompRatio], 3.f));
            setp (t, P::CompAttack, 8.f); setp (t, P::CompRelease, 100.f); setp (t, P::CompKnee, 6.f); setp (t, P::CompDetector, 1);
            if (! s.on (P::CompOn)) setp (t, P::CompThresh, m0.integratedLufs + 4.f);
            for (int it = 0; it < 6; ++it)
            {
                const auto m = session.evaluate (t);
                ++res.iterations;
                after = m.crestDb;
                if (std::abs (after - target) < 0.7f) break;
                setp (t, P::CompThresh, t[P::CompThresh] + std::clamp ((after - target) * -1.2f, -6.f, 6.f));
                if (t[P::CompThresh] <= -59.f) setp (t, P::CompRatio, t[P::CompRatio] + 1.f);
            }
        }
        else if (target > cur + 0.7f)
        {
            if (t.on (P::CompOn) || t.on (P::LimOn) || t.on (P::LvlOn))
            {
                for (int it = 0; it < 4; ++it)
                {
                    if (t.on (P::CompOn)) { setp (t, P::CompThresh, t[P::CompThresh] + 3.f); setp (t, P::CompRatio, std::max (1.2f, t[P::CompRatio] - 0.7f)); }
                    if (t.on (P::LimOn)) setp (t, P::LimGain, std::max (0.f, t[P::LimGain] - 1.5f));
                    const auto m = session.evaluate (t);
                    ++res.iterations;
                    after = m.crestDb;
                    if (after >= target - 0.7f) break;
                }
            }
            if (after < target - 0.7f)
                rep.limitation = "the source is already denser than the reference; NOVA can relax its own compression but cannot add dynamics that are not in the recording";
        }
        rep.after = after;
        rep.distanceAfter = std::abs (target - after);
        rep.improved = rep.distanceAfter < rep.distanceBefore - 0.2f;
        rep.confidence = 0.75f;
        rep.summary = "crest " + f1 (cur) + " -> " + f1 (after) + " dB (reference " + f1 (rc) + ", target " + f1 (target) + ")";
        s = t;
        if (rep.improved) simpleParts.push_back (after < cur ? session.say ("made it denser like the reference", "خليته أكثف زي الـ reference")
                                                            : session.say ("gave it back some dynamics like the reference", "رجّعتله شوية ديناميكس زي الـ reference"));
        engParts.push_back ("Dynamics: " + rep.summary);
        res.reports.push_back (rep);
    }

    //--------------------------------------------------------------------------
    if (dims.space)
    {
        DimensionReport rep;
        rep.dimension = "space"; rep.unit = "dB tail@80ms"; rep.attempted = true;
        const auto m0 = session.evaluate (s, true);
        const bool confident = ref.features.spaceConfidence > 0.3f;
        const float cur = m0.tailToDirectDb, rt = ref.features.tailToDirectDb;
        rep.current = cur; rep.reference = rt;
        if (! confident || cur < -99.f)
        {
            rep.limitation = "could not measure the space reliably (no clear phrase endings)";
            rep.confidence = 0.2f;
        }
        else
        {
            const float target = cur + influence * (rt - cur);
            rep.distanceBefore = std::abs (target - cur);
            ChainSettings t = s;
            float after = cur;
            if (target > cur + 1.5f)
            {
                float mix = std::max (12.f, t.on (P::SpcOn) ? t[P::SpcRevMix] : 12.f);
                setp (t, P::SpcOn, 1);
                setp (t, P::SpcDecay, std::clamp (ref.features.decayTimeSec > 0.2f ? ref.features.decayTimeSec : 1.6f, 0.6f, 4.f));
                setp (t, P::SpcPreDelay, 20.f);
                setp (t, P::SpcLowCut, 220.f);
                for (int it = 0; it < 6; ++it)
                {
                    setp (t, P::SpcRevMix, mix);
                    const auto m = session.evaluate (t, true);
                    ++res.iterations;
                    after = m.tailToDirectDb;
                    if (std::abs (after - target) < 1.5f) break;
                    mix = std::clamp (mix * (after < target ? 1.4f : 0.75f), 3.f, 90.f);
                }
            }
            else if (target < cur - 1.5f)
            {
                if (t.on (P::SpcOn))
                {
                    for (int it = 0; it < 4; ++it)
                    {
                        setp (t, P::SpcRevMix, t[P::SpcRevMix] * 0.6f);
                        const auto m = session.evaluate (t, true);
                        ++res.iterations;
                        after = m.tailToDirectDb;
                        if (after <= target + 1.5f) break;
                    }
                }
                if (after > target + 1.5f)
                    rep.limitation = "the reference is drier than the recording itself; room sound baked into the recording cannot be removed yet";
            }
            rep.after = after;
            rep.distanceAfter = std::abs (target - after);
            rep.improved = rep.distanceAfter < rep.distanceBefore - 0.5f;
            rep.confidence = std::min (ref.features.spaceConfidence, 0.8f);
            rep.summary = "tail " + f1 (cur) + " -> " + f1 (after) + " dB (reference " + f1 (rt) + ")";
            s = t;
            if (rep.improved) simpleParts.push_back (after > cur ? session.say ("added space to match its ambience", "زودت المساحة عشان تقرب من جوّه")
                                                                : session.say ("dried it up like the reference", "نشّفته زي الـ reference"));
        }
        engParts.push_back ("Space: " + (rep.summary.empty() ? rep.limitation : rep.summary));
        res.reports.push_back (rep);
    }

    //--------------------------------------------------------------------------
    if (dims.width)
    {
        DimensionReport rep;
        rep.dimension = "width"; rep.unit = "dB side/mid"; rep.attempted = true;
        const auto m0 = session.evaluate (s, true);
        const float cur = m0.sideToMidDb, rw = ref.features.sideToMidDb;
        const float target = cur + influence * (rw - cur);
        rep.current = cur; rep.reference = rw; rep.distanceBefore = std::abs (target - cur);
        ChainSettings t = s;
        float after = cur;
        if (std::abs (target - cur) > 2.f)
        {
            const bool mono = inF.isEffectivelyMono;
            for (int it = 0; it < 5; ++it)
            {
                if (mono)
                {
                    setp (t, P::SpcOn, 1);
                    setp (t, P::SpcDlyPingPong, target > cur ? 1.f : 0.f);
                    setp (t, P::SpcRevWidth, target > cur ? 100.f : 40.f);
                    if (target > cur) setp (t, P::SpcDlyMix, std::max (t[P::SpcDlyMix], 6.f) * (it == 0 ? 1.f : 1.4f));
                }
                else
                    setp (t, P::ImgWidth, std::clamp (t[P::ImgWidth] + (target - after) * 4.f, 40.f, 170.f));
                const auto m = session.evaluate (t, true);
                ++res.iterations;
                after = m.sideToMidDb;
                if (std::abs (after - target) < 1.5f || (! mono && m.monoLossDb < -2.f)) break;
            }
            if (mono) rep.limitation = "the source is mono, so width comes from the effects returns only (the vocal stays centred)";
        }
        rep.after = after;
        rep.distanceAfter = std::abs (target - after);
        rep.improved = rep.distanceAfter < rep.distanceBefore - 0.5f;
        rep.confidence = 0.6f;
        rep.summary = "side/mid " + f1 (cur) + " -> " + f1 (after) + " dB (reference " + f1 (rw) + ")";
        s = t;
        if (rep.improved) simpleParts.push_back (session.say ("matched its width", "قرّبت العرض منه"));
        engParts.push_back ("Width: " + rep.summary);
        res.reports.push_back (rep);
    }

    //--------------------------------------------------------------------------
    if (dims.color)
    {
        DimensionReport rep;
        rep.dimension = "color"; rep.attempted = true; rep.unit = "dB crest";
        rep.confidence = 0.35f;
        const float densityGap = inF.crestDb - ref.features.crestDb;
        if (densityGap > 3.f)
        {
            setp (s, P::ColOn, 1);
            setp (s, P::ColType, 0);
            setp (s, P::ColDrive, std::clamp (2.f + densityGap * 0.5f * influence, 2.f, 8.f));
            rep.summary = "added tape saturation (reference sounds denser; crest gap " + f1 (densityGap) + " dB)";
            rep.improved = true;
            simpleParts.push_back (session.say ("added a little saturation for the reference's density", "ضفت saturation خفيف عشان كثافة الـ reference"));
        }
        else rep.summary = "no clear colour/density difference measured";
        rep.limitation = "saturation character is estimated from density only (low confidence)";
        engParts.push_back ("Colour: " + rep.summary);
        res.reports.push_back (rep);
    }

    //--------------------------------------------------------------------------
    if (dims.vocal && ref.features.sibilantEvents.size() > 1 && inF.sibilantEvents.size() > 1
        && ref.features.sibilanceSeverityDb < inF.sibilanceSeverityDb - 3.f)
    {
        session.candidate = s;
        ai::TreatmentRequest tr; tr.id = "sibilance"; tr.amount = std::clamp ((inF.sibilanceSeverityDb - ref.features.sibilanceSeverityDb) / 10.f * influence, 0.2f, 0.9f);
        auto d = session.run (tr);
        if (d.applied) { s = d.settings; simpleParts.push_back (session.say ("tamed the 's' sounds like the reference", "هدّيت حروف الـ S زي الـ reference")); engParts.push_back ("Vocal character: " + d.engineer); }
    }

    //--------------------------------------------------------------------------
    // loudness: either match it (loudness dimension) or hold it so the comparison stays fair
    {
        const float targetL = dims.loudness ? startLoudness + influence * (ref.features.integratedLufs - startLoudness) : startLoudness;
        if (dims.loudness && targetL > startLoudness + 1.f)
        {
            session.candidate = s;
            ai::TreatmentRequest tr; tr.id = "loudness"; tr.targetLufs = targetL;
            auto l = session.run (tr);
            if (l.applied) { s = l.settings; simpleParts.push_back (session.say ("matched its loudness to " + f1 (l.after.integratedLufs) + " LUFS", "ظبطت الـ loudness على " + f1 (l.after.integratedLufs) + " LUFS")); engParts.push_back ("Loudness: " + l.engineer); }
        }
        else
        {
            auto m = session.evaluate (s);
            for (int i = 0; i < 2 && std::abs (m.integratedLufs - targetL) > 0.4f && m.integratedLufs > -69.f; ++i)
            {
                setp (s, P::OutGain, s[P::OutGain] + (targetL - m.integratedLufs));
                m = session.evaluate (s);
                ++res.iterations;
            }
        }
    }

    res.settings = s;
    res.applied = ! ai::diffSettings (session.candidate, s).empty() || ! ai::diffSettings (session.startSettings, s).empty();
    session.candidate = s;
    std::string simple = session.say ("I compared your audio with \"" + ref.name + "\" and ", "قارنت الصوت بتاعك بـ \"" + ref.name + "\" و");
    const std::string andWord = session.say (" and ", " و"), comma = session.say (", ", "، ");
    for (size_t i = 0; i < simpleParts.size(); ++i) simple += (i == 0 ? "" : (i + 1 == simpleParts.size() ? andWord : comma)) + simpleParts[i];
    if (simpleParts.empty()) simple += session.say ("found it already close on the dimensions you picked", "لقيته قريب منه أصلًا في الأبعاد اللي اخترتها");
    simple += session.say (". Use Reference Influence to dial it in.", ". استخدم Match Strength عشان تظبط قد إيه يقرب منه.");
    res.simple = simple;
    for (auto& r : res.reports) if (! r.limitation.empty()) res.warnings.push_back (r.dimension + ": " + r.limitation);
    std::string eng;
    for (auto& e : engParts) eng += e + ". ";
    res.engineer = eng + "All comparisons loudness-matched; output held at " + f1 (startLoudness) + " LUFS unless loudness was selected.";
    return res;
}

//==============================================================================
void ReferenceManager::loadAsync (const juce::File& file, analysis::WorkMode mode, std::function<void()> onDone)
{
    state.store (State::Loading);
    {
        std::lock_guard<std::mutex> g (lock);
        path = file.getFullPathName();
        error.clear();
    }
    pool.addJob ([this, file, mode, onDone]
    {
        juce::String err;
        auto p = loadReference (file, mode, err);
        {
            std::lock_guard<std::mutex> g (lock);
            if (p != nullptr) { profile = p; lastMatch.reset(); error.clear(); }
            else error = err;
        }
        state.store (p != nullptr ? State::Ready : State::Error);
        if (onDone) onDone();
    });
}

void ReferenceManager::clear()
{
    std::lock_guard<std::mutex> g (lock);
    profile.reset(); lastMatch.reset(); path.clear(); error.clear();
    state.store (State::Empty);
}

std::shared_ptr<const ReferenceProfile> ReferenceManager::get() const { std::lock_guard<std::mutex> g (lock); return profile; }
juce::String ReferenceManager::getError() const { std::lock_guard<std::mutex> g (lock); return error; }
juce::String ReferenceManager::getPath() const { std::lock_guard<std::mutex> g (lock); return path; }
void ReferenceManager::setLastMatch (std::shared_ptr<const MatchResult> m) { std::lock_guard<std::mutex> g (lock); lastMatch = std::move (m); }
std::shared_ptr<const MatchResult> ReferenceManager::getLastMatch() const { std::lock_guard<std::mutex> g (lock); return lastMatch; }
void ReferenceManager::setDimensions (const MatchDimensions& d) { std::lock_guard<std::mutex> g (lock); dims = d; }
MatchDimensions ReferenceManager::getDimensions() const { std::lock_guard<std::mutex> g (lock); return dims; }

juce::ValueTree ReferenceManager::toValueTree() const
{
    std::lock_guard<std::mutex> g (lock);
    juce::ValueTree v ("REFERENCE");
    v.setProperty ("path", path, nullptr);
    juce::StringArray d;
    for (auto& n : dims.names()) d.add (n);
    v.setProperty ("dims", d.joinIntoString (","), nullptr);
    return v;
}

void ReferenceManager::fromValueTree (const juce::ValueTree& v, analysis::WorkMode mode)
{
    if (! v.isValid()) return;
    std::vector<std::string> names;
    for (auto& n : juce::StringArray::fromTokens (v.getProperty ("dims").toString(), ",", "")) names.push_back (n.toStdString());
    {
        std::lock_guard<std::mutex> g (lock);
        if (! names.empty()) dims = MatchDimensions::fromNames (names);
    }
    const juce::File f (v.getProperty ("path").toString());
    if (v.getProperty ("path").toString().isNotEmpty() && f.existsAsFile())
        loadAsync (f, mode, {});
}

} // namespace nova::reference
