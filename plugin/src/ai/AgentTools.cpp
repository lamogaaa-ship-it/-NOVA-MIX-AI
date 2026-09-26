#include "AgentTools.h"
#include "../analysis/Analyzer.h"
#include "../hosting/PluginCatalog.h"

namespace nova::ai
{

namespace
{
juce::var obj() { return juce::var (new juce::DynamicObject()); }
void put (juce::var& o, const juce::Identifier& k, const juce::var& v) { o.getDynamicObject()->setProperty (k, v); }
double r1 (double v) { return std::round (v * 10.0) / 10.0; }

const char* kToolSchemas = R"JSON([
{
 "name": "analyze_audio",
 "description": "Get NOVA's measurements of the audio. Call this before deciding what a problem is, when you need evidence you do not already have in the context block, or to inspect a time region (e.g. a harsh phrase). source='input' is the unprocessed captured audio, 'candidate' is the audio rendered through the settings you are building this turn, 'reference' is the loaded reference track. Measurements are real; do not claim anything they do not support.",
 "input_schema": {"type": "object", "properties": {
   "source": {"type": "string", "enum": ["input", "candidate", "reference"]},
   "aspects": {"type": "array", "items": {"type": "string", "enum": ["semantic", "level", "spectrum", "voice", "events", "stereo", "space", "structure"]}, "description": "Subset to return; omit for semantic + level + events"},
   "start_s": {"type": "number", "description": "Optional region start in seconds (input/candidate only)"},
   "end_s": {"type": "number"}
 }, "required": ["source"]}
},
{
 "name": "get_chain_state",
 "description": "Return NOVA's processing chain state (module on/off, order and parameter values with units and allowed ranges) for the candidate settings. Use module to narrow it down. Call before manual parameter edits so you know current values and valid ids.",
 "input_schema": {"type": "object", "properties": {
   "module": {"type": "string", "enum": ["all", "global", "level", "eq", "tone_match", "dynamic_eq", "compressor", "deesser", "color", "space", "motion", "image", "limiter"]},
   "only_changed": {"type": "boolean", "description": "Only parameters that differ from defaults (default true)"}
 }}
},
{
 "name": "run_treatment",
 "description": "Run one of NOVA's closed-loop engineering routines on the candidate. Each routine sets processing from the measured evidence (never presets), renders the result through the real DSP chain, measures the targeted problem on the same time regions loudness-matched, and refines until it meets its target without breaking its protection constraint. Returns before/after metrics, iteration log, parameter changes and an explanation. Prefer this for standard problems; it is safer and better verified than hand-setting parameters. Treatments: level_consistency, harshness, sibilance, clarity (diagnoses muffled/unclear), presence, air, brightness, warmth, mud, resonance, rumble, plosives, space, width, loudness, saturation, rhythmic_gate, delay, punch, body.",
 "input_schema": {"type": "object", "properties": {
   "treatment": {"type": "string", "enum": ["level_consistency", "harshness", "sibilance", "clarity", "presence", "air", "brightness", "warmth", "mud", "resonance", "rumble", "plosives", "space", "width", "loudness", "saturation", "rhythmic_gate", "delay", "punch", "body"]},
   "amount": {"type": "number", "description": "0.15 subtle ... 0.5 moderate ... 1.0 strong"},
   "direction": {"type": "integer", "enum": [-1, 1], "description": "For presence/air/brightness/warmth/space/width/body: +1 more, -1 less (space -1 = closer/drier)"},
   "preserve_dynamics": {"type": "boolean"},
   "avoid_dullness": {"type": "boolean", "description": "Protect clarity/presence (user said 'without making it dull')"},
   "target_lufs": {"type": "number", "description": "loudness treatment only"},
   "division": {"type": "integer", "description": "rhythmic_gate: 0=1/4 1=1/8 2=1/8T 3=1/16 4=1/16T 5=1/32; delay: index into 1/2,1/4,1/4D,1/4T,1/8,1/8D,1/8T,1/16,1/16T"},
   "pattern": {"type": "integer", "description": "rhythmic_gate: 0 straight 1 offbeat 2 stutter 3-3-2 3 broken 4 half-time; saturation: 0 tape 1 tube 2 soft clip"},
   "reason": {"type": "string", "description": "Short reason tied to the evidence"}
 }, "required": ["treatment", "reason"]}
},
{
 "name": "set_parameters",
 "description": "Set specific NOVA parameters on the candidate (fine-tuning after a treatment, or moves no routine covers). Values are validated: unknown ids are rejected, values are clamped to their range, and changes larger than the per-action safety step are limited. Monitoring controls (bypass, A/B, delta) belong to the user and are rejected. Verify afterwards with render_and_measure.",
 "input_schema": {"type": "object", "properties": {
   "changes": {"type": "array", "items": {"type": "object", "properties": {
      "id": {"type": "string", "description": "Parameter id from get_chain_state, e.g. comp_thresh, eq_b4_gain, dess_freq"},
      "value": {"description": "Number, boolean, or choice name for choice parameters"}
   }, "required": ["id", "value"]}},
   "reason": {"type": "string"}
 }, "required": ["changes", "reason"]}
},
{
 "name": "set_module_enabled",
 "description": "Turn a NOVA module on or off on the candidate.",
 "input_schema": {"type": "object", "properties": {
   "module": {"type": "string", "enum": ["level", "eq", "tone_match", "dynamic_eq", "compressor", "deesser", "color", "space", "motion", "limiter"]},
   "enabled": {"type": "boolean"}
 }, "required": ["module", "enabled"]}
},
{
 "name": "reorder_chain",
 "description": "Change the order of NOVA's insert modules. Provide all eight slots: level, eq, tone_match, dynamic_eq, compressor, deesser, color, motion.",
 "input_schema": {"type": "object", "properties": {
   "order": {"type": "array", "items": {"type": "string", "enum": ["level", "eq", "tone_match", "dynamic_eq", "compressor", "deesser", "color", "motion"]}}
 }, "required": ["order"]}
},
{
 "name": "render_and_measure",
 "description": "Render the candidate settings through the real DSP chain and measure the result against the state at the start of this turn (and against the raw input): loudness-matched band levels on the harsh moments, sibilants, presence and air elsewhere, phrase spread, crest, loudness, true peak, gain reduction. Call this after changes to verify you actually fixed the problem without collateral damage, and before your final answer.",
 "input_schema": {"type": "object", "properties": {
   "include_space": {"type": "boolean", "description": "Also measure reverb tail and stereo (slower)"}
 }}
},
{
 "name": "compare_to_reference",
 "description": "Compare the candidate audio with the loaded reference in perceptual, loudness-independent terms (tone balance per region, brightness, dynamics, space, width, loudness, sibilance). Call when the user mentions the reference.",
 "input_schema": {"type": "object", "properties": {}}
},
{
 "name": "match_reference",
 "description": "Iteratively move the candidate toward the loaded reference on the chosen dimensions: tone (8-band tone-match curve fitted then refined against renders), dynamics (crest), space (reverb tail), width, color, loudness, vocal (sibilance character). influence 0-1 scales how far to move; the user keeps a live Reference Influence control for tone. Returns per-dimension before/after distances and honest limitations.",
 "input_schema": {"type": "object", "properties": {
   "dimensions": {"type": "array", "items": {"type": "string", "enum": ["tone", "dynamics", "space", "width", "color", "loudness", "vocal", "full"]}},
   "influence": {"type": "number"}
 }, "required": ["dimensions"]}
},
{
 "name": "revert_previous_action",
 "description": "Put back part or all of NOVA's previous action from the session history, e.g. when the user says 'bring back the highs' / 'undo the compression'. target selects which parameters of the last action are restored.",
 "input_schema": {"type": "object", "properties": {
   "target": {"type": "string", "enum": ["last", "highs", "lows", "reverb", "compression", "width"]}
 }, "required": ["target"]}
},
{
 "name": "retrieve_knowledge",
 "description": "Search NOVA's engineering knowledge base (mixing, mastering, psychoacoustics, reference matching, effects). Knowledge never overrides audio evidence.",
 "input_schema": {"type": "object", "properties": {"query": {"type": "string"}}, "required": ["query"]}
},
{
 "name": "get_user_preferences",
 "description": "Return this user's learned taste for the current context (bounded biases learned from their corrections, feedback and A/B choices) and similar past situations with what worked. Use to personalise amounts, never to override evidence.",
 "input_schema": {"type": "object", "properties": {}}
}
])JSON";

