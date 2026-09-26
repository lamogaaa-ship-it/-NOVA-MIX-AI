#pragma once

// Rule-based natural-language understanding for the OFFLINE engineer (used when no cloud LLM is
// configured or reachable, and as a deterministic fallback). English + Arabic (MSA and
// Egyptian colloquial). The cloud engineer does its own language understanding; this parser is
// deliberately conservative and only reports what it recognises.

#include <string>
#include <vector>

namespace nova::ai
{

struct Intent
{
    std::string id;                 // treatment id, or special: "reference_match", "undo", "redo", "explain", "reset", "listen", "revert"
    int direction = +1;
    float amount = 0.5f;
    bool preserveDynamics = false;
    bool avoidDullness = false;
    float targetLufs = 0.f;
    int division = -1, pattern = -1;
    std::vector<std::string> referenceDims;   // tone, dynamics, space, width, color, vocal, full
    float referenceInfluence = -1.f;
    std::string revertTarget;                 // highs, lows, reverb, compression, width, last
    std::string evidence;                     // the words that triggered it
};

struct ParsedRequest
{
    std::vector<Intent> intents;
    bool generalMix = false;        // "mix this vocal", "make it sound pro"
    bool masterRequest = false;     // "master the song"
    bool isQuestion = false;
    bool refersToPrevious = false;  // "no, now it's ...", "too much", "بقى"
    float intensity = 0.5f;
    std::string language;           // "en", "ar", "mixed"
    std::string styleHint;          // "modern pop", "rap", "rnb", "rock", "acoustic", ...

    bool has (const std::string& id) const;
    const Intent* find (const std::string& id) const;
};

ParsedRequest parseRequest (const std::string& utf8Text);

// Arabic/Latin normalisation used by the parser (exposed for tests)
std::string normaliseForMatching (const std::string& utf8Text);

} // namespace nova::ai
