#include "HostedRack.h"

namespace nova::hosting
{

namespace
{
// Prefer stereo (or mono) main buses and disable auxiliary inputs such as side-chains: NOVA
// inserts plugins as plain in-line effects.
void configureLayout (juce::AudioPluginInstance& p)
{
    for (auto set : { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono() })
    {
        auto layout = p.getBusesLayout();
        if (layout.outputBuses.isEmpty()) return;
        if (! layout.inputBuses.isEmpty())
        {
            layout.inputBuses.getReference (0) = set;
            for (int i = 1; i < layout.inputBuses.size(); ++i) layout.inputBuses.getReference (i) = juce::AudioChannelSet::disabled();
        }
        layout.outputBuses.getReference (0) = set;
        for (int i = 1; i < layout.outputBuses.size(); ++i) layout.outputBuses.getReference (i) = juce::AudioChannelSet::disabled();
        if (p.setBusesLayout (layout)) return;
    }
}
} // namespace

HostedRack::HostedRack() = default;

HostedRack::~HostedRack()
{
    *alive = false;
    for (int i = 0; i < kMaxSlots; ++i) live[(size_t) i].store (nullptr);
    waitForAudioToLeave();
    for (auto& o : owned)
        if (o != nullptr && o->instance != nullptr)
            o->instance->releaseResources();
}

juce::AudioPluginFormatManager& HostedRack::getFormatManager()
{
    if (formats == nullptr)
    {
        formats = std::make_unique<juce::AudioPluginFormatManager>();
        juce::addDefaultFormatsToManager (*formats);
    }
    return *formats;
}

std::optional<juce::PluginDescription> HostedRack::descriptionFor (const PluginEntry& e, juce::AudioPluginFormatManager& fm)
{
    if (e.descriptionXml.isNotEmpty())
        if (auto xml = juce::parseXML (e.descriptionXml))
        {
            juce::PluginDescription d;
            if (d.loadFromXml (*xml)) return d;
        }
    // Older databases: ask the format for the plugin's types again (reads the plugin's own metadata).
    for (int i = 0; i < fm.getNumFormats(); ++i)
    {
        auto* f = fm.getFormat (i);
        if (f->getName() != e.format) continue;
        juce::OwnedArray<juce::PluginDescription> types;
        f->findAllTypesForFile (types, e.fileOrIdentifier);
        for (auto* d : types)
            if (d->name == e.name || types.size() == 1) return *d;
    }
    return std::nullopt;
}

//==============================================================================
void HostedRack::prepare (double sr, int block)
{
    sampleRate = sr;
    maxBlock = std::max (1, block);
    for (auto& o : owned)
        if (o != nullptr)
        {
            o->instance->releaseResources();
            prepareSlot (*o);
        }
    recomputeLatency();
}

void HostedRack::prepareSlot (Slot& s)
{
    auto& p = *s.instance;
    p.setNonRealtime (false);
    p.prepareToPlay (sampleRate, maxBlock);
    s.inChannels = std::max (0, p.getTotalNumInputChannels());
    s.outChannels = std::max (1, p.getTotalNumOutputChannels());
    const int bufCh = std::max (2, std::max (s.inChannels, s.outChannels));
    s.scratch.setSize (bufCh, maxBlock, false, true, false);
    s.midi.ensureSize (256);
    s.latency = juce::jlimit (0, kMaxLatency, p.getLatencySamples());
    s.bypassDelay.prepare (s.latency + 4, dsp::kMaxChannels);
    s.wet.setTime (15.f, sampleRate);
    s.wet.snap (s.bypassed.load() ? 0.f : 1.f);
}

bool HostedRack::waitForAudioToLeave()
{
    const uint32_t seq = audioSeq.load();
    if ((seq & 1u) == 0) return true;   // not inside process(): the next call sees the new pointers
    const auto start = juce::Time::getMillisecondCounter();
    while (audioSeq.load() == seq)
    {
        if (juce::Time::getMillisecondCounter() - start > 2000) return false;
        juce::Thread::sleep (1);
    }
    return true;
}

void HostedRack::publish (int slot, std::unique_ptr<Slot> s)
{
    auto old = std::move (owned[(size_t) slot]);
    owned[(size_t) slot] = std::move (s);
    live[(size_t) slot].store (owned[(size_t) slot].get());
    if (old != nullptr)
    {
        if (waitForAudioToLeave())
            old->instance->releaseResources();
        else
            old.release();   // the audio thread never came back: leak rather than free under it
    }
    int n = 0;
    for (auto& o : owned) n += o != nullptr ? 1 : 0;
    activeCount.store (n);
    recomputeLatency();
    notify();
}

void HostedRack::recomputeLatency()
{
    int total = 0;
    for (auto& o : owned)
        if (o != nullptr) total += o->latency;
    clamped = total > kMaxLatency;
    totalLatency.store (std::min (total, kMaxLatency));
}

void HostedRack::notify()
{
    if (onChanged) onChanged();
}

//==============================================================================
bool HostedRack::install (int slot, std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& entryId, juce::String& error)
{
    if (slot < 0 || slot >= kMaxSlots) { error = "invalid slot"; return false; }
    if (instance == nullptr) { error = "plugin could not be created"; return false; }
    const auto desc = instance->getPluginDescription();
    if (desc.isInstrument) { error = "instruments cannot be used as insert effects"; return false; }
    configureLayout (*instance);
    if (instance->getTotalNumOutputChannels() <= 0) { error = "the plugin has no audio outputs"; return false; }
    auto s = std::make_unique<Slot>();
    s->instance = std::move (instance);
    s->entryId = entryId;
    s->desc = desc;
    s->hasEditor = s->instance->hasEditor();
    prepareSlot (*s);
    publish (slot, std::move (s));
    return true;
}

bool HostedRack::loadSync (int slot, const juce::PluginDescription& d, const juce::String& entryId, juce::String& error)
{
    auto inst = getFormatManager().createPluginInstance (d, sampleRate, maxBlock, error);
    return install (slot, std::move (inst), entryId, error);
}

void HostedRack::loadAsync (int slot, const PluginEntry& entry, LoadCallback done)
{
    auto& fm = getFormatManager();
    const auto d = descriptionFor (entry, fm);
    if (! d)
    {
        if (done) done (false, "could not read the plugin's description - rescan plugins");
        return;
    }
    std::weak_ptr<bool> weak = alive;
    const auto id = entry.id;
    fm.createPluginInstanceAsync (*d, sampleRate, maxBlock,
        [this, weak, slot, id, done] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err)
        {
            if (weak.expired() || ! *weak.lock()) return;
            juce::String e = err;
            const bool ok = install (slot, std::move (inst), id, e);
            if (done) done (ok, ok ? juce::String() : (e.isNotEmpty() ? e : juce::String ("failed to load")));
        });
}

