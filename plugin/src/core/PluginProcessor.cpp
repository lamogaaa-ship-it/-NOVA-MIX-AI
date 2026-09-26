#include "PluginProcessor.h"
#include "NovaEngine.h"
#include "../ui/PluginEditor.h"

namespace nova
{

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout NovaAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < P::Count; ++i)
    {
        const auto& spec = kParams[(size_t) i];
        const juce::ParameterID pid { spec.id, 1 };
        switch (spec.kind)
        {
            case ParamKind::Float:
            {
                juce::NormalisableRange<float> range (
                    spec.minValue, spec.maxValue,
                    [i] (float, float, float n) { return denormalise (kParams[(size_t) i], n); },
                    [i] (float, float, float v) { return normalise (kParams[(size_t) i], v); },
                    [i] (float, float, float v) { return clampToSpec (kParams[(size_t) i], v); });
                const auto unit = spec.unit;
                auto attrs = juce::AudioParameterFloatAttributes()
                    .withLabel (unitSuffix (spec.unit))
                    .withStringFromValueFunction ([unit] (float v, int)
                    {
                        if (unit == Unit::Hz)
                            return v >= 1000.f ? juce::String (v / 1000.f, 2) + " k" : juce::String (v, 0);
                        if (unit == Unit::Ratio || unit == Unit::Q || unit == Unit::Seconds)
                            return juce::String (v, 2);
                        return juce::String (v, 1);
                    });
                layout.add (std::make_unique<juce::AudioParameterFloat> (pid, spec.name, range, spec.defaultValue, attrs));
                break;
            }
            case ParamKind::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, spec.name, spec.defaultValue > 0.5f));
                break;
            case ParamKind::Choice:
            {
                juce::StringArray choices;
                choices.addTokens (spec.choices, "|", "");
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, spec.name, choices, (int) spec.defaultValue));
                break;
            }
        }
    }
    return layout;
}

NovaAudioProcessor::NovaAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    for (int i = 0; i < P::Count; ++i)
    {
        paramPtrs[(size_t) i] = apvts.getRawParameterValue (kParams[(size_t) i].id);
        paramObjects[(size_t) i] = apvts.getParameter (kParams[(size_t) i].id);
        jassert (paramPtrs[(size_t) i] != nullptr);
    }
    engine = std::make_unique<NovaEngine> (*this);
    rack.onChanged = [this]
    {
        const int total = getTotalLatency();
        if (total != getLatencySamples()) setLatencySamples (total);
    };
    rackWatch.fn = [this]
    {
        if (! rack.hasActiveSlots()) return;
        updateHostedLatency();
        if (++rackWatchTicks % 4 == 0) rack.refreshStateCache();   // for hosts that save state off the message thread
    };
    rackWatch.startTimer (500);
}

NovaAudioProcessor::~NovaAudioProcessor()
{
    *alive = false;
    rackWatch.stopTimer();
    rack.onChanged = nullptr;
    engine.reset();   // stop worker threads before the realtime objects go away
}

void NovaAudioProcessor::updateHostedLatency()
{
    rack.refreshLatency();
    const int total = getTotalLatency();
    if (total != getLatencySamples()) setLatencySamples (total);
}

void NovaAudioProcessor::restoreRack (const juce::ValueTree& rackState)
{
    auto problems = rack.restoreState (rackState, engine != nullptr ? &engine->getPluginCatalog() : nullptr);
    if (engine != nullptr)
        for (auto& p : problems)
            engine->postStatus ("Hosted plugin could not be restored - " + p + ". The rest of the session loaded normally.");
}

//==============================================================================
void NovaAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int block = std::max (1, std::min (samplesPerBlock, kMaxInternalBlock));
    chain.prepare (sampleRate, block);
    const int latency = chain.getLatencySamples();
    rack.prepare (sampleRate, kMaxInternalBlock);
    monitor.prepare (sampleRate, latency + rack.getLatencySamples(), kMaxInternalBlock, latency + hosting::HostedRack::kMaxLatency);
    dryScratch.setSize (2, kMaxInternalBlock, false, true, false);
    capture.prepare ((int) (sampleRate * 2.0) + kMaxInternalBlock);
    ctx = {};
    ctx.sampleRate = sampleRate;
    chainLatency.store (latency);
    setLatencySamples (latency + rack.getLatencySamples());
    preparedRate.store (sampleRate);
    if (engine != nullptr)
        engine->audioPrepared (sampleRate, samplesPerBlock);
}

void NovaAudioProcessor::releaseResources() {}

