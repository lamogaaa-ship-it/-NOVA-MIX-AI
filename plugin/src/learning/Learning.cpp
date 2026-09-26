#include "Learning.h"
#include "../ai/IntentParser.h"

namespace nova
{

juce::File novaUserDataDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    dir = dir.getChildFile ("NOVA MIX AI");
    if (! dir.isDirectory()) dir.createDirectory();
    return dir;
}

//==============================================================================
PreferenceStore::PreferenceStore (juce::File f) : file (std::move (f)) { load(); }

void PreferenceStore::setEnabled (bool e) { std::lock_guard<std::mutex> g (lock); enabled = e; }
bool PreferenceStore::isEnabled() const { std::lock_guard<std::mutex> g (lock); return enabled; }

void PreferenceStore::observe (const TasteContext& ctx, const std::string& dim, float delta, const std::string& why)
{
    {
        std::lock_guard<std::mutex> g (lock);
        if (! enabled) return;
        auto& d = contexts[ctx.key()][dim];
        // bounded running estimate: early observations move it more, later ones refine it
        const float lr = 1.f / (float) std::min (d.count + 2, 8);
        d.value = std::clamp (d.value + lr * delta, -3.f, 3.f);
        ++d.count;
        journal.push_back (juce::Time::getCurrentTime().toISO8601 (false) + " [" + juce::String (ctx.key()) + "] " + juce::String (dim)
                           + (delta >= 0 ? " +" : " ") + juce::String (delta, 2) + " : " + juce::String (why));
        while (journal.size() > 50) journal.erase (journal.begin());
    }
    save();
}

void PreferenceStore::learnFromReaction (const TasteContext& ctx, const ActionRecord& action, const std::string& reactionText, int feedback, bool undone)
{
    auto contains = [&] (const std::string& t) { return std::find (action.treatments.begin(), action.treatments.end(), t) != action.treatments.end(); };
    const bool brightened = contains ("presence") || contains ("air") || (contains ("brightness") && action.simple.find ("brighten") != std::string::npos) || contains ("clarity");
    const bool darkened = contains ("brightness") && action.simple.find ("darker") != std::string::npos;

    if (! reactionText.empty())
    {
        const auto p = ai::parseRequest (reactionText);
        if (p.refersToPrevious)
        {
            if (p.has ("harshness") || (p.find ("brightness") && p.find ("brightness")->direction < 0))
                observe (ctx, "brightness", brightened ? -1.f : -0.5f, "user found the result too bright/harsh: \"" + reactionText + "\"");
            if (p.find ("brightness") && p.find ("brightness")->direction > 0)
                observe (ctx, "brightness", darkened ? 1.f : 0.5f, "user wanted more brightness after: \"" + action.request + "\"");
            if (p.find ("space") && p.find ("space")->direction < 0 && contains ("space"))
                observe (ctx, "reverb", -0.3f, "user found the added space too much");
            if (p.find ("space") && p.find ("space")->direction > 0)
                observe (ctx, "reverb", 0.2f, "user wanted more space");
            if (p.has ("level_consistency") && contains ("level_consistency"))
                observe (ctx, "compression", 0.2f, "user wanted even more consistency");
            if (p.find ("revert") && p.find ("revert")->revertTarget == "compression")
                observe (ctx, "compression", -0.3f, "user reverted compression");
        }
    }
    if (feedback != 0 || undone)
    {
        const float sign = undone ? -1.f : (float) feedback;
        if (brightened) observe (ctx, "brightness", 0.3f * sign, undone ? "user undid a brightening action" : "user rated a brightening action");
        if (contains ("level_consistency") || contains ("loudness")) observe (ctx, "compression", 0.1f * sign, "reaction to dynamics processing");
        if (contains ("space")) observe (ctx, "reverb", 0.1f * sign, "reaction to added space");
        if (contains ("sibilance")) observe (ctx, "deess", 0.1f * sign, "reaction to de-essing");
    }
}

