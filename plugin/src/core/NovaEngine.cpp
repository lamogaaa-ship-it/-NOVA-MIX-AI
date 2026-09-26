#include "NovaEngine.h"
#include "PluginProcessor.h"
#include "../ai/IntentParser.h"
#include "../net/CompanionClient.h"

namespace nova
{

NovaEngine::NovaEngine (NovaAudioProcessor& p)
    : juce::Thread ("NOVA Engineer"), processor (p), analysisEngine (p)
{
    const auto dir = novaUserDataDirectory();
    const auto settings = SettingsStore::shared().get();
    preferences = std::make_unique<PreferenceStore> (dir.getChildFile ("taste_profile.json"));
    preferences->setEnabled (settings.learningEnabled);
    experiences = std::make_unique<ExperienceStore> (dir.getChildFile ("experiences.jsonl"));
    experiences->setEnabled (settings.experienceEnabled);
    pluginCatalog = std::make_unique<hosting::PluginCatalog> (dir.getChildFile ("plugin_database.json"));
    companion = std::make_unique<CompanionClient> (settings.companionUrl);

    analysisEngine.setResultCallback ([this] (std::shared_ptr<const analysis::AnalysisResult> r)
    {
        // A finished LISTEN session: tell the user what was actually measured.
        if (r == nullptr || busy.load()) return;
        ai::ChatMessage m;
        m.role = ai::ChatMessage::Role::Assistant;
        m.text = ai::OfflineEngineer::analysisSummary (*r, false);
        m.engineerText = ai::OfflineEngineer::analysisSummary (*r, true);
        m.engine = "analysis";
        conversation.add (m);
    });
    analysisEngine.startEngine();
    startThread (juce::Thread::Priority::normal);
}

NovaEngine::~NovaEngine()
{
    cancelFlag.store (true);
    references.shutdown();          // reference jobs post into the conversation: finish them first
    signalThreadShouldExit();
    queueEvent.signal();
    stopThread (8000);
    analysisEngine.setResultCallback ({});
    analysisEngine.stopEngine();
}

void NovaEngine::audioPrepared (double sampleRate, int)
{
    analysisEngine.audioPrepared (sampleRate);
}

void NovaEngine::setSettings (const EngineerSettings& s)
{
    SettingsStore::shared().set (s);
    preferences->setEnabled (s.learningEnabled);
    experiences->setEnabled (s.experienceEnabled);
    companion->setBaseUrl (s.companionUrl);
}

void NovaEngine::setWorkMode (analysis::WorkMode m)
{
    analysisEngine.setWorkMode (m);
}

//==============================================================================
void NovaEngine::setPhase (ai::AgentPhase p, const juce::String& detail)
{
    {
        std::lock_guard<std::mutex> g (miscLock);
        phaseDetail = detail;
    }
    phase.store (p);
    phaseChangedMs.store (juce::Time::currentTimeMillis());
}

juce::String NovaEngine::getPhaseDetail() const
{
    std::lock_guard<std::mutex> g (miscLock);
    return phaseDetail;
}

void NovaEngine::submitRequest (const juce::String& text, Source source)
{
    if (text.trim().isEmpty()) return;
    ai::ChatMessage m;
    m.role = ai::ChatMessage::Role::User;
    m.text = text.trim();
    m.engine = source == Source::Voice ? "voice" : "typed";
    conversation.add (m);
    {
        std::lock_guard<std::mutex> g (queueLock);
        queue.push_back ({ text.trim(), source, false });
    }
    busy.store (true);
    queueEvent.signal();
}

void NovaEngine::matchReferenceNow()
{
    const auto dims = references.getDimensions().names();
    juce::String dimText;
    for (auto& d : dims) dimText << (dimText.isEmpty() ? "" : " and ") << juce::String (d);
    submitRequest ("Match the reference on " + dimText, Source::Button);
}

void NovaEngine::cancelCurrent()
{
    cancelFlag.store (true);
    {
        std::lock_guard<std::mutex> g (queueLock);
        queue.clear();
    }
    analysisEngine.cancelListening();
}

bool NovaEngine::waitUntilIdle (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (juce::Time::getMillisecondCounter() < end)
    {
        {
            std::lock_guard<std::mutex> g (queueLock);
            if (queue.empty() && ! busy.load()) return true;
        }
        juce::Thread::sleep (20);
    }
    return false;
}

TasteContext NovaEngine::tasteContext (const std::string& source) const
{
    TasteContext t;
    t.mode = analysis::workModeName (analysisEngine.getWorkMode());
    t.source = source.empty() ? "unknown" : source;
    t.style = getStyle().toStdString();
    return t;
}

//==============================================================================
void NovaEngine::run()
{
    juce::int64 lastHealth = 0;
    while (! threadShouldExit())
    {
        queueEvent.wait (1000);
        if (threadShouldExit()) break;

        const auto now = juce::Time::currentTimeMillis();
        if (now - lastHealth > 8000 && getSettings().companionEnabled)
        {
            companion->checkHealth();
            lastHealth = now;
        }

        for (;;)
        {
            Request r;
            {
                std::lock_guard<std::mutex> g (queueLock);
                if (queue.empty()) break;
                r = queue.front();
                queue.pop_front();
            }
            busy.store (true);
            cancelFlag.store (false);
            try
            {
                handleRequest (r);
            }
            catch (const std::exception& e)
            {
                postAssistant ("Something went wrong while working on that - your audio and settings were not changed.", e.what(), {}, {}, "error", 0, false, true);
                setPhase (ai::AgentPhase::Error, e.what());
            }
            if (threadShouldExit()) return;
        }
        busy.store (false);
        if (phase.load() != ai::AgentPhase::Idle && juce::Time::currentTimeMillis() - phaseChangedMs.load() > 2500)
            setPhase (ai::AgentPhase::Idle);
    }
}

std::shared_ptr<const analysis::AnalysisResult> NovaEngine::ensureWorkingAudio()
{
    auto latest = analysisEngine.getLatestResult();
    if (latest != nullptr && latest->inputFeatures.valid)
        return latest;

    // Nothing listened yet: listen now if audio is flowing (the orb shows real LISTENING state).
    const auto t0 = juce::Time::getMillisecondCounter();
    if (analysisEngine.getListenState() != analysis::AnalysisEngine::ListenState::Listening)
        analysisEngine.beginListening (12.0);
    setPhase (ai::AgentPhase::Listening, "play your track - NOVA is listening");
    bool announced = false;
    while (juce::Time::getMillisecondCounter() - t0 < 45000 && ! threadShouldExit() && ! cancelFlag.load())
    {
        const auto st = analysisEngine.getListenState();
        if (st == analysis::AnalysisEngine::ListenState::Analyzing) setPhase (ai::AgentPhase::Analyzing, "measuring");
        if (st == analysis::AnalysisEngine::ListenState::Ready)
        {
            auto r = analysisEngine.getLatestResult();
            if (r != nullptr && r->inputFeatures.valid) return r;
            break;
        }
        if (st == analysis::AnalysisEngine::ListenState::Idle) break;
        if (! announced && juce::Time::getMillisecondCounter() - t0 > 2500 && ! analysisEngine.isReceivingAudio())
        {
            ai::ChatMessage m;
            m.role = ai::ChatMessage::Role::Status;
            m.text = "I'm listening, but no audio is reaching NOVA yet - press play in your DAW.";
            conversation.add (m);
            announced = true;
        }
        juce::Thread::sleep (100);
    }
    // enough recent audio for a partial analysis?
    if (analysisEngine.getListenProgressSeconds() >= 3.0)
    {
        analysisEngine.finishListening();
        const auto t1 = juce::Time::getMillisecondCounter();
        while (juce::Time::getMillisecondCounter() - t1 < 10000 && analysisEngine.getListenState() != analysis::AnalysisEngine::ListenState::Ready)
            juce::Thread::sleep (50);
        if (auto r = analysisEngine.getLatestResult(); r != nullptr && r->inputFeatures.valid) return r;
    }
    analysisEngine.cancelListening();
    return nullptr;
}

void NovaEngine::handleRequest (const Request& req)
{
    const std::string text = req.text.toStdString();
    const auto settings = getSettings();
    const auto parsed = ai::parseRequest (text);

    // learning signal: a correction of the previous action
    if (parsed.refersToPrevious)
        if (auto last = memory.last())
            preferences->learnFromReaction (tasteContext (last->source), *last, text, 0, false);

    if (parsed.has ("undo") && parsed.intents.size() == 1)
    {
        if (undo()) postAssistant ("Undone - you're back to the previous version.", {}, {}, {}, "offline", 0, false);
        else postAssistant ("There's nothing to undo yet.", {}, {}, {}, "offline", 0, false);
        setPhase (ai::AgentPhase::Idle);
        return;
    }

    ai::EngineerContext ctx;
    const bool needsAudio = ! (parsed.isQuestion && parsed.intents.empty());
    setPhase (ai::AgentPhase::Analyzing, "preparing");
    ctx.audio = needsAudio ? ensureWorkingAudio() : analysisEngine.getLatestResult();
    ctx.current = processor.getCurrentSettings();
    ctx.order = processor.getChainOrder();
    ctx.transport = processor.getTransport();
    ctx.mode = analysisEngine.getWorkMode();
    ctx.reference = references.get();
    ctx.referenceDims = references.getDimensions();
    ctx.referenceInfluence = ctx.current[P::TmAmount] / 100.f;
    ctx.memory = &memory;
    ctx.styleHint = parsed.styleHint.empty() ? getStyle().toStdString() : parsed.styleHint;
    const std::string sourceName = ctx.audio != nullptr ? analysis::sourceTypeName (ctx.audio->semantic.source) : "unknown";
    ctx.bias = preferences->getBias (tasteContext (sourceName));
    if (ctx.bias.evidenceCount > 0)
        ctx.userPreferenceSummary = juce::JSON::toString (preferences->toJson()["contexts"][juce::Identifier (tasteContext (sourceName).key())], true);
    if (ctx.audio != nullptr && ctx.audio->inputFeatures.valid)
    {
        const auto fv = ExperienceStore::featureVector (ctx.audio->inputFeatures);
        for (auto& [sim, e] : experiences->similar (fv, analysis::workModeName (ctx.mode), 3))
            if (sim > 0.9f)
                ctx.experienceSummary << "similarity " << juce::String (sim, 2) << ": \"" << juce::String (e.request) << "\" -> "
                                      << juce::String (e.treatments.empty() ? "" : e.treatments.front()) << (e.accepted > 0 ? " (accepted)" : "") << "\n";
    }
    hosting::RackSnapshot rackBefore;
    runOnMessageThread ([&]
    {
        auto& rack = processor.getHostedRack();
        if (rack.hasActiveSlots())
        {
            ctx.rack = rack.view (pluginCatalog.get());
            rackBefore = rack.captureSnapshot();
        }
    });
    ctx.cancel = &cancelFlag;
    ctx.onStatus = [this] (ai::AgentPhase p, const std::string& d) { setPhase (p, juce::String (d)); };

    // choose the engine
    ai::EngineerOutcome out;
    bool usedCloud = false;
    const auto provider = settings.effectiveProvider();
    if (provider == "anthropic" || provider == "nova_cloud")
    {
        std::shared_ptr<ai::HttpTransport> transport;
        { std::lock_guard<std::mutex> g (miscLock); transport = transportOverride; }
        if (transport == nullptr) transport = std::make_shared<ai::JuceHttpTransport>();
        auto client = provider == "anthropic"
            ? std::make_shared<ai::LLMClient> (ai::LLMClient::Kind::AnthropicDirect, settings.anthropicBaseUrl, settings.effectiveApiKey(), transport)
            : std::make_shared<ai::LLMClient> (ai::LLMClient::Kind::NovaCloud, settings.cloudUrl, settings.cloudToken, transport);
        ai::CloudEngineer cloud (client, settings, pluginCatalog.get());
        setPhase (ai::AgentPhase::Thinking, "asking " + client->describe());
        out = cloud.handle (text, ctx, llmHistory);
        usedCloud = true;
        if (! out.ok && out.reply == "__cloud_error__")
        {
            const auto why = juce::String (out.replyEngineer);
            ai::ChatMessage st;
            st.role = ai::ChatMessage::Role::Status;
            st.text = "The cloud engineer is unavailable (" + why + ") - using NOVA's offline engineer instead.";
            conversation.add (st);
            usedCloud = false;
            out = ai::OfflineEngineer::handle (text, ctx);
        }
    }
    else
        out = ai::OfflineEngineer::handle (text, ctx);

    if (out.undoRequested)
    {
        if (undo()) postAssistant (out.reply, {}, {}, {}, "offline", 0, false);
        setPhase (ai::AgentPhase::Idle);
        return;
    }

    int actionId = 0;
    juce::StringArray changes, warnings;
    for (auto& w : out.warnings) warnings.add (w);
    if (out.changed && ! cancelFlag.load())
    {
        // snapshot for undo, then apply the verified candidate
        Snapshot before;
        before.label = "Before: " + text;
        before.settings = ctx.current;
        before.order = ctx.order;
        before.rack = rackBefore;
        ActionRecord rec;
        rec.request = text;
        rec.engine = usedCloud ? out.engine : "offline";
        rec.treatments = out.treatments;
        rec.changes = ai::diffSettings (ctx.current, out.settings);
        rec.simple = out.reply;
        rec.engineer = out.replyEngineer;
        rec.mode = analysis::workModeName (ctx.mode);
        rec.source = sourceName;
        actionId = memory.add (rec);
        before.actionId = actionId;
        snapshots.push (before);
        applyToProcessor (out.settings, out.order, nullptr, &out.rackChanges, &out.rackBypass);
        for (auto& c : rec.changes) changes.add (ai::describeChange (c));
        for (auto& rc : out.rackChanges)
        {
            juce::String pluginName = "Rack slot " + juce::String (rc.slot + 1);
            for (auto& sv : ctx.rack)
                if (sv.info.slot == rc.slot) pluginName = sv.info.name;
            changes.add (pluginName + ": " + rc.paramName + " " + juce::String (rc.before, 2) + " -> " + juce::String (rc.after, 2));
        }
        for (auto& rb : out.rackBypass)
            changes.add ("Rack slot " + juce::String (rb.slot + 1) + (rb.bypassed ? " bypassed" : " enabled"));
        if (out.orderChanged) changes.add ("Chain order changed");
        if (out.match != nullptr) references.setLastMatch (out.match);

        if (ctx.audio != nullptr)
        {
            Experience e;
            e.mode = rec.mode; e.source = rec.source; e.style = ctx.styleHint; e.request = text;
            e.treatments = out.treatments;
            e.featureVector = ExperienceStore::featureVector (ctx.audio->inputFeatures);
            if (out.hasMetrics) e.outcome = ai::metricsDeltaJson (out.before, out.after);
            e.actionId = actionId;
            experiences->add (e);
        }
    }
    else if (cancelFlag.load())
    {
        postAssistant ("Stopped - nothing was changed.", {}, {}, {}, "offline", 0, false);
        setPhase (ai::AgentPhase::Idle);
        return;
    }

    const juce::String engineName = usedCloud ? juce::String (out.engine) : juce::String ("offline");
    postAssistant (juce::String::fromUTF8 (out.reply.c_str()), juce::String::fromUTF8 (out.replyEngineer.c_str()), changes, warnings, engineName, actionId, out.changed);
    // Spoken request -> spoken answer (the simple explanation only), when the companion can speak.
    if (req.source == Source::Voice && settings.speakReplies && companion != nullptr && companion->lastHealth().tts)
    {
        const auto spoken = juce::String::fromUTF8 (out.reply.c_str());
        bool arabic = false;
        for (auto c : spoken) if (c >= 0x0600 && c <= 0x06FF) { arabic = true; break; }
        companion->speak (spoken, arabic ? "ar" : "en");
    }
    setPhase (out.changed ? ai::AgentPhase::Complete : ai::AgentPhase::Idle);
}

void NovaEngine::postAssistant (const juce::String& text, const juce::String& eng, const juce::StringArray& changes, const juce::StringArray& warnings,
                                const juce::String& engine, int actionId, bool canUndo, bool isError)
{
    ai::ChatMessage m;
    m.role = isError ? ai::ChatMessage::Role::Error : ai::ChatMessage::Role::Assistant;
    m.text = text;
    m.engineerText = eng;
    m.changes = changes;
    m.warnings = warnings;
    m.engine = engine;
    m.actionId = actionId;
    m.canUndo = canUndo;
    conversation.add (m);
}

void NovaEngine::postStatus (const juce::String& text)
{
    ai::ChatMessage m;
    m.role = ai::ChatMessage::Role::Status;
    m.text = text;
    conversation.add (m);
}

void NovaEngine::runOnMessageThread (std::function<void()> fn)
{
    if (applyDirectly || juce::MessageManager::existsAndIsCurrentThread() || juce::MessageManager::getInstanceWithoutCreating() == nullptr)
    {
        fn();
        return;
    }
    struct Job { std::function<void()> fn; juce::WaitableEvent done; std::atomic<bool> claimed { false }; };
    auto job = std::make_shared<Job>();
    job->fn = std::move (fn);
    juce::MessageManager::callAsync ([job] { if (! job->claimed.exchange (true)) job->fn(); job->done.signal(); });
    if (! job->done.wait (3000))
    {
        if (! job->claimed.exchange (true)) job->fn();   // no message loop pumping (offline render host)
        else job->done.wait (-1);                         // already running on the message thread
    }
}

void NovaEngine::applyToProcessor (const ChainSettings& s, const ChainOrder& order, const hosting::RackSnapshot* rack,
                                   const std::vector<hosting::RackParamChange>* rackChanges, const std::vector<hosting::RackBypassChange>* rackBypass)
{
    runOnMessageThread ([&]
    {
        processor.applySettings (s);
        processor.setChainOrder (order);
        auto& r = processor.getHostedRack();
        if (rack != nullptr) r.restoreSnapshot (*rack);
        if (rackChanges != nullptr || rackBypass != nullptr)
            r.applyChanges (rackChanges != nullptr ? *rackChanges : std::vector<hosting::RackParamChange> {},
                            rackBypass != nullptr ? *rackBypass : std::vector<hosting::RackBypassChange> {});
    });
}

bool NovaEngine::undo()
{
    Snapshot cur;
    cur.settings = processor.getCurrentSettings();
    cur.order = processor.getChainOrder();
    runOnMessageThread ([&] { cur.rack = processor.getHostedRack().captureSnapshot(); });
    auto s = snapshots.undo (cur);
    if (! s) return false;
    applyToProcessor (s->settings, s->order, &s->rack);
    if (s->actionId > 0)
    {
        memory.markUndone (s->actionId, true);
        experiences->setAccepted (s->actionId, -1);
        if (auto a = memory.get (s->actionId))
            preferences->learnFromReaction (tasteContext (a->source), *a, {}, 0, true);
    }
    return true;
}

bool NovaEngine::redo()
{
    Snapshot cur;
    cur.settings = processor.getCurrentSettings();
    cur.order = processor.getChainOrder();
    runOnMessageThread ([&] { cur.rack = processor.getHostedRack().captureSnapshot(); });
    auto s = snapshots.redo (cur);
    if (! s) return false;
    applyToProcessor (s->settings, s->order, &s->rack);
    if (s->actionId > 0) memory.markUndone (s->actionId, false);
    return true;
}

void NovaEngine::giveFeedback (int messageId, int value)
{
    conversation.setFeedback (messageId, value);
    for (auto& m : conversation.messages())
        if (m.id == messageId && m.actionId > 0)
        {
            memory.setFeedback (m.actionId, value);
            experiences->setAccepted (m.actionId, value);
            if (auto a = memory.get (m.actionId))
                preferences->learnFromReaction (tasteContext (a->source), *a, {}, value, false);
        }
}

//==============================================================================
juce::StringArray NovaEngine::suggestions() const
{
    juce::StringArray s;
    const auto latest = analysisEngine.getLatestResult();
    const bool master = analysisEngine.getWorkMode() == analysis::WorkMode::Master;
    if (latest != nullptr)
    {
        for (auto& p : latest->semantic.problems)
        {
            if (p.confidence < 0.45f || s.size() >= 2) continue;
            if (p.id == "harshness") s.addIfNotAlreadyThere ("Tame the harsh notes without making it dull");
            else if (p.id == "sibilance") s.addIfNotAlreadyThere ("The S sounds are too sharp");
            else if (p.id == "inconsistent_level" || p.id == "word_level_spikes") s.addIfNotAlreadyThere ("Make the level more consistent");
            else if (p.id == "low_mid_congestion" || p.id == "boxiness") s.addIfNotAlreadyThere ("Clean up the muddy low-mids");
            else if (p.id == "lack_of_presence") s.addIfNotAlreadyThere ("Bring the vocal forward");
            else if (p.id == "lack_of_air") s.addIfNotAlreadyThere ("Add some air on top");
            else if (p.id == "rumble") s.addIfNotAlreadyThere ("Remove the low rumble");
            else if (p.id == "plosives") s.addIfNotAlreadyThere ("Fix the P pops");
            else if (p.id == "roomy_recording") s.addIfNotAlreadyThere ("Bring the vocal closer");
            else if (p.id == "true_peak_over" || p.id == "over_limited") s.addIfNotAlreadyThere ("Make it loud but keep the punch");
        }
    }
    if (master)
    {
        s.addIfNotAlreadyThere ("Master this for streaming (-14 LUFS)");
        s.addIfNotAlreadyThere ("Make it commercially loud but keep the punch");
    }
    else
    {
        s.addIfNotAlreadyThere ("Make the vocal brighter and clearer");
        s.addIfNotAlreadyThere ("More warmth and analog feel");
    }
    if (references.getState() == reference::ReferenceManager::State::Ready)
        s.addIfNotAlreadyThere (master ? "Match this reference master" : "Match this reference vocal");
    s.addIfNotAlreadyThere (master ? "Master the song but don't crush the dynamics" : "Make it sound like a modern pop record");
    while (s.size() > 4) s.remove (s.size() - 1);
    return s;
}

bool NovaEngine::isCloudEngineActive() const
{
    const auto p = getSettings().effectiveProvider();
    return p == "anthropic" || p == "nova_cloud";
}

juce::String NovaEngine::engineLabel() const
{
    const auto s = getSettings();
    const auto p = s.effectiveProvider();
    if (p == "anthropic") return "Claude (" + s.model + ")";
    if (p == "nova_cloud") return "NOVA Cloud (" + s.model + ")";
    return "Offline engineer";
}

//==============================================================================
void NovaEngine::loadReference (const juce::File& f)
{
    references.loadAsync (f, analysisEngine.getWorkMode(), [this]
    {
        ai::ChatMessage m;
        m.engine = "analysis";
        if (references.getState() == reference::ReferenceManager::State::Ready)
        {
            auto ref = references.get();
            m.role = ai::ChatMessage::Role::Assistant;
            m.text = "Reference loaded: " + juce::String (ref->name) + " (" + juce::String (ref->durationSec, 0) + " s, "
                     + juce::String (ref->features.integratedLufs, 1) + " LUFS" + (ref->isFullMix ? ", full mix" : "") + ").";
            if (auto latest = analysisEngine.getLatestResult(); latest != nullptr && latest->inputFeatures.valid)
            {
                const auto cmp = reference::compareToReference (latest->inputFeatures, *ref);
                juce::StringArray st;
                if (auto* arr = cmp["statements"].getArray()) for (auto& x : *arr) st.add (x.toString());
                if (! st.isEmpty()) m.engineerText = "Compared with what I heard: " + st.joinIntoString ("; ") + ".";
            }
        }
        else
        {
            m.role = ai::ChatMessage::Role::Error;
            m.text = "Couldn't load the reference: " + references.getError();
        }
        conversation.add (m);
    });
}

void NovaEngine::clearReference() { references.clear(); }

//==============================================================================
juce::ValueTree NovaEngine::saveState() const
{
    juce::ValueTree v ("SESSION");
    v.setProperty ("mode", (int) analysisEngine.getWorkMode(), nullptr);
    v.setProperty ("style", getStyle(), nullptr);
    v.appendChild (memory.toValueTree(), nullptr);
    v.appendChild (snapshots.toValueTree(), nullptr);
    v.appendChild (conversation.toValueTree(), nullptr);
    v.appendChild (references.toValueTree(), nullptr);
    return v;
}

void NovaEngine::restoreState (const juce::ValueTree& v)
{
    if (! v.isValid()) return;
    analysisEngine.setWorkMode ((analysis::WorkMode) std::clamp ((int) v.getProperty ("mode", 0), 0, 2));
    setStyle (v.getProperty ("style", "").toString());
    memory.fromValueTree (v.getChildWithName ("MEMORY"));
    snapshots.fromValueTree (v.getChildWithName ("SNAPSHOTS"));
    conversation.fromValueTree (v.getChildWithName ("CHAT"));
    references.fromValueTree (v.getChildWithName ("REFERENCE"), analysisEngine.getWorkMode());
}

} // namespace nova
