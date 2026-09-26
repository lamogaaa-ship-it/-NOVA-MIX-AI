#pragma once

// LEVEL 2 - user taste profile: small, bounded, context-dependent biases learned from explicit
//            user behaviour (corrections, feedback, A/B keeps, undos). No model weights change.
// LEVEL 3 - experience retrieval: numeric feature vectors + request + strategy + outcome of past
//            situations, retrieved by similarity. Audio is never stored.
//
// Both live in the user's application-data folder and can be disabled / deleted from Settings.

#include <juce_core/juce_core.h>

#include "../ai/Treatments.h"
#include "../analysis/Features.h"
#include "SessionMemory.h"

#include <mutex>

namespace nova
{

struct TasteContext
{
    std::string mode, source, style;
    std::string key() const { return mode + "/" + source + "/" + (style.empty() ? "any" : style); }
};

class PreferenceStore
{
public:
    explicit PreferenceStore (juce::File file);

    void setEnabled (bool e);
    bool isEnabled() const;

    // Learning signals. `dimension` in: brightness (dB), compression (scale), reverb (scale),
    // deess (scale), width (scale), loudness (LU).
    void observe (const TasteContext& ctx, const std::string& dimension, float delta, const std::string& why);
    // Derive signals from a user's reaction to an action (complaint text, feedback, undo, A/B).
    void learnFromReaction (const TasteContext& ctx, const ActionRecord& action, const std::string& reactionText, int feedback, bool undone);

    ai::PreferenceBias getBias (const TasteContext& ctx) const;
    juce::var toJson() const;
    int totalObservations() const;
    void clear();
    juce::File getFile() const { return file; }

private:
    struct Dim { float value = 0.f; int count = 0; };
    mutable std::mutex lock;
    juce::File file;
    bool enabled = true;
    std::map<std::string, std::map<std::string, Dim>> contexts;   // context key -> dim -> value
    std::vector<juce::String> journal;                            // human-readable "why" log (last 50)

    void load();
    void save() const;
};

struct Experience
{
    juce::int64 timeMs = 0;
    std::string mode, source, style, request;
    std::vector<std::string> treatments;
    std::vector<float> featureVector;
    juce::var outcome;          // measured before/after deltas
    int accepted = 0;           // +1 accepted, -1 rejected, 0 unknown
    int actionId = 0;
};

class ExperienceStore
{
public:
    explicit ExperienceStore (juce::File file);
    void setEnabled (bool e);
    bool isEnabled() const;

    static std::vector<float> featureVector (const analysis::AudioFeatures& f);
    void add (Experience e);
    void setAccepted (int actionId, int accepted);
    std::vector<std::pair<float, Experience>> similar (const std::vector<float>& fv, const std::string& mode, int k = 3) const;
    int size() const;
    void clear();
    juce::File getFile() const { return file; }

private:
    mutable std::mutex lock;
    juce::File file;
    bool enabled = true;
    std::vector<Experience> items;
    void load();
    void saveAll() const;
};

juce::File novaUserDataDirectory();

} // namespace nova
