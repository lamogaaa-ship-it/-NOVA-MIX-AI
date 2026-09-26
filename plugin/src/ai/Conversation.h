#pragma once

#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>

#include <mutex>
#include <string>
#include <vector>

namespace nova::ai
{

struct ChatMessage
{
    enum class Role { User, Assistant, Status, Error };
    int id = 0;
    Role role = Role::Assistant;
    juce::String text;             // simple explanation / user text
    juce::String engineerText;     // technical explanation
    juce::StringArray changes;     // human-readable parameter changes
    juce::StringArray warnings;
    juce::String engine;           // "offline" or model id
    int actionId = 0;              // session-memory id (for feedback / undo)
    bool canUndo = false;
    juce::int64 timeMs = 0;
    int feedback = 0;
};

// Thread-safe chat log. Listeners are notified on the message thread (ChangeBroadcaster).
class Conversation : public juce::ChangeBroadcaster
{
public:
    int add (ChatMessage m);
    void setFeedback (int messageId, int value);
    std::vector<ChatMessage> messages() const;
    void clear();

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& v);

private:
    mutable std::mutex lock;
    std::vector<ChatMessage> items;
    int nextId = 1;
};

} // namespace nova::ai
