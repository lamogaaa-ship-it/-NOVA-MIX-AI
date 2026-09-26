// nova-plugin-scanner: scans installed VST3 (and AU on macOS) plugins in its OWN process and writes
// NOVA's plugin database. A plugin that crashes during scanning only kills this process; the plugin
// being scanned is recorded ("dead man's pedal") and skipped on the next run.
//
//   nova-plugin-scanner --output <db.json> [--path <dir>]... [--no-instantiate] [--limit N]

#include <juce_audio_utils/juce_audio_utils.h>

#include "hosting/PluginCatalog.h"

using namespace nova::hosting;

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    auto opt = [&] (const juce::String& k) { const int i = args.indexOf (k); return i >= 0 && i + 1 < args.size() ? args[i + 1] : juce::String(); };
    const auto outPath = opt ("--output");
    if (outPath.isEmpty()) { std::cerr << "usage: nova-plugin-scanner --output db.json [--path dir]... [--no-instantiate]\n"; return 2; }
    const juce::File db (outPath);
    const bool instantiate = ! args.contains ("--no-instantiate");
    const int limit = opt ("--limit").isNotEmpty() ? opt ("--limit").getIntValue() : 100000;

    // dead man's pedal: a plugin left "pending" by a previous run crashed the scanner -> block it
    const auto pending = db.getSiblingFile ("scan_pending.txt");
    const auto blockFile = db.getSiblingFile ("scan_blocklist.txt");
    juce::StringArray blocked;
    if (blockFile.existsAsFile()) blockFile.readLines (blocked);
    if (pending.existsAsFile())
    {
        const auto crashed = pending.loadFileAsString().trim();
        if (crashed.isNotEmpty() && ! blocked.contains (crashed))
        {
            blocked.add (crashed);
            blockFile.appendText (crashed + "\n");
            std::cerr << "previous scan crashed on " << crashed << " - it is now blocked\n";
        }
        pending.deleteFile();
    }

    juce::AudioPluginFormatManager fm;
    juce::addDefaultFormatsToManager (fm);

    PluginCatalog catalog (db);
    std::map<juce::String, PluginEntry> byId;
    // keep entries from earlier (partial) runs
    for (auto& e : catalog.search ({}, {}, 100000)) byId[e.id] = e;

    int scanned = 0;
    for (int fi = 0; fi < fm.getNumFormats(); ++fi)
    {
        auto* format = fm.getFormat (fi);
        const auto formatName = format->getName();
        if (formatName != "VST3" && formatName != "AudioUnit") continue;
        juce::FileSearchPath paths = format->getDefaultLocationsToSearch();
        for (int i = 0; i < args.size(); ++i)
            if (args[i] == "--path" && i + 1 < args.size()) paths.add (juce::File (args[i + 1]));
        const auto ids = format->searchPathsForPlugins (paths, true, false);
        std::cout << formatName << ": " << ids.size() << " candidate(s)\n";
        for (auto& id : ids)
        {
            if (scanned >= limit) break;
            if (blocked.contains (id)) { std::cout << "  skip (blocked) " << id << "\n"; continue; }
            pending.replaceWithText (id);
            juce::OwnedArray<juce::PluginDescription> types;
            format->findAllTypesForFile (types, id);
            for (auto* d : types)
            {
                PluginEntry e;
                e.id = formatName + ":" + d->fileOrIdentifier + ":" + juce::String::toHexString (d->uniqueId);
                e.name = d->name;
                e.manufacturer = d->manufacturerName;
                e.format = formatName;
                e.version = d->version;
                e.category = d->category;
                e.fileOrIdentifier = d->fileOrIdentifier;
                e.numInputs = d->numInputChannels;
                e.numOutputs = d->numOutputChannels;
                e.isInstrument = d->isInstrument;
                e.scannedAtMs = juce::Time::currentTimeMillis();
                if (instantiate && ! d->isInstrument)
                {
                    juce::String err;
                    if (auto inst = fm.createPluginInstance (*d, 48000.0, 512, err))
                    {
                        inst->prepareToPlay (48000.0, 512);
                        e.loadedOk = true;
                        e.latencySamples = inst->getLatencySamples();
                        e.hasEditor = inst->hasEditor();
                        e.numInputs = inst->getTotalNumInputChannels();
                        e.numOutputs = inst->getTotalNumOutputChannels();
                        int idx = 0;
                        for (auto* p : inst->getParameters())
                        {
                            ParamInfo pi;
                            pi.index = idx++;
                            pi.name = p->getName (64);
                            pi.label = p->getLabel();
                            pi.defaultValue = p->getDefaultValue();
                            pi.defaultText = p->getText (p->getDefaultValue(), 32);
                            pi.automatable = p->isAutomatable();
                            pi.numSteps = p->getNumSteps() < 0x7fffffff ? p->getNumSteps() : 0;
                            auto [mapped, conf] = CapabilityMapper::conceptFor (pi.name, pi.label);
                            pi.mappedConcept = mapped;
                            pi.conceptConfidence = conf;
                            e.params.push_back (pi);
                            if (e.params.size() >= 512) break;
                        }
                        inst->releaseResources();
                    }
                    else
                    {
                        e.loadedOk = false;
                        e.loadError = err.isNotEmpty() ? err : "failed to instantiate";
                    }
                }
                else e.loadedOk = ! instantiate;
                e.capabilities = CapabilityMapper::capabilitiesFor (e);
                std::cout << "  " << (e.loadedOk ? "ok  " : "FAIL") << " " << e.name << " (" << e.manufacturer << ") " << (int) e.params.size() << " params\n";
                byId[e.id] = e;
            }
            pending.deleteFile();
            ++scanned;
            // save incrementally so a later crash keeps everything scanned so far
            std::vector<PluginEntry> all;
            for (auto& [k, v] : byId) all.push_back (v);
            catalog.replaceAll (all);
            catalog.save();
        }
    }
    std::vector<PluginEntry> all;
    for (auto& [k, v] : byId) all.push_back (v);
    catalog.replaceAll (all);
    const bool ok = catalog.save();
    std::cout << "database: " << db.getFullPathName() << " (" << all.size() << " entries)\n";
    return ok ? 0 : 1;
}
