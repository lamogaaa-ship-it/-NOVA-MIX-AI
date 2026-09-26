#include "Conversation.h"

namespace nova::ai
{

int Conversation::add (ChatMessage m)
{
    int id;
    {
        std::lock_guard<std::mutex> g (lock);
        m.id = id = nextId++;
        if (m.timeMs == 0) m.timeMs = juce::Time::currentTimeMillis();
        items.push_back (std::move (m));
        if (items.size() > 300) items.erase (items.begin(), items.begin() + 50);
    }
    sendChangeMessage();
    return id;
}

void Conversation::setFeedback (int messageId, int value)
{
    {
        std::lock_guard<std::mutex> g (lock);
        for (auto& m : items) if (m.id == messageId) m.feedback = value;
    }
    sendChangeMessage();
}

std::vector<ChatMessage> Conversation::messages() const
{
    std::lock_guard<std::mutex> g (lock);
    return items;
}

void Conversation::clear()
{
    { std::lock_guard<std::mutex> g (lock); items.clear(); }
    sendChangeMessage();
}

juce::ValueTree Conversation::toValueTree() const
{
    std::lock_guard<std::mutex> g (lock);
    juce::ValueTree v ("CHAT");
    v.setProperty ("nextId", nextId, nullptr);
    const size_t start = items.size() > 60 ? items.size() - 60 : 0;
    for (size_t i = start; i < items.size(); ++i)
    {
        const auto& m = items[i];
        if (m.role == ChatMessage::Role::Status) continue;
        juce::ValueTree c ("MSG");
        c.setProperty ("id", m.id, nullptr);
        c.setProperty ("role", (int) m.role, nullptr);
        c.setProperty ("text", m.text, nullptr);
        c.setProperty ("eng", m.engineerText, nullptr);
        c.setProperty ("changes", m.changes.joinIntoString ("\n"), nullptr);
        c.setProperty ("warnings", m.warnings.joinIntoString ("\n"), nullptr);
        c.setProperty ("engine", m.engine, nullptr);
        c.setProperty ("action", m.actionId, nullptr);
        c.setProperty ("time", m.timeMs, nullptr);
        c.setProperty ("fb", m.feedback, nullptr);
        v.appendChild (c, nullptr);
    }
    return v;
}

void Conversation::fromValueTree (const juce::ValueTree& v)
{
    {
        std::lock_guard<std::mutex> g (lock);
        items.clear();
        if (! v.isValid()) return;
        nextId = std::max (1, (int) v.getProperty ("nextId", 1));
        for (auto c : v)
        {
            ChatMessage m;
            m.id = c.getProperty ("id");
            m.role = (ChatMessage::Role) (int) c.getProperty ("role");
            m.text = c.getProperty ("text").toString();
            m.engineerText = c.getProperty ("eng").toString();
            m.changes = juce::StringArray::fromLines (c.getProperty ("changes").toString());
            m.changes.removeEmptyStrings();
            m.warnings = juce::StringArray::fromLines (c.getProperty ("warnings").toString());
            m.warnings.removeEmptyStrings();
            m.engine = c.getProperty ("engine").toString();
            m.actionId = c.getProperty ("action");
            m.timeMs = (juce::int64) c.getProperty ("time");
            m.feedback = c.getProperty ("fb");
            items.push_back (m);
            nextId = std::max (nextId, m.id + 1);
        }
    }
    sendChangeMessage();
}

} // namespace nova::ai
