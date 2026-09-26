#include "PluginCatalog.h"

namespace nova::hosting
{

PluginCatalog::PluginCatalog (juce::File f) : file (std::move (f)) { reload(); }

void PluginCatalog::reload()
{
    std::lock_guard<std::mutex> g (lock);
    entries.clear();
    if (! file.existsAsFile()) return;
    const auto v = juce::JSON::parse (file);
    if (auto* arr = v.getProperty ("plugins", {}).getArray())
        for (auto& p : *arr) entries.push_back (entryFromJson (p));
}

bool PluginCatalog::save() const
{
    std::lock_guard<std::mutex> g (lock);
    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("saved_at", juce::Time::getCurrentTime().toISO8601 (true));
    juce::Array<juce::var> arr;
    for (auto& e : entries) arr.add (entryToJson (e, true));
    root->setProperty ("plugins", arr);
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (juce::var (root)));
}

void PluginCatalog::replaceAll (std::vector<PluginEntry> e)
{
    std::lock_guard<std::mutex> g (lock);
    entries = std::move (e);
}

std::vector<PluginEntry> PluginCatalog::search (const juce::String& capability, const juce::String& query, int maxResults) const
{
    std::lock_guard<std::mutex> g (lock);
    std::vector<PluginEntry> out;
    for (auto& e : entries)
    {
        if (! e.loadedOk || e.isInstrument) continue;
        if (capability.isNotEmpty() && std::find (e.capabilities.begin(), e.capabilities.end(), capability) == e.capabilities.end()) continue;
        if (query.isNotEmpty() && ! (e.name.containsIgnoreCase (query) || e.manufacturer.containsIgnoreCase (query) || e.category.containsIgnoreCase (query))) continue;
        out.push_back (e);
        if ((int) out.size() >= maxResults) break;
    }
    return out;
}

const PluginEntry* PluginCatalog::find (const juce::String& id) const
{
    std::lock_guard<std::mutex> g (lock);
    for (auto& e : entries) if (e.id == id) return &e;
    return nullptr;
}

int PluginCatalog::size() const { std::lock_guard<std::mutex> g (lock); return (int) entries.size(); }

juce::int64 PluginCatalog::lastScanMs() const
{
    std::lock_guard<std::mutex> g (lock);
    juce::int64 t = 0;
    for (auto& e : entries) t = std::max (t, e.scannedAtMs);
    return t;
}

juce::var PluginCatalog::entryToJson (const PluginEntry& e, bool withParams)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", e.id);
    o->setProperty ("name", e.name);
    o->setProperty ("manufacturer", e.manufacturer);
    o->setProperty ("format", e.format);
    o->setProperty ("version", e.version);
    o->setProperty ("category", e.category);
    o->setProperty ("file", e.fileOrIdentifier);
    o->setProperty ("inputs", e.numInputs);
    o->setProperty ("outputs", e.numOutputs);
    o->setProperty ("latency_samples", e.latencySamples);
    o->setProperty ("has_editor", e.hasEditor);
    o->setProperty ("loaded_ok", e.loadedOk);
    o->setProperty ("is_instrument", e.isInstrument);
    if (e.loadError.isNotEmpty()) o->setProperty ("load_error", e.loadError);
    juce::Array<juce::var> caps;
    for (auto& c : e.capabilities) caps.add (c);
    o->setProperty ("capabilities", caps);
    o->setProperty ("num_params", (int) e.params.size());
    o->setProperty ("scanned_at", e.scannedAtMs);
    if (withParams)
    {
        juce::Array<juce::var> ps;
        for (auto& p : e.params)
        {
            auto* po = new juce::DynamicObject();
            po->setProperty ("index", p.index);
            po->setProperty ("name", p.name);
            if (p.label.isNotEmpty()) po->setProperty ("label", p.label);
            if (p.mappedConcept.isNotEmpty()) { po->setProperty ("concept", p.mappedConcept); po->setProperty ("concept_confidence", p.conceptConfidence); }
            po->setProperty ("default", p.defaultValue);
            if (p.defaultText.isNotEmpty()) po->setProperty ("default_text", p.defaultText);
            po->setProperty ("automatable", p.automatable);
            if (p.numSteps > 0) po->setProperty ("steps", p.numSteps);
            ps.add (juce::var (po));
        }
        o->setProperty ("params", ps);
    }
    return juce::var (o);
}

