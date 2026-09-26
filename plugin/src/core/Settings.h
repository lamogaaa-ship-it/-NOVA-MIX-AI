#pragma once

// Global (per-user, not per-project) NOVA settings stored in the user data folder.
// Secrets: for development the API key may live here or in ANTHROPIC_API_KEY; production builds
// should use the NOVA Cloud provider (server-held keys) - see docs/PRIVACY.md.

#include <juce_core/juce_core.h>

#include <mutex>

namespace nova
{

struct EngineerSettings
{
    juce::String provider = "auto";        // auto | offline | anthropic | nova_cloud
    juce::String anthropicApiKey;          // dev only
    juce::String anthropicBaseUrl = "https://api.anthropic.com";
    juce::String cloudUrl;                 // NOVA backend base URL, e.g. https://api.novamix.ai
    juce::String cloudToken;
    juce::String model = "claude-fable-5-1";
    juce::String effort = "medium";        // low | medium | high
    bool useServerFallbacks = true;        // Claude server-side refusal fallbacks
    bool allowCloudAnalysis = true;        // send analysis NUMBERS (never audio) to the cloud engineer
    bool allowCloudAudio = false;          // explicit opt-in for sending audio to cloud models (unused by default)
    bool learningEnabled = true;
    bool experienceEnabled = true;
    juce::String explanationMode = "simple";   // simple | engineer
    juce::String companionUrl = "http://127.0.0.1:47800";
    bool companionEnabled = true;
    bool diagnosticsVisible = false;
    bool speakReplies = true;
    juce::String voiceLanguage = "auto";       // push-to-talk language: auto | ar | en                  // read replies aloud when the request was spoken (needs companion TTS)
    float uiScale = 1.0f;
    float editorScale = 0.f;                   // last editor size chosen by the user (fraction of 1440x1080); 0 = fit the screen

    juce::String effectiveApiKey() const;
    juce::String effectiveProvider() const;   // resolves "auto"
};

class SettingsStore
{
public:
    explicit SettingsStore (juce::File file);
    EngineerSettings get() const;
    void set (const EngineerSettings& s);
    juce::File getFile() const { return file; }
    static SettingsStore& shared();

private:
    mutable std::mutex lock;
    juce::File file;
    EngineerSettings settings;
    void load();
    void save() const;
};

} // namespace nova
