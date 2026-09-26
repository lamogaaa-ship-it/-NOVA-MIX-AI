#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "../ai/Treatments.h"
#include "../analysis/Semantic.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace nova::reference
{

struct ReferenceProfile
{
    std::string name, path;
    double sampleRate = 0, durationSec = 0;
    analysis::AudioFeatures features;
    analysis::SemanticProfile semantic;
    bool isFullMix = false;
    juce::int64 analysedAtMs = 0;
    std::vector<float> embedding;      // optional (companion neural model)
    std::string embeddingModel;
};

// Decode (WAV/AIFF/FLAC/OGG/MP3; CoreAudio formats on macOS) and analyse a reference file.
std::shared_ptr<ReferenceProfile> loadReference (const juce::File& file, analysis::WorkMode mode, juce::String& error);

struct MatchDimensions
{
    bool tone = true, dynamics = false, space = false, width = false, color = false, loudness = false, vocal = false;
    static MatchDimensions fromNames (const std::vector<std::string>& names);
    std::vector<std::string> names() const;
};

struct DimensionReport
{
    std::string dimension;
    float current = 0, reference = 0, after = 0;    // the dimension's key metric
    std::string unit;
    float distanceBefore = 0, distanceAfter = 0;     // |target - value| (loudness matched)
    std::string summary;
    float confidence = 0;
    bool attempted = false, improved = false;
    std::string limitation;
};

struct MatchResult
{
    bool applied = false;
    ChainSettings settings;
    std::vector<DimensionReport> reports;
    std::array<float, kNumToneMatchBands> toneGains {};
    std::string simple, engineer;
    std::vector<std::string> warnings;
    int iterations = 0;
    float influence = 0.75f;
};

// Perceptual differences between the current audio and the reference (what an engineer would
// say: "reference is brighter, more compressed, drier, wider..."), loudness independent.
juce::var compareToReference (const analysis::AudioFeatures& current, const ReferenceProfile& ref);

// Iterative optimisation toward the reference on the selected dimensions. Tone is written into
// the realtime Tone Match module whose "Reference Influence" parameter stays live for the user.
MatchResult matchReference (ai::TreatmentSession& session, const ReferenceProfile& ref, const MatchDimensions& dims, float influence);

// Holds the current reference for the plugin instance (thread safe).
class ReferenceManager
{
public:
    enum class State { Empty, Loading, Ready, Error };

    void loadAsync (const juce::File& file, analysis::WorkMode mode, std::function<void()> onDone);
    void clear();
    void shutdown() { pool.removeAllJobs (true, 10000); }
    State getState() const noexcept { return state.load(); }
    std::shared_ptr<const ReferenceProfile> get() const;
    juce::String getError() const;
    juce::String getPath() const;
    void setLastMatch (std::shared_ptr<const MatchResult> m);
    std::shared_ptr<const MatchResult> getLastMatch() const;
    void setDimensions (const MatchDimensions& d);
    MatchDimensions getDimensions() const;

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& v, analysis::WorkMode mode);

private:
    std::atomic<State> state { State::Empty };
    mutable std::mutex lock;
    std::shared_ptr<const ReferenceProfile> profile;
    std::shared_ptr<const MatchResult> lastMatch;
    juce::String error, path;
    MatchDimensions dims;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1).withThreadName ("NOVA Reference") };
};

} // namespace nova::reference
