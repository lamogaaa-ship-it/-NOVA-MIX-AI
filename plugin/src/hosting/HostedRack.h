#pragma once

// Hosted third-party plugin rack. Up to kMaxSlots VST3 (and AU on macOS) plugins run in series
// after NOVA's built-in chain, inside NOVA's own latency-compensated signal path.
//
// Threading
//   * loading / removing / parameter edits: message thread
//   * process(): audio thread; lock-free and allocation-free. Slots are published through atomic
//     pointers; a replaced slot is destroyed on the message thread only after the audio thread has
//     provably left process() (sequence counter), so the audio thread never waits on anything.
//
// Latency: each slot reports its own latency; the rack total is added to NOVA's reported latency
// and to the dry path used by A/B, Delta and bypass. Bypassing a slot crossfades to a copy of its
// input delayed by that slot's latency, so timing never jumps.
//
// Only plugins the user has legitimately installed are loaded; licensing or copy protection is
// never touched.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginCatalog.h"
#include "../dsp/DspCommon.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace nova::hosting
{

struct RackSlotInfo
{
    int slot = -1;
    juce::String entryId, name, manufacturer, format;
    bool bypassed = false, hasEditor = false;
    int latencySamples = 0, numParams = 0;
};

struct RackParamState
{
    int index = -1;
    juce::String name, label, text;
    float value = 0.f, defaultValue = 0.f;     // normalised 0..1
    int numSteps = 0;
    bool automatable = true;
};

// Plain-data views used by the AI engineer and undo snapshots (no plugin pointers).
struct RackSlotView
{
    RackSlotInfo info;
    std::vector<juce::String> capabilities;
    std::vector<RackParamState> params;
};

struct RackParamChange
{
    int slot = -1, index = -1;
    juce::String entryId, paramName;
    float before = 0.f, after = 0.f;         // normalised
};

struct RackBypassChange
{
    int slot = -1;
    juce::String entryId;
    bool bypassed = false;
};

struct RackSnapshot
{
    struct SlotState { int slot = -1; juce::String entryId; bool bypassed = false; std::vector<float> values; };
    std::vector<SlotState> slots;
    bool empty() const noexcept { return slots.empty(); }
};

class HostedRack
{
public:
    static constexpr int kMaxSlots = 4;
    static constexpr int kMaxLatency = 32768;   // samples; larger reports are clamped and flagged

    HostedRack();
    ~HostedRack();

    // Called from the processor's prepareToPlay (audio stopped).
    void prepare (double sampleRate, int maxBlock);

    //==============================================================================
    // Message thread
    using LoadCallback = std::function<void (bool ok, const juce::String& error)>;
    void loadAsync (int slot, const PluginEntry& entry, LoadCallback done);
    bool loadSync (int slot, const juce::PluginDescription& d, const juce::String& entryId, juce::String& error);
    // loadedFrom: the description the instance was created from (saved for session recall; an
    // instance's own getPluginDescription() is not always enough to find it again).
    bool install (int slot, std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& entryId, juce::String& error,
                  const juce::PluginDescription* loadedFrom = nullptr);
    void remove (int slot);
    void clear();
    void setBypassed (int slot, bool bypassed);
    bool isBypassed (int slot) const;
    std::vector<RackSlotInfo> getSlots() const;
    int firstFreeSlot() const;
    juce::AudioPluginInstance* getInstance (int slot) const;
    bool refreshLatency();                       // re-reads plugin latencies; true when the total changed
    int getLatencySamples() const noexcept { return totalLatency.load (std::memory_order_acquire); }
    bool latencyWasClamped() const noexcept { return clamped; }

    std::vector<RackParamState> getParameters (int slot, int maxParams = 256) const;
    // Sets a normalised value (0..1) through the plugin's own parameter object (host notified).
    bool setParameter (int slot, int paramIndex, float normalisedValue);

    std::vector<RackSlotView> view (const PluginCatalog* catalog, int maxParamsPerSlot = 256) const;
    RackSnapshot captureSnapshot() const;
    // Restores parameter values / bypass of slots that still hold the same plugin. Returns the
    // number of values changed.
    int restoreSnapshot (const RackSnapshot& snap);
    void applyChanges (const std::vector<RackParamChange>& params, const std::vector<RackBypassChange>& bypass);

    // Any thread; off the message thread it returns the copy taken at the last message-thread
    // save / change (call refreshStateCache() periodically from the message thread).
    juce::ValueTree saveState() const;
    void refreshStateCache() { if (onMessageThread()) (void) saveState(); }
    // Re-instantiates saved slots (message thread). Plugins that are no longer installed are
    // reported through the returned list and skipped - the rest of the session still loads.
    juce::StringArray restoreState (const juce::ValueTree& state, const PluginCatalog* catalog);

    std::function<void()> onChanged;             // message thread: slots / latency changed

    juce::AudioPluginFormatManager& getFormatManager();
    static std::optional<juce::PluginDescription> descriptionFor (const PluginEntry& e, juce::AudioPluginFormatManager& fm);

    //==============================================================================
    // Audio thread. ch holds numCh (1 or 2) channels of n <= maxBlock samples, processed in place.
    void process (float* const* ch, int numCh, int n) noexcept;
    bool hasActiveSlots() const noexcept { return activeCount.load (std::memory_order_acquire) > 0; }

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::String entryId;
        juce::PluginDescription desc;
        juce::AudioBuffer<float> scratch;
        juce::MidiBuffer midi;
        dsp::DelayLine bypassDelay;
        dsp::Smoother wet;
        std::atomic<bool> bypassed { false };
        bool hasEditor = false;                 // cached at load (asking a hosted VST3 creates its view)
        int latency = 0, inChannels = 2, outChannels = 2;
    };

    std::array<std::atomic<Slot*>, kMaxSlots> live {};
    std::array<std::unique_ptr<Slot>, kMaxSlots> owned;
    std::atomic<uint32_t> audioSeq { 0 };
    std::atomic<int> totalLatency { 0 }, activeCount { 0 };
    double sampleRate = 48000.0;
    int maxBlock = 512;
    bool clamped = false;
    std::unique_ptr<juce::AudioPluginFormatManager> formats;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    mutable std::mutex cacheLock;
    mutable juce::ValueTree stateCache { "HOSTED_RACK" };

    static bool onMessageThread();
    void prepareAll();
    void prepareSlot (Slot& s);
    void publish (int slot, std::unique_ptr<Slot> s);
    bool waitForAudioToLeave();
    void recomputeLatency();
    void notify();

    JUCE_DECLARE_NON_COPYABLE (HostedRack)
};

} // namespace nova::hosting