const char* kPluginToolSchemas = R"JSON([
{
 "name": "search_available_plugins",
 "description": "Search third-party plugins installed on this computer that NOVA has scanned (name, manufacturer, format, capabilities). Only legitimately installed plugins are listed.",
 "input_schema": {"type": "object", "properties": {
   "capability": {"type": "string", "enum": ["", "eq", "dynamic_eq", "compressor", "deesser", "limiter", "reverb", "delay", "saturation", "gate", "stereo", "modulation", "pitch"]},
   "query": {"type": "string"}
 }}
},
{
 "name": "inspect_plugin_parameters",
 "description": "Return the real parameter list of a scanned plugin (index, name, label, default, mapped engineering concept with confidence). Inspect before suggesting any settings for a third-party plugin; never guess parameter ids.",
 "input_schema": {"type": "object", "properties": {"plugin_id": {"type": "string"}}, "required": ["plugin_id"]}
}
])JSON";

const char* kRackToolSchemas = R"JSON([
{
 "name": "get_plugin_rack",
 "description": "Return the third-party plugins loaded in NOVA's plugin rack (they run after NOVA's built-in chain): slot, name, capabilities, bypass, latency, and live parameter values as normalised 0-1 numbers plus the plugin's own display text. Use query to filter parameters by name. Read this before changing any rack plugin; never guess parameter indices.",
 "input_schema": {"type": "object", "properties": {
   "slot": {"type": "integer", "description": "Only this slot"},
   "query": {"type": "string", "description": "Only parameters whose name contains this text (case-insensitive)"}
 }}
},
{
 "name": "set_plugin_rack_parameters",
 "description": "Change parameters of a plugin in NOVA's rack. Values are normalised 0-1 (see the value and display text from get_plugin_rack). Continuous parameters move at most 0.25 per call; stepped parameters snap to their steps. Changes are applied to the live plugin together with the rest of your turn and are undoable. NOVA's offline renders do not include third-party plugins, so render_and_measure does not show these changes: tell the user and suggest listening again (LISTEN) to verify.",
 "input_schema": {"type": "object", "properties": {
   "slot": {"type": "integer"},
   "changes": {"type": "array", "items": {"type": "object", "properties": {
      "index": {"type": "integer", "description": "Parameter index from get_plugin_rack"},
      "value": {"type": "number", "description": "Normalised target 0-1"}
   }, "required": ["index", "value"]}},
   "reason": {"type": "string"}
 }, "required": ["slot", "changes", "reason"]}
},
{
 "name": "set_plugin_rack_bypass",
 "description": "Bypass or re-enable a plugin in NOVA's rack (latency-compensated crossfade, undoable).",
 "input_schema": {"type": "object", "properties": {
   "slot": {"type": "integer"},
   "bypassed": {"type": "boolean"}
 }, "required": ["slot", "bypassed"]}
}
])JSON";
} // namespace

