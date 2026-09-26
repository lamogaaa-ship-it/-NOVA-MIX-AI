#pragma once

// Knowledge about third-party plugins installed on this machine (built by the out-of-process
// scanner, see tools/scanner). Only legitimately installed plugins are listed; nothing about
// licensing or copy protection is touched.

#include <juce_core/juce_core.h>

#include <mutex>
#include <string>
#include <vector>

namespace nova::hosting
{

struct ParamInfo
{
    int index = -1;
    juce::String name, label, mappedConcept;   // concept from the capability mapper (frequency, gain, threshold, ...)
    float defaultValue = 0;              // normalised 0..1
    juce::String defaultText;
    bool automatable = true;
    int numSteps = 0;
    float conceptConfidence = 0;
};

struct PluginEntry
{
    juce::String id;                     // stable identifier (format + file + uid)
    juce::String name, manufacturer, format, version, category, fileOrIdentifier;
    int numInputs = 0, numOutputs = 0, latencySamples = 0;
    bool hasEditor = false, loadedOk = false, isInstrument = false;
    juce::String loadError;
    std::vector<juce::String> capabilities;    // eq, dynamic_eq, compressor, deesser, reverb, delay, saturation, limiter, ...
    std::vector<ParamInfo> params;
    juce::int64 scannedAtMs = 0;
    juce::String descriptionXml;         // juce::PluginDescription as XML (storage only, never sent to the AI)
};

class PluginCatalog
{
public:
    explicit PluginCatalog (juce::File databaseFile);
    void reload();
    bool save() const;
    void replaceAll (std::vector<PluginEntry> entries);

    std::vector<PluginEntry> search (const juce::String& capability, const juce::String& query, int maxResults = 20) const;
    const PluginEntry* find (const juce::String& id) const;
    int size() const;
    juce::File getFile() const { return file; }
    juce::int64 lastScanMs() const;

    static juce::var entryToJson (const PluginEntry& e, bool withParams, bool forStorage = false);
    static PluginEntry entryFromJson (const juce::var& v);

private:
    mutable std::mutex lock;
    juce::File file;
    std::vector<PluginEntry> entries;
};

// Maps parameter names/units to engineering concepts and plugins to capabilities using only
// metadata the plugin itself reports (names, categories, labels) - never guessed IDs.
struct CapabilityMapper
{
    static std::vector<juce::String> capabilitiesFor (const PluginEntry& e);
    static std::pair<juce::String, float> conceptFor (const juce::String& paramName, const juce::String& label);
};

} // namespace nova::hosting
