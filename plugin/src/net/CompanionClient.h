#pragma once

// Client for the local NOVA Companion service (python, 127.0.0.1 only): streaming-capable
// speech-to-text for the mic button, optional text-to-speech, neural audio embeddings and
// (optional) source separation for reference analysis. Everything here is optional: when the
// companion is not running, NOVA keeps working and the UI says so honestly.

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <mutex>

namespace nova
{

class CompanionClient
{
public:
    explicit CompanionClient (juce::String baseUrl);

    struct Health
    {
        bool reachable = false;
        juce::String version;
        bool stt = false, tts = false, embeddings = false, separation = false;
        juce::String sttEngine, embeddingModel;
        juce::int64 checkedMs = 0;
    };

    Health checkHealth();                     // blocking, call from a worker thread
    Health lastHealth() const;
    void setBaseUrl (const juce::String& url);

    // Push-to-talk (the companion owns the microphone, so the DAW's audio devices are untouched)
    bool startListening (const juce::String& languageHint, juce::String& error);
    juce::String stopListening (juce::String& detectedLanguage, juce::String& error);   // returns transcript
    bool speak (const juce::String& text, const juce::String& language);

    // Neural embedding of an audio excerpt (sent to localhost only)
    std::vector<float> embed (const juce::AudioBuffer<float>& audio, double sampleRate, juce::String& model, juce::String& error);

private:
    juce::var postJson (const juce::String& path, const juce::var& body, int timeoutMs, juce::String& error);
    juce::var getJson (const juce::String& path, int timeoutMs, juce::String& error);
    mutable std::mutex lock;
    juce::String base;
    Health health;
};

} // namespace nova
