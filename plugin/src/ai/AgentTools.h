#pragma once

#include "Engineer.h"
#include "Knowledge.h"

namespace nova::hosting { class PluginCatalog; }

namespace nova::ai
{

// Validated engineering tools exposed to the cloud engineer. Every tool:
//   * validates its input against the parameter table (unknown ids rejected, ranges clamped,
//     per-action safety steps enforced, monitoring controls protected)
//   * acts on the *candidate* state (applied to the plugin only after the turn is verified)
//   * returns compact machine-readable JSON
class AgentToolbox
{
public:
    AgentToolbox (EngineerContext& ctx, TreatmentSession& session, const KnowledgeBase& kb, hosting::PluginCatalog* plugins);

    static juce::var toolDefinitions (bool includePluginTools);
    juce::var execute (const juce::String& name, const juce::var& input, bool& isError);

    // Parameter validation shared with tests / UI
    struct Validation { int index = -1; float requested = 0, applied = 0; juce::String status, note; };
    static Validation validateChange (const ChainSettings& current, const juce::String& id, const juce::var& value);

    std::vector<std::string> treatments, warnings;
    std::shared_ptr<reference::MatchResult> lastMatch;
    juce::Array<juce::var> trace;
    int toolCalls = 0;

private:
    EngineerContext& ctx;
    TreatmentSession& session;
    const KnowledgeBase& kb;
    hosting::PluginCatalog* plugins;

    juce::var analyzeAudio (const juce::var& in, bool& err);
    juce::var getChainState (const juce::var& in);
    juce::var runTreatment (const juce::var& in, bool& err);
    juce::var setParameters (const juce::var& in, bool& err);
    juce::var setModuleEnabled (const juce::var& in, bool& err);
    juce::var reorderChain (const juce::var& in, bool& err);
    juce::var renderAndMeasure (const juce::var& in);
    juce::var compareReference (bool& err);
    juce::var matchReferenceTool (const juce::var& in, bool& err);
    juce::var revertPrevious (const juce::var& in, bool& err);
    juce::var retrieveKnowledge (const juce::var& in);
    juce::var userPreferences();
    juce::var searchPlugins (const juce::var& in, bool& err);
    juce::var inspectPlugin (const juce::var& in, bool& err);
};

juce::var chainStateJson (const ChainSettings& s, const ChainOrder& order, bool onlyNonDefault, const std::string& moduleFilter = {});
juce::var treatmentResultJson (const TreatmentResult& r);

} // namespace nova::ai