void NovaAudioProcessor::reset()
{
    chain.reset();
    monitor.reset();
}

bool NovaAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == out;
}

juce::AudioProcessorParameter* NovaAudioProcessor::getBypassParameter() const
{
    return paramObjects[(size_t) P::Bypass];
}

//==============================================================================
ChainSettings NovaAudioProcessor::getCurrentSettings() const noexcept
{
    ChainSettings s;
    for (int i = 0; i < P::Count; ++i)
        s.v[(size_t) i] = paramPtrs[(size_t) i]->load (std::memory_order_relaxed);
    return s;
}

void NovaAudioProcessor::setParameterValue (int index, float value)
{
    if (index < 0 || index >= P::Count) return;
    auto* p = paramObjects[(size_t) index];
    const float v = clampToSpec (kParams[(size_t) index], value);
    const float norm = p->convertTo0to1 (v);
    if (std::abs (p->getValue() - norm) > 1.0e-7f)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (norm);
        p->endChangeGesture();
    }
}

void NovaAudioProcessor::applySettings (const ChainSettings& s)
{
    for (int i = 0; i < P::Count; ++i)
    {
        if (i == P::Bypass || i == P::MonitorA || i == P::Delta)
            continue;   // monitoring state belongs to the user, never to snapshots / AI
        setParameterValue (i, s.v[(size_t) i]);
    }
}

TransportSnapshot NovaAudioProcessor::getTransport() const noexcept
{
    TransportSnapshot t;
    t.bpm = tBpm.load();
    t.ppq = tPpq.load();
    t.hasPpq = tHasPpq.load();
    t.isPlaying = tPlaying.load();
    t.hostProvidesTempo = tHostTempo.load();
    t.timeSigNum = tSigNum.load();
    t.timeSigDen = tSigDen.load();
    return t;
}

void NovaAudioProcessor::updateTransport() noexcept
{
    ctx.hasPpq = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) { ctx.bpm = std::clamp (*bpm, 20.0, 400.0); tHostTempo.store (true); }
            ctx.isPlaying = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition(); ppq && ctx.isPlaying)
            {
                ctx.ppqAtBlockStart = *ppq;
                ctx.hasPpq = true;
            }
            if (auto sig = pos->getTimeSignature()) { tSigNum.store (sig->numerator); tSigDen.store (sig->denominator); }
        }
    }
    tBpm.store (ctx.bpm);
    tPpq.store (ctx.ppqAtBlockStart);
    tHasPpq.store (ctx.hasPpq);
    tPlaying.store (ctx.isPlaying);
}

void NovaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer, false);
}

void NovaAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer, true);
}