void HostedRack::remove (int slot)
{
    if (slot >= 0 && slot < kMaxSlots && owned[(size_t) slot] != nullptr)
        publish (slot, nullptr);
}

void HostedRack::clear()
{
    for (int i = 0; i < kMaxSlots; ++i) remove (i);
}

void HostedRack::setBypassed (int slot, bool b)
{
    if (slot >= 0 && slot < kMaxSlots && owned[(size_t) slot] != nullptr)
    {
        owned[(size_t) slot]->bypassed.store (b);
        notify();
    }
}

bool HostedRack::isBypassed (int slot) const
{
    return slot >= 0 && slot < kMaxSlots && owned[(size_t) slot] != nullptr && owned[(size_t) slot]->bypassed.load();
}

int HostedRack::firstFreeSlot() const
{
    for (int i = 0; i < kMaxSlots; ++i)
        if (owned[(size_t) i] == nullptr) return i;
    return -1;
}

juce::AudioPluginInstance* HostedRack::getInstance (int slot) const
{
    return slot >= 0 && slot < kMaxSlots && owned[(size_t) slot] != nullptr ? owned[(size_t) slot]->instance.get() : nullptr;
}

bool HostedRack::refreshLatency()
{
    bool changed = false;
    for (int i = 0; i < kMaxSlots; ++i)
    {
        auto& o = owned[(size_t) i];
        if (o == nullptr) continue;
        const int l = juce::jlimit (0, kMaxLatency, o->instance->getLatencySamples());
        if (l == o->latency) continue;
        // Take the slot out of the audio path while its delay line is resized.
        live[(size_t) i].store (nullptr);
        if (! waitForAudioToLeave()) { live[(size_t) i].store (o.get()); continue; }
        o->latency = l;
        o->bypassDelay.prepare (l + 4, dsp::kMaxChannels);
        live[(size_t) i].store (o.get());
        changed = true;
    }
    if (changed)
    {
        recomputeLatency();
        notify();
    }
    return changed;
}

