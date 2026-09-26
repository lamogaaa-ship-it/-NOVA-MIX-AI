#include "LLMClient.h"

namespace nova::ai
{

HttpResponse JuceHttpTransport::post (const juce::String& url, const juce::StringPairArray& headers, const juce::String& body, int timeoutMs)
{
    HttpResponse r;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    juce::String headerText;
    for (auto& k : headers.getAllKeys())
        headerText << k << ": " << headers[k] << "\r\n";

    int status = 0;
    juce::StringPairArray responseHeaders;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                    .withExtraHeaders (headerText)
                    .withConnectionTimeoutMs (timeoutMs)
                    .withStatusCode (&status)
                    .withResponseHeaders (&responseHeaders)
                    .withHttpRequestCmd ("POST")
                    .withNumRedirectsToFollow (2);
    const juce::URL u = juce::URL (url).withPOSTData (body);
    if (auto stream = u.createInputStream (opts))
    {
        r.body = stream->readEntireStreamAsString();
        r.status = status;
    }
    else
    {
        r.status = status;
        r.error = status > 0 ? "HTTP " + juce::String (status) : "could not connect to " + juce::URL (url).getDomain();
    }
    r.elapsedMs = juce::Time::getMillisecondCounterHiRes() - t0;
    return r;
}

HttpResponse ScriptedTransport::post (const juce::String&, const juce::StringPairArray&, const juce::String& body, int)
{
    requests.push_back (body);
    HttpResponse r;
    if (requests.size() <= responses.size())
    {
        r.status = status;
        r.body = responses[requests.size() - 1];
    }
    else
        r.error = "scripted transport exhausted";
    return r;
}

//==============================================================================
LLMClient::LLMClient (Kind k, juce::String base, juce::String cred, std::shared_ptr<HttpTransport> t)
    : kind (k), baseUrl (std::move (base)), credential (std::move (cred)), transport (std::move (t))
{
    while (baseUrl.endsWithChar ('/')) baseUrl = baseUrl.dropLastCharacters (1);
}

juce::String LLMClient::describe() const
{
    return kind == Kind::AnthropicDirect ? "Claude API (direct)" : "NOVA Cloud (" + juce::URL (baseUrl).getDomain() + ")";
}

LLMResult LLMClient::send (const juce::var& body, bool useFallbackBeta)
{
    LLMResult res;
    juce::StringPairArray headers;
    headers.set ("content-type", "application/json");
    juce::String url;
    if (kind == Kind::AnthropicDirect)
    {
        url = baseUrl + "/v1/messages";
        headers.set ("x-api-key", credential);
        headers.set ("anthropic-version", "2023-06-01");
        if (useFallbackBeta) headers.set ("anthropic-beta", "server-side-fallback-2026-07-01");
    }
    else
    {
        url = baseUrl + "/v1/engineer/messages";
        headers.set ("authorization", "Bearer " + credential);
        headers.set ("x-nova-client", "nova-mix-ai-plugin/" NOVA_VERSION_STRING);
        if (useFallbackBeta) headers.set ("x-nova-fallbacks", "default");
    }

    const auto http = transport->post (url, headers, juce::JSON::toString (body, true), 240000);
    res.elapsedMs = http.elapsedMs;
    res.httpStatus = http.status;
    if (http.error.isNotEmpty() && http.body.isEmpty())
    {
        res.error = http.error;
        res.retryable = true;
        return res;
    }
    const auto parsed = juce::JSON::parse (http.body);
    if (http.status >= 200 && http.status < 300 && parsed.isObject() && parsed.hasProperty ("content"))
    {
        res.ok = true;
        res.message = parsed;
        return res;
    }
    // API error envelope: {"type":"error","error":{"type":..., "message":...}}
    juce::String msg = parsed.getProperty ("error", {}).getProperty ("message", {}).toString();
    if (msg.isEmpty()) msg = http.body.substring (0, 300);
    res.error = "HTTP " + juce::String (http.status) + ": " + msg;
    res.retryable = http.status == 429 || http.status >= 500 || http.status == 408 || http.status == 409;
    return res;
}

} // namespace nova::ai
