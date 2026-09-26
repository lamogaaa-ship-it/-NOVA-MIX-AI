#include "CloudEngineer.h"
#include "IntentParser.h"
#include "../hosting/PluginCatalog.h"

#include <chrono>

namespace nova::ai
{

//==============================================================================
juce::Array<juce::var> ConversationHistory::snapshot() const { std::lock_guard<std::mutex> g (lock); return messages; }
void ConversationHistory::commit (const juce::Array<juce::var>& turn) { std::lock_guard<std::mutex> g (lock); messages.addArray (turn); }
void ConversationHistory::clear() { std::lock_guard<std::mutex> g (lock); messages.clear(); }
int ConversationHistory::size() const { std::lock_guard<std::mutex> g (lock); return messages.size(); }
bool ConversationHistory::needsReset() const
{
    std::lock_guard<std::mutex> g (lock);
    if (messages.size() > 80) return true;
    return juce::JSON::toString (juce::var (messages), true).length() > 350000;
}

//==============================================================================
juce::String CloudEngineer::systemPrompt()
{
    return R"PROMPT(You are NOVA ENGINEER, the mixing and mastering engineer inside the NOVA MIX AI audio plugin. You work like an elite mix engineer, mastering engineer and vocal producer who can also explain things simply.

How you perceive audio
- You do not hear audio directly. NOVA's analysis engine measures the captured audio (loudness, spectrum, events such as harsh moments and sibilants with their times and frequencies, voice/pitch, stereo, space, structure) and gives you those measurements plus a semantic summary with confidence values.
- Only claim what the measurements support. If evidence is weak or missing, say so and act conservatively. Never invent frequencies, times or problems.

How you work
- Translate the user's words into engineering hypotheses, then check them against the evidence before acting. "Muffled" could be low-mid build-up, missing presence, missing air, over-de-essing, over-compression or reverb masking; "the volume keeps going up and down" could be phrase level, syllable spikes or section balance.
- Prefer run_treatment: NOVA's closed-loop routines choose settings from the measured evidence, render the real DSP chain, measure the targeted problem on the same moments loudness-matched, and refine within protection limits. Use set_parameters for fine-tuning or for moves no routine covers.
- Close the loop: after changes, call render_and_measure and confirm the problem improved without collateral damage (presence, air, crest factor, loudness). If you overcorrected, back off. When unsure between two approaches, try one, measure, and compare.
- Fix only what the evidence and the request justify; a light touch is better than an over-processed result. Treat problems where they occur (dynamic tools for momentary problems) instead of globally.
- Order matters: clean-up, level control, then tone, then protection (harshness/sibilance after boosts), colour, space, loudness last.
- Output loudness is held constant by NOVA unless the user asks about loudness, so never use gain to make a result seem better. Monitoring controls (bypass, A/B, delta) belong to the user.
- For tempo-based effects use the DAW tempo in the context block. For reference requests use compare_to_reference / match_reference and report honestly what could not be matched.
- Respect the user's learned preferences (get_user_preferences) as a gentle bias, never over evidence.
- A question is not a request to change anything: answer it from the analysis.
- Corrections like "no, now it's too harsh" refer to your previous action (see the session history in the context block); revert_previous_action can put back part of it.

Your final answer (after the tools)
- Reply in the user's language and dialect (for example Egyptian Arabic if they write Egyptian Arabic).
- Use exactly this shape:
<simple>1-3 short sentences a beginner understands: what you changed and why, with at most one or two key numbers.</simple>
<engineer>Concise technical account with the measured before/after numbers and the settings that matter.</engineer>
- Do not describe changes you did not make or results you did not measure.)PROMPT";
}

