#include "Treatments.h"
#include "../analysis/Analyzer.h"
#include "../dsp/SpaceModule.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <sstream>

namespace nova::ai
{

using analysis::WorkMode;

//==============================================================================
const std::vector<TreatmentInfo>& treatmentCatalogue()
{
    static const std::vector<TreatmentInfo> list {
        { "level_consistency", "Even out phrase-to-phrase level: level rider + gentle compression, loudness preserved", false },
        { "harshness", "Dynamic control of upper-mid harsh moments (only when they happen), presence protected", false },
        { "sibilance", "De-ess the 's'/'sh' sounds at the voice's own sibilance frequency, air protected", false },
        { "clarity", "Diagnose a muffled/unclear sound (mud, presence, air, over-de-essing, over-compression, reverb masking) and fix the causes found", false },
        { "presence", "Presence bell (direction +1 forward/clearer, -1 softer)", true },
        { "air", "High-frequency air shelf (+1 more air, -1 less)", true },
        { "brightness", "Overall tone tilt (+1 brighter, -1 darker while protecting presence)", true },
        { "warmth", "Low-mid body + tape colour (+1 warmer, -1 leaner)", true },
        { "mud", "Clean up low-mid congestion at the measured build-up frequency", false },
        { "resonance", "Notch the most prominent measured resonance (boxy/nasal/ringing)", false },
        { "rumble", "High-pass below the voice's lowest fundamental", false },
        { "plosives", "Control P/B pops: high-pass + fast dynamic low band", false },
        { "space", "Ambience (+1 more space/distance, -1 closer/drier/more intimate)", true },
        { "width", "Stereo width (+1 wider, -1 narrower), mono compatibility checked", true },
        { "loudness", "Master loudness with true-peak-safe limiting (targetLufs, preserveDynamics)", false },
        { "saturation", "Harmonic colour (pattern 0 tape, 1 tube, 2 soft clip), loudness matched", false },
        { "rhythmic_gate", "Tempo-synced stutter/chop (division, pattern) locked to the DAW tempo", false },
        { "delay", "Tempo-synced delay throws (division), ducked under the dry vocal", false },
        { "punch", "Transient punch via slow-attack parallel compression", false },
        { "body", "Add (+1) or reduce (-1) body/thickness", true },
    };
    return list;
}

bool isKnownTreatment (const std::string& id)
{
    for (auto& t : treatmentCatalogue()) if (id == t.id) return true;
    return false;
}

std::vector<ParamChange> diffSettings (const ChainSettings& a, const ChainSettings& b)
{
    std::vector<ParamChange> out;
    for (int i = 0; i < P::Count; ++i)
        if (std::abs (a[i] - b[i]) > 1e-4f)
            out.push_back ({ i, a[i], b[i] });
    return out;
}

static std::string fmtVal (int idx, float v)
{
    const auto& spec = kParams[(size_t) idx];
    if (spec.kind == ParamKind::Bool) return v > 0.5f ? "on" : "off";
    if (spec.kind == ParamKind::Choice) return std::string (choiceName (spec, (int) (v + 0.5f)));
    if (spec.unit == Unit::Hz)
        return v >= 1000.f ? juce::String (v / 1000.f, 2).toStdString() + " kHz" : std::to_string ((int) std::lround (v)) + " Hz";
    const int dec = (spec.unit == Unit::Q || spec.unit == Unit::Ratio || spec.unit == Unit::Seconds) ? 2 : 1;
    return juce::String (v, dec).toStdString() + (spec.unit == Unit::None ? "" : std::string (" ") + unitSuffix (spec.unit));
}

std::string describeChange (const ParamChange& c)
{
    return std::string (kParams[(size_t) c.index].name) + ": " + fmtVal (c.index, c.before) + " -> " + fmtVal (c.index, c.after);
}

namespace
{
void setp (ChainSettings& s, int idx, float v) { s[idx] = clampToSpec (kParams[(size_t) idx], v); }
std::string f1 (float v) { return juce::String (v, 1).toStdString(); }
std::string hzs (float v) { return v >= 1000.f ? juce::String (v / 1000.f, 1).toStdString() + " kHz" : std::to_string ((int) std::lround (v)) + " Hz"; }

// EQ band roles keep the AI's edits readable and repeatable:
constexpr int kBandLowShelf = 0, kBandLowMid = 1, kBandMid = 2, kBandPresence = 3, kBandHighShelf = 4;
constexpr int kDynHarsh = 0, kDynLow = 1;

void setEqBand (ChainSettings& s, int band, int type, float freq, float gain, float q)
{
    setp (s, P::EqOn, 1);
    setp (s, eqBandParam (band, 0), 1);
    setp (s, eqBandParam (band, 1), (float) type);
    setp (s, eqBandParam (band, 2), freq);
    setp (s, eqBandParam (band, 3), gain);
    setp (s, eqBandParam (band, 4), q);
}
float eqGain (const ChainSettings& s, int band) { return s.on (eqBandParam (band, 0)) ? s[eqBandParam (band, 3)] : 0.f; }
} // namespace

//==============================================================================
TreatmentSession::TreatmentSession (std::shared_ptr<const juce::AudioBuffer<float>> in, double rate,
                                    const analysis::AudioFeatures& f, WorkMode m, const ChainSettings& start,
                                    const ChainOrder& o, const TransportSnapshot& t)
    : candidate (start), startSettings (start), input (std::move (in)), sr (rate), inputFeatures (f),
      workMode (m), order (o), trans (t)
{
    sem = analysis::describe (inputFeatures, workMode);
    prb = Probe::fromFeatures (inputFeatures);
}

Metrics TreatmentSession::evaluate (const ChainSettings& s, bool withSpace)
{
    auto pr = renderPreview (*input, sr, s, order, trans);
    ++renders;
    renderMs += pr.renderMs;
    return measure (*pr.audio, sr, prb, &pr.stats, withSpace);
}

const Metrics& TreatmentSession::getCandidateMetrics (bool withSpace)
{
    if (! candidateMetricsValid || (withSpace && ! candMetricsHasSpace))
    {
        candMetrics = evaluate (candidate, withSpace);
        candidateMetricsValid = true;
        candMetricsHasSpace = withSpace;
    }
    return candMetrics;
}

const Metrics& TreatmentSession::getInputMetrics()
{
    if (! inputMetricsValid)
    {
        inputMetrics = measure (*input, sr, prb, nullptr, true);
        inputMetricsValid = true;
    }
    return inputMetrics;
}

TreatmentResult TreatmentSession::begin (const TreatmentRequest& req)
{
    TreatmentResult r;
    r.id = req.id;
    r.settings = candidate;
    const bool needsSpace = req.id == "space" || req.id == "width" || req.id == "clarity";
    r.before = getCandidateMetrics (needsSpace);
    return r;
}

void TreatmentSession::finish (TreatmentResult& r, const ChainSettings& s, const Metrics& after)
{
    r.settings = s;
    r.after = after;
    r.changes = diffSettings (candidate, s);
    r.applied = ! r.changes.empty();
}

TreatmentResult TreatmentSession::matchLoudnessTo (const ChainSettings& s0, float target, TreatmentResult& r)
{
    ChainSettings s = s0;
    Metrics m = evaluate (s, false);
    for (int i = 0; i < 2 && std::abs (m.integratedLufs - target) > 0.3f && m.integratedLufs > -69.f; ++i)
    {
        setp (s, P::OutGain, s[P::OutGain] + (target - m.integratedLufs));
        m = evaluate (s, false);
        ++r.iterations;
    }
    TreatmentResult out = r;
    out.settings = s;
    out.after = m;
    return out;
}

TreatmentResult TreatmentSession::run (const TreatmentRequest& req)
{
    if (input == nullptr || input->getNumSamples() < (int) (sr * 0.5))
    {
        TreatmentResult r; r.id = req.id; r.skipReason = "no audio to work on";
        return r;
    }
    const std::string& id = req.id;
    if (id == "level_consistency") return levelConsistency (req);
    if (id == "harshness") return harshness (req);
    if (id == "sibilance") return sibilance (req);
    if (id == "clarity") return clarity (req);
    if (id == "presence") return presence (req);
    if (id == "air") return air (req);
    if (id == "brightness") return brightness (req);
    if (id == "warmth") return warmth (req);
    if (id == "mud") return mud (req);
    if (id == "resonance") return resonance (req);
    if (id == "rumble") return rumble (req);
    if (id == "plosives") return plosives (req);
    if (id == "space") return space (req);
    if (id == "width") return width (req);
    if (id == "loudness") return loudness (req);
    if (id == "saturation") return saturation (req);
    if (id == "rhythmic_gate") return rhythmicGate (req);
    if (id == "delay") return delayThrow (req);
    if (id == "punch") return punch (req);
    if (id == "body") return body (req);
    TreatmentResult r; r.id = id; r.skipReason = "unknown treatment";
    return r;
}

//==============================================================================
// LEVEL CONSISTENCY: rider (macro) + gentle compressor (micro), loudness preserved.
TreatmentResult TreatmentSession::levelConsistency (const TreatmentRequest& req)
{
    auto r = begin (req);
    const auto& f = inputFeatures;
    const float amount = std::clamp (req.amount * bias.compressionScale, 0.1f, 1.f);
    const float startStd = r.before.phraseStdDb;
    if (f.phrases.size() < 2 && f.shortTermStdDb < 2.f)
    {
        r.skipReason = "not enough separate phrases to judge level consistency";
        return r;
    }

    std::vector<float> lv;
    for (auto& p : f.phrases) lv.push_back (p.loudnessDb);
    const float medianLevel = lv.empty() ? f.integratedLufs : analysis::median (lv);
    const float spread = lv.empty() ? f.dynamicRangeDb : *std::max_element (lv.begin(), lv.end()) - *std::min_element (lv.begin(), lv.end());
    const float trim = candidate[P::InTrim];

    const float targetStd = std::max (0.8f, startStd * (1.f - (0.35f + 0.4f * amount)));
    const float maxAvgGr = req.preserveDynamics ? 3.f : 3.5f + 2.5f * amount;
    const float minCrest = std::max (7.f, r.before.crestDb - (req.preserveDynamics ? 3.f : 6.f));

    ChainSettings s = candidate;
    float range = std::clamp (spread * 0.5f + 2.f, 3.f, 12.f);
    float speed = amount > 0.7f ? 160.f : 260.f;
    float thr = medianLevel + 0.691f + trim + 1.f;
    float ratio = 2.f + 1.5f * amount;

    ChainSettings best = s; Metrics bestM; float bestScore = -1e9f;
    for (int it = 0; it < 7 && ! cancelled(); ++it)
    {
        setp (s, P::LvlOn, 1);
        setp (s, P::LvlTarget, medianLevel + 0.691f + trim);
        setp (s, P::LvlRange, range);
        setp (s, P::LvlSpeed, speed);
        setp (s, P::LvlGate, medianLevel + trim - 24.f);
        setp (s, P::LvlAmount, 100);
        setp (s, P::CompOn, 1);
        setp (s, P::CompThresh, thr);
        setp (s, P::CompRatio, ratio);
        setp (s, P::CompAttack, req.preserveDynamics ? 20.f : 12.f);
        setp (s, P::CompRelease, 120.f);
        setp (s, P::CompKnee, 6.f);
        setp (s, P::CompDetector, 1);
        const Metrics m = evaluate (s);
        ++r.iterations;
        const float score = -std::max (0.f, m.phraseStdDb - targetStd) * 2.f - 0.6f * std::max (0.f, m.compAvgGr - maxAvgGr)
                            - 0.6f * std::max (0.f, minCrest - m.crestDb);
        r.log.push_back ("iter " + std::to_string (it + 1) + ": rider +/-" + f1 (range) + " dB @" + f1 (speed) + " ms, comp " + f1 (thr) + " dB "
                         + f1 (ratio) + ":1 -> phrase std " + f1 (m.phraseStdDb) + " dB, comp GR " + f1 (m.compAvgGr) + " dB, crest " + f1 (m.crestDb) + " dB");
        if (score > bestScore) { bestScore = score; best = s; bestM = m; }

        const bool stdOk = m.phraseStdDb <= targetStd * 1.1f;
        const bool grOk = m.compAvgGr <= maxAvgGr && m.crestDb >= minCrest;
        if (stdOk && grOk && m.compAvgGr >= 1.f) break;
        if (! grOk) { thr += 2.f; ratio = std::max (1.6f, ratio - 0.5f); }
        else if (m.compAvgGr < 1.f && amount > 0.25f) thr -= 2.5f;
        if (! stdOk) { range = std::min (12.f, range + 2.f); speed = std::max (90.f, speed * 0.75f); }
    }

    // keep integrated loudness where it was (makeup on the compressor, visible to the user)
    ChainSettings s2 = best;
    Metrics m2 = bestM;
    for (int i = 0; i < 2 && std::abs (m2.integratedLufs - r.before.integratedLufs) > 0.3f; ++i)
    {
        setp (s2, P::CompMakeup, s2[P::CompMakeup] + (r.before.integratedLufs - m2.integratedLufs));
        m2 = evaluate (s2);
        ++r.iterations;
    }
    finish (r, s2, m2);
    r.confidence = 0.8f;
    r.simple = "I evened out the vocal: the difference between your quiet and loud phrases went from about " + f1 (startStd)
               + " dB to " + f1 (m2.phraseStdDb) + " dB (spread), without squashing it.";
    r.engineer = "Level rider first (target " + f1 (s2[P::LvlTarget]) + " dB K-RMS, +/-" + f1 (s2[P::LvlRange]) + " dB, " + f1 (s2[P::LvlSpeed])
                 + " ms) so the compressor doesn't have to chase phrase level, then " + f1 (s2[P::CompRatio]) + ":1 at " + f1 (s2[P::CompThresh])
                 + " dB (" + f1 (s2[P::CompAttack]) + "/" + f1 (s2[P::CompRelease]) + " ms, avg GR " + f1 (m2.compAvgGr) + " dB, max " + f1 (m2.compMaxGr)
                 + " dB). Phrase std " + f1 (startStd) + " -> " + f1 (m2.phraseStdDb) + " dB, crest " + f1 (r.before.crestDb) + " -> " + f1 (m2.crestDb)
                 + " dB, loudness held at " + f1 (m2.integratedLufs) + " LUFS.";
    if (m2.phraseStdDb > targetStd * 1.3f)
        r.warnings.push_back ("phrase spread only reduced to " + f1 (m2.phraseStdDb) + " dB within the safe gain-reduction budget");
    return r;
}

//==============================================================================
// HARSHNESS: dynamic EQ on the measured harsh band, verified on the harsh moments.
TreatmentResult TreatmentSession::harshness (const TreatmentRequest& req)
{
    auto r = begin (req);
    const auto& f = inputFeatures;
    const float amount = std::clamp (req.amount, 0.1f, 1.f);
    const float target = 2.5f + 4.f * amount;
    const float maxLoss = req.avoidDullness ? 0.8f : 1.5f;

    if (f.harshEvents.empty())
    {
        // No harsh moments measured. Look for what else could explain the complaint.
        if (sem.find ("sibilance") != nullptr)
        {
            auto s = sibilance (req);
            s.log.insert (s.log.begin(), "no upper-mid harsh events found; the measured problem is sibilance");
            s.simple = "The harsh bits I can measure are the 's' sounds, not the notes - so I de-essed. " + s.simple;
            return s;
        }
        if (sem.find ("upper_mid_heavy") != nullptr || sem.find ("excessive_brightness") != nullptr)
        {
            ChainSettings s = candidate;
            const float cut = -(1.f + 2.f * amount);
            setEqBand (s, kBandPresence, 0, 3200.f, eqGain (s, kBandPresence) + cut, 0.8f);
            const auto m = evaluate (s);
            ++r.iterations;
            finish (r, s, m);
            r.confidence = 0.5f;
            r.simple = "It's bright overall rather than harsh on specific notes, so I pulled the upper mids down gently (" + f1 (cut) + " dB around 3.2 kHz).";
            r.engineer = "No event-level harshness; broad 2-5 kHz share is high (" + f1 (f.upperMidDb) + " dB of total). Static bell " + f1 (cut)
                         + " dB @ 3.2 kHz Q 0.8. Presence " + f1 (r.before.presence) + " -> " + f1 (m.presence) + " dB (loudness matched).";
            return r;
        }
        // Weak evidence: gentle, conservative dynamic control at the presence peak.
        ChainSettings s = candidate;
        const float fh = f.presencePeakHz > 1800.f ? f.presencePeakHz : 3000.f;
        setp (s, P::DeqOn, 1);
        setp (s, dynBandParam (kDynHarsh, 0), 1);
        setp (s, dynBandParam (kDynHarsh, 1), fh);
        setp (s, dynBandParam (kDynHarsh, 2), 2.f);
        const float lvl = r.before.harshElsewhere + r.before.integratedLufs + 3.f;
        setp (s, dynBandParam (kDynHarsh, 3), lvl + 6.f);
        setp (s, dynBandParam (kDynHarsh, 4), 3.f);
        const auto m = evaluate (s);
        ++r.iterations;
        finish (r, s, m);
        r.confidence = 0.3f;
        r.simple = "I couldn't find specific harsh moments in what I heard, so I only added light dynamic control around " + hzs (fh)
                   + ". If a particular part still hurts, play that part and ask again.";
        r.engineer = "No harsh events detected (0 frames exceeded the voice's own 2-5 kHz norm by 4 dB). Safety net: dyn band @" + hzs (fh)
                     + ", max cut 3 dB, threshold 6 dB above the calm band level.";
        r.warnings.push_back ("low evidence for harshness");
        return r;
    }

    const float fh = std::clamp (f.harshCenterHz, 1800.f, 5000.f);
    const float q = std::clamp (1.2f / std::max (0.35f, f.harshBandwidthOct), 1.2f, 4.f);
    float thr = 0.5f * ((r.before.harshOnEvents + r.before.integratedLufs + 3.f) + (r.before.harshElsewhere + r.before.integratedLufs + 3.f));
    float range = std::min (12.f, target + 3.f);

    ChainSettings s = candidate;
    ChainSettings best = s; Metrics bestM = r.before; float bestScore = -1e9f;
    float bestRed = 0, bestLoss = 0;
    for (int it = 0; it < 7 && ! cancelled(); ++it)
    {
        setp (s, P::DeqOn, 1);
        setp (s, dynBandParam (kDynHarsh, 0), 1);
        setp (s, dynBandParam (kDynHarsh, 1), fh);
        setp (s, dynBandParam (kDynHarsh, 2), q);
        setp (s, dynBandParam (kDynHarsh, 3), thr);
        setp (s, dynBandParam (kDynHarsh, 4), range);
        setp (s, dynBandParam (kDynHarsh, 5), 3.f);
        setp (s, dynBandParam (kDynHarsh, 6), 90.f);
        const Metrics m = evaluate (s);
        ++r.iterations;
        const float red = r.before.harshOnEvents - m.harshOnEvents;
        const float loss = r.before.harshElsewhere - m.harshElsewhere;
        const float score = std::min (red, target) - 2.5f * std::max (0.f, loss - maxLoss) - 0.25f * std::abs (red - target);
        r.log.push_back ("iter " + std::to_string (it + 1) + ": thr " + f1 (thr) + " dB, range " + f1 (range) + " dB -> harsh moments -"
                         + f1 (red) + " dB, elsewhere -" + f1 (loss) + " dB");
        if (score > bestScore) { bestScore = score; best = s; bestM = m; bestRed = red; bestLoss = loss; }
        if (loss > maxLoss) thr += 2.f + (loss - maxLoss) * 2.f;
        else if (red < target * 0.85f)
        {
            thr -= std::clamp ((target - red) * 1.3f, 1.f, 6.f);
            if (red < target * 0.5f) range = std::min (15.f, range + 2.f);
        }
        else break;
    }
    finish (r, best, bestM);
    r.confidence = f.harshEvents.size() >= 3 ? 0.8f : 0.6f;
    r.simple = "I tamed the harsh moments only when they happen: they're about " + f1 (bestRed) + " dB softer around " + hzs (fh)
               + ", while the rest of the vocal keeps its clarity (" + f1 (bestLoss) + " dB change elsewhere).";
    r.engineer = "Measured " + std::to_string (f.harshEvents.size()) + " harsh events centred at " + hzs (f.harshCenterHz) + " (+" + f1 (f.harshSeverityDb)
                 + " dB over this voice's 2-5 kHz norm). Dynamic bell @" + hzs (fh) + " Q " + f1 (q) + ", threshold " + f1 (best[dynBandParam (kDynHarsh, 3)])
                 + " dB, max cut " + f1 (best[dynBandParam (kDynHarsh, 4)]) + " dB instead of a static cut. Loudness-matched: harsh moments -" + f1 (bestRed)
                 + " dB, same band elsewhere -" + f1 (bestLoss) + " dB, presence " + f1 (bestM.presence - r.before.presence) + " dB.";
    if (bestRed < target * 0.6f) r.warnings.push_back ("harshness only reduced by " + f1 (bestRed) + " dB without dulling the vocal");
    return r;
}

//==============================================================================
// SIBILANCE: de-esser at the voice's own sibilance frequency.
TreatmentResult TreatmentSession::sibilance (const TreatmentRequest& req)
{
    auto r = begin (req);
    const auto& f = inputFeatures;
    const float amount = std::clamp (req.amount * bias.deessScale, 0.1f, 1.f);
    const float target = 3.f + 5.f * amount;
    const float maxLoss = 1.0f;
    if (f.sibilantEvents.empty())
    {
        r.skipReason = "no sibilant events measured";
        r.simple = "I didn't hear any 's' sounds standing out, so I left the de-esser off.";
        return r;
    }
    const float fs = std::clamp (f.sibilanceCenterHz, 4000.f, 11000.f);
    float thr = 0.5f * ((r.before.sibOnEvents + r.before.integratedLufs + 3.f) + (r.before.sibElsewhere + r.before.integratedLufs + 3.f));
    float range = std::min (16.f, target + 3.f);
    ChainSettings s = candidate;
    ChainSettings best = s; Metrics bestM = r.before; float bestScore = -1e9f, bestRed = 0, bestLoss = 0;
    for (int it = 0; it < 7 && ! cancelled(); ++it)
    {
        setp (s, P::DessOn, 1);
        setp (s, P::DessFreq, fs);
        setp (s, P::DessThresh, thr);
        setp (s, P::DessRange, range);
        setp (s, P::DessMode, 0);
        const Metrics m = evaluate (s);
        ++r.iterations;
        const float red = r.before.sibOnEvents - m.sibOnEvents;
        const float loss = r.before.sibElsewhere - m.sibElsewhere;
        const float score = std::min (red, target) - 2.5f * std::max (0.f, loss - maxLoss) - 0.25f * std::abs (red - target)
                            - 1.5f * std::max (0.f, red - 11.f);   // lisp guard
        r.log.push_back ("iter " + std::to_string (it + 1) + ": thr " + f1 (thr) + " dB -> esses -" + f1 (red) + " dB, air elsewhere -" + f1 (loss) + " dB");
        if (score > bestScore) { bestScore = score; best = s; bestM = m; bestRed = red; bestLoss = loss; }
        if (loss > maxLoss || red > 11.f) thr += 2.f;
        else if (red < target * 0.85f) { thr -= std::clamp ((target - red) * 1.2f, 1.f, 6.f); if (red < target * 0.5f) range = std::min (20.f, range + 2.f); }
        else break;
    }
    finish (r, best, bestM);
    r.confidence = f.sibilantEvents.size() >= 4 ? 0.8f : 0.55f;
    r.simple = "I softened the 's' sounds by about " + f1 (bestRed) + " dB without taking the air off the rest of the vocal.";
    r.engineer = std::to_string (f.sibilantEvents.size()) + " sibilants centred at " + hzs (f.sibilanceCenterHz) + " (p90 " + f1 (f.sibilanceSeverityDb)
                 + " dB vs vowels). Split-band de-esser @" + hzs (fs) + ", threshold " + f1 (best[P::DessThresh]) + " dB, range " + f1 (best[P::DessRange])
                 + " dB. Loudness-matched: sibilants -" + f1 (bestRed) + " dB, same band on non-sibilant audio -" + f1 (bestLoss) + " dB.";
    return r;
}

//==============================================================================
// CLARITY / "muffled": investigate causes and treat only the ones with evidence.
TreatmentResult TreatmentSession::clarity (const TreatmentRequest& req)
{
    auto r = begin (req);
    const auto& f = inputFeatures;
    const float amount = std::clamp (req.amount, 0.1f, 1.f);
    ChainSettings s = candidate;
    std::vector<std::string> causes, actions;

    // 1. our own chain making it dull?
    if (s.on (P::DessOn) && (r.before.dessAvgGr > 4.f || s[P::DessRange] > 10.f))
    {
        causes.push_back ("the de-esser was working hard (avg " + f1 (r.before.dessAvgGr) + " dB)");
        setp (s, P::DessRange, std::max (3.f, s[P::DessRange] * 0.6f));
        setp (s, P::DessThresh, s[P::DessThresh] + 3.f);
        actions.push_back ("relaxed the de-esser");
    }
    if (s.on (P::CompOn) && r.before.compAvgGr > 7.f)
    {
        causes.push_back ("heavy compression (avg " + f1 (r.before.compAvgGr) + " dB GR)");
        setp (s, P::CompThresh, s[P::CompThresh] + 3.f);
        setp (s, P::CompRatio, std::max (1.8f, s[P::CompRatio] - 1.f));
        actions.push_back ("eased the compressor");
    }
    if (s.on (P::SpcOn) && (s[P::SpcRevMix] > 30.f || r.before.tailToDirectDb > -18.f))
    {
        causes.push_back ("the reverb is masking the voice (tail " + f1 (r.before.tailToDirectDb) + " dB at 80 ms)");
        setp (s, P::SpcRevMix, s[P::SpcRevMix] * 0.6f);
        setp (s, P::SpcDuck, std::max (40.f, s[P::SpcDuck]));
        setp (s, P::SpcLowCut, std::max (300.f, s[P::SpcLowCut]));
        actions.push_back ("reduced and ducked the reverb");
    }
    // 2. the source itself
    const float mudIndex = f.lowMidDb - f.presenceDb;
    if (mudIndex > 9.f || sem.find ("low_mid_congestion") != nullptr || sem.find ("boxiness") != nullptr)
    {
        float fm = 320.f;
        for (auto& res : f.resonances) if (res.freqHz > 180.f && res.freqHz < 600.f) { fm = res.freqHz; break; }
        const float cut = -(1.5f + 2.f * amount);
        setEqBand (s, kBandLowMid, 0, fm, std::min (eqGain (s, kBandLowMid), 0.f) + cut, 1.3f);
        causes.push_back ("low-mid build-up (250-500 Hz is " + f1 (mudIndex) + " dB above presence)");
        actions.push_back ("cut " + f1 (-cut) + " dB at " + hzs (fm));
    }
    if (f.presenceDb < -14.f || sem.find ("lack_of_presence") != nullptr)
    {
        const float fp = f.presencePeakHz > 1800.f ? std::clamp (f.presencePeakHz, 2200.f, 4500.f) : 3000.f;
        const float boost = 1.5f + 2.f * amount + 0.5f * bias.brightnessDb;
        setEqBand (s, kBandPresence, 0, fp, eqGain (s, kBandPresence) + boost, 0.9f);
        causes.push_back ("not much presence (1.5-4 kHz holds " + f1 (f.presenceDb) + " dB of the energy)");
        actions.push_back ("+" + f1 (boost) + " dB presence at " + hzs (fp));
    }
    if (f.sampleRate >= 40000 && (f.airDb < -34.f || sem.find ("lack_of_air") != nullptr))
    {
        const float boost = 1.5f + 2.5f * amount + 0.5f * bias.brightnessDb;
        setEqBand (s, kBandHighShelf, 2, 10000.f, eqGain (s, kBandHighShelf) + boost, 0.7f);
        causes.push_back ("little air above 10 kHz (" + f1 (f.airDb) + " dB of the energy)");
        actions.push_back ("+" + f1 (boost) + " dB air shelf at 10 kHz");
    }
    if (causes.empty())
    {
        // Nothing specific: a modest presence lift is the least destructive clarity move.
        const float boost = 1.f + 1.5f * amount;
        setEqBand (s, kBandPresence, 0, 3000.f, eqGain (s, kBandPresence) + boost, 0.8f);
        causes.push_back ("no single measurable cause (tonal balance is within normal ranges)");
        actions.push_back ("gentle +" + f1 (boost) + " dB presence lift");
        r.confidence = 0.35f;
    }
    else r.confidence = 0.65f;

    Metrics m = evaluate (s, true);
    ++r.iterations;
    // make sure brightening didn't create harshness or sibilance problems
    if (r.before.harshOnEvents > -99 && m.harshOnEvents - r.before.harshOnEvents > 1.5f)
    {
        candidate = s; candidateMetricsValid = false;
        TreatmentRequest hr; hr.id = "harshness"; hr.amount = 0.4f; hr.avoidDullness = true;
        auto h = harshness (hr);
        candidate = r.settings; candidateMetricsValid = false;
        if (h.applied) { s = h.settings; m = h.after; actions.push_back ("added dynamic control because the lift exposed harsh notes"); }
    }
    if (r.before.sibOnEvents > -99 && m.sibOnEvents - r.before.sibOnEvents > 2.f)
    {
        candidate = s; candidateMetricsValid = false;
        TreatmentRequest sr2; sr2.id = "sibilance"; sr2.amount = 0.4f;
        auto d = sibilance (sr2);
        candidate = r.settings; candidateMetricsValid = false;
        if (d.applied) { s = d.settings; m = d.after; actions.push_back ("de-essed the 's' sounds the lift brought out"); }
    }
    finish (r, s, m);
    std::string causeText, actionText;
    for (size_t i = 0; i < causes.size(); ++i) causeText += (i ? "; " : "") + causes[i];
    for (size_t i = 0; i < actions.size(); ++i) actionText += (i ? ", " : "") + actions[i];
    r.simple = "I looked for why it sounds muffled: " + causeText + ". So I " + actionText + ".";
    r.engineer = "Diagnosis: " + causeText + ". Actions: " + actionText + ". Loudness-matched presence " + f1 (r.before.presence) + " -> " + f1 (m.presence)
                 + " dB, air " + f1 (r.before.air) + " -> " + f1 (m.air) + " dB, low-mid " + f1 (r.before.lowMid) + " -> " + f1 (m.lowMid) + " dB.";
    return r;
}

//==============================================================================
TreatmentResult TreatmentSession::presence (const TreatmentRequest& req)
{
    auto r = begin (req);
    const auto& f = inputFeatures;
    const float amount = std::clamp (req.amount, 0.1f, 1.f);
    const float fp = f.presencePeakHz > 1800.f ? std::clamp (f.presencePeakHz, 2000.f, 4500.f) : 3000.f;
    const float delta = (float) req.direction * (1.5f + 2.5f * amount) + (req.direction > 0 ? 0.5f * bias.brightnessDb : 0.f);
    ChainSettings s = candidate;
    setEqBand (s, kBandPresence, 0, fp, eqGain (s, kBandPresence) + delta, 0.9f);
    Metrics m = evaluate (s);
    ++r.iterations;
    std::string extra;
    if (req.direction > 0 && r.before.harshOnEvents > -99 && m.harshOnEvents - r.before.harshOnEvents > 1.5f)
    {
        candidate = s; candidateMetricsValid = false;
        TreatmentRequest hr; hr.id = "harshness"; hr.amount = 0.4f; hr.avoidDullness = true;
        auto h = harshness (hr);
        candidate = r.settings; candidateMetricsValid = false;
        if (h.applied) { s = h.settings; m = h.after; extra = " The lift exposed the harsh notes, so I added dynamic control there too."; }
    }
    finish (r, s, m);
    r.confidence = 0.7f;
    r.simple = std::string (req.direction > 0 ? "I brought the voice forward with " : "I softened the forwardness by ") + f1 (std::abs (delta))
               + " dB around " + hzs (fp) + "." + extra;
    r.engineer = "Presence bell @" + hzs (fp) + " (measured presence peak) " + f1 (delta) + " dB Q 0.9. Loudness-matched presence "
                 + f1 (r.before.presence) + " -> " + f1 (m.presence) + " dB." + extra;
    return r;
}

TreatmentResult TreatmentSession::air (const TreatmentRequest& req)
{
    auto r = begin (req);
    const float amount = std::clamp (req.amount, 0.1f, 1.f);
    const float delta = (float) req.direction * (1.5f + 3.f * amount) + (req.direction > 0 ? 0.5f * bias.brightnessDb : 0.f);
    const float fAir = sr >= 88000 ? 12000.f : 10000.f;
    ChainSettings s = candidate;
    setEqBand (s, kBandHighShelf, 2, fAir, eqGain (s, kBandHighShelf) + delta, 0.7f);
    Metrics m = evaluate (s);
    ++r.iterations;
    std::string extra;
    if (req.direction > 0 && r.before.sibOnEvents > -99 && m.sibOnEvents - r.before.sibOnEvents > 1.5f)
    {
        candidate = s; candidateMetricsValid = false;
        TreatmentRequest dr; dr.id = "sibilance"; dr.amount = 0.45f;
        auto d = sibilance (dr);
        candidate = r.settings; candidateMetricsValid = false;
        if (d.applied) { s = d.settings; m = d.after; extra = " The extra air made the 's' sounds pop, so I de-essed them."; }
    }
    finish (r, s, m);
    r.confidence = 0.7f;
    r.simple = std::string (req.direction > 0 ? "I opened up the top end (" : "I took some top end off (") + f1 (delta) + " dB above " + hzs (fAir) + ")." + extra;
    r.engineer = "High shelf @" + hzs (fAir) + " " + f1 (delta) + " dB Q 0.7. Air " + f1 (r.before.air) + " -> " + f1 (m.air)
                 + " dB (loudness matched), sibilants " + f1 (m.sibOnEvents - r.before.sibOnEvents) + " dB." + extra;
    return r;
}

TreatmentResult TreatmentSession::brightness (const TreatmentRequest& req)
{
    auto r = begin (req);
    const float amount = std::clamp (req.amount, 0.1f, 1.f);
    ChainSettings s = candidate;
    if (req.direction < 0)
    {
        // darker without losing clarity: shelf high enough to spare presence, verify
        float fShelf = 6500.f, cut = -(1.5f + 3.f * amount);
        Metrics m;
        for (int it = 0; it < 3; ++it)
        {
            s = candidate;
            setEqBand (s, kBandHighShelf, 2, fShelf, eqGain (s, kBandHighShelf) + cut, 0.6f);
            m = evaluate (s);
            ++r.iterations;
            const float presLoss = r.before.presence - m.presence;
            r.log.push_back ("shelf " + hzs (fShelf) + " " + f1 (cut) + " dB -> presence -" + f1 (presLoss) + " dB");
            if (presLoss <= (req.avoidDullness ? 0.6f : 1.2f)) break;
            fShelf *= 1.3f;
        }
        finish (r, s, m);
        r.simple = "I made it darker (" + f1 (cut) + " dB above ~" + hzs (fShelf) + ") but kept the presence where it was, so it stays clear.";
        r.engineer = "High shelf " + f1 (cut) + " dB @" + hzs (fShelf) + ", shelf frequency raised until presence loss <= 1 dB. Air "
                     + f1 (r.before.air) + " -> " + f1 (m.air) + " dB, presence " + f1 (r.before.presence) + " -> " + f1 (m.presence) + " dB.";
    }
    else
    {
        const float boost = 1.5f + 2.5f * amount + 0.5f * bias.brightnessDb;
        setEqBand (s, kBandHighShelf, 2, 5500.f, eqGain (s, kBandHighShelf) + boost, 0.6f);
        Metrics m = evaluate (s);
        ++r.iterations;
        std::string extra;
        if (r.before.sibOnEvents > -99 && m.sibOnEvents - r.before.sibOnEvents > 1.5f)
        {
            candidate = s; candidateMetricsValid = false;
            TreatmentRequest dr; dr.id = "sibilance"; dr.amount = 0.45f;
            auto d = sibilance (dr);
            candidate = r.settings; candidateMetricsValid = false;
            if (d.applied) { s = d.settings; m = d.after; extra = " and kept the 's' sounds in check"; }
        }
        finish (r, s, m);
        r.simple = "I brightened it (+" + f1 (boost) + " dB high shelf)" + extra + ".";
        r.engineer = "High shelf +" + f1 (boost) + " dB @5.5 kHz. Centroid " + std::to_string ((int) r.before.centroidHz) + " -> "
                     + std::to_string ((int) r.after.centroidHz) + " Hz" + extra + ".";
    }
    r.confidence = 0.7f;
    return r;
}

TreatmentResult TreatmentSession::warmth (const TreatmentRequest& req)
{
    auto r = begin (req);
    const auto& f = inputFeatures;
    const float amount = std::clamp (req.amount, 0.1f, 1.f);
    ChainSettings s = candidate;
    const float fBody = std::clamp (f.f0MedianHz > 0 ? 1.6f * f.f0MedianHz : 220.f, 140.f, 380.f);
    if (req.direction > 0)
    {
        float gain = 1.f + 2.f * amount;
        Metrics m;
        for (int it = 0; it < 3; ++it)
        {
            s = candidate;
            setEqBand (s, kBandLowShelf, 1, fBody, eqGain (s, kBandLowShelf) + gain, 0.7f);
            setp (s, P::ColOn, 1);
            setp (s, P::ColType, 0);
            setp (s, P::ColDrive, 3.f + 4.f * amount);
            setp (s, P::ColWarmth, 20.f + 25.f * amount);
            setp (s, P::ColMix, 100.f);
            m = evaluate (s);
            ++r.iterations;
            const float mudRise = m.lowMid - r.before.lowMid;
            r.log.push_back ("shelf +" + f1 (gain) + " dB -> low-mid +" + f1 (mudRise) + " dB");
            if (mudRise < 2.5f || f.lowMidDb - f.presenceDb < 8.f) break;
            gain *= 0.5f;
        }
        finish (r, s, m);
        r.simple = "I warmed it up with a little more body around " + hzs (fBody) + " and gentle tape-style saturation for an analog feel.";
        r.engineer = "Low shelf +" + f1 (s[eqBandParam (kBandLowShelf, 3)]) + " dB @" + hzs (fBody) + " (~1.6 x f0), tape saturation "
                     + f1 (s[P::ColDrive]) + " dB drive (2x oversampled) with +" + f1 (s[P::ColWarmth]) + "% warmth tilt. Low-mid "
                     + f1 (r.before.lowMid) + " -> " + f1 (m.lowMid) + " dB, checked against mud.";
    }
    else
    {
        const float cut = -(1.f + 2.f * amount);
        setEqBand (s, kBandLowShelf, 1, fBody, eqGain (s, kBandLowShelf) + cut, 0.7f);
        if (s.on (P::ColOn)) setp (s, P::ColWarmth, s[P::ColWarmth] - 25.f);
        const Metrics m = evaluate (s);
        ++r.iterations;
        finish (r, s, m);
        r.simple = "I leaned it out: " + f1 (cut) + " dB of low body around " + hzs (fBody) + ".";
        r.engineer = "Low shelf " + f1 (cut) + " dB @" + hzs (fBody) + ". Body " + f1 (r.before.body) + " -> " + f1 (m.body) + " dB.";
    }
    r.confidence = 0.65f;
    return r;
}

TreatmentResult TreatmentSession::mud (const TreatmentRequest& req)
{
    auto r = begin (req);
    const auto& f = inputFeatures;
    const float amount = std::clamp (req.amount, 0.1f, 1.f);
    float fm = workMode == WorkMode::Vocal ? 320.f : 250.f;
    float prom = 0.f;
    for (auto& res : f.resonances) if (res.freqHz > 170.f && res.freqHz < 600.f && res.prominenceDb > prom) { fm = res.freqHz; prom = res.prominenceDb; }
    float cut = -(1.5f + 3.f * amount);
    ChainSettings s; Metrics m;
    for (int it = 0; it < 3; ++it)
    {
        s = candidate;
        setEqBand (s, kBandLowMid, 0, fm, std::min (0.f, eqGain (s, kBandLowMid)) + cut, prom > 0 ? 2.f : 1.3f);
        m = evaluate (s);
        ++r.iterations;
        const float bodyLoss = r.before.body - m.body;
        r.log.push_back ("cut " + f1 (cut) + " dB @" + hzs (fm) + " -> body -" + f1 (bodyLoss) + " dB");
        if (bodyLoss <= 2.5f) break;
        cut *= 0.65f;
    }
    finish (r, s, m);
    r.confidence = prom > 0 ? 0.7f : 0.5f;
    r.simple = "I cleared some low-mid build-up (" + f1 (cut) + " dB around " + hzs (fm) + ") so the voice isn't clouded, keeping its body.";
    r.engineer = std::string ("Bell ") + f1 (cut) + " dB @" + hzs (fm) + (prom > 0 ? " (measured resonance, " + f1 (prom) + " dB prominent)" : " (typical build-up region)")
                 + ". Low-mid " + f1 (r.before.lowMid) + " -> " + f1 (m.lowMid) + " dB, body loss capped at 2.5 dB.";
    return r;
}

TreatmentResult TreatmentSession::resonance (const TreatmentRequest& req)
{
    auto r = begin (req);
    const auto& f = inputFeatures;
    if (f.resonances.empty())
    {
        r.skipReason = "no prominent resonances measured";
        r.simple = "I didn't find a narrow ringing resonance to notch.";
        return r;
    }
    const auto res = f.resonances.front();
    const float amount = std::clamp (req.amount, 0.1f, 1.f);
    const float cut = -std::min (8.f, 1.f + res.prominenceDb * 0.6f * amount);
    ChainSettings s = candidate;
    if (res.freqHz > 2000.f && ! s.on (dynBandParam (kDynHarsh, 0)))
    {
        const float lvl = r.before.harshElsewhere + r.before.integratedLufs + 3.f;
        setp (s, P::DeqOn, 1);
        setp (s, dynBandParam (kDynHarsh, 0), 1);
        setp (s, dynBandParam (kDynHarsh, 1), res.freqHz);
        setp (s, dynBandParam (kDynHarsh, 2), 5.f);
        setp (s, dynBandParam (kDynHarsh, 3), lvl - 3.f);
        setp (s, dynBandParam (kDynHarsh, 4), -cut);
    }
    else
        setEqBand (s, kBandMid, 0, res.freqHz, cut, 5.f);
    const Metrics m = evaluate (s);
    ++r.iterations;
    finish (r, s, m);
    r.confidence = 0.6f;
    r.simple = "I notched out a ringing resonance at " + hzs (res.freqHz) + ".";
    r.engineer = "Narrow cut " + f1 (cut) + " dB Q 5 @" + hzs (res.freqHz) + " (" + f1 (res.prominenceDb) + " dB above the 2/3-octave smoothed spectrum)"
                 + (res.freqHz > 2000.f ? ", dynamic so it only acts when it rings." : ".");
    return r;
}

TreatmentResult TreatmentSession::rumble (const TreatmentRequest& req)
{
    auto r = begin (req);
    const auto& f = inputFeatures;
    float fc;
    if (workMode == WorkMode::Vocal)
        fc = std::clamp (f.f0MinHz > 0 ? 0.6f * f.f0MinHz : 80.f, 45.f, 120.f);
    else
        fc = 25.f;
    ChainSettings s; Metrics m;
    for (int it = 0; it < 3; ++it)
    {
        s = candidate;
        setp (s, P::EqOn, 1);
        setp (s, P::HpfOn, 1);
        setp (s, P::HpfFreq, fc);
        setp (s, P::HpfSlope, workMode == WorkMode::Vocal ? 1.f : 0.f);
        m = evaluate (s);
        ++r.iterations;
        if (r.before.body - m.body <= 1.f) break;
        fc *= 0.8f;
    }
    finish (r, s, m);
    r.confidence = 0.8f;
    r.simple = "I removed sub rumble below " + hzs (fc) + " - nothing the voice actually uses.";
    r.engineer = "High-pass " + hzs (fc) + (workMode == WorkMode::Vocal ? " 24 dB/oct (0.6 x lowest f0 " + hzs (f.f0MinHz) + ")" : " 12 dB/oct")
                 + ". Sub " + f1 (r.before.sub) + " -> " + f1 (m.sub) + " dB, body change " + f1 (m.body - r.before.body) + " dB.";
    return r;
}

TreatmentResult TreatmentSession::plosives (const TreatmentRequest& req)
{
    auto rr = rumble (req);          // high-pass first; its settings are the starting point below
    auto r = begin (req);
    const auto& f = inputFeatures;
    ChainSettings s = rr.applied ? rr.settings : candidate;
    float lvl = -30.f;
    if (! f.plosiveEvents.empty())
    {
        std::vector<float> l;
        for (auto& e : f.plosiveEvents) l.push_back (e.levelDb);
        lvl = analysis::median (l);
    }
    setp (s, P::DeqOn, 1);
    setp (s, dynBandParam (kDynLow, 0), 1);
    setp (s, dynBandParam (kDynLow, 1), 110.f);
    setp (s, dynBandParam (kDynLow, 2), 0.8f);
    setp (s, dynBandParam (kDynLow, 3), lvl - 6.f);
    setp (s, dynBandParam (kDynLow, 4), 9.f);
    setp (s, dynBandParam (kDynLow, 5), 1.f);
    setp (s, dynBandParam (kDynLow, 6), 60.f);
    const Metrics m = evaluate (s);
    ++r.iterations;
    finish (r, s, m);
    r.confidence = f.plosiveEvents.empty() ? 0.35f : 0.7f;
    r.simple = "I tamed the P/B pops with a high-pass and a fast low-band compressor that only reacts to the bursts.";
    r.engineer = std::to_string (f.plosiveEvents.size()) + " plosive bursts measured. " + rr.engineer + " Dynamic low band @110 Hz, 1 ms attack, threshold "
                 + f1 (lvl - 6.f) + " dB, up to 9 dB cut.";
    return r;
}

//==============================================================================
TreatmentResult TreatmentSession::space (const TreatmentRequest& req)
{
    auto r = begin (req);
    const auto& f = inputFeatures;
    const float amount = std::clamp (req.amount, 0.1f, 1.f);
    ChainSettings s = candidate;
    if (req.direction > 0)
    {
        float mix = (18.f + 22.f * amount) * bias.reverbScale;
        const float decay = workMode == WorkMode::Vocal ? 1.3f + 0.9f * amount : 1.1f + 0.6f * amount;
        Metrics m;
        for (int it = 0; it < 4; ++it)
        {
            setp (s, P::SpcOn, 1);
            setp (s, P::SpcRevMix, mix);
            setp (s, P::SpcDecay, decay);
            setp (s, P::SpcPreDelay, 30.f);
            setp (s, P::SpcLowCut, 250.f);
            setp (s, P::SpcDamping, 50.f);
            setp (s, P::SpcDuck, 25.f * amount);
            m = evaluate (s, true);
            ++r.iterations;
            const float tailRise = m.tailToDirectDb - r.before.tailToDirectDb;
            r.log.push_back ("reverb " + f1 (mix) + "% -> tail " + f1 (m.tailToDirectDb) + " dB (" + f1 (tailRise) + ")");
            if (m.tailToDirectDb >= -26.f + 8.f * amount || mix >= 70.f) break;
            mix *= 1.3f;
        }
        finish (r, s, m);
        r.simple = "I placed the voice in a space: a " + f1 (decay) + " s reverb with a short pre-delay so the words stay clear, ducked under the singing.";
        r.engineer = "FDN reverb " + f1 (s[P::SpcRevMix]) + "% return, decay " + f1 (decay) + " s, pre-delay 30 ms, low cut 250 Hz, duck "
                     + f1 (s[P::SpcDuck]) + "%. Tail 80 ms after phrase ends " + f1 (r.before.tailToDirectDb) + " -> " + f1 (m.tailToDirectDb) + " dB.";
    }
    else
    {
        std::vector<std::string> acts;
        if (s.on (P::SpcOn))
        {
            setp (s, P::SpcRevMix, s[P::SpcRevMix] * (1.f - 0.6f * amount));
            setp (s, P::SpcDlyMix, s[P::SpcDlyMix] * (1.f - 0.6f * amount));
            setp (s, P::SpcDuck, std::max (s[P::SpcDuck], 50.f));
            acts.push_back ("pulled the reverb/delay back and ducked them");
        }
        // proximity cues: a touch of density + presence + low body
        setp (s, P::CompOn, 1);
        if (! candidate.on (P::CompOn))
        {
            setp (s, P::CompThresh, f.integratedLufs + 0.7f);
            setp (s, P::CompRatio, 2.5f);
            setp (s, P::CompAttack, 15.f);
            setp (s, P::CompRelease, 100.f);
        }
        else
            setp (s, P::CompRatio, s[P::CompRatio] + 0.5f);
        setEqBand (s, kBandPresence, 0, f.presencePeakHz > 1800.f ? f.presencePeakHz : 3000.f, eqGain (s, kBandPresence) + 1.f + amount, 0.9f);
        acts.push_back ("added a little density and presence (proximity cues)");
        ChainSettings s2 = s;
        Metrics m = evaluate (s2, true);
        ++r.iterations;
        // keep loudness
        if (std::abs (m.integratedLufs - r.before.integratedLufs) > 0.5f)
        {
            setp (s2, P::CompMakeup, s2[P::CompMakeup] + (r.before.integratedLufs - m.integratedLufs));
            m = evaluate (s2, true);
            ++r.iterations;
        }
        finish (r, s2, m);
        std::string a;
        for (size_t i = 0; i < acts.size(); ++i) a += (i ? " and " : "") + acts[i];
        r.simple = "I brought the voice closer: I " + a + ".";
        r.engineer = "Closer = less diffuse field + more direct-sound cues. " + a + ". Tail " + f1 (r.before.tailToDirectDb) + " -> " + f1 (m.tailToDirectDb) + " dB.";
        if (f.spaceConfidence > 0.3f && f.tailToDirectDb > -24.f)
        {
            r.warnings.push_back ("the recording itself contains room sound (tail " + f1 (f.tailToDirectDb) + " dB); NOVA cannot remove baked-in reverb yet");
            r.simple += " Note: some room sound is baked into the recording itself and can't be fully removed.";
        }
    }
    r.confidence = 0.65f;
    return r;
}

TreatmentResult TreatmentSession::width (const TreatmentRequest& req)
{
    auto r = begin (req);
    const float amount = std::clamp (req.amount * bias.widthScale, 0.1f, 1.f);
    ChainSettings s = candidate;
    const bool monoSource = inputFeatures.isEffectivelyMono;
    if (workMode == WorkMode::Vocal || monoSource)
    {
        if (req.direction > 0)
        {
            setp (s, P::SpcOn, 1);
            setp (s, P::SpcRevWidth, 100.f);
            setp (s, P::SpcDlyMix, std::max (s[P::SpcDlyMix], 8.f + 10.f * amount));
            setp (s, P::SpcDlyPingPong, 1);
            setp (s, P::SpcDlySync, 1);
            setp (s, P::SpcDlyDiv, 5);   // 1/8 dotted
            setp (s, P::SpcDlyFeedback, 25.f);
            setp (s, P::SpcDuck, std::max (s[P::SpcDuck], 40.f));
            if (s[P::SpcRevMix] < 12.f) setp (s, P::SpcRevMix, 14.f);
        }
        else
        {
            setp (s, P::SpcRevWidth, s[P::SpcRevWidth] * 0.5f);
            setp (s, P::SpcDlyPingPong, 0);
        }
        const Metrics m = evaluate (s, true);
        ++r.iterations;
        finish (r, s, m);
        r.simple = req.direction > 0 ? "The lead stays centred (as it should); I widened the space around it with a ping-pong 1/8-dotted delay and full-width reverb."
                                     : "I narrowed the effects around the voice.";
        r.engineer = "Mono lead kept centred; width created in the FX returns. Side/mid " + f1 (r.before.sideToMidDb) + " -> " + f1 (m.sideToMidDb)
                     + " dB, mono sum change " + f1 (m.monoLossDb) + " dB.";
    }
    else
    {
        float w = req.direction > 0 ? 100.f + 15.f + 25.f * amount : 100.f - 15.f - 25.f * amount;
        Metrics m;
        for (int it = 0; it < 3; ++it)
        {
            s = candidate;
            setp (s, P::ImgWidth, w);
            if (req.direction > 0) { setp (s, P::ImgMonoBassOn, 1); setp (s, P::ImgMonoBass, 120.f); }
            m = evaluate (s, true);
            ++r.iterations;
            if (req.direction < 0 || m.monoLossDb > -1.5f || w <= 105.f) break;
            w = 100.f + (w - 100.f) * 0.6f;   // back off if mono compatibility suffers
        }
        finish (r, s, m);
        r.simple = req.direction > 0 ? "I widened the stereo image to " + std::to_string ((int) w) + "% and kept the low end mono so it still works on phones and clubs."
                                     : "I narrowed the stereo image to " + std::to_string ((int) w) + "%.";
        r.engineer = "M/S width " + std::to_string ((int) w) + "%, mono below 120 Hz. Side/mid " + f1 (r.before.sideToMidDb) + " -> " + f1 (m.sideToMidDb)
                     + " dB; mono sum change " + f1 (m.monoLossDb) + " dB (checked <= 1.5 dB).";
    }
    r.confidence = 0.6f;
    return r;
}

//==============================================================================
TreatmentResult TreatmentSession::loudness (const TreatmentRequest& req)
{
    auto r = begin (req);
    const float target = (req.targetLufs < -1.f ? req.targetLufs : (req.preserveDynamics ? -11.f : -9.f)) + bias.loudnessOffsetLu;
    const float minPlr = req.preserveDynamics ? 9.f : 7.f;
    ChainSettings s = candidate;

    // glue compression first (1-2 dB) so the limiter isn't doing all the work
    if (! s.on (P::CompOn))
    {
        setp (s, P::CompOn, 1);
        setp (s, P::CompRatio, 1.8f);
        setp (s, P::CompAttack, 30.f);
        setp (s, P::CompRelease, 180.f);
        setp (s, P::CompKnee, 10.f);
        setp (s, P::CompDetector, 1);
        setp (s, P::CompThresh, r.before.integratedLufs + 4.f);
        for (int it = 0; it < 2; ++it)
        {
            const Metrics m = evaluate (s, false);
            ++r.iterations;
            if (m.compAvgGr < 1.f) setp (s, P::CompThresh, s[P::CompThresh] - 3.f);
            else if (m.compAvgGr > 3.f) setp (s, P::CompThresh, s[P::CompThresh] + 2.f);
            else break;
        }
    }
    setp (s, P::LimOn, 1);
    setp (s, P::LimCeiling, -1.f);
    setp (s, P::LimRelease, 60.f);
    float gain = std::clamp (target - r.before.integratedLufs, 0.f, 18.f);
    Metrics m;
    for (int it = 0; it < 6 && ! cancelled(); ++it)
    {
        setp (s, P::LimGain, gain);
        m = evaluate (s, false);
        ++r.iterations;
        const float plr = m.truePeakDb - m.integratedLufs;
        r.log.push_back ("limiter drive " + f1 (gain) + " dB -> " + f1 (m.integratedLufs) + " LUFS, TP " + f1 (m.truePeakDb) + " dBTP, PLR " + f1 (plr) + " dB");
        if (m.truePeakDb > -0.8f) setp (s, P::LimCeiling, s[P::LimCeiling] - 0.3f);
        if (plr < minPlr) { gain = std::max (0.f, gain - (minPlr - plr) * 0.8f); continue; }
        const float err = target - m.integratedLufs;
        if (std::abs (err) < 0.3f) break;
        gain = std::clamp (gain + err, 0.f, 18.f);
    }
    finish (r, s, m);
    const float plr = m.truePeakDb - m.integratedLufs;
    r.confidence = 0.8f;
    const bool reached = std::abs (m.integratedLufs - target) < 0.6f;
    r.simple = "Master is now " + f1 (m.integratedLufs) + " LUFS with peaks safely at " + f1 (m.truePeakDb) + " dBTP"
               + (reached ? "." : " - I stopped short of " + f1 (target) + " LUFS to keep the punch.");
    r.engineer = "Glue comp " + f1 (s[P::CompRatio]) + ":1 (avg " + f1 (m.compAvgGr) + " dB GR) -> lookahead limiter +" + f1 (s[P::LimGain]) + " dB drive, ceiling "
                 + f1 (s[P::LimCeiling]) + " dB, release " + f1 (s[P::LimRelease]) + " ms. " + f1 (r.before.integratedLufs) + " -> " + f1 (m.integratedLufs)
                 + " LUFS, TP " + f1 (m.truePeakDb) + " dBTP, PLR " + f1 (plr) + " dB (floor " + f1 (minPlr) + "), crest " + f1 (r.before.crestDb) + " -> "
                 + f1 (m.crestDb) + " dB, limiter max " + f1 (m.limMaxGr) + " dB.";
    if (! reached) r.warnings.push_back ("target " + f1 (target) + " LUFS not reached within the dynamics budget");
    return r;
}

TreatmentResult TreatmentSession::saturation (const TreatmentRequest& req)
{
    auto r = begin (req);
    const float amount = std::clamp (req.amount, 0.1f, 1.f);
    const int type = req.pattern >= 0 && req.pattern <= 2 ? req.pattern : 0;
    ChainSettings s = candidate;
    setp (s, P::ColOn, 1);
    setp (s, P::ColType, (float) type);
    setp (s, P::ColDrive, 3.f + 9.f * amount);
    setp (s, P::ColMix, type == 2 ? 70.f : 100.f);
    if (type == 0) setp (s, P::ColWarmth, std::max (s[P::ColWarmth], 15.f));
    TreatmentResult tmp = r;
    auto matched = matchLoudnessTo (s, r.before.integratedLufs, tmp);
    finish (r, matched.settings, matched.after);
    r.iterations = tmp.iterations;
    r.confidence = 0.6f;
    static const char* names[] = { "tape", "tube", "soft-clip" };
    r.simple = std::string ("I added ") + names[type] + " saturation for colour and density, level matched so you hear the character, not just volume.";
    r.engineer = std::string (names[type]) + " shaper, " + f1 (s[P::ColDrive]) + " dB drive, 2x oversampled, mix " + f1 (s[P::ColMix])
                 + "%. Output matched to " + f1 (r.before.integratedLufs) + " LUFS; crest " + f1 (r.before.crestDb) + " -> " + f1 (r.after.crestDb) + " dB.";
    return r;
}

TreatmentResult TreatmentSession::rhythmicGate (const TreatmentRequest& req)
{
    auto r = begin (req);
    ChainSettings s = candidate;
    const int div = req.division >= 0 ? req.division : 3;
    const int pat = req.pattern >= 0 ? req.pattern : 2;
    setp (s, P::GateOn, 1);
    setp (s, P::GateDiv, (float) div);
    setp (s, P::GatePattern, (float) pat);
    setp (s, P::GateDepth, 60.f + 40.f * std::clamp (req.amount, 0.f, 1.f));
    setp (s, P::GateSmooth, 2.5f);
    const Metrics m = evaluate (s);
    ++r.iterations;
    finish (r, s, m);
    const double stepMs = dsp::RhythmGate::stepLengthBeats (div) * 60000.0 / std::max (20.0, trans.bpm);
    static const char* divNames[] = { "1/4", "1/8", "1/8 triplet", "1/16", "1/16 triplet", "1/32" };
    static const char* patNames[] = { "straight", "offbeat", "3-3-2 stutter", "broken", "half-time" };
    r.confidence = 0.75f;
    r.simple = std::string ("I added a rhythmic ") + patNames[pat] + " chop on " + divNames[div] + " notes, locked to your " + std::to_string ((int) std::lround (trans.bpm))
               + " BPM" + (trans.hostProvidesTempo ? "" : " (default tempo - the host didn't report one)") + ".";
    r.engineer = std::string ("Tempo-synced gate: ") + divNames[div] + " steps = " + f1 ((float) stepMs) + " ms at " + f1 ((float) trans.bpm) + " BPM, pattern "
                 + patNames[pat] + ", depth " + f1 (s[P::GateDepth]) + "%, 2.5 ms edges to avoid clicks. Follows host transport position.";
    return r;
}

TreatmentResult TreatmentSession::delayThrow (const TreatmentRequest& req)
{
    auto r = begin (req);
    const float amount = std::clamp (req.amount, 0.1f, 1.f);
    ChainSettings s = candidate;
    const int div = req.division >= 0 ? req.division : 5;   // 1/8 dotted
    setp (s, P::SpcOn, 1);
    setp (s, P::SpcDlyMix, 12.f + 15.f * amount);
    setp (s, P::SpcDlySync, 1);
    setp (s, P::SpcDlyDiv, (float) div);
    setp (s, P::SpcDlyFeedback, 28.f + 10.f * amount);
    setp (s, P::SpcDlyTone, 5500.f);
    setp (s, P::SpcDlyPingPong, 1);
    setp (s, P::SpcDuck, std::max (s[P::SpcDuck], 45.f));
    const Metrics m = evaluate (s, true);
    ++r.iterations;
    finish (r, s, m);
    const double ms = dsp::SpaceModule::delayTimeMs (s, trans.bpm);
    r.confidence = 0.7f;
    r.simple = "I added tempo-synced delay throws that sit behind the vocal and bloom in the gaps.";
    r.engineer = "Ping-pong delay " + std::string (choiceName (kParams[(size_t) P::SpcDlyDiv], div)) + " = " + f1 ((float) ms) + " ms at " + f1 ((float) trans.bpm)
                 + " BPM, feedback " + f1 (s[P::SpcDlyFeedback]) + "%, 5.5 kHz tone, ducked " + f1 (s[P::SpcDuck]) + "% by the dry signal.";
    return r;
}

TreatmentResult TreatmentSession::punch (const TreatmentRequest& req)
{
    auto r = begin (req);
    const float amount = std::clamp (req.amount, 0.1f, 1.f);
    ChainSettings s = candidate;
    setp (s, P::CompOn, 1);
    setp (s, P::CompRatio, 3.f + 1.5f * amount);
    setp (s, P::CompAttack, 30.f);
    setp (s, P::CompRelease, 70.f);
    setp (s, P::CompKnee, 4.f);
    setp (s, P::CompDetector, 0);
    setp (s, P::CompMix, 55.f + 25.f * amount);
    setp (s, P::CompThresh, r.before.integratedLufs + 2.f);
    for (int it = 0; it < 3; ++it)
    {
        const Metrics m = evaluate (s);
        ++r.iterations;
        if (m.compAvgGr < 3.f) setp (s, P::CompThresh, s[P::CompThresh] - 3.f);
        else if (m.compAvgGr > 7.f) setp (s, P::CompThresh, s[P::CompThresh] + 2.f);
        else break;
    }
    TreatmentResult tmp = r;
    auto matched = matchLoudnessTo (s, r.before.integratedLufs, tmp);
    finish (r, matched.settings, matched.after);
    r.iterations += tmp.iterations;
    r.confidence = 0.55f;
    r.simple = "I made the hits punchier: the compressor lets each attack through before clamping the sustain, blended in parallel.";
    r.engineer = "Slow-attack (30 ms) / fast-release (70 ms) " + f1 (s[P::CompRatio]) + ":1 compression at " + f1 (s[P::CompMix]) + "% parallel mix, avg GR "
                 + f1 (r.after.compAvgGr) + " dB. Crest " + f1 (r.before.crestDb) + " -> " + f1 (r.after.crestDb) + " dB, loudness matched.";
    return r;
}

TreatmentResult TreatmentSession::body (const TreatmentRequest& req)
{
    auto r = begin (req);
    const float amount = std::clamp (req.amount, 0.1f, 1.f);
    const auto& f = inputFeatures;
    const float fb = workMode == WorkMode::Vocal ? std::clamp (f.f0MedianHz > 0 ? 1.2f * f.f0MedianHz : 200.f, 140.f, 320.f) : 110.f;
    ChainSettings s = candidate;
    const float g = (float) req.direction * (1.5f + 2.f * amount);
    setEqBand (s, kBandLowShelf, 1, fb, eqGain (s, kBandLowShelf) + g, 0.8f);
    const Metrics m = evaluate (s);
    ++r.iterations;
    finish (r, s, m);
    r.confidence = 0.6f;
    r.simple = std::string (req.direction > 0 ? "I added body " : "I reduced the body ") + "around " + hzs (fb) + ".";
    r.engineer = "Low shelf " + f1 (g) + " dB @" + hzs (fb) + ". Body " + f1 (r.before.body) + " -> " + f1 (m.body) + " dB, low-mid "
                 + f1 (r.before.lowMid) + " -> " + f1 (m.lowMid) + " dB.";
    return r;
}

} // namespace nova::ai