//==============================================================================
juce::var AgentToolbox::toolDefinitions (bool includePluginTools, bool includeRackTools)
{
    static const juce::var base = juce::JSON::parse (kToolSchemas);
    static const juce::var plug = juce::JSON::parse (kPluginToolSchemas);
    static const juce::var rack = juce::JSON::parse (kRackToolSchemas);
    jassert (base.isArray() && plug.isArray() && rack.isArray());
    juce::Array<juce::var> all;
    for (auto& t : *base.getArray()) all.add (t);
    if (includePluginTools)
        for (auto& t : *plug.getArray()) all.add (t);
    if (includeRackTools)
        for (auto& t : *rack.getArray()) all.add (t);
    return all;
}

AgentToolbox::AgentToolbox (EngineerContext& c, TreatmentSession& s, const KnowledgeBase& k, hosting::PluginCatalog* p)
    : ctx (c), session (s), kb (k), plugins (p) {}

juce::var AgentToolbox::execute (const juce::String& name, const juce::var& input, bool& isError)
{
    ++toolCalls;
    isError = false;
    juce::var result;
    if (name == "analyze_audio") result = analyzeAudio (input, isError);
    else if (name == "get_chain_state") result = getChainState (input);
    else if (name == "run_treatment") result = runTreatment (input, isError);
    else if (name == "set_parameters") result = setParameters (input, isError);
    else if (name == "set_module_enabled") result = setModuleEnabled (input, isError);
    else if (name == "reorder_chain") result = reorderChain (input, isError);
    else if (name == "render_and_measure") result = renderAndMeasure (input);
    else if (name == "compare_to_reference") result = compareReference (isError);
    else if (name == "match_reference") result = matchReferenceTool (input, isError);
    else if (name == "revert_previous_action") result = revertPrevious (input, isError);
    else if (name == "retrieve_knowledge") result = retrieveKnowledge (input);
    else if (name == "get_user_preferences") result = userPreferences();
    else if (name == "search_available_plugins") result = searchPlugins (input, isError);
    else if (name == "inspect_plugin_parameters") result = inspectPlugin (input, isError);
    else if (name == "get_plugin_rack") result = getRack (input, isError);
    else if (name == "set_plugin_rack_parameters") result = setRackParameters (input, isError);
    else if (name == "set_plugin_rack_bypass") result = setRackBypass (input, isError);
    else
    {
        isError = true;
        result = obj();
        put (result, "error", "unknown tool: " + name);
    }
    auto t = obj();
    put (t, "tool", name);
    put (t, "input", input);
    put (t, "error", isError);
    trace.add (t);
    return result;
}

//==============================================================================
static juce::var paramJson (int i, float v)
{
    const auto& spec = kParams[(size_t) i];
    auto o = obj();
    put (o, "id", spec.id);
    if (spec.kind == ParamKind::Bool) put (o, "value", v > 0.5f);
    else if (spec.kind == ParamKind::Choice)
    {
        put (o, "value", juce::String (std::string (choiceName (spec, (int) (v + 0.5f)))));
        juce::StringArray ch; ch.addTokens (spec.choices, "|", "");
        put (o, "choices", ch.joinIntoString (" | "));
    }
    else
    {
        put (o, "value", r1 (v));
        put (o, "unit", unitSuffix (spec.unit));
        put (o, "range", juce::Array<juce::var> { spec.minValue, spec.maxValue });
    }
    return o;
}