ai::PreferenceBias PreferenceStore::getBias (const TasteContext& ctx) const
{
    std::lock_guard<std::mutex> g (lock);
    ai::PreferenceBias b;
    if (! enabled) return b;
    // exact context first, then broader ones - a rap-vocal preference never leaks into mastering
    const std::string keys[] = { ctx.key(), ctx.mode + "/" + ctx.source + "/any" };
    for (auto& k : keys)
    {
        auto it = contexts.find (k);
        if (it == contexts.end()) continue;
        auto val = [&] (const char* d, float scale) -> std::pair<float, int>
        {
            auto jt = it->second.find (d);
            if (jt == it->second.end() || jt->second.count < 2) return { 0.f, 0 };   // need repeated evidence
            return { jt->second.value * scale, jt->second.count };
        };
        auto [br, nb] = val ("brightness", 1.f);
        auto [co, nc] = val ("compression", 1.f);
        auto [rv, nr] = val ("reverb", 1.f);
        auto [de, nd] = val ("deess", 1.f);
        auto [wd, nw] = val ("width", 1.f);
        auto [ld, nl] = val ("loudness", 1.f);
        b.brightnessDb = std::clamp (br, -2.f, 2.f);
        b.compressionScale = std::clamp (1.f + 0.25f * co, 0.6f, 1.4f);
        b.reverbScale = std::clamp (1.f + 0.3f * rv, 0.5f, 1.6f);
        b.deessScale = std::clamp (1.f + 0.25f * de, 0.6f, 1.4f);
        b.widthScale = std::clamp (1.f + 0.25f * wd, 0.6f, 1.4f);
        b.loudnessOffsetLu = std::clamp (ld, -3.f, 2.f);
        b.evidenceCount = nb + nc + nr + nd + nw + nl;
        if (b.evidenceCount > 0) break;
    }
    return b;
}

juce::var PreferenceStore::toJson() const
{
    std::lock_guard<std::mutex> g (lock);
    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("enabled", enabled);
    auto* ctxs = new juce::DynamicObject();
    for (auto& [k, dims] : contexts)
    {
        auto* d = new juce::DynamicObject();
        for (auto& [name, dim] : dims)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("value", dim.value);
            o->setProperty ("count", dim.count);
            d->setProperty (juce::Identifier (name), juce::var (o));
        }
        ctxs->setProperty (juce::Identifier (k), juce::var (d));
    }
    root->setProperty ("contexts", juce::var (ctxs));
    juce::Array<juce::var> j;
    for (auto& s : journal) j.add (s);
    root->setProperty ("journal", j);
    return juce::var (root);
}

int PreferenceStore::totalObservations() const
{
    std::lock_guard<std::mutex> g (lock);
    int n = 0;
    for (auto& [k, dims] : contexts) for (auto& [name, d] : dims) n += d.count;
    return n;
}

void PreferenceStore::clear()
{
    {
        std::lock_guard<std::mutex> g (lock);
        contexts.clear();
        journal.clear();
    }
    file.deleteFile();
}

void PreferenceStore::load()
{
    std::lock_guard<std::mutex> g (lock);
    if (! file.existsAsFile()) return;
    const auto v = juce::JSON::parse (file);
    if (! v.isObject()) return;
    enabled = v.getProperty ("enabled", true);
    if (auto* ctxs = v.getProperty ("contexts", {}).getDynamicObject())
        for (auto& c : ctxs->getProperties())
            if (auto* dims = c.value.getDynamicObject())
                for (auto& d : dims->getProperties())
                {
                    Dim dim;
                    dim.value = (float) d.value.getProperty ("value", 0.0);
                    dim.count = (int) d.value.getProperty ("count", 0);
                    contexts[c.name.toString().toStdString()][d.name.toString().toStdString()] = dim;
                }
    if (auto* arr = v.getProperty ("journal", {}).getArray())
        for (auto& s : *arr) journal.push_back (s.toString());
}

void PreferenceStore::save() const
{
    const auto json = juce::JSON::toString (toJson());
    std::lock_guard<std::mutex> g (lock);
    file.getParentDirectory().createDirectory();
    file.replaceWithText (json);
}

//==============================================================================
ExperienceStore::ExperienceStore (juce::File f) : file (std::move (f)) { load(); }

void ExperienceStore::setEnabled (bool e) { std::lock_guard<std::mutex> g (lock); enabled = e; }
bool ExperienceStore::isEnabled() const { std::lock_guard<std::mutex> g (lock); return enabled; }

std::vector<float> ExperienceStore::featureVector (const analysis::AudioFeatures& f)
{
    auto c = [] (float v, float lo, float hi) { return std::clamp ((v - lo) / (hi - lo), 0.f, 1.f); };
    return {
        c (f.integratedLufs, -40, -5), c (f.crestDb, 5, 25), c (f.phraseLevelStdDb, 0, 8), c (f.lra, 0, 20),
        c (f.centroidHz, 500, 5000), c (f.tiltDbPerOct, -8, 4), c (f.subDb, -50, -5), c (f.lowDb, -30, 0),
        c (f.lowMidDb, -30, 0), c (f.presenceDb, -30, 0), c (f.airDb, -60, -10), c (f.sibilanceSeverityDb, -15, 10),
        c (f.harshSeverityDb, 0, 15), c (f.voicedRatio, 0, 1), c (f.f0MedianHz, 60, 600), c (f.tailToDirectDb, -60, -5),
        c (f.sideToMidDb, -60, 0), c (f.onsetRate, 0, 8)
    };
}