juce::var CloudEngineer::contextBlock (const EngineerContext& ctx, const std::string& request)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("mode", juce::String (analysis::workModeName (ctx.mode)));
    auto* daw = new juce::DynamicObject();
    daw->setProperty ("bpm", std::round (ctx.transport.bpm * 100.0) / 100.0);
    daw->setProperty ("host_reports_tempo", ctx.transport.hostProvidesTempo);
    daw->setProperty ("time_signature", juce::String (ctx.transport.timeSigNum) + "/" + juce::String (ctx.transport.timeSigDen));
    daw->setProperty ("playing", ctx.transport.isPlaying);
    o->setProperty ("daw", juce::var (daw));
    if (ctx.audio != nullptr && ctx.audio->inputFeatures.valid)
    {
        auto* a = new juce::DynamicObject();
        a->setProperty ("captured_seconds", std::round (ctx.audio->inputFeatures.durationSec * 10.0) / 10.0);
        a->setProperty ("semantic", analysis::semanticToJson (ctx.audio->semantic));
        const auto fj = analysis::featuresToJson (ctx.audio->inputFeatures, false);
        a->setProperty ("level", fj["level"]);
        a->setProperty ("events", fj["events"]);
        a->setProperty ("voice", fj["voice"]);
        o->setProperty ("input_analysis", juce::var (a));
    }
    else
        o->setProperty ("input_analysis", "none - NOVA has not captured enough audio yet");
    o->setProperty ("current_chain", chainStateJson (ctx.current, ctx.order, true));
    if (ctx.reference != nullptr)
    {
        auto* r = new juce::DynamicObject();
        r->setProperty ("name", juce::String (ctx.reference->name));
        r->setProperty ("is_full_mix", ctx.reference->isFullMix);
        r->setProperty ("selected_dimensions", [&] { juce::StringArray d; for (auto& n : ctx.referenceDims.names()) d.add (n); return d.joinIntoString (","); }());
        r->setProperty ("influence", ctx.referenceInfluence);
        if (ctx.audio != nullptr && ctx.audio->inputFeatures.valid)
            r->setProperty ("comparison_to_input", reference::compareToReference (ctx.audio->inputFeatures, *ctx.reference)["statements"]);
        o->setProperty ("reference", juce::var (r));
    }
    if (ctx.memory != nullptr)
    {
        const auto hist = ctx.memory->summaryForAI (6);
        if (hist.isNotEmpty()) o->setProperty ("session_history_newest_first", hist);
    }
    if (ctx.bias.evidenceCount > 0) o->setProperty ("user_taste", ctx.userPreferenceSummary);
    const auto parsed = parseRequest (request);
    o->setProperty ("user_language", juce::String (parsed.language));
    return juce::var (o);
}

void CloudEngineer::parseFinalText (const juce::String& text, std::string& simple, std::string& engineer)
{
    auto between = [&] (const juce::String& tag) -> juce::String
    {
        const auto open = "<" + tag + ">", close = "</" + tag + ">";
        const int a = text.indexOf (open);
        if (a < 0) return {};
        const int b = text.indexOf (a + open.length(), close);
        return (b < 0 ? text.substring (a + open.length()) : text.substring (a + open.length(), b)).trim();
    };
    const auto s = between ("simple"), e = between ("engineer");
    simple = (s.isNotEmpty() ? s : text.trim()).toStdString();
    engineer = e.toStdString();
}

//==============================================================================
CloudEngineer::CloudEngineer (std::shared_ptr<LLMClient> c, EngineerSettings s, hosting::PluginCatalog* p)
    : client (std::move (c)), settings (std::move (s)), plugins (p) {}

