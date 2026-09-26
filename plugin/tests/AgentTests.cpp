#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "SyntheticAudio.h"
#include "ai/CloudEngineer.h"
#include "ai/Knowledge.h"

using namespace nova;
using namespace nova::ai;

namespace
{
juce::String toolUseResponse (const juce::String& id, const juce::String& name, const juce::String& inputJson)
{
    return R"({"id":"msg_1","type":"message","role":"assistant","model":"claude-fable-5-1","stop_reason":"tool_use",
              "content":[{"type":"thinking","thinking":"","signature":"sig-abc"},
                         {"type":"text","text":"Let me check that."},
                         {"type":"tool_use","id":")" + id + R"(","name":")" + name + R"(","input":)" + inputJson + "}]}";
}
juce::String finalResponse (const juce::String& text)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", "msg_f");
    o->setProperty ("type", "message");
    o->setProperty ("role", "assistant");
    o->setProperty ("model", "claude-fable-5-1");
    o->setProperty ("stop_reason", "end_turn");
    juce::Array<juce::var> content;
    auto* t = new juce::DynamicObject();
    t->setProperty ("type", "text");
    t->setProperty ("text", text);
    content.add (juce::var (t));
    o->setProperty ("content", content);
    return juce::JSON::toString (juce::var (o));
}
} // namespace

TEST_CASE ("Tool definitions are valid Messages API tool schemas", "[agent][tools]")
{
    const auto defs = AgentToolbox::toolDefinitions (true);
    REQUIRE (defs.isArray());
    REQUIRE (defs.size() >= 12);
    std::set<juce::String> names;
    for (auto& d : *defs.getArray())
    {
        CHECK (d["name"].toString().isNotEmpty());
        CHECK (d["description"].toString().length() > 40);   // prescriptive "when to call" descriptions
        CHECK (d["input_schema"]["type"].toString() == "object");
        names.insert (d["name"].toString());
    }
    CHECK (names.count ("run_treatment"));
    CHECK (names.count ("render_and_measure"));
    CHECK (names.count ("match_reference"));
    CHECK (names.count ("inspect_plugin_parameters"));
}

TEST_CASE ("set_parameters validation: unknown ids, user-owned controls, ranges and safety steps", "[agent][tools]")
{
    ChainSettings s;
    auto v = AgentToolbox::validateChange (s, "not_a_param", 1.0);
    CHECK (v.status == "rejected");
    v = AgentToolbox::validateChange (s, "bypass", true);
    CHECK (v.status == "rejected");
    v = AgentToolbox::validateChange (s, "ab_monitor_a", true);
    CHECK (v.status == "rejected");
    v = AgentToolbox::validateChange (s, "comp_ratio", 50.0);
    CHECK (v.status == "clamped");
    CHECK_THAT (v.applied, Catch::Matchers::WithinAbs (20.0, 1e-4));
    v = AgentToolbox::validateChange (s, "eq_b4_gain", 15.0);   // max 6 dB per AI action
    CHECK (v.status == "limited");
    CHECK_THAT (v.applied, Catch::Matchers::WithinAbs (6.0, 1e-4));
    v = AgentToolbox::validateChange (s, "col_type", juce::var ("Tube"));
    CHECK (v.status == "ok");
    CHECK_THAT (v.applied, Catch::Matchers::WithinAbs (1.0, 1e-4));
    v = AgentToolbox::validateChange (s, "col_type", juce::var ("Laser"));
    CHECK (v.status == "rejected");
    v = AgentToolbox::validateChange (s, "comp_on", true);
    CHECK (v.status == "ok");
    CHECK (v.applied > 0.5f);
    v = AgentToolbox::validateChange (s, "comp_thresh", std::numeric_limits<double>::quiet_NaN());
    CHECK (v.status == "rejected");
}

TEST_CASE ("Knowledge base retrieves relevant engineering notes", "[agent][knowledge]")
{
    auto& kb = KnowledgeBase::shared();
    REQUIRE (kb.size() > 10);
    auto hits = kb.search ("how to fix harsh notes versus sibilance", 3);
    REQUIRE (! hits.empty());
    CHECK ((juce::String (hits[0].chunk->heading).containsIgnoreCase ("harsh") || juce::String (hits[0].chunk->text).containsIgnoreCase ("harsh")));
    auto m = kb.search ("streaming loudness target LUFS true peak", 2);
    REQUIRE (! m.empty());
    CHECK (juce::String (m[0].chunk->doc).containsIgnoreCase ("master"));
}

