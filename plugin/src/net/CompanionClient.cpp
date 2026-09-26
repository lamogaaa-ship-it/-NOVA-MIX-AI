#include "CompanionClient.h"

namespace nova
{

CompanionClient::CompanionClient (juce::String baseUrl) : base (std::move (baseUrl)) {}

void CompanionClient::setBaseUrl (const juce::String& url) { std::lock_guard<std::mutex> g (lock); base = url; }

CompanionClient::Health CompanionClient::lastHealth() const { std::lock_guard<std::mutex> g (lock); return health; }

juce::var CompanionClient::getJson (const juce::String& path, int timeoutMs, juce::String& error)
{
    juce::String b;
    { std::lock_guard<std::mutex> g (lock); b = base; }
    int status = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (timeoutMs).withStatusCode (&status);
    if (auto s = juce::URL (b + path).createInputStream (opts))
    {
        const auto body = s->readEntireStreamAsString();
        if (status >= 200 && status < 300) return juce::JSON::parse (body);
        error = "companion HTTP " + juce::String (status);
        return {};
    }
    error = "companion not reachable";
    return {};
}

juce::var CompanionClient::postJson (const juce::String& path, const juce::var& body, int timeoutMs, juce::String& error)
{
    juce::String b;
    { std::lock_guard<std::mutex> g (lock); b = base; }
    int status = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                    .withExtraHeaders ("Content-Type: application/json\r\n")
                    .withConnectionTimeoutMs (timeoutMs)
                    .withStatusCode (&status)
                    .withHttpRequestCmd ("POST");
    if (auto s = juce::URL (b + path).withPOSTData (juce::JSON::toString (body, true)).createInputStream (opts))
    {
        const auto resp = s->readEntireStreamAsString();
        if (status >= 200 && status < 300) return juce::JSON::parse (resp);
        error = "companion HTTP " + juce::String (status) + ": " + resp.substring (0, 200);
        return {};
    }
    error = "companion not reachable";
    return {};
}

CompanionClient::Health CompanionClient::checkHealth()
{
    Health h;
    juce::String err;
    const auto v = getJson ("/v1/health", 800, err);
    h.checkedMs = juce::Time::currentTimeMillis();
    if (v.isObject())
    {
        h.reachable = true;
        h.version = v.getProperty ("version", "").toString();
        const auto caps = v.getProperty ("capabilities", {});
        h.stt = caps.getProperty ("stt", false);
        h.tts = caps.getProperty ("tts", false);
        h.embeddings = caps.getProperty ("embeddings", false);
        h.separation = caps.getProperty ("separation", false);
        h.sttEngine = caps.getProperty ("stt_engine", "").toString();
        h.embeddingModel = caps.getProperty ("embedding_model", "").toString();
    }
    std::lock_guard<std::mutex> g (lock);
    health = h;
    return h;
}

bool CompanionClient::startListening (const juce::String& languageHint, juce::String& error)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("language", languageHint);
    const auto v = postJson ("/v1/stt/start", juce::var (o), 1500, error);
    return v.isObject() && (bool) v.getProperty ("listening", false);
}

juce::String CompanionClient::stopListening (juce::String& detectedLanguage, juce::String& error)
{
    const auto v = postJson ("/v1/stt/stop", juce::var (new juce::DynamicObject()), 30000, error);
    if (! v.isObject()) return {};
    detectedLanguage = v.getProperty ("language", "").toString();
    if (v.hasProperty ("error")) error = v["error"].toString();
    return v.getProperty ("text", "").toString().trim();
}

bool CompanionClient::speak (const juce::String& text, const juce::String& language)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("text", text);
    o->setProperty ("language", language);
    juce::String err;
    const auto v = postJson ("/v1/tts", juce::var (o), 2000, err);
    return v.isObject() && (bool) v.getProperty ("ok", false);
}

std::vector<float> CompanionClient::embed (const juce::AudioBuffer<float>& audio, double sr, juce::String& model, juce::String& error)
{
    // mono float32 little-endian, base64 (localhost only)
    const int n = std::min (audio.getNumSamples(), (int) (sr * 30.0));
    juce::MemoryBlock mb ((size_t) n * sizeof (float));
    auto* dst = static_cast<float*> (mb.getData());
    for (int i = 0; i < n; ++i)
    {
        float v = 0.f;
        for (int c = 0; c < audio.getNumChannels(); ++c) v += audio.getSample (c, i);
        dst[i] = v / (float) std::max (1, audio.getNumChannels());
    }
    auto* o = new juce::DynamicObject();
    o->setProperty ("sample_rate", sr);
    o->setProperty ("pcm_f32_b64", juce::Base64::toBase64 (mb.getData(), mb.getSize()));   // standard RFC 4648
    const auto v = postJson ("/v1/embed", juce::var (o), 60000, error);
    std::vector<float> out;
    if (auto* arr = v.getProperty ("embedding", {}).getArray())
        for (auto& x : *arr) out.push_back ((float) (double) x);
    model = v.getProperty ("model", "").toString();
    return out;
}

} // namespace nova