std::vector<RackSlotInfo> HostedRack::getSlots() const
{
    std::vector<RackSlotInfo> out;
    for (int i = 0; i < kMaxSlots; ++i)
    {
        auto& o = owned[(size_t) i];
        if (o == nullptr) continue;
        RackSlotInfo r;
        r.slot = i;
        r.entryId = o->entryId;
        r.name = o->desc.name;
        r.manufacturer = o->desc.manufacturerName;
        r.format = o->desc.pluginFormatName;
        r.bypassed = o->bypassed.load();
        r.hasEditor = o->hasEditor;
        r.latencySamples = o->latency;
        r.numParams = o->instance->getParameters().size();
        out.push_back (r);
    }
    return out;
}

std::vector<RackParamState> HostedRack::getParameters (int slot, int maxParams) const
{
    std::vector<RackParamState> out;
    auto* inst = getInstance (slot);
    if (inst == nullptr) return out;
    int idx = 0;
    for (auto* p : inst->getParameters())
    {
        if (idx >= maxParams) break;
        RackParamState s;
        s.index = idx++;
        s.name = p->getName (64);
        s.label = p->getLabel();
        s.value = p->getValue();
        s.defaultValue = p->getDefaultValue();
        s.text = p->getText (s.value, 32);
        const int steps = p->getNumSteps();
        s.numSteps = steps > 0 && steps < 0x7fffffff && steps != juce::AudioProcessor::getDefaultNumParameterSteps() ? steps : 0;
        s.automatable = p->isAutomatable();
        out.push_back (s);
    }
    return out;
}

bool HostedRack::setParameter (int slot, int paramIndex, float v)
{
    auto* inst = getInstance (slot);
    if (inst == nullptr) return false;
    auto params = inst->getParameters();
    if (paramIndex < 0 || paramIndex >= params.size()) return false;
    auto* p = params[paramIndex];
    const float n = juce::jlimit (0.f, 1.f, v);
    if (std::abs (p->getValue() - n) < 1.0e-6f) return true;
    p->beginChangeGesture();
    p->setValueNotifyingHost (n);
    p->endChangeGesture();
    return true;
}

std::vector<RackSlotView> HostedRack::view (const PluginCatalog* catalog, int maxParamsPerSlot) const
{
    std::vector<RackSlotView> out;
    for (auto& info : getSlots())
    {
        RackSlotView v;
        v.info = info;
        if (catalog != nullptr)
            if (auto* e = catalog->find (info.entryId)) v.capabilities = e->capabilities;
        if (v.capabilities.empty())
        {
            PluginEntry tmp;
            tmp.name = info.name;
            if (auto* inst = getInstance (info.slot)) tmp.category = inst->getPluginDescription().category;
            v.capabilities = CapabilityMapper::capabilitiesFor (tmp);
        }
        v.params = getParameters (info.slot, maxParamsPerSlot);
        out.push_back (std::move (v));
    }
    return out;
}

RackSnapshot HostedRack::captureSnapshot() const
{
    RackSnapshot snap;
    for (int i = 0; i < kMaxSlots; ++i)
    {
        auto& o = owned[(size_t) i];
        if (o == nullptr) continue;
        RackSnapshot::SlotState st;
        st.slot = i;
        st.entryId = o->entryId;
        st.bypassed = o->bypassed.load();
        for (auto* p : o->instance->getParameters()) st.values.push_back (p->getValue());
        snap.slots.push_back (std::move (st));
    }
    return snap;
}

int HostedRack::restoreSnapshot (const RackSnapshot& snap)
{
    int changed = 0;
    for (auto& st : snap.slots)
    {
        auto* inst = getInstance (st.slot);
        if (inst == nullptr || owned[(size_t) st.slot]->entryId != st.entryId) continue;
        auto params = inst->getParameters();
        for (int i = 0; i < std::min ((int) st.values.size(), params.size()); ++i)
            if (std::abs (params[i]->getValue() - st.values[(size_t) i]) > 1.0e-6f)
            {
                setParameter (st.slot, i, st.values[(size_t) i]);
                ++changed;
            }
        if (isBypassed (st.slot) != st.bypassed) { setBypassed (st.slot, st.bypassed); ++changed; }
    }
    return changed;
}

void HostedRack::applyChanges (const std::vector<RackParamChange>& params, const std::vector<RackBypassChange>& bypass)
{
    for (auto& c : params)
        if (getInstance (c.slot) != nullptr && owned[(size_t) c.slot]->entryId == c.entryId)
            setParameter (c.slot, c.index, c.after);
    for (auto& b : bypass)
        if (getInstance (b.slot) != nullptr && owned[(size_t) b.slot]->entryId == b.entryId)
            setBypassed (b.slot, b.bypassed);
}