juce::var chainStateJson (const ChainSettings& s, const ChainOrder& order, bool onlyNonDefault, const std::string& moduleFilter)
{
    auto root = obj();
    juce::Array<juce::var> ord;
    for (auto slot : order) ord.add (chainSlotName (slot));
    put (root, "insert_order", ord);
    put (root, "fixed_tail", "space (parallel returns) -> image -> limiter -> output gain -> mix");
    auto mods = obj();
    for (int m = 0; m < (int) Module::Count; ++m)
    {
        const auto mod = (Module) m;
        const std::string mname = moduleName (mod);
        if (! moduleFilter.empty() && moduleFilter != "all" && moduleFilter != mname) continue;
        juce::Array<juce::var> ps;
        for (int i = 0; i < P::Count; ++i)
        {
            if (kParams[(size_t) i].module != mod) continue;
            if (i == P::Bypass || i == P::MonitorA || i == P::Delta) continue;
            if (onlyNonDefault && std::abs (s[i] - kParams[(size_t) i].defaultValue) < 1e-4f) continue;
            ps.add (paramJson (i, s[i]));
        }
        if (! ps.isEmpty()) put (mods, juce::Identifier (mname), ps);
    }
    put (root, "modules", mods);
    if (onlyNonDefault) put (root, "note", "only parameters that differ from defaults are listed; defaults: all processors off, EQ enabled with bands off");
    return root;
}

juce::var treatmentResultJson (const TreatmentResult& r)
{
    auto o = obj();
    put (o, "treatment", juce::String (r.id));
    put (o, "applied", r.applied);
    if (! r.skipReason.empty()) put (o, "skipped_because", juce::String (r.skipReason));
    put (o, "iterations", r.iterations);
    put (o, "confidence", r1 (r.confidence));
    juce::Array<juce::var> log; for (auto& l : r.log) log.add (juce::String (l));
    if (! log.isEmpty()) put (o, "iteration_log", log);
    juce::Array<juce::var> ch; for (auto& c : r.changes) ch.add (juce::String (describeChange (c)));
    put (o, "changes", ch);
    if (r.applied) put (o, "measured_delta", metricsDeltaJson (r.before, r.after));
    put (o, "explanation_simple", juce::String (r.simple));
    put (o, "explanation_engineer", juce::String (r.engineer));
    juce::Array<juce::var> w; for (auto& x : r.warnings) w.add (juce::String (x));
    if (! w.isEmpty()) put (o, "warnings", w);
    return o;
}

//==============================================================================
juce::var AgentToolbox::analyzeAudio (const juce::var& in, bool& err)
{
    const auto source = in.getProperty ("source", "input").toString();
    juce::StringArray aspects;
    if (auto* a = in.getProperty ("aspects", {}).getArray()) for (auto& x : *a) aspects.add (x.toString());
    if (aspects.isEmpty()) aspects = { "semantic", "level", "events" };

    analysis::AudioFeatures f;
    analysis::SemanticProfile sem;
    if (source == "reference")
    {
        if (ctx.reference == nullptr) { err = true; auto e = obj(); put (e, "error", "no reference loaded - ask the user to drop one into the Reference Match panel"); return e; }
        f = ctx.reference->features;
        sem = ctx.reference->semantic;
    }
    else
    {
        std::shared_ptr<const juce::AudioBuffer<float>> audio = session.inputAudio();
        if (source == "candidate")
        {
            auto pr = renderPreview (*session.inputAudio(), session.sampleRate(), session.candidate, session.chainOrder(), session.transport());
            audio = pr.audio;
        }
        const double sr = session.sampleRate();
        int s0 = 0, s1 = audio->getNumSamples();
        if (in.hasProperty ("start_s")) s0 = std::clamp ((int) ((double) in["start_s"] * sr), 0, s1);
        if (in.hasProperty ("end_s")) s1 = std::clamp ((int) ((double) in["end_s"] * sr), s0, s1);
        if (s1 - s0 < (int) (0.5 * sr)) { err = true; auto e = obj(); put (e, "error", "region shorter than 0.5 s"); return e; }
        if (source == "input" && s0 == 0 && s1 == audio->getNumSamples())
            f = session.features();
        else
        {
            juce::AudioBuffer<float> region (2, s1 - s0);
            for (int c = 0; c < 2; ++c) region.copyFrom (c, 0, *audio, std::min (c, audio->getNumChannels() - 1), s0, s1 - s0);
            analysis::AnalysisOptions o;
            o.hint = ctx.mode == analysis::WorkMode::Vocal ? analysis::SourceType::LeadVocal : analysis::SourceType::FullMix;
            f = analysis::Analyzer::analyze (region.getArrayOfReadPointers(), 2, region.getNumSamples(), sr, o);
        }
        sem = analysis::describe (f, ctx.mode);
    }

    const auto full = analysis::featuresToJson (f, aspects.contains ("events") || aspects.contains ("structure"));
    auto out = obj();
    put (out, "source", source);
    put (out, "valid", f.valid);
    auto copy = [&] (const char* aspect, const char* key) { if (aspects.contains (aspect) && full.hasProperty (key)) put (out, key, full[key]); };
    copy ("level", "level");
    copy ("spectrum", "spectrum");
    copy ("voice", "voice");
    copy ("events", "events");
    copy ("stereo", "stereo");
    copy ("space", "space");
    copy ("structure", "structure");
    if (aspects.contains ("semantic")) put (out, "semantic", analysis::semanticToJson (sem));
    return out;
}