void ExperienceStore::add (Experience e)
{
    std::lock_guard<std::mutex> g (lock);
    if (! enabled) return;
    if (e.timeMs == 0) e.timeMs = juce::Time::currentTimeMillis();
    items.push_back (std::move (e));
    if (items.size() > 2000) items.erase (items.begin(), items.begin() + (long) (items.size() - 2000));
    // append one JSON line
    const auto& x = items.back();
    auto* o = new juce::DynamicObject();
    o->setProperty ("t", x.timeMs);
    o->setProperty ("mode", juce::String (x.mode));
    o->setProperty ("source", juce::String (x.source));
    o->setProperty ("style", juce::String (x.style));
    o->setProperty ("request", juce::String (x.request));
    juce::Array<juce::var> tr; for (auto& t : x.treatments) tr.add (juce::String (t));
    o->setProperty ("treatments", tr);
    juce::Array<juce::var> fv; for (float v : x.featureVector) fv.add (std::round (v * 1000.f) / 1000.f);
    o->setProperty ("fv", fv);
    o->setProperty ("outcome", x.outcome);
    o->setProperty ("accepted", x.accepted);
    o->setProperty ("action", x.actionId);
    file.getParentDirectory().createDirectory();
    file.appendText (juce::JSON::toString (juce::var (o), true) + "\n");
}

void ExperienceStore::setAccepted (int actionId, int accepted)
{
    {
        std::lock_guard<std::mutex> g (lock);
        for (auto& e : items) if (e.actionId == actionId) e.accepted = accepted;
    }
    saveAll();
}

std::vector<std::pair<float, Experience>> ExperienceStore::similar (const std::vector<float>& fv, const std::string& mode, int k) const
{
    std::lock_guard<std::mutex> g (lock);
    std::vector<std::pair<float, Experience>> scored;
    for (auto& e : items)
    {
        if (e.mode != mode || e.featureVector.size() != fv.size() || e.accepted < 0) continue;
        double dot = 0, na = 0, nb = 0;
        for (size_t i = 0; i < fv.size(); ++i) { dot += fv[i] * e.featureVector[i]; na += fv[i] * fv[i]; nb += e.featureVector[i] * e.featureVector[i]; }
        const float sim = (na > 0 && nb > 0) ? (float) (dot / std::sqrt (na * nb)) : 0.f;
        scored.emplace_back (sim, e);
    }
    std::sort (scored.begin(), scored.end(), [] (auto& a, auto& b) { return a.first > b.first; });
    if ((int) scored.size() > k) scored.resize ((size_t) k);
    return scored;
}

int ExperienceStore::size() const { std::lock_guard<std::mutex> g (lock); return (int) items.size(); }

void ExperienceStore::clear()
{
    { std::lock_guard<std::mutex> g (lock); items.clear(); }
    file.deleteFile();
}

void ExperienceStore::load()
{
    std::lock_guard<std::mutex> g (lock);
    if (! file.existsAsFile()) return;
    juce::StringArray lines;
    file.readLines (lines);
    for (auto& l : lines)
    {
        const auto v = juce::JSON::parse (l);
        if (! v.isObject()) continue;
        Experience e;
        e.timeMs = (juce::int64) v.getProperty ("t", 0);
        e.mode = v.getProperty ("mode", "").toString().toStdString();
        e.source = v.getProperty ("source", "").toString().toStdString();
        e.style = v.getProperty ("style", "").toString().toStdString();
        e.request = v.getProperty ("request", "").toString().toStdString();
        if (auto* tr = v.getProperty ("treatments", {}).getArray()) for (auto& t : *tr) e.treatments.push_back (t.toString().toStdString());
        if (auto* fv = v.getProperty ("fv", {}).getArray()) for (auto& x : *fv) e.featureVector.push_back ((float) (double) x);
        e.outcome = v.getProperty ("outcome", {});
        e.accepted = v.getProperty ("accepted", 0);
        e.actionId = v.getProperty ("action", 0);
        items.push_back (std::move (e));
    }
}

void ExperienceStore::saveAll() const
{
    std::lock_guard<std::mutex> g (lock);
    juce::String out;
    for (auto& x : items)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("t", x.timeMs);
        o->setProperty ("mode", juce::String (x.mode));
        o->setProperty ("source", juce::String (x.source));
        o->setProperty ("style", juce::String (x.style));
        o->setProperty ("request", juce::String (x.request));
        juce::Array<juce::var> tr; for (auto& t : x.treatments) tr.add (juce::String (t));
        o->setProperty ("treatments", tr);
        juce::Array<juce::var> fv; for (float v : x.featureVector) fv.add (std::round (v * 1000.f) / 1000.f);
        o->setProperty ("fv", fv);
        o->setProperty ("outcome", x.outcome);
        o->setProperty ("accepted", x.accepted);
        o->setProperty ("action", x.actionId);
        out << juce::JSON::toString (juce::var (o), true) << "\n";
    }
    file.getParentDirectory().createDirectory();
    file.replaceWithText (out);
}

} // namespace nova
