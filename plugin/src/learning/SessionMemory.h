#pragma once

// LEVEL 1 learning: what happened in this session (persisted with the DAW project).
// Snapshots give safe undo/redo for every AI action and manual A/B comparisons.

#include <juce_data_structures/juce_data_structures.h>

#include "../ai/Treatments.h"
#include "../hosting/HostedRack.h"

#include <deque>
#include <mutex>
#include <map>
#include <optional>

namespace nova
{

struct ActionRecord
{
    int id = 0;
    juce::int64 timeMs = 0;
    std::string request;
    std::string engine;                  // "offline" / model id
    std::vector<std::string> treatments;
    std::vector<ai::ParamChange> changes;
    std::string simple, engineer;
    std::string mode, source;
    bool undone = false;
    int feedback = 0;                    // +1 liked, -1 disliked, 0 none
    int abChoice = 0;                    // +1 user kept B (processed), -1 preferred A after listening
};

class SessionMemory
{
public:
    int add (ActionRecord r);
    std::optional<ActionRecord> last (bool includeUndone = false) const;
    std::optional<ActionRecord> get (int id) const;
    std::vector<ActionRecord> all() const;
    void markUndone (int id, bool undone);
    void setFeedback (int id, int value);
    void setAbChoice (int id, int value);
    juce::String summaryForAI (int maxItems = 6) const;

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& v);

private:
    mutable std::mutex lock;
    std::deque<ActionRecord> records;
    int nextId = 1;
};

struct Snapshot
{
    std::string label;
    ChainSettings settings;
    ChainOrder order {};
    hosting::RackSnapshot rack;          // hosted plugin parameter values / bypass
    juce::int64 timeMs = 0;
    int actionId = 0;
};

class SnapshotManager
{
public:
    static constexpr size_t kMax = 40;
    void push (Snapshot s);                                         // before an action
    std::optional<Snapshot> undo (const Snapshot& current);         // returns state to restore
    std::optional<Snapshot> redo (const Snapshot& current);
    bool canUndo() const;
    bool canRedo() const;
    std::vector<Snapshot> history() const;

    // user-named snapshots (A/B slots, "save this version")
    void store (const std::string& name, Snapshot s);
    std::optional<Snapshot> recall (const std::string& name) const;
    std::vector<std::string> storedNames() const;

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& v);

private:
    mutable std::mutex lock;
    std::deque<Snapshot> undoStack, redoStack;
    std::map<std::string, Snapshot> named;
};

juce::ValueTree settingsToTree (const juce::Identifier& type, const ChainSettings& s);
ChainSettings settingsFromTree (const juce::ValueTree& v);

} // namespace nova