PluginEntry PluginCatalog::entryFromJson (const juce::var& v)
{
    PluginEntry e;
    e.id = v.getProperty ("id", {}).toString();
    e.name = v.getProperty ("name", {}).toString();
    e.manufacturer = v.getProperty ("manufacturer", {}).toString();
    e.format = v.getProperty ("format", {}).toString();
    e.version = v.getProperty ("version", {}).toString();
    e.category = v.getProperty ("category", {}).toString();
    e.fileOrIdentifier = v.getProperty ("file", {}).toString();
    e.numInputs = v.getProperty ("inputs", 0);
    e.numOutputs = v.getProperty ("outputs", 0);
    e.latencySamples = v.getProperty ("latency_samples", 0);
    e.hasEditor = v.getProperty ("has_editor", false);
    e.loadedOk = v.getProperty ("loaded_ok", false);
    e.isInstrument = v.getProperty ("is_instrument", false);
    e.loadError = v.getProperty ("load_error", {}).toString();
    e.scannedAtMs = (juce::int64) v.getProperty ("scanned_at", 0);
    if (auto* caps = v.getProperty ("capabilities", {}).getArray()) for (auto& c : *caps) e.capabilities.push_back (c.toString());
    if (auto* ps = v.getProperty ("params", {}).getArray())
        for (auto& p : *ps)
        {
            ParamInfo pi;
            pi.index = p.getProperty ("index", -1);
            pi.name = p.getProperty ("name", {}).toString();
            pi.label = p.getProperty ("label", {}).toString();
            pi.mappedConcept = p.getProperty ("concept", {}).toString();
            pi.conceptConfidence = (float) (double) p.getProperty ("concept_confidence", 0.0);
            pi.defaultValue = (float) (double) p.getProperty ("default", 0.0);
            pi.defaultText = p.getProperty ("default_text", {}).toString();
            pi.automatable = p.getProperty ("automatable", true);
            pi.numSteps = p.getProperty ("steps", 0);
            e.params.push_back (pi);
        }
    return e;
}

//==============================================================================
std::vector<juce::String> CapabilityMapper::capabilitiesFor (const PluginEntry& e)
{
    std::vector<juce::String> caps;
    const auto hay = (e.name + " " + e.category).toLowerCase();
    auto has = [&] (std::initializer_list<const char*> ws) { for (auto* w : ws) if (hay.contains (w)) return true; return false; };
    auto add = [&] (const char* c) { if (std::find (caps.begin(), caps.end(), c) == caps.end()) caps.push_back (c); };
    if (has ({ "eq", "equal", "filter" })) add ("eq");
    if (has ({ "dynamic eq", "dyn eq", "soothe", "resonance" })) add ("dynamic_eq");
    if (has ({ "comp", "dynamics", "leveler", "leveller", "1176", "la-2a", "la2a" })) add ("compressor");
    if (has ({ "de-ess", "deess", "de ess", "sibil" })) add ("deesser");
    if (has ({ "limit", "maximi", "loud" })) add ("limiter");
    if (has ({ "reverb", "verb", "room", "hall", "plate" })) add ("reverb");
    if (has ({ "delay", "echo" })) add ("delay");
    if (has ({ "satur", "tape", "tube", "distort", "drive", "warm" })) add ("saturation");
    if (has ({ "gate", "expander" })) add ("gate");
    if (has ({ "stereo", "imager", "width", "wider" })) add ("stereo");
    if (has ({ "chorus", "flang", "phaser", "modulat" })) add ("modulation");
    if (has ({ "pitch", "tune", "autotune" })) add ("pitch");
    // parameter concepts add evidence
    int eqish = 0, compish = 0;
    for (auto& p : e.params)
    {
        if (p.mappedConcept == "frequency" || p.mappedConcept == "q") ++eqish;
        if (p.mappedConcept == "threshold" || p.mappedConcept == "ratio" || p.mappedConcept == "attack" || p.mappedConcept == "release") ++compish;
    }
    if (eqish >= 3) add ("eq");
    if (compish >= 3) add ("compressor");
    return caps;
}

std::pair<juce::String, float> CapabilityMapper::conceptFor (const juce::String& name, const juce::String& label)
{
    const auto n = name.toLowerCase();
    const auto l = label.toLowerCase();
    auto hasAny = [&] (std::initializer_list<const char*> ws) { for (auto* w : ws) if (n.contains (w)) return true; return false; };
    if (hasAny ({ "freq", "frequency" }) || l == "hz" || l == "khz") return { "frequency", l.contains ("hz") ? 0.95f : 0.8f };
    if (hasAny ({ "thresh" })) return { "threshold", 0.9f };
    if (hasAny ({ "ratio" })) return { "ratio", 0.9f };
    if (hasAny ({ "attack" })) return { "attack", 0.9f };
    if (hasAny ({ "release" })) return { "release", 0.9f };
    if (hasAny ({ "makeup", "make-up", "make up" })) return { "makeup_gain", 0.85f };
    if (hasAny ({ " q", "q ", "bandwidth", "width" }) || n == "q" || n.endsWith (" q")) return { n.contains ("stereo") ? "stereo_width" : "q", 0.7f };
    if (hasAny ({ "mix", "dry/wet", "wet", "blend" })) return { "mix", 0.85f };
    if (hasAny ({ "decay", "rt60", "reverb time" })) return { "decay", 0.85f };
    if (hasAny ({ "pre-delay", "predelay", "pre delay" })) return { "predelay", 0.9f };
    if (hasAny ({ "feedback" })) return { "feedback", 0.9f };
    if (hasAny ({ "drive", "saturation" })) return { "drive", 0.8f };
    if (hasAny ({ "ceiling", "out ceiling" })) return { "ceiling", 0.85f };
    if (hasAny ({ "gain" }) || l == "db") return { "gain", 0.7f };
    if (hasAny ({ "bypass" })) return { "bypass", 0.95f };
    return { {}, 0.f };
}

} // namespace nova::hosting
