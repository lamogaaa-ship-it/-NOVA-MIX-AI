#include "SessionMemory.h"

namespace nova
{

juce::ValueTree settingsToTree (const juce::Identifier& type, const ChainSettings& s)
{
    juce::ValueTree v (type);
    for (int i = 0; i < P::Count; ++i)
        v.setProperty (kParams[(size_t) i].id, s[i], nullptr);
    return v;
}

ChainSettings settingsFromTree (const juce::ValueTree& v)
{
    ChainSettings s;
    for (int i = 0; i < P::Count; ++i)
        if (v.hasProperty (kParams[(size_t) i].id))
            s[i] = clampToSpec (kParams[(size_t) i], (float) v.getProperty (kParams[(size_t) i].id));
    return s;
}

//==============================================================================
int SessionMemory::add (ActionRecord r)
{
    std::lock_guard<std::mutex> g (lock);
    r.id = nextId++;
    if (r.timeMs == 0) r.timeMs = juce::Time::currentTimeMillis();
    records.push_back (std::move (r));
    while (records.size() > 200) records.pop_front();
    return records.back().id;
}

std::optional<ActionRecord> SessionMemory::last (bool includeUndone) const
{
    std::lock_guard<std::mutex> g (lock);
    for (auto it = records.rbegin(); it != records.rend(); ++it)
        if (includeUndone || ! it->undone) return *it;
    return std::nullopt;
}

std::optional<ActionRecord> SessionMemory::get (int id) const
{
    std::lock_guard<std::mutex> g (lock);
    for (auto& r : records) if (r.id == id) return r;
    return std::nullopt;
}

std::vector<ActionRecord> SessionMemory::all() const
{
    std::lock_guard<std::mutex> g (lock);
    return { records.begin(), records.end() };
}

void SessionMemory::markUndone (int id, bool undone)
{
    std::lock_guard<std::mutex> g (lock);
    for (auto& r : records) if (r.id == id) r.undone = undone;
}

void SessionMemory::setFeedback (int id, int value)
{
    std::lock_guard<std::mutex> g (lock);
    for (auto& r : records) if (r.id == id) r.feedback = value;
}

void SessionMemory::setAbChoice (int id, int value)
{
    std::lock_guard<std::mutex> g (lock);
    for (auto& r : records) if (r.id == id) r.abChoice = value;
}

juce::String SessionMemory::summaryForAI (int maxItems) const
{
    std::lock_guard<std::mutex> g (lock);
    juce::String s;
    int n = 0;
    for (auto it = records.rbegin(); it != records.rend() && n < maxItems; ++it, ++n)
    {
        s << "#" << it->id << (it->undone ? " (UNDONE)" : "") << " request: \"" << juce::String (it->request) << "\" -> ";
        for (size_t i = 0; i < it->treatments.size(); ++i) s << (i ? ", " : "") << juce::String (it->treatments[i]);
        s << "; changes: ";
        for (size_t i = 0; i < std::min<size_t> (it->changes.size(), 12); ++i)
            s << (i ? "; " : "") << juce::String (ai::describeChange (it->changes[i]));
        if (it->feedback != 0) s << "; user feedback " << (it->feedback > 0 ? "positive" : "negative");
        if (it->abChoice != 0) s << "; A/B: user " << (it->abChoice > 0 ? "kept processed" : "preferred original");
        s << "\n";
    }
    return s;
}

juce::ValueTree SessionMemory::toValueTree() const
{
    std::lock_guard<std::mutex> g (lock);
    juce::ValueTree v ("MEMORY");
    v.setProperty ("nextId", nextId, nullptr);
    const size_t start = records.size() > 60 ? records.size() - 60 : 0;
    for (size_t i = start; i < records.size(); ++i)
    {
        const auto& r = records[i];
        juce::ValueTree a ("ACTION");
        a.setProperty ("id", r.id, nullptr);
        a.setProperty ("time", r.timeMs, nullptr);
        a.setProperty ("request", juce::String (r.request), nullptr);
        a.setProperty ("engine", juce::String (r.engine), nullptr);
        juce::StringArray tr;
        for (auto& t : r.treatments) tr.add (t);
        a.setProperty ("treatments", tr.joinIntoString (","), nullptr);
        a.setProperty ("simple", juce::String (r.simple), nullptr);
        a.setProperty ("engineer", juce::String (r.engineer), nullptr);
        a.setProperty ("mode", juce::String (r.mode), nullptr);
        a.setProperty ("source", juce::String (r.source), nullptr);
        a.setProperty ("undone", r.undone, nullptr);
        a.setProperty ("feedback", r.feedback, nullptr);
        a.setProperty ("ab", r.abChoice, nullptr);
        for (auto& c : r.changes)
        {
            juce::ValueTree ch ("CHANGE");
            ch.setProperty ("p", kParams[(size_t) c.index].id, nullptr);
            ch.setProperty ("a", c.before, nullptr);
            ch.setProperty ("b", c.after, nullptr);
            a.appendChild (ch, nullptr);
        }
        v.appendChild (a, nullptr);
    }
    return v;
}

void SessionMemory::fromValueTree (const juce::ValueTree& v)
{
    std::lock_guard<std::mutex> g (lock);
    records.clear();
    if (! v.isValid()) return;
    nextId = std::max (1, (int) v.getProperty ("nextId", 1));
    for (auto a : v)
    {
        ActionRecord r;
        r.id = a.getProperty ("id");
        r.timeMs = (juce::int64) a.getProperty ("time");
        r.request = a.getProperty ("request").toString().toStdString();
        r.engine = a.getProperty ("engine").toString().toStdString();
        for (auto& t : juce::StringArray::fromTokens (a.getProperty ("treatments").toString(), ",", "")) r.treatments.push_back (t.toStdString());
        r.simple = a.getProperty ("simple").toString().toStdString();
        r.engineer = a.getProperty ("engineer").toString().toStdString();
        r.mode = a.getProperty ("mode").toString().toStdString();
        r.source = a.getProperty ("source").toString().toStdString();
        r.undone = a.getProperty ("undone");
        r.feedback = a.getProperty ("feedback");
        r.abChoice = a.getProperty ("ab");
        for (auto ch : a)
            if (auto idx = findParamIndex (ch.getProperty ("p").toString().toStdString()))
                r.changes.push_back ({ *idx, (float) ch.getProperty ("a"), (float) ch.getProperty ("b") });
        records.push_back (std::move (r));
        nextId = std::max (nextId, records.back().id + 1);
    }
}

//==============================================================================
void SnapshotManager::push (Snapshot s)
{
    std::lock_guard<std::mutex> g (lock);
    if (s.timeMs == 0) s.timeMs = juce::Time::currentTimeMillis();
    undoStack.push_back (std::move (s));
    while (undoStack.size() > kMax) undoStack.pop_front();
    redoStack.clear();
}

std::optional<Snapshot> SnapshotManager::undo (const Snapshot& current)
{
    std::lock_guard<std::mutex> g (lock);
    if (undoStack.empty()) return std::nullopt;
    auto s = undoStack.back();
    undoStack.pop_back();
    Snapshot cur = current;
    cur.actionId = s.actionId;
    cur.label = s.label;
    redoStack.push_back (cur);
    return s;
}

std::optional<Snapshot> SnapshotManager::redo (const Snapshot& current)
{
    std::lock_guard<std::mutex> g (lock);
    if (redoStack.empty()) return std::nullopt;
    auto s = redoStack.back();
    redoStack.pop_back();
    Snapshot cur = current;
    cur.actionId = s.actionId;
    cur.label = s.label;
    undoStack.push_back (cur);
    return s;
}

bool SnapshotManager::canUndo() const { std::lock_guard<std::mutex> g (lock); return ! undoStack.empty(); }
bool SnapshotManager::canRedo() const { std::lock_guard<std::mutex> g (lock); return ! redoStack.empty(); }
std::vector<Snapshot> SnapshotManager::history() const { std::lock_guard<std::mutex> g (lock); return { undoStack.begin(), undoStack.end() }; }

void SnapshotManager::store (const std::string& name, Snapshot s)
{
    std::lock_guard<std::mutex> g (lock);
    s.label = name;
    if (s.timeMs == 0) s.timeMs = juce::Time::currentTimeMillis();
    named[name] = std::move (s);
}

std::optional<Snapshot> SnapshotManager::recall (const std::string& name) const
{
    std::lock_guard<std::mutex> g (lock);
    auto it = named.find (name);
    if (it == named.end()) return std::nullopt;
    return it->second;
}

std::vector<std::string> SnapshotManager::storedNames() const
{
    std::lock_guard<std::mutex> g (lock);
    std::vector<std::string> n;
    for (auto& kv : named) n.push_back (kv.first);
    return n;
}

static juce::ValueTree snapToTree (const juce::Identifier& type, const Snapshot& s)
{
    auto v = settingsToTree (type, s.settings);
    v.setProperty ("_label", juce::String (s.label), nullptr);
    v.setProperty ("_order", (juce::int64) packChainOrder (s.order), nullptr);
    v.setProperty ("_time", s.timeMs, nullptr);
    v.setProperty ("_action", s.actionId, nullptr);
    return v;
}

static Snapshot snapFromTree (const juce::ValueTree& v)
{
    Snapshot s;
    s.settings = settingsFromTree (v);
    s.label = v.getProperty ("_label").toString().toStdString();
    s.order = unpackChainOrder ((uint64_t) (juce::int64) v.getProperty ("_order", (juce::int64) packChainOrder (defaultChainOrder())));
    s.timeMs = (juce::int64) v.getProperty ("_time");
    s.actionId = v.getProperty ("_action");
    return s;
}

juce::ValueTree SnapshotManager::toValueTree() const
{
    std::lock_guard<std::mutex> g (lock);
    juce::ValueTree v ("SNAPSHOTS");
    for (auto& s : undoStack) v.appendChild (snapToTree ("UNDO", s), nullptr);
    for (auto& s : redoStack) v.appendChild (snapToTree ("REDO", s), nullptr);
    for (auto& kv : named) v.appendChild (snapToTree ("NAMED", kv.second), nullptr);
    return v;
}

void SnapshotManager::fromValueTree (const juce::ValueTree& v)
{
    std::lock_guard<std::mutex> g (lock);
    undoStack.clear(); redoStack.clear(); named.clear();
    if (! v.isValid()) return;
    for (auto c : v)
    {
        auto s = snapFromTree (c);
        if (c.hasType ("UNDO")) undoStack.push_back (s);
        else if (c.hasType ("REDO")) redoStack.push_back (s);
        else if (c.hasType ("NAMED")) named[s.label] = s;
    }
}

} // namespace nova
