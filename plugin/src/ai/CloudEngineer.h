#pragma once

#include "AgentTools.h"
#include "LLMClient.h"
#include "../core/Settings.h"

#include <mutex>

namespace nova::ai
{

// Messages-API conversation for the plugin session. Append-only: completed turns (including
// thinking blocks, tool_use and tool_result blocks) are never edited, so provider-side prompt
// caching and preserved-thinking checks stay valid.
class ConversationHistory
{
public:
    juce::Array<juce::var> snapshot() const;
    void commit (const juce::Array<juce::var>& turnMessages);
    void clear();
    int size() const;
    bool needsReset() const;       // bounded context: start fresh (with a memory summary) when long

private:
    mutable std::mutex lock;
    juce::Array<juce::var> messages;
};

class CloudEngineer
{
public:
    CloudEngineer (std::shared_ptr<LLMClient> client, EngineerSettings settings, hosting::PluginCatalog* plugins);

    EngineerOutcome handle (const std::string& request, EngineerContext& ctx, ConversationHistory& history);

    static juce::String systemPrompt();
    static juce::var contextBlock (const EngineerContext& ctx, const std::string& request);
    static void parseFinalText (const juce::String& text, std::string& simple, std::string& engineer);

    int maxToolRounds = 14;

private:
    std::shared_ptr<LLMClient> client;
    EngineerSettings settings;
    hosting::PluginCatalog* plugins;
};

} // namespace nova::ai