juce::var AgentToolbox::getChainState (const juce::var& in)
{
    const bool only = in.getProperty ("only_changed", true);
    return chainStateJson (session.candidate, session.chainOrder(), only, in.getProperty ("module", "all").toString().toStdString());
}

juce::var AgentToolbox::runTreatment (const juce::var& in, bool& err)
{
    TreatmentRequest req;
    req.id = in.getProperty ("treatment", "").toString().toStdString();
    if (! isKnownTreatment (req.id)) { err = true; auto e = obj(); put (e, "error", "unknown treatment " + juce::String (req.id)); return e; }
    req.amount = std::clamp ((float) (double) in.getProperty ("amount", 0.5), 0.05f, 1.f);
    req.direction = (int) in.getProperty ("direction", 1) < 0 ? -1 : 1;
    req.preserveDynamics = in.getProperty ("preserve_dynamics", false);
    req.avoidDullness = in.getProperty ("avoid_dullness", false);
    req.targetLufs = (float) (double) in.getProperty ("target_lufs", 0.0);
    req.division = in.getProperty ("division", -1);
    req.pattern = in.getProperty ("pattern", -1);
    req.note = in.getProperty ("reason", "").toString().toStdString();
    if (ctx.onStatus) ctx.onStatus (AgentPhase::Processing, req.id);
    auto r = session.run (req);
    if (r.applied)
    {
        session.accept (r);
        treatments.push_back (req.id);
        for (auto& w : r.warnings) warnings.push_back (w);
    }
    return treatmentResultJson (r);
}

AgentToolbox::Validation AgentToolbox::validateChange (const ChainSettings& current, const juce::String& id, const juce::var& value)
{
    Validation v;
    auto idx = findParamIndex (id.toStdString());
    if (! idx) { v.status = "rejected"; v.note = "unknown parameter id"; return v; }
    v.index = *idx;
    const auto& spec = kParams[(size_t) *idx];
    if (*idx == P::Bypass || *idx == P::MonitorA || *idx == P::Delta)
    {
        v.status = "rejected"; v.note = "monitoring controls belong to the user";
        return v;
    }
    float req;
    if (spec.kind == ParamKind::Choice && value.isString())
    {
        int found = -1;
        for (int c = 0; c < numChoices (spec); ++c)
            if (juce::String (std::string (choiceName (spec, c))).equalsIgnoreCase (value.toString())) found = c;
        if (found < 0) { v.status = "rejected"; v.note = "unknown choice; valid: " + juce::String (spec.choices); return v; }
        req = (float) found;
    }
    else if (value.isBool()) req = (bool) value ? 1.f : 0.f;
    else if (value.isInt() || value.isDouble() || value.isInt64()) req = (float) (double) value;
    else if (value.isString() && value.toString().containsOnly ("-0123456789.")) req = value.toString().getFloatValue();
    else { v.status = "rejected"; v.note = "value must be a number, boolean or choice name"; return v; }
    if (! std::isfinite (req)) { v.status = "rejected"; v.note = "not a finite number"; return v; }
    v.requested = req;
    float applied = clampToSpec (spec, req);
    v.status = std::abs (applied - req) > 1e-4f ? "clamped" : "ok";
    if (v.status == "clamped") v.note = "clamped to allowed range [" + juce::String (spec.minValue) + ", " + juce::String (spec.maxValue) + "]";
    if (spec.maxAiStep > 0.f && std::abs (applied - current[*idx]) > spec.maxAiStep)
    {
        applied = current[*idx] + std::copysign (spec.maxAiStep, applied - current[*idx]);
        v.status = "limited";
        v.note = "change limited to the safety step of " + juce::String (spec.maxAiStep) + " " + unitSuffix (spec.unit) + " per action";
    }
    v.applied = applied;
    return v;
}

juce::var AgentToolbox::setParameters (const juce::var& in, bool& err)
{
    auto* arr = in.getProperty ("changes", {}).getArray();
    if (arr == nullptr || arr->isEmpty()) { err = true; auto e = obj(); put (e, "error", "changes must be a non-empty array"); return e; }
    juce::Array<juce::var> results;
    int applied = 0;
    for (auto& ch : *arr)
    {
        const auto v = validateChange (session.candidate, ch.getProperty ("id", "").toString(), ch.getProperty ("value", {}));
        auto o = obj();
        put (o, "id", ch.getProperty ("id", ""));
        put (o, "status", v.status);
        if (v.note.isNotEmpty()) put (o, "note", v.note);
        if (v.index >= 0 && v.status != "rejected")
        {
            put (o, "before", r1 (session.candidate[v.index]));
            session.candidate[v.index] = v.applied;
            put (o, "after", r1 (v.applied));
            ++applied;
        }
        results.add (o);
    }
    if (applied > 0) treatments.push_back ("manual_adjustment");
    auto out = obj();
    put (out, "results", results);
    put (out, "applied", applied);
    put (out, "next", "verify with render_and_measure");
    err = applied == 0;
    return out;
}

