#pragma once

// Shared types for NOVA ENGINEER (offline planner + cloud LLM agent).

#include "Treatments.h"
#include "../analysis/AnalysisEngine.h"
#include "../learning/SessionMemory.h"
#include "../reference/Reference.h"

#include <functional>

namespace nova::ai
{

enum class AgentPhase { Idle, Listening, Analyzing, Thinking, Processing, Matching, Complete, Error };
const char* agentPhaseName (AgentPhase p);

struct EngineerContext
{
    std::shared_ptr<const analysis::AnalysisResult> audio;   // working audio (listened excerpt) + analysis
    ChainSettings current;
    ChainOrder order = defaultChainOrder();
    TransportSnapshot transport;
    analysis::WorkMode mode = analysis::WorkMode::Vocal;
    std::shared_ptr<const reference::ReferenceProfile> reference;
    reference::MatchDimensions referenceDims;
    float referenceInfluence = 0.75f;
    const SessionMemory* memory = nullptr;
    PreferenceBias bias;
    std::string styleHint;
    std::atomic<bool>* cancel = nullptr;
    std::function<void (AgentPhase, const std::string&)> onStatus;
    juce::String userPreferenceSummary, experienceSummary;
};

struct EngineerOutcome
{
    bool ok = true;
    bool changed = false;
    std::string reply, replyEngineer;
    ChainSettings settings;
    ChainOrder order = defaultChainOrder();
    bool orderChanged = false;
    std::vector<std::string> treatments;
    std::vector<TreatmentResult> steps;
    std::vector<std::string> warnings;
    Metrics before, after;
    bool hasMetrics = false;
    std::string engine = "offline";
    int renders = 0;
    double elapsedMs = 0;
    bool undoRequested = false, redoRequested = false;
    std::shared_ptr<const reference::MatchResult> match;
    juce::var trace;                          // tool / step trace for diagnostics
    std::string requestText;
};

// Deterministic evidence-driven engineer (no network). Also the fallback when the cloud
// engineer is unavailable.
class OfflineEngineer
{
public:
    static EngineerOutcome handle (const std::string& request, EngineerContext& ctx);

    // Pieces reused by the cloud agent's tools
    static ChainSettings revertChanges (const ChainSettings& current, const ActionRecord& action, const std::string& target, int& reverted);
    static std::string analysisSummary (const analysis::AnalysisResult& a, bool engineer);
    static std::string verificationLine (const Metrics& before, const Metrics& after);
};

} // namespace nova::ai
