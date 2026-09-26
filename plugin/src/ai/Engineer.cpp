#include "Engineer.h"
#include "IntentParser.h"

#include <chrono>

namespace nova::ai
{

const char* agentPhaseName (AgentPhase p)
{
    switch (p)
    {
        case AgentPhase::Idle:       return "IDLE";
        case AgentPhase::Listening:  return "LISTENING";
        case AgentPhase::Analyzing:  return "ANALYZING";
        case AgentPhase::Thinking:   return "THINKING";
        case AgentPhase::Processing: return "PROCESSING";
        case AgentPhase::Matching:   return "MATCHING";
        case AgentPhase::Complete:   return "COMPLETE";
        case AgentPhase::Error:      return "ERROR";
    }
    return "IDLE";
}

namespace
{
std::string f1 (float v) { return juce::String (v, 1).toStdString(); }

bool isHighFreqParam (int idx, const ChainSettings& s)
{
    for (int b = 0; b < kNumEqBands; ++b)
        if (idx >= eqBandParam (b, 0) && idx <= eqBandParam (b, 4))
            return s[eqBandParam (b, 2)] >= 1800.f || s.choice (eqBandParam (b, 1)) == 2;
    if (idx >= dynBandParam (0, 0) && idx <= dynBandParam (0, 6)) return true;
    if (idx >= P::DessOn && idx <= P::DessMode) return true;
    if (idx == P::ColWarmth || idx == P::LpfOn || idx == P::LpfFreq) return true;
    if (idx == P::TmG6 || idx == P::TmG7 || idx == P::TmG8) return true;
    return false;
}
bool isLowFreqParam (int idx, const ChainSettings& s)
{
    for (int b = 0; b < kNumEqBands; ++b)
        if (idx >= eqBandParam (b, 0) && idx <= eqBandParam (b, 4))
            return s[eqBandParam (b, 2)] <= 600.f || s.choice (eqBandParam (b, 1)) == 1;
    if (idx >= dynBandParam (1, 0) && idx <= dynBandParam (1, 6)) return true;
    if (idx == P::HpfOn || idx == P::HpfFreq || idx == P::HpfSlope) return true;
    if (idx == P::TmG1 || idx == P::TmG2 || idx == P::TmG3) return true;
    return false;
}
bool inRange (int idx, int a, int b) { return idx >= a && idx <= b; }
} // namespace

ChainSettings OfflineEngineer::revertChanges (const ChainSettings& current, const ActionRecord& action, const std::string& target, int& reverted)
{
    ChainSettings s = current;
    reverted = 0;
    for (auto& c : action.changes)
    {
        bool match = target == "last" || target.empty();
        if (target == "highs") match = isHighFreqParam (c.index, current);
        else if (target == "lows") match = isLowFreqParam (c.index, current);
        else if (target == "reverb") match = inRange (c.index, P::SpcOn, P::SpcDlyTone);
        else if (target == "compression") match = inRange (c.index, P::CompOn, P::CompDetector) || inRange (c.index, P::LvlOn, P::LvlAmount) || inRange (c.index, P::LimOn, P::LimRelease);
        else if (target == "width") match = inRange (c.index, P::ImgWidth, P::ImgMonoBass) || c.index == P::SpcRevWidth || c.index == P::SpcDlyPingPong;
        if (match)
        {
            s[c.index] = c.before;
            ++reverted;
        }
    }
    return s;
}

std::string OfflineEngineer::verificationLine (const Metrics& a, const Metrics& b)
{
    std::string s = "Verified on the rendered result (loudness matched): ";
    std::vector<std::string> parts;
    if (std::abs (a.phraseStdDb - b.phraseStdDb) > 0.3f) parts.push_back ("phrase spread " + f1 (a.phraseStdDb) + " -> " + f1 (b.phraseStdDb) + " dB");
    if (a.harshOnEvents > -99 && std::abs (a.harshOnEvents - b.harshOnEvents) > 0.3f) parts.push_back ("harsh moments " + f1 (b.harshOnEvents - a.harshOnEvents) + " dB");
    if (a.sibOnEvents > -99 && std::abs (a.sibOnEvents - b.sibOnEvents) > 0.3f) parts.push_back ("sibilants " + f1 (b.sibOnEvents - a.sibOnEvents) + " dB");
    if (std::abs (a.presence - b.presence) > 0.3f) parts.push_back ("presence " + std::string (b.presence >= a.presence ? "+" : "") + f1 (b.presence - a.presence) + " dB");
    if (std::abs (a.air - b.air) > 0.3f) parts.push_back ("air " + std::string (b.air >= a.air ? "+" : "") + f1 (b.air - a.air) + " dB");
    if (std::abs (a.crestDb - b.crestDb) > 0.5f) parts.push_back ("crest " + f1 (a.crestDb) + " -> " + f1 (b.crestDb) + " dB");
    parts.push_back ("loudness " + f1 (a.integratedLufs) + " -> " + f1 (b.integratedLufs) + " LUFS");
    parts.push_back ("true peak " + f1 (b.truePeakDb) + " dBTP");
    for (size_t i = 0; i < parts.size(); ++i) s += (i ? ", " : "") + parts[i];
    return s + ".";
}

std::string OfflineEngineer::analysisSummary (const analysis::AnalysisResult& a, bool engineer)
{
    const auto& sem = a.semantic;
    const auto& f = a.inputFeatures;
    std::string s;
    if (! f.valid)
        return "I don't have enough audio to analyse yet - play the track so I can listen.";
    s += "I listened to " + f1 ((float) f.activeSec) + " s of " + std::string (analysis::sourceTypeName (sem.source))
         + " (confidence " + std::to_string ((int) std::lround (sem.sourceConfidence * 100)) + "%). ";
    int shown = 0;
    std::string probs;
    for (auto& p : sem.problems)
    {
        if (p.confidence < 0.35f || shown >= 4) continue;
        probs += (shown ? "; " : "") + p.title + (engineer ? " (" + p.evidence + ")" : "");
        ++shown;
    }
    s += shown > 0 ? "What stands out: " + probs + ". " : "Nothing stands out as a clear problem. ";
    if (engineer)
        s += "Dynamics: " + sem.dynamicsSummary + ". Space: " + sem.spaceSummary + ".";
    else
    {
        std::string tags;
        int n = 0;
        for (auto& c : sem.character) if (c.confidence >= 0.4f && n < 4) { tags += (n ? ", " : "") + c.tag; ++n; }
        if (! tags.empty()) s += "Character: " + tags + ".";
    }
    return s;
}

//==============================================================================
EngineerOutcome OfflineEngineer::handle (const std::string& request, EngineerContext& ctx)
{
    const auto t0 = std::chrono::steady_clock::now();
    EngineerOutcome out;
    out.requestText = request;
    out.settings = ctx.current;
    auto status = [&] (AgentPhase p, const std::string& d) { if (ctx.onStatus) ctx.onStatus (p, d); };

    const auto parsed = parseRequest (request);
    const bool haveAudio = ctx.audio != nullptr && ctx.audio->input != nullptr && ctx.audio->inputFeatures.valid;

    // ---- undo / redo / explain need no audio
    if (parsed.has ("undo"))
    {
        out.undoRequested = true;
        out.reply = "Undone - you're back to the previous version.";
        return out;
    }
    if (parsed.isQuestion && parsed.intents.empty())
    {
        auto last = ctx.memory != nullptr ? ctx.memory->last() : std::nullopt;
        if (last && (request.find ("did") != std::string::npos || request.find ("why") != std::string::npos || request.find ("explain") != std::string::npos
                     || request.find ("عملت") != std::string::npos || request.find ("ليه") != std::string::npos))
        {
            out.reply = last->simple;
            out.replyEngineer = last->engineer;
        }
        else if (haveAudio)
        {
            out.reply = analysisSummary (*ctx.audio, false);
            out.replyEngineer = analysisSummary (*ctx.audio, true);
        }
        else out.reply = "Play your track and press LISTEN - I need to hear it before I can answer.";
        return out;
    }
    if (! haveAudio)
    {
        out.ok = false;
        out.reply = "I haven't heard enough audio yet. Press play in your DAW and hit LISTEN (or just play - I'll listen for about 20 seconds), then ask again.";
        return out;
    }
    if (parsed.has ("listen") && parsed.intents.size() == 1)
    {
        out.reply = analysisSummary (*ctx.audio, false);
        out.replyEngineer = analysisSummary (*ctx.audio, true);
        return out;
    }

    status (AgentPhase::Thinking, "planning");
    TreatmentSession session (ctx.audio->input, ctx.audio->sampleRate, ctx.audio->inputFeatures, ctx.mode, ctx.current, ctx.order, ctx.transport);
    session.bias = ctx.bias;
    session.cancelFlag = ctx.cancel;
    const Metrics startMetrics = session.getCandidateMetrics();
    std::vector<std::string> simple, engineer;
    const auto& sem = session.semantic();

    // ---- revert parts of the previous action first ("bring back the highs")
    bool revertedHighs = false;
    if (const auto* rv = parsed.find ("revert"))
    {
        auto last = ctx.memory != nullptr ? ctx.memory->last() : std::nullopt;
        if (last)
        {
            int n = 0;
            session.candidate = revertChanges (session.candidate, *last, rv->revertTarget, n);
            if (n > 0)
            {
                revertedHighs = rv->revertTarget == "highs";
                const std::string what = rv->revertTarget == "last" ? "my last change" : "what I did to the " + rv->revertTarget;
                simple.push_back ("I put back " + what + " (from \"" + last->request + "\").");
                engineer.push_back ("Reverted " + std::to_string (n) + " parameter(s) of action #" + std::to_string (last->id) + " (" + rv->revertTarget + ").");
                out.treatments.push_back ("revert_" + rv->revertTarget);
            }
        }
        else simple.push_back ("There was nothing earlier to put back.");
    }

    // ---- build the plan
    std::vector<TreatmentRequest> plan;
    auto addReq = [&] (const std::string& id, int dir, float amount, const Intent* src = nullptr)
    {
        for (auto& p : plan) if (p.id == id) return;
        TreatmentRequest r;
        r.id = id; r.direction = dir; r.amount = amount;
        if (src != nullptr)
        {
            r.preserveDynamics = src->preserveDynamics;
            r.avoidDullness = src->avoidDullness;
            r.targetLufs = src->targetLufs;
            r.division = src->division;
            r.pattern = src->pattern;
        }
        plan.push_back (r);
    };
    for (auto& in : parsed.intents)
    {
        if (in.id == "revert" || in.id == "undo" || in.id == "listen" || in.id == "reference_match") continue;
        if (in.id == "harshness" && revertedHighs) continue;   // the complaint was about the change we just reverted
        if (isKnownTreatment (in.id)) addReq (in.id, in.direction, in.amount, &in);
    }
    if (parsed.generalMix || (plan.empty() && ! parsed.has ("reference_match") && ! parsed.has ("revert")))
    {
        // Evidence-driven: only treat problems the analysis actually supports.
        const float a = parsed.intensity;
        if (ctx.mode == analysis::WorkMode::Master || parsed.masterRequest)
        {
            if (sem.find ("rumble") || sem.find ("dc_offset")) addReq ("rumble", 1, 0.5f);
            if (sem.find ("low_mid_congestion") || sem.find ("boxiness")) addReq ("mud", 1, 0.35f);
            if (sem.find ("harshness")) addReq ("harshness", 1, 0.35f);
            if (sem.find ("narrow_image")) addReq ("width", 1, 0.3f);
            Intent li; li.targetLufs = parsed.has ("loudness") ? parsed.find ("loudness")->targetLufs : -10.f;
            li.preserveDynamics = parsed.intents.empty() ? true : parsed.intents.front().preserveDynamics;
            addReq ("loudness", 1, a, &li);
        }
        else
        {
            for (auto& p : sem.problems)
            {
                if (p.confidence < 0.45f) continue;
                if (p.id == "rumble" || p.id == "dc_offset") addReq ("rumble", 1, 0.5f);
                else if (p.id == "plosives") addReq ("plosives", 1, 0.5f);
                else if (p.id == "low_mid_congestion" || p.id == "boxiness") addReq ("mud", 1, 0.4f);
                else if (p.id == "nasality" || p.id == "resonance") addReq ("resonance", 1, 0.4f);
                else if (p.id == "inconsistent_level" || p.id == "word_level_spikes") addReq ("level_consistency", 1, 0.5f);
                else if (p.id == "harshness") addReq ("harshness", 1, 0.5f);
                else if (p.id == "sibilance") addReq ("sibilance", 1, 0.5f);
                else if (p.id == "lack_of_presence") addReq ("presence", 1, 0.4f);
                else if (p.id == "lack_of_air") addReq ("air", 1, 0.4f);
                else if (p.id == "thinness") addReq ("body", 1, 0.4f);
            }
            if (ctx.mode == analysis::WorkMode::Vocal && parsed.generalMix)
            {
                // a finished vocal is controlled; add style-appropriate polish
                addReq ("level_consistency", 1, parsed.styleHint == "rap" ? 0.7f : 0.5f);
                if (parsed.styleHint == "modern_pop" || parsed.styleHint.empty())
                {
                    if (! sem.find ("excessive_brightness")) addReq ("air", 1, 0.3f);
                    addReq ("space", 1, 0.25f);
                }
                else if (parsed.styleHint == "rnb") addReq ("space", 1, 0.35f);
                else if (parsed.styleHint == "rap") addReq ("presence", 1, 0.3f);
            }
        }
    }

    // canonical engineering order: clean up -> control -> tone -> protect -> colour -> space -> loudness
    static const std::vector<std::string> order {
        "rumble", "plosives", "mud", "resonance", "level_consistency", "punch", "body", "warmth", "clarity", "presence", "brightness", "air",
        "harshness", "sibilance", "saturation", "width", "space", "delay", "rhythmic_gate", "loudness" };
    std::stable_sort (plan.begin(), plan.end(), [] (const TreatmentRequest& a, const TreatmentRequest& b)
    {
        auto ia = std::find (order.begin(), order.end(), a.id), ib = std::find (order.begin(), order.end(), b.id);
        return ia < ib;
    });

    juce::Array<juce::var> trace;
    for (auto& req : plan)
    {
        if (ctx.cancel != nullptr && ctx.cancel->load()) break;
        status (AgentPhase::Processing, req.id);
        auto r = session.run (req);
        auto* t = new juce::DynamicObject();
        t->setProperty ("treatment", juce::String (req.id));
        t->setProperty ("applied", r.applied);
        t->setProperty ("iterations", r.iterations);
        t->setProperty ("skip", juce::String (r.skipReason));
        juce::Array<juce::var> lg; for (auto& l : r.log) lg.add (juce::String (l));
        t->setProperty ("log", lg);
        trace.add (juce::var (t));
        if (r.applied)
        {
            session.accept (r);
            simple.push_back (r.simple);
            engineer.push_back (r.engineer);
            out.treatments.push_back (req.id);
            for (auto& w : r.warnings) out.warnings.push_back (w);
            out.steps.push_back (r);
        }
        else if (! r.simple.empty())
            simple.push_back (r.simple);
    }

    // ---- reference matching
    if (const auto* rm = parsed.find ("reference_match"))
    {
        if (ctx.reference == nullptr)
            simple.push_back ("Drop a reference file into the Reference Match panel first, then ask me again.");
        else
        {
            status (AgentPhase::Matching, "reference");
            auto dims = rm->referenceDims.empty() ? ctx.referenceDims : reference::MatchDimensions::fromNames (rm->referenceDims);
            const float infl = rm->referenceInfluence >= 0 ? rm->referenceInfluence : ctx.referenceInfluence;
            auto match = std::make_shared<reference::MatchResult> (reference::matchReference (session, *ctx.reference, dims, infl));
            out.match = match;
            simple.push_back (match->simple);
            engineer.push_back (match->engineer);
            for (auto& w : match->warnings) out.warnings.push_back (w);
            out.treatments.push_back ("reference_match");
        }
    }

    // ---- final verification: loudness held (unless loudness was the request) and true peak safe
    status (AgentPhase::Processing, "verifying");
    ChainSettings final = session.candidate;
    const bool loudnessRequested = std::find (out.treatments.begin(), out.treatments.end(), "loudness") != out.treatments.end();
    Metrics fm = session.evaluate (final);
    if (! loudnessRequested && std::abs (fm.integratedLufs - startMetrics.integratedLufs) > 1.0f && fm.integratedLufs > -69.f)
    {
        final[P::OutGain] = clampToSpec (kParams[(size_t) P::OutGain], final[P::OutGain] + (startMetrics.integratedLufs - fm.integratedLufs));
        fm = session.evaluate (final);
    }
    if (! final.on (P::LimOn) && fm.truePeakDb > -0.3f)
    {
        final[P::OutGain] = clampToSpec (kParams[(size_t) P::OutGain], final[P::OutGain] - (fm.truePeakDb + 0.5f));
        fm = session.evaluate (final);
        out.warnings.push_back ("output gain lowered to keep true peak below -0.3 dBTP");
    }

    out.settings = final;
    out.order = session.chainOrder();
    out.orderChanged = out.order != ctx.order;
    out.changed = ! diffSettings (ctx.current, final).empty() || out.orderChanged;
    out.before = startMetrics;
    out.after = fm;
    out.hasMetrics = true;
    out.renders = session.getRenderCount();
    out.trace = trace;

    if (simple.empty())
    {
        out.reply = parsed.intents.empty() && ! parsed.generalMix
                        ? "I'm not sure what you'd like me to change. Try something like \"make it warmer\", \"the S sounds hurt\", or \"mix this vocal\". "
                              + analysisSummary (*ctx.audio, false)
                        : "I checked, and the audio doesn't show a problem I should fix for that request. " + analysisSummary (*ctx.audio, false);
    }
    else
    {
        for (auto& s : simple) out.reply += (out.reply.empty() ? "" : " ") + s;
        for (auto& s : engineer) out.replyEngineer += (out.replyEngineer.empty() ? "" : " ") + s;
        if (out.changed) out.replyEngineer += " " + verificationLine (startMetrics, fm);
    }
    out.elapsedMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    status (AgentPhase::Complete, "");
    return out;
}

} // namespace nova::ai