juce::var AgentToolbox::setModuleEnabled (const juce::var& in, bool& err)
{
    const auto m = in.getProperty ("module", "").toString().toStdString();
    const bool en = in.getProperty ("enabled", false);
    static const std::map<std::string, int> onParam {
        { "level", P::LvlOn }, { "eq", P::EqOn }, { "tone_match", P::TmOn }, { "dynamic_eq", P::DeqOn }, { "compressor", P::CompOn },
        { "deesser", P::DessOn }, { "color", P::ColOn }, { "space", P::SpcOn }, { "motion", P::GateOn }, { "limiter", P::LimOn } };
    auto it = onParam.find (m);
    if (it == onParam.end()) { err = true; auto e = obj(); put (e, "error", "unknown module"); return e; }
    session.candidate[it->second] = en ? 1.f : 0.f;
    treatments.push_back ("manual_adjustment");
    auto o = obj();
    put (o, "module", juce::String (m));
    put (o, "enabled", en);
    return o;
}

juce::var AgentToolbox::reorderChain (const juce::var& in, bool& err)
{
    auto* arr = in.getProperty ("order", {}).getArray();
    ChainOrder order {};
    bool ok = arr != nullptr && arr->size() == kNumChainSlots;
    for (int i = 0; ok && i < kNumChainSlots; ++i)
    {
        auto slot = chainSlotFromName ((*arr)[i].toString().toStdString());
        if (! slot) ok = false; else order[(size_t) i] = *slot;
    }
    if (! ok || ! isValidChainOrder (order))
    {
        err = true;
        auto e = obj();
        put (e, "error", "order must list each of the 8 slots exactly once: level, eq, tone_match, dynamic_eq, compressor, deesser, color, motion");
        return e;
    }
    session.setChainOrder (order);
    treatments.push_back ("reorder_chain");
    auto o = obj();
    juce::Array<juce::var> ord; for (auto sl : order) ord.add (chainSlotName (sl));
    put (o, "order", ord);
    put (o, "next", "verify with render_and_measure");
    return o;
}

juce::var AgentToolbox::renderAndMeasure (const juce::var& in)
{
    const bool withSpace = in.getProperty ("include_space", false);
    if (ctx.onStatus) ctx.onStatus (AgentPhase::Processing, "render_and_measure");
    const auto startM = session.evaluate (session.startSettings, withSpace);
    const auto m = session.evaluate (session.candidate, withSpace);
    const auto& raw = session.getInputMetrics();
    auto o = obj();
    put (o, "candidate", metricsToJson (m));
    put (o, "change_vs_turn_start", metricsDeltaJson (startM, m));
    put (o, "change_vs_raw_input", metricsDeltaJson (raw, m));
    juce::Array<juce::var> ch; for (auto& c : diffSettings (session.startSettings, session.candidate)) ch.add (juce::String (describeChange (c)));
    put (o, "parameter_changes_this_turn", ch);
    put (o, "renders_so_far", session.getRenderCount());
    return o;
}

juce::var AgentToolbox::compareReference (bool& err)
{
    if (ctx.reference == nullptr) { err = true; auto e = obj(); put (e, "error", "no reference loaded"); return e; }
    auto pr = renderPreview (*session.inputAudio(), session.sampleRate(), session.candidate, session.chainOrder(), session.transport());
    const auto f = analysis::Analyzer::analyze (pr.audio->getArrayOfReadPointers(), 2, pr.audio->getNumSamples(), session.sampleRate(), {});
    auto cmp = reference::compareToReference (f, *ctx.reference);
    put (cmp, "reference_name", juce::String (ctx.reference->name));
    return cmp;
}

juce::var AgentToolbox::matchReferenceTool (const juce::var& in, bool& err)
{
    if (ctx.reference == nullptr) { err = true; auto e = obj(); put (e, "error", "no reference loaded"); return e; }
    std::vector<std::string> dims;
    if (auto* a = in.getProperty ("dimensions", {}).getArray()) for (auto& d : *a) dims.push_back (d.toString().toStdString());
    const float infl = in.hasProperty ("influence") ? (float) (double) in["influence"] : ctx.referenceInfluence;
    if (ctx.onStatus) ctx.onStatus (AgentPhase::Matching, "reference");
    auto m = std::make_shared<reference::MatchResult> (reference::matchReference (session, *ctx.reference, reference::MatchDimensions::fromNames (dims), infl));
    lastMatch = m;
    treatments.push_back ("reference_match");
    for (auto& w : m->warnings) warnings.push_back (w);
    auto o = obj();
    juce::Array<juce::var> reps;
    for (auto& r : m->reports)
    {
        auto ro = obj();
        put (ro, "dimension", juce::String (r.dimension));
        put (ro, "summary", juce::String (r.summary));
        put (ro, "distance_before", r1 (r.distanceBefore));
        put (ro, "distance_after", r1 (r.distanceAfter));
        put (ro, "improved", r.improved);
        put (ro, "confidence", r1 (r.confidence));
        if (! r.limitation.empty()) put (ro, "limitation", juce::String (r.limitation));
        reps.add (ro);
    }
    put (o, "reports", reps);
    put (o, "influence", r1 (m->influence));
    put (o, "explanation_simple", juce::String (m->simple));
    put (o, "explanation_engineer", juce::String (m->engineer));
    return o;
}