void NovaAudioProcessor::processInternal (juce::AudioBuffer<float>& buffer, bool forceBypass) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    const auto startTicks = juce::Time::getHighResolutionTicks();

    const int numCh = std::min (2, std::min (getTotalNumInputChannels(), buffer.getNumChannels()));
    const int total = buffer.getNumSamples();
    for (int c = numCh; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, total);
    if (numCh <= 0 || total <= 0 || ! isPrepared())
        return;

    const ChainSettings s = getCurrentSettings();
    const ChainOrder order = getChainOrder();
    updateTransport();

    MonitorStage::Flags flags;
    flags.monitorA = s.on (P::MonitorA);
    flags.loudnessMatch = s.on (P::LoudMatch);
    flags.delta = s.on (P::Delta);
    flags.bypass = forceBypass || s.on (P::Bypass);

    float inPk[2] = { 0, 0 }, inSq[2] = { 0, 0 }, outPk[2] = { 0, 0 }, outSq[2] = { 0, 0 };
    const bool hosting = rack.hasActiveSlots();
    monitor.setLatency (chainLatency.load (std::memory_order_relaxed) + rack.getLatencySamples());

    for (int offset = 0; offset < total; offset += kMaxInternalBlock)
    {
        const int n = std::min (kMaxInternalBlock, total - offset);
        float* ch[2] = { buffer.getWritePointer (0, offset), numCh > 1 ? buffer.getWritePointer (1, offset) : nullptr };
        float* dry[2] = { dryScratch.getWritePointer (0), dryScratch.getWritePointer (1) };

        for (int c = 0; c < numCh; ++c)
        {
            std::memcpy (dry[c], ch[c], sizeof (float) * (size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const float v = ch[c][i];
                inPk[c] = std::max (inPk[c], std::abs (v));
                inSq[c] += v * v;
            }
        }

        chain.process (ch, numCh, n, s, order, ctx);
        if (hosting)
            rack.process (ch, numCh, n);
        if (ctx.hasPpq)
            ctx.ppqAtBlockStart += n * ctx.bpm / 60.0 / ctx.sampleRate;

        capture.push (dry[0], numCh > 1 ? dry[1] : nullptr, ch[0], numCh > 1 ? ch[1] : nullptr, n);

        const float* dryConst[2] = { dry[0], dry[1] };
        monitor.process (ch, dryConst, numCh, n, flags);

        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float v = ch[c][i];
                outPk[c] = std::max (outPk[c], std::abs (v));
                outSq[c] += v * v;
            }
    }

    // publish meters
    for (int c = 0; c < 2; ++c)
    {
        const int src = std::min (c, numCh - 1);
        meters.inPeak[c].store (std::max (meters.inPeak[c].load (std::memory_order_relaxed), inPk[src]), std::memory_order_relaxed);
        meters.outPeak[c].store (std::max (meters.outPeak[c].load (std::memory_order_relaxed), outPk[src]), std::memory_order_relaxed);
        meters.inRms[c].store (std::sqrt (inSq[src] / (float) total), std::memory_order_relaxed);
        meters.outRms[c].store (std::sqrt (outSq[src] / (float) total), std::memory_order_relaxed);
    }
    const auto& inst = chain.getInstant();
    meters.riderGainDb.store (inst.riderGainDb, std::memory_order_relaxed);
    meters.compGrDb.store (inst.compGrDb, std::memory_order_relaxed);
    meters.dessGrDb.store (inst.dessGrDb, std::memory_order_relaxed);
    meters.limGrDb.store (inst.limGrDb, std::memory_order_relaxed);
    for (int b = 0; b < kNumDynBands; ++b)
        meters.dynCutDb[b].store (inst.dynCutDb[(size_t) b], std::memory_order_relaxed);
    meters.matchGainDb.store (monitor.getMatchGainDb(), std::memory_order_relaxed);
    meters.dryLoudnessDb.store (monitor.getDryLoudnessDb() - 0.691f, std::memory_order_relaxed);
    meters.wetLoudnessDb.store (monitor.getWetLoudnessDb() - 0.691f, std::memory_order_relaxed);
    chain.getStats().reset();   // realtime path does not accumulate statistics
    meters.blocksProcessed.fetch_add (1, std::memory_order_relaxed);

    const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
    const double budget = (double) total / std::max (1.0, ctx.sampleRate);
    const float load = (float) (elapsed / budget);
    meters.callbackLoad.store (load, std::memory_order_relaxed);
    if (load > meters.maxCallbackLoad.load (std::memory_order_relaxed))
        meters.maxCallbackLoad.store (load, std::memory_order_relaxed);
}

//==============================================================================
void NovaAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("NOVA_STATE");
    root.setProperty ("version", JucePlugin_VersionString, nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    juce::ValueTree chainNode ("CHAIN");
    chainNode.setProperty ("order", (juce::int64) chainOrderPacked.load(), nullptr);
    root.appendChild (chainNode, nullptr);

    if (engine != nullptr)
        root.appendChild (engine->saveState(), nullptr);

    auto rackState = rack.saveState();
    if (rackState.getNumChildren() > 0)
        root.appendChild (rackState, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void NovaAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.isValid())
        return;

    // Accept both the full NOVA state and a bare APVTS tree (older / external presets)
    auto params = root.hasType (apvts.state.getType()) ? root : root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);

    auto chainNode = root.getChildWithName ("CHAIN");
    if (chainNode.isValid())
        chainOrderPacked.store (packChainOrder (unpackChainOrder ((uint64_t) (juce::int64) chainNode.getProperty ("order"))));

    if (engine != nullptr)
        engine->restoreState (root.getChildWithName ("SESSION"));

    // Hosted plugins must be created on the message thread.
    const auto rackState = root.getChildWithName ("HOSTED_RACK");
    if (rackState.isValid() || rack.hasActiveSlots())
    {
        if (juce::MessageManager::existsAndIsCurrentThread())
            restoreRack (rackState);
        else
        {
            std::weak_ptr<bool> weak = alive;
            juce::MessageManager::callAsync ([this, weak, rackState]
            {
                if (auto a = weak.lock(); a != nullptr && *a) restoreRack (rackState);
            });
        }
    }
}

juce::AudioProcessorEditor* NovaAudioProcessor::createEditor()
{
    return new NovaEditor (*this);
}

} // namespace nova

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new nova::NovaAudioProcessor();
}
