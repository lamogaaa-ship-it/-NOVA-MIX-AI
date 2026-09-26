#pragma once

// Closed-loop treatment routines ("what a senior engineer would do, then check").
//
// Every routine:
//   1. reads evidence from the analysis of the original audio (never a fixed preset)
//   2. proposes processing on the *candidate* settings
//   3. renders the candidate offline through the exact realtime chain
//   4. measures the targeted problem on the same time regions, loudness matched
//   5. refines until the target is met without violating its protection constraints
//      (e.g. "reduce harsh moments by 4 dB but lose < 1 dB of presence elsewhere")
//
// Results carry the before/after metrics and plain-language + engineer explanations built
// from those measured numbers.

#include "Measure.h"
#include "../analysis/Semantic.h"

#include <atomic>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nova::ai
{

// Context-dependent user taste applied as a bounded bias (see learning/PreferenceStore).
struct PreferenceBias
{
    float brightnessDb = 0.f;     // + user tends to prefer brighter results
    float compressionScale = 1.f; // > 1 user likes more control
    float reverbScale = 1.f;
    float deessScale = 1.f;
    float widthScale = 1.f;
    float loudnessOffsetLu = 0.f;
    int evidenceCount = 0;        // how many accepted decisions this bias is based on
};

struct TreatmentRequest
{
    std::string id;               // see treatmentCatalogue()
    float amount = 0.5f;          // 0.15 slight .. 1.0 strong
    int direction = +1;           // +1 more / -1 less (for bidirectional treatments)
    bool preserveDynamics = false;
    bool avoidDullness = false;   // "without making it dull"
    float targetLufs = 0.f;       // loudness treatments (0 = choose)
    int division = -1;            // tempo effects (-1 = choose)
    int pattern = -1;
    std::string note;             // free text from the planner/LLM for the log
};

struct ParamChange { int index = -1; float before = 0, after = 0; };

struct TreatmentResult
{
    std::string id;
    bool applied = false;
    std::string skipReason;
    ChainSettings settings;
    Metrics before, after;
    int iterations = 0;
    std::vector<std::string> log;
    std::string simple, engineer;
    float confidence = 0.f;
    std::vector<ParamChange> changes;
    std::vector<std::string> warnings;
};

struct TreatmentInfo { const char* id; const char* description; bool bidirectional; };
const std::vector<TreatmentInfo>& treatmentCatalogue();
bool isKnownTreatment (const std::string& id);

std::vector<ParamChange> diffSettings (const ChainSettings& a, const ChainSettings& b);
std::string describeChange (const ParamChange& c);

class TreatmentSession
{
public:
    TreatmentSession (std::shared_ptr<const juce::AudioBuffer<float>> input, double sampleRate,
                      const analysis::AudioFeatures& inputFeatures, analysis::WorkMode mode,
                      const ChainSettings& start, const ChainOrder& order, const TransportSnapshot& transport);

    TreatmentResult run (const TreatmentRequest& request);
    void accept (const TreatmentResult& r) { if (r.applied) { candidate = r.settings; candidateMetricsValid = false; } }

    Metrics evaluate (const ChainSettings& s, bool withSpace = false);
    const Metrics& getCandidateMetrics (bool withSpace = false);
    const Metrics& getInputMetrics();       // original audio, nothing processed

    ChainSettings candidate;
    const ChainSettings startSettings;
    PreferenceBias bias;
    std::atomic<bool>* cancelFlag = nullptr;
    std::string language = "en";      // "en" or "ar": language of the SIMPLE explanations
    std::string say (std::string en, std::string ar) const { return language == "ar" ? std::move (ar) : std::move (en); }

    const analysis::AudioFeatures& features() const noexcept { return inputFeatures; }
    const analysis::SemanticProfile& semantic() const noexcept { return sem; }
    const Probe& probe() const noexcept { return prb; }
    analysis::WorkMode mode() const noexcept { return workMode; }
    const TransportSnapshot& transport() const noexcept { return trans; }
    int getRenderCount() const noexcept { return renders; }
    double getRenderMs() const noexcept { return renderMs; }
    double sampleRate() const noexcept { return sr; }
    std::shared_ptr<const juce::AudioBuffer<float>> inputAudio() const { return input; }
    const ChainOrder& chainOrder() const noexcept { return order; }
    void setChainOrder (const ChainOrder& o) { order = o; candidateMetricsValid = false; }

private:
    std::shared_ptr<const juce::AudioBuffer<float>> input;
    double sr;
    analysis::AudioFeatures inputFeatures;
    analysis::SemanticProfile sem;
    Probe prb;
    analysis::WorkMode workMode;
    ChainOrder order;
    TransportSnapshot trans;
    Metrics inputMetrics, candMetrics;
    bool inputMetricsValid = false, candidateMetricsValid = false, candMetricsHasSpace = false;
    int renders = 0;
    double renderMs = 0;

    bool cancelled() const { return cancelFlag != nullptr && cancelFlag->load(); }

    TreatmentResult levelConsistency (const TreatmentRequest&);
    TreatmentResult harshness (const TreatmentRequest&);
    TreatmentResult sibilance (const TreatmentRequest&);
    TreatmentResult clarity (const TreatmentRequest&);
    TreatmentResult presence (const TreatmentRequest&);
    TreatmentResult air (const TreatmentRequest&);
    TreatmentResult brightness (const TreatmentRequest&);
    TreatmentResult warmth (const TreatmentRequest&);
    TreatmentResult mud (const TreatmentRequest&);
    TreatmentResult resonance (const TreatmentRequest&);
    TreatmentResult rumble (const TreatmentRequest&);
    TreatmentResult plosives (const TreatmentRequest&);
    TreatmentResult space (const TreatmentRequest&);
    TreatmentResult width (const TreatmentRequest&);
    TreatmentResult loudness (const TreatmentRequest&);
    TreatmentResult saturation (const TreatmentRequest&);
    TreatmentResult rhythmicGate (const TreatmentRequest&);
    TreatmentResult delayThrow (const TreatmentRequest&);
    TreatmentResult punch (const TreatmentRequest&);
    TreatmentResult body (const TreatmentRequest&);
    TreatmentResult matchLoudnessTo (const ChainSettings& s, float targetLufs, TreatmentResult& r);

    TreatmentResult begin (const TreatmentRequest& req);
    void finish (TreatmentResult& r, const ChainSettings& s, const Metrics& after);
};

} // namespace nova::ai