TEST_CASE ("Cloud engineer runs a tool-use loop with verified Messages API requests", "[agent][cloud]")
{
    test::VoiceSpec spec;
    auto voice = test::makeVoice (spec);
    EngineerContext ctx;
    ctx.audio = analysis::AnalysisEngine::analyseBuffers (voice, nullptr, spec.sampleRate, analysis::WorkMode::Vocal, 1);
    SessionMemory mem;
    ctx.memory = &mem;

    auto transport = std::make_shared<ScriptedTransport>();
    transport->responses = {
        toolUseResponse ("toolu_01", "run_treatment", R"({"treatment":"harshness","amount":0.5,"avoid_dullness":true,"reason":"measured harsh events"})"),
        toolUseResponse ("toolu_02", "render_and_measure", "{}"),
        finalResponse ("<simple>I softened the harsh notes only when they happen.</simple><engineer>Dynamic bell on the measured harsh band.</engineer>")
    };
    auto client = std::make_shared<LLMClient> (LLMClient::Kind::AnthropicDirect, "https://api.anthropic.com", "test-key", transport);
    EngineerSettings settings;
    settings.model = "claude-fable-5-1";
    CloudEngineer cloud (client, settings, nullptr);
    ConversationHistory history;

    auto out = cloud.handle ("The loud notes hurt my ears", ctx, history);
    INFO (out.reply << " | " << out.replyEngineer);
    REQUIRE (out.ok);
    CHECK (out.changed);
    CHECK (out.reply == "I softened the harsh notes only when they happen.");
    CHECK (out.replyEngineer.find ("Verified on the rendered result") != std::string::npos);
    CHECK (std::find (out.treatments.begin(), out.treatments.end(), "harshness") != out.treatments.end());
    CHECK (out.settings.on (P::DeqB1On));
    REQUIRE (transport->requests.size() == 3);

    // request 1: cached system prompt, tools, context block, effort + server fallbacks
    const auto r1 = juce::JSON::parse (transport->requests[0]);
    CHECK (r1["model"].toString() == "claude-fable-5-1");
    CHECK (r1["system"][0]["cache_control"]["type"].toString() == "ephemeral");
    CHECK (r1["tools"].size() >= 12);
    CHECK (r1["output_config"]["effort"].toString() == "medium");
    CHECK (r1["fallbacks"].toString() == "default");
    CHECK_FALSE (r1.hasProperty ("tool_choice"));        // forced tool use is rejected by Fable 5.1
    CHECK_FALSE (r1.hasProperty ("thinking"));           // always on for this model family
    CHECK (r1["messages"][0]["content"][0]["text"].toString().contains ("<nova_context>"));

    // request 2: the assistant turn is replayed verbatim (thinking block + signature) and the
    // tool_result answers the tool_use id in a single user message
    const auto r2 = juce::JSON::parse (transport->requests[1]);
    const auto msgs = r2["messages"];
    REQUIRE (msgs.size() == 3);
    CHECK (msgs[1]["role"].toString() == "assistant");
    CHECK (msgs[1]["content"][0]["type"].toString() == "thinking");
    CHECK (msgs[1]["content"][0]["signature"].toString() == "sig-abc");
    CHECK (msgs[2]["role"].toString() == "user");
    CHECK (msgs[2]["content"][0]["type"].toString() == "tool_result");
    CHECK (msgs[2]["content"][0]["tool_use_id"].toString() == "toolu_01");
    const auto toolPayload = juce::JSON::parse (msgs[2]["content"][0]["content"].toString());
    CHECK (toolPayload["treatment"].toString() == "harshness");
    CHECK ((bool) toolPayload["applied"]);

    // history is committed append-only
    CHECK (history.size() == 6);
}

TEST_CASE ("Cloud engineer reports transport failures so the offline engineer can take over", "[agent][cloud]")
{
    EngineerContext ctx;
    auto transport = std::make_shared<ScriptedTransport>();   // no responses -> transport error
    auto client = std::make_shared<LLMClient> (LLMClient::Kind::NovaCloud, "https://example.invalid", "tok", transport);
    CloudEngineer cloud (client, EngineerSettings(), nullptr);
    ConversationHistory history;
    auto out = cloud.handle ("make it warmer", ctx, history);
    CHECK_FALSE (out.ok);
    CHECK (out.reply == "__cloud_error__");
    CHECK (history.size() == 0);   // failed turns are never committed
}