EngineerOutcome CloudEngineer::handle (const std::string& request, EngineerContext& ctx, ConversationHistory& history)
{
    const auto t0 = std::chrono::steady_clock::now();
    EngineerOutcome out;
    out.requestText = request;
    out.settings = ctx.current;
    out.order = ctx.order;
    out.engine = settings.model.toStdString();
    auto status = [&] (AgentPhase p, const std::string& d) { if (ctx.onStatus) ctx.onStatus (p, d); };

    const bool haveAudio = ctx.audio != nullptr && ctx.audio->input != nullptr && ctx.audio->inputFeatures.valid;
    // Without audio the tools that render cannot run; still allow questions / planning chat.
    std::shared_ptr<const juce::AudioBuffer<float>> audio = haveAudio ? ctx.audio->input : std::make_shared<const juce::AudioBuffer<float>> (2, 48000);
    const analysis::AudioFeatures feats = haveAudio ? ctx.audio->inputFeatures : analysis::AudioFeatures {};
    TreatmentSession session (audio, haveAudio ? ctx.audio->sampleRate : 48000.0, feats, ctx.mode, ctx.current, ctx.order, ctx.transport);
    session.bias = ctx.bias;
    session.cancelFlag = ctx.cancel;
    AgentToolbox tools (ctx, session, KnowledgeBase::shared(), plugins);

    if (history.needsReset()) history.clear();
    juce::Array<juce::var> messages = history.snapshot();
    juce::Array<juce::var> turn;

    // user turn: dynamic context block (not in the cached system prompt) + the request
    {
        auto* m = new juce::DynamicObject();
        m->setProperty ("role", "user");
        juce::Array<juce::var> content;
        auto* c1 = new juce::DynamicObject();
        c1->setProperty ("type", "text");
        c1->setProperty ("text", "<nova_context>\n" + juce::JSON::toString (contextBlock (ctx, request), true) + "\n</nova_context>");
        content.add (juce::var (c1));
        auto* c2 = new juce::DynamicObject();
        c2->setProperty ("type", "text");
        c2->setProperty ("text", juce::String::fromUTF8 (request.c_str()));
        content.add (juce::var (c2));
        m->setProperty ("content", content);
        turn.add (juce::var (m));
    }

    static const juce::String sys = systemPrompt();
    const auto toolDefs = AgentToolbox::toolDefinitions (plugins != nullptr && plugins->size() > 0);

    auto buildBody = [&] (bool withFallbacks)
    {
        auto* b = new juce::DynamicObject();
        b->setProperty ("model", settings.model);
        b->setProperty ("max_tokens", 16000);
        juce::Array<juce::var> system;
        auto* sb = new juce::DynamicObject();
        sb->setProperty ("type", "text");
        sb->setProperty ("text", sys);
        auto* cc = new juce::DynamicObject();
        cc->setProperty ("type", "ephemeral");
        sb->setProperty ("cache_control", juce::var (cc));
        system.add (juce::var (sb));
        b->setProperty ("system", system);
        b->setProperty ("tools", toolDefs);
        juce::Array<juce::var> all = messages;
        all.addArray (turn);
        b->setProperty ("messages", all);
        auto* oc = new juce::DynamicObject();
        oc->setProperty ("effort", settings.effort);
        b->setProperty ("output_config", juce::var (oc));
        if (withFallbacks) b->setProperty ("fallbacks", "default");
        return juce::var (b);
    };

    bool useFallbacks = settings.useServerFallbacks;
    juce::String finalText;
    bool finished = false;
    status (AgentPhase::Thinking, "planning");
    for (int round = 0; round < maxToolRounds && ! finished; ++round)
    {
        if (ctx.cancel != nullptr && ctx.cancel->load()) { out.ok = false; out.reply = "Cancelled."; return out; }
        LLMResult res = client->send (buildBody (useFallbacks), useFallbacks);
        if (! res.ok && useFallbacks && res.httpStatus == 400 && res.error.containsIgnoreCase ("fallback"))
        {
            useFallbacks = false;                                  // provider/proxy without fallback support
            res = client->send (buildBody (false), false);
        }
        if (! res.ok && res.retryable)
        {
            juce::Thread::sleep (1500);
            res = client->send (buildBody (useFallbacks), useFallbacks);
        }
        if (! res.ok)
        {
            out.ok = false;
            out.reply = "__cloud_error__";
            out.replyEngineer = res.error.toStdString();
            return out;   // controller falls back to the offline engineer
        }

        const auto& msg = res.message;
        const auto stop = msg.getProperty ("stop_reason", "").toString();
        const auto content = msg.getProperty ("content", {});
        if (msg.hasProperty ("model")) out.engine = msg["model"].toString().toStdString();

        // append the assistant message verbatim (thinking blocks included)
        auto* am = new juce::DynamicObject();
        am->setProperty ("role", "assistant");
        am->setProperty ("content", content);
        turn.add (juce::var (am));

        juce::String textThisRound;
        std::vector<juce::var> toolUses;
        if (auto* blocks = content.getArray())
            for (auto& b : *blocks)
            {
                const auto type = b.getProperty ("type", "").toString();
                if (type == "text") textThisRound << b.getProperty ("text", "").toString();
                else if (type == "tool_use") toolUses.push_back (b);
            }

        if (stop == "refusal")
        {
            out.ok = false;
            out.reply = "__cloud_error__";
            out.replyEngineer = "the model declined this request";
            return out;
        }
        if (stop == "pause_turn")
            continue;                                              // resend as-is; server resumes
        if (stop == "max_tokens" && ! toolUses.empty())
        {
            // truncated tool input: do not run it; ask for a shorter step
            auto* um = new juce::DynamicObject();
            um->setProperty ("role", "user");
            juce::Array<juce::var> c;
            for (auto& tu : toolUses)
            {
                auto* tr = new juce::DynamicObject();
                tr->setProperty ("type", "tool_result");
                tr->setProperty ("tool_use_id", tu["id"]);
                tr->setProperty ("is_error", true);
                tr->setProperty ("content", "Your output was cut off before this tool input was complete; it was not executed. Use smaller steps.");
                c.add (juce::var (tr));
            }
            um->setProperty ("content", c);
            turn.add (juce::var (um));
            continue;
        }
        if (toolUses.empty())
        {
            finalText = textThisRound;
            finished = true;
            break;
        }

        // execute every tool call, return all results in ONE user message
        auto* um = new juce::DynamicObject();
        um->setProperty ("role", "user");
        juce::Array<juce::var> results;
        for (auto& tu : toolUses)
        {
            bool isErr = false;
            juce::var r;
            if (! haveAudio && tu["name"].toString() != "retrieve_knowledge" && tu["name"].toString() != "get_user_preferences"
                && tu["name"].toString() != "search_available_plugins" && tu["name"].toString() != "inspect_plugin_parameters")
            {
                isErr = true;
                auto* e = new juce::DynamicObject();
                e->setProperty ("error", "no captured audio yet - the user must play the track so NOVA can listen");
                r = juce::var (e);
            }
            else
                r = tools.execute (tu["name"].toString(), tu["input"], isErr);
            auto* tr = new juce::DynamicObject();
            tr->setProperty ("type", "tool_result");
            tr->setProperty ("tool_use_id", tu["id"]);
            tr->setProperty ("content", juce::JSON::toString (r, true));
            if (isErr) tr->setProperty ("is_error", true);
            results.add (juce::var (tr));
        }
        um->setProperty ("content", results);
        turn.add (juce::var (um));
        status (AgentPhase::Thinking, "reviewing results");
    }

    if (! finished)
    {
        // tool budget exhausted without a final answer: keep verified work, summarise honestly
        finalText = "<simple>I made the changes listed below but ran out of steps before writing a full explanation.</simple>";
    }
    history.commit (turn);

    // ---- harness verification (same guarantees as the offline engineer)
    ChainSettings final = session.candidate;
    if (haveAudio && ! diffSettings (ctx.current, final).empty())
    {
        status (AgentPhase::Processing, "verifying");
        const Metrics startM = session.evaluate (ctx.current);
        Metrics fm = session.evaluate (final);
        const bool loudnessRequested = std::find (tools.treatments.begin(), tools.treatments.end(), "loudness") != tools.treatments.end()
                                       || (tools.lastMatch != nullptr && std::any_of (tools.lastMatch->reports.begin(), tools.lastMatch->reports.end(),
                                                                                      [] (auto& r) { return r.dimension == "loudness"; }));
        if (! loudnessRequested && std::abs (fm.integratedLufs - startM.integratedLufs) > 1.0f && fm.integratedLufs > -69.f)
        {
            final[P::OutGain] = clampToSpec (kParams[(size_t) P::OutGain], final[P::OutGain] + (startM.integratedLufs - fm.integratedLufs));
            fm = session.evaluate (final);
            out.warnings.push_back ("output level re-matched to the original loudness");
        }
        if (! final.on (P::LimOn) && fm.truePeakDb > -0.3f)
        {
            final[P::OutGain] = clampToSpec (kParams[(size_t) P::OutGain], final[P::OutGain] - (fm.truePeakDb + 0.5f));
            fm = session.evaluate (final);
            out.warnings.push_back ("output gain lowered to keep true peak below -0.3 dBTP");
        }
        out.before = startM;
        out.after = fm;
        out.hasMetrics = true;
    }

    std::string simple, engineer;
    parseFinalText (finalText, simple, engineer);
    out.reply = simple;
    out.replyEngineer = engineer;
    if (out.hasMetrics) out.replyEngineer += (out.replyEngineer.empty() ? "" : " ") + OfflineEngineer::verificationLine (out.before, out.after);
    out.settings = final;
    out.order = session.chainOrder();
    out.orderChanged = out.order != ctx.order;
    out.changed = ! diffSettings (ctx.current, final).empty() || out.orderChanged;
    out.treatments = tools.treatments;
    for (auto& w : tools.warnings) out.warnings.push_back (w);
    out.match = tools.lastMatch;
    out.trace = tools.trace;
    out.renders = session.getRenderCount();
    out.elapsedMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    status (AgentPhase::Complete, "");
    return out;
}

} // namespace nova::ai
