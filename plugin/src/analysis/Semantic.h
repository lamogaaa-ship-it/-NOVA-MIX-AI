#pragma once

#include "Features.h"

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace nova::analysis
{

enum class WorkMode { Vocal = 0, Mix = 1, Master = 2 };
const char* workModeName (WorkMode m);

// One engineering observation, always backed by a measured value.
struct Observation
{
    std::string id;           // machine id, e.g. "sibilance"
    std::string title;        // short human text
    float severity = 0;       // 0..1
    float confidence = 0;     // 0..1 - how much the measurement supports the claim
    float freqHz = 0;         // relevant frequency when applicable
    std::string evidence;     // the numbers behind the claim
};

struct CharacterTag { std::string tag; float confidence = 0; std::string evidence; };

struct SemanticProfile
{
    SourceType source = SourceType::Unknown;
    float sourceConfidence = 0;
    std::vector<CharacterTag> character;
    std::vector<Observation> problems;          // sorted by severity * confidence
    std::vector<std::string> recommendations;   // treatment ids (e.g. "dynamic_harshness_control")
    std::string dynamicsSummary, spaceSummary;
    bool enoughAudio = false;

    const Observation* find (const std::string& id) const;
};

// Interpret measured features. Thresholds are engineering heuristics (see
// docs/AUDIO_INTELLIGENCE.md) and confidences are deliberately conservative where a claim
// depends on genre/voice priors rather than on event-level evidence.
SemanticProfile describe (const AudioFeatures& f, WorkMode mode);

// Compact JSON for the AI engineer / UI / companion. Numbers are rounded to keep tokens low.
juce::var featuresToJson (const AudioFeatures& f, bool includeEvents = true);
juce::var semanticToJson (const SemanticProfile& s);

} // namespace nova::analysis
