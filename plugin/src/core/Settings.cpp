#include "Settings.h"
#include "../learning/Learning.h"

#if JUCE_MAC || JUCE_LINUX
 #include <sys/stat.h>
#endif

namespace nova
{

juce::String EngineerSettings::effectiveApiKey() const
{
    if (anthropicApiKey.isNotEmpty()) return anthropicApiKey;
    return juce::SystemStats::getEnvironmentVariable ("ANTHROPIC_API_KEY", {});
}

juce::String EngineerSettings::effectiveProvider() const
{
    if (provider != "auto") return provider;
    if (! allowCloudAnalysis) return "offline";
    if (cloudUrl.isNotEmpty() && cloudToken.isNotEmpty()) return "nova_cloud";
    if (effectiveApiKey().isNotEmpty()) return "anthropic";
    return "offline";
}

SettingsStore::SettingsStore (juce::File f) : file (std::move (f)) { load(); }

SettingsStore& SettingsStore::shared()
{
    static SettingsStore s (novaUserDataDirectory().getChildFile ("settings.json"));
    return s;
}

EngineerSettings SettingsStore::get() const
{
    std::lock_guard<std::mutex> g (lock);
    return settings;
}

void SettingsStore::set (const EngineerSettings& s)
{
    {
        std::lock_guard<std::mutex> g (lock);
        settings = s;
    }
    save();
}

void SettingsStore::load()
{
    std::lock_guard<std::mutex> g (lock);
    if (! file.existsAsFile()) return;
    const auto v = juce::JSON::parse (file);
    if (! v.isObject()) return;
    auto str = [&] (const char* k, juce::String& dst) { if (v.hasProperty (k)) dst = v.getProperty (k, dst).toString(); };
    auto bl = [&] (const char* k, bool& dst) { if (v.hasProperty (k)) dst = (bool) v.getProperty (k, dst); };
    str ("provider", settings.provider);
    str ("anthropic_api_key", settings.anthropicApiKey);
    str ("anthropic_base_url", settings.anthropicBaseUrl);
    str ("cloud_url", settings.cloudUrl);
    str ("cloud_token", settings.cloudToken);
    str ("model", settings.model);
    str ("effort", settings.effort);
    bl ("server_fallbacks", settings.useServerFallbacks);
    bl ("allow_cloud_analysis", settings.allowCloudAnalysis);
    bl ("allow_cloud_audio", settings.allowCloudAudio);
    bl ("learning", settings.learningEnabled);
    bl ("experience", settings.experienceEnabled);
    str ("explanation_mode", settings.explanationMode);
    str ("companion_url", settings.companionUrl);
    bl ("companion_enabled", settings.companionEnabled);
    bl ("diagnostics_visible", settings.diagnosticsVisible);
    bl ("speak_replies", settings.speakReplies);
    if (v.hasProperty ("ui_scale")) settings.uiScale = std::clamp ((float) (double) v.getProperty ("ui_scale", 1.0), 0.6f, 2.0f);
    if (v.hasProperty ("editor_scale")) settings.editorScale = std::clamp ((float) (double) v.getProperty ("editor_scale", 0.0), 0.f, 1.6f);
}

void SettingsStore::save() const
{
    std::lock_guard<std::mutex> g (lock);
    auto* o = new juce::DynamicObject();
    o->setProperty ("provider", settings.provider);
    o->setProperty ("anthropic_api_key", settings.anthropicApiKey);
    o->setProperty ("anthropic_base_url", settings.anthropicBaseUrl);
    o->setProperty ("cloud_url", settings.cloudUrl);
    o->setProperty ("cloud_token", settings.cloudToken);
    o->setProperty ("model", settings.model);
    o->setProperty ("effort", settings.effort);
    o->setProperty ("server_fallbacks", settings.useServerFallbacks);
    o->setProperty ("allow_cloud_analysis", settings.allowCloudAnalysis);
    o->setProperty ("allow_cloud_audio", settings.allowCloudAudio);
    o->setProperty ("learning", settings.learningEnabled);
    o->setProperty ("experience", settings.experienceEnabled);
    o->setProperty ("explanation_mode", settings.explanationMode);
    o->setProperty ("companion_url", settings.companionUrl);
    o->setProperty ("companion_enabled", settings.companionEnabled);
    o->setProperty ("diagnostics_visible", settings.diagnosticsVisible);
    o->setProperty ("speak_replies", settings.speakReplies);
    o->setProperty ("ui_scale", settings.uiScale);
    o->setProperty ("editor_scale", settings.editorScale);
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (juce::var (o)));
   #if JUCE_MAC || JUCE_LINUX
    file.setExecutePermission (false);
    chmod (file.getFullPathName().toRawUTF8(), 0600);   // the file may hold a developer API key
   #endif
}

} // namespace nova