juce::var AgentToolbox::revertPrevious (const juce::var& in, bool& err)
{
    auto last = ctx.memory != nullptr ? ctx.memory->last() : std::nullopt;
    if (! last) { err = true; auto e = obj(); put (e, "error", "no previous action in this session"); return e; }
    int n = 0;
    const auto target = in.getProperty ("target", "last").toString().toStdString();
    session.candidate = OfflineEngineer::revertChanges (session.candidate, *last, target, n);
    treatments.push_back ("revert_" + target);
    auto o = obj();
    put (o, "reverted_parameters", n);
    put (o, "from_action", juce::String (last->request));
    return o;
}

juce::var AgentToolbox::retrieveKnowledge (const juce::var& in)
{
    juce::Array<juce::var> hits;
    for (auto& h : kb.search (in.getProperty ("query", "").toString().toStdString(), 3))
    {
        auto o = obj();
        put (o, "source", juce::String (h.chunk->doc + " / " + h.chunk->heading));
        put (o, "text", juce::String (h.chunk->text));
        hits.add (o);
    }
    auto o = obj();
    put (o, "results", hits);
    return o;
}

juce::var AgentToolbox::userPreferences()
{
    auto o = obj();
    const auto& b = ctx.bias;
    put (o, "evidence_count", b.evidenceCount);
    put (o, "brightness_bias_db", r1 (b.brightnessDb));
    put (o, "compression_scale", r1 (b.compressionScale));
    put (o, "reverb_scale", r1 (b.reverbScale));
    put (o, "deess_scale", r1 (b.deessScale));
    put (o, "width_scale", r1 (b.widthScale));
    put (o, "loudness_offset_lu", r1 (b.loudnessOffsetLu));
    put (o, "note", b.evidenceCount == 0 ? "no learned preferences for this context yet" : "bounded biases learned from this user's reactions in this context");
    put (o, "similar_past_situations", ctx.experienceSummary);
    return o;
}

juce::var AgentToolbox::searchPlugins (const juce::var& in, bool& err)
{
    if (plugins == nullptr || plugins->size() == 0)
    {
        err = true;
        auto e = obj();
        put (e, "error", "no scanned plugins - the user can run a plugin scan from NOVA's settings. NOVA's built-in modules cover all standard processing.");
        return e;
    }
    juce::Array<juce::var> arr;
    for (auto& p : plugins->search (in.getProperty ("capability", "").toString(), in.getProperty ("query", "").toString(), 15))
        arr.add (hosting::PluginCatalog::entryToJson (p, false));
    auto o = obj();
    put (o, "plugins", arr);
    return o;
}

juce::var AgentToolbox::inspectPlugin (const juce::var& in, bool& err)
{
    const auto* p = plugins != nullptr ? plugins->find (in.getProperty ("plugin_id", "").toString()) : nullptr;
    if (p == nullptr) { err = true; auto e = obj(); put (e, "error", "unknown plugin id"); return e; }
    return hosting::PluginCatalog::entryToJson (*p, true);
}

//==============================================================================
namespace
{
juce::var errorJson (bool& err, const juce::String& msg) { err = true; auto e = obj(); put (e, "error", msg); return e; }

hosting::RackSlotView* findSlot (std::vector<hosting::RackSlotView>& rack, int slot)
{
    for (auto& s : rack)
        if (s.info.slot == slot) return &s;
    return nullptr;
}
} // namespace

