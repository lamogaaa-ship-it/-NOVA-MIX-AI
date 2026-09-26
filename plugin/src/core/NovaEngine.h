#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../analysis/AnalysisEngine.h"
#include "../ai/CloudEngineer.h"
#include "../ai/Conversation.h"
#include "../ai/Engineer.h"
#include "../hosting/PluginCatalog.h"
#include "../learning/Learning.h"
#include "../learning/SessionMemory.h"
#include "../reference/Reference.h"
#include "Settings.h"

#include <deque>

namespace nova
{

class NovaAudioProcessor;
class CompanionClient;

// Owns every non-realtime subsystem (analysis, AI engineer, reference matching, memory,
// learning, plugin catalog). Lives as long as the processor; never touched by the audio thread.
class NovaEngine : private juce::Thread
{
public:
    explicit NovaEngine (NovaAudioProcessor& p);
    ~NovaEngine() override;

    void audioPrepared (double sampleRate, int blockSize);
    juce::ValueTree saveState() const;
    void restoreState (const juce::ValueTree& session);

    //==============================================================================
    // Conversation / engineer (message thread API)
    enum class Source { Typed, Voice, Button };
    void submitRequest (const juce::String& text, Source source = Source::Typed);
    void cancelCurrent();
    bool isBusy() const noexcept { return busy.load(); }
    ai::AgentPhase getPhase() const noexcept { return phase.load(); }
    juce::String getPhaseDetail() const;
    juce::int64 getPhaseChangedMs() const noexcept { return phaseChangedMs.load(); }

    bool undo();
    bool redo();
    bool canUndo() const { return snapshots.canUndo(); }
    bool canRedo() const { return snapshots.canRedo(); }
    void giveFeedback (int messageId, int value);
    void postStatus (const juce::String& text);

    juce::StringArray suggestions() const;
    juce::String engineLabel() const;           // honest: which engine will answer
    bool isCloudEngineActive() const;

    //==============================================================================
    void setWorkMode (analysis::WorkMode m);
    analysis::WorkMode getWorkMode() const noexcept { return analysisEngine.getWorkMode(); }
    void setStyle (const juce::String& s) { std::lock_guard<std::mutex> g (miscLock); style = s; }
    juce::String getStyle() const { std::lock_guard<std::mutex> g (miscLock); return style; }

    // Reference
    void loadReference (const juce::File& f);
    void clearReference();
    reference::ReferenceManager& getReferences() noexcept { return references; }
    void matchReferenceNow();

    // Subsystems
    analysis::AnalysisEngine& getAnalysis() noexcept { return analysisEngine; }
    ai::Conversation& getConversation() noexcept { return conversation; }
    SessionMemory& getMemory() noexcept { return memory; }
    SnapshotManager& getSnapshots() noexcept { return snapshots; }
    PreferenceStore& getPreferences() noexcept { return *preferences; }
    ExperienceStore& getExperiences() noexcept { return *experiences; }
    hosting::PluginCatalog& getPluginCatalog() noexcept { return *pluginCatalog; }
    CompanionClient* getCompanion() noexcept { return companion.get(); }
    EngineerSettings getSettings() const { return SettingsStore::shared().get(); }
    void setSettings (const EngineerSettings& s);

    // For tests: apply settings directly on the calling thread instead of the message thread
    void setApplyDirectly (bool b) noexcept { applyDirectly = b; }
    // For tests: inject a transport for the cloud engineer
    void setTransportOverride (std::shared_ptr<ai::HttpTransport> t) { std::lock_guard<std::mutex> g (miscLock); transportOverride = std::move (t); }
    bool waitUntilIdle (int timeoutMs);

    NovaAudioProcessor& processor;

private:
    struct Request { juce::String text; Source source; bool internalMatch = false; };

    void run() override;
    void handleRequest (const Request& r);
    std::shared_ptr<const analysis::AnalysisResult> ensureWorkingAudio();
    void setPhase (ai::AgentPhase p, const juce::String& detail = {});
    void applyToProcessor (const ChainSettings& s, const ChainOrder& order, const hosting::RackSnapshot* rack = nullptr,
                           const std::vector<hosting::RackParamChange>* rackChanges = nullptr, const std::vector<hosting::RackBypassChange>* rackBypass = nullptr);
    // Runs fn on the message thread and waits (hosted plugins must be touched there). Falls back to
    // the calling thread when no message loop is pumping (tests / offline hosts).
    void runOnMessageThread (std::function<void()> fn);
    void postAssistant (const juce::String& text, const juce::String& eng, const juce::StringArray& changes, const juce::StringArray& warnings,
                        const juce::String& engine, int actionId, bool canUndo, bool isError = false);
    TasteContext tasteContext (const std::string& source) const;

    analysis::AnalysisEngine analysisEngine;
    reference::ReferenceManager references;
    ai::Conversation conversation;
    SessionMemory memory;
    SnapshotManager snapshots;
    std::unique_ptr<PreferenceStore> preferences;
    std::unique_ptr<ExperienceStore> experiences;
    std::unique_ptr<hosting::PluginCatalog> pluginCatalog;
    std::unique_ptr<CompanionClient> companion;
    ai::ConversationHistory llmHistory;

    std::mutex queueLock;
    std::deque<Request> queue;
    juce::WaitableEvent queueEvent;
    std::atomic<bool> busy { false }, cancelFlag { false };
    std::atomic<ai::AgentPhase> phase { ai::AgentPhase::Idle };
    std::atomic<juce::int64> phaseChangedMs { 0 };
    mutable std::mutex miscLock;
    juce::String phaseDetail, style;
    std::shared_ptr<ai::HttpTransport> transportOverride;
    bool applyDirectly = false;
};

} // namespace nova