//==============================================================================
juce::ValueTree HostedRack::saveState() const
{
    juce::ValueTree rack ("HOSTED_RACK");
    for (int i = 0; i < kMaxSlots; ++i)
    {
        auto& o = owned[(size_t) i];
        if (o == nullptr) continue;
        juce::ValueTree s ("SLOT");
        s.setProperty ("index", i, nullptr);
        s.setProperty ("entry", o->entryId, nullptr);
        s.setProperty ("name", o->desc.name, nullptr);
        s.setProperty ("bypassed", o->bypassed.load(), nullptr);
        if (auto xml = o->desc.createXml())
            s.setProperty ("desc", xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader()), nullptr);
        juce::MemoryBlock mb;
        o->instance->getStateInformation (mb);
        s.setProperty ("state", mb.toBase64Encoding(), nullptr);
        rack.appendChild (s, nullptr);
    }
    return rack;
}

juce::StringArray HostedRack::restoreState (const juce::ValueTree& state, const PluginCatalog* catalog)
{
    juce::StringArray problems;
    clear();
    if (! state.isValid()) return problems;
    for (const auto& s : state)
    {
        const int slot = s.getProperty ("index", -1);
        const auto entryId = s.getProperty ("entry").toString();
        const auto name = s.getProperty ("name").toString();
        if (slot < 0 || slot >= kMaxSlots) continue;
        std::optional<juce::PluginDescription> desc;
        if (auto xml = juce::parseXML (s.getProperty ("desc").toString()))
        {
            juce::PluginDescription d;
            if (d.loadFromXml (*xml)) desc = d;
        }
        if (! desc && catalog != nullptr)
            if (auto* e = catalog->find (entryId)) desc = descriptionFor (*e, getFormatManager());
        if (! desc) { problems.add (name + ": not found on this computer"); continue; }
        juce::String err;
        if (! loadSync (slot, *desc, entryId, err)) { problems.add (name + ": " + err); continue; }
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (s.getProperty ("state").toString()) && mb.getSize() > 0)
            owned[(size_t) slot]->instance->setStateInformation (mb.getData(), (int) mb.getSize());
        setBypassed (slot, (bool) s.getProperty ("bypassed", false));
        refreshLatency();   // restoring state can change a plugin's latency
    }
    return problems;
}

//==============================================================================
void HostedRack::process (float* const* ch, int numCh, int n) noexcept
{
    audioSeq.fetch_add (1);   // odd while inside
    for (int si = 0; si < kMaxSlots; ++si)
    {
        Slot* s = live[(size_t) si].load();
        if (s == nullptr) continue;
        auto& buf = s->scratch;
        if (n > buf.getNumSamples()) continue;
        const int bufCh = buf.getNumChannels();
        for (int c = 0; c < bufCh; ++c)
        {
            auto* d = buf.getWritePointer (c);
            if (c < numCh) std::memcpy (d, ch[c], sizeof (float) * (size_t) n);
            else if (c == 1 && numCh == 1) std::memcpy (d, ch[0], sizeof (float) * (size_t) n);   // mono track into a stereo plugin
            else juce::FloatVectorOperations::clear (d, n);
        }
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), bufCh, n);   // refers to scratch, no allocation
        s->midi.clear();
        s->instance->processBlock (view, s->midi);

        s->wet.setTarget (s->bypassed.load (std::memory_order_relaxed) ? 0.f : 1.f);
        const float* out0 = buf.getReadPointer (0);
        const float* out1 = s->outChannels >= 2 ? buf.getReadPointer (1) : out0;
        for (int i = 0; i < n; ++i)
        {
            const float w = s->wet.next();
            for (int c = 0; c < numCh; ++c)
            {
                s->bypassDelay.push (c, ch[c][i]);
                const float dry = s->bypassDelay.read (c, s->latency);
                float wet = numCh == 1 ? (s->outChannels >= 2 ? 0.5f * (out0[i] + out1[i]) : out0[i]) : (c == 0 ? out0[i] : out1[i]);
                if (! std::isfinite (wet)) wet = 0.f;   // a misbehaving plugin must never reach the DAW with NaN/inf
                ch[c][i] = dry + w * (wet - dry);
            }
            s->bypassDelay.advance();
        }
    }
    audioSeq.fetch_add (1);
}

} // namespace nova::hosting