juce::var AgentToolbox::getRack (const juce::var& in, bool& err)
{
    if (ctx.rack.empty())
        return errorJson (err, "no plugins are loaded in NOVA's rack. The user can load scanned plugins from the plugin rack in NOVA's advanced view.");
    const int onlySlot = in.hasProperty ("slot") ? (int) in["slot"] : -1;
    const auto query = in.getProperty ("query", "").toString().trim();
    juce::Array<juce::var> slots;
    for (auto& s : ctx.rack)
    {
        if (onlySlot >= 0 && s.info.slot != onlySlot) continue;
        auto o = obj();
        put (o, "slot", s.info.slot);
        put (o, "name", s.info.name);
        put (o, "manufacturer", s.info.manufacturer);
        put (o, "format", s.info.format);
        juce::StringArray caps;
        for (auto& c : s.capabilities) caps.add (c);
        put (o, "capabilities", caps.joinIntoString (","));
        put (o, "bypassed", s.info.bypassed);
        put (o, "latency_samples", s.info.latencySamples);
        juce::Array<juce::var> ps;
        int shown = 0;
        for (auto& p : s.params)
        {
            if (query.isNotEmpty() && ! p.name.containsIgnoreCase (query)) continue;
            if (++shown > 80) break;
            auto po = obj();
            put (po, "index", p.index);
            put (po, "name", p.name);
            put (po, "value", std::round (p.value * 1000.0) / 1000.0);
            if (p.text.isNotEmpty()) put (po, "display", p.text + (p.label.isNotEmpty() ? " " + p.label : juce::String()));
            if (p.numSteps > 0) put (po, "steps", p.numSteps);
            const auto mapped = hosting::CapabilityMapper::conceptFor (p.name, p.label);
            if (mapped.first.isNotEmpty() && mapped.second >= 0.5f) put (po, "concept", mapped.first);
            ps.add (po);
        }
        put (o, "parameters", ps);
        if (shown > 80) put (o, "note", "more parameters exist - narrow with query");
        slots.add (o);
    }
    auto o = obj();
    put (o, "rack", slots);
    put (o, "position", "after NOVA's built-in chain, before output monitoring");
    return o;
}

juce::var AgentToolbox::setRackParameters (const juce::var& in, bool& err)
{
    auto* s = findSlot (ctx.rack, (int) in.getProperty ("slot", -1));
    if (s == nullptr) return errorJson (err, "no plugin in that rack slot - call get_plugin_rack");
    const auto* changes = in.getProperty ("changes", {}).getArray();
    if (changes == nullptr || changes->isEmpty()) return errorJson (err, "changes must be a non-empty array");
    juce::Array<juce::var> results;
    int applied = 0;
    for (auto& c : *changes)
    {
        const int idx = c.getProperty ("index", -1);
        auto r = obj();
        put (r, "index", idx);
        auto it = std::find_if (s->params.begin(), s->params.end(), [idx] (auto& p) { return p.index == idx; });
        const auto v = c.getProperty ("value", {});
        if (it == s->params.end()) { put (r, "status", "rejected: unknown parameter index"); results.add (r); continue; }
        if (! (v.isDouble() || v.isInt() || v.isInt64())) { put (r, "status", "rejected: value must be a number 0-1"); results.add (r); continue; }
        const float requested = (float) (double) v;
        float target = juce::jlimit (0.f, 1.f, requested);
        juce::String note;
        if (it->numSteps > 1)
            target = std::round (target * (float) (it->numSteps - 1)) / (float) (it->numSteps - 1);
        else if (std::abs (target - it->value) > kMaxRackStep)
        {
            target = it->value + std::copysign (kMaxRackStep, target - it->value);
            note = "limited to a 0.25 step this call";
        }
        // merge with an earlier change of the same parameter this turn (keep the original 'before')
        auto prev = std::find_if (rackChanges.begin(), rackChanges.end(), [&] (auto& rc) { return rc.slot == s->info.slot && rc.index == idx; });
        if (prev != rackChanges.end()) prev->after = target;
        else
        {
            hosting::RackParamChange rc;
            rc.slot = s->info.slot;
            rc.index = idx;
            rc.entryId = s->info.entryId;
            rc.paramName = it->name;
            rc.before = it->value;
            rc.after = target;
            rackChanges.push_back (rc);
        }
        it->value = target;
        it->text = {};   // the plugin's display text is only known after it applies the value
        put (r, "name", it->name);
        put (r, "requested", requested);
        put (r, "applied", std::round (target * 1000.0) / 1000.0);
        put (r, "status", note.isEmpty() ? juce::String ("ok") : "clamped: " + note);
        results.add (r);
        ++applied;
    }
    if (applied == 0) err = true;
    auto o = obj();
    put (o, "results", results);
    put (o, "verification", "not rendered offline: third-party plugins run only live. Ask the user to LISTEN again to verify.");
    return o;
}

juce::var AgentToolbox::setRackBypass (const juce::var& in, bool& err)
{
    auto* s = findSlot (ctx.rack, (int) in.getProperty ("slot", -1));
    if (s == nullptr) return errorJson (err, "no plugin in that rack slot - call get_plugin_rack");
    const auto b = in.getProperty ("bypassed", {});
    if (! b.isBool()) return errorJson (err, "bypassed must be true or false");
    s->info.bypassed = (bool) b;
    auto prev = std::find_if (rackBypass.begin(), rackBypass.end(), [&] (auto& rb) { return rb.slot == s->info.slot; });
    if (prev != rackBypass.end()) prev->bypassed = (bool) b;
    else rackBypass.push_back ({ s->info.slot, s->info.entryId, (bool) b });
    auto o = obj();
    put (o, "slot", s->info.slot);
    put (o, "bypassed", (bool) b);
    return o;
}

} // namespace nova::ai
