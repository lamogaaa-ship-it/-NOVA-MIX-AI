#pragma once

// Provider-independent Messages-API transport for the cloud engineer.
//   AnthropicDirect : POST {base}/v1/messages with x-api-key (developer setups)
//   NovaCloud       : POST {cloud}/v1/engineer/messages with a NOVA bearer token; the backend
//                     holds the model credentials (production)
// Runs only on the agent worker thread - never on the audio thread.

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>

namespace nova::ai
{

struct HttpResponse
{
    int status = 0;
    juce::String body;
    juce::String error;       // transport-level failure (no connection, timeout)
    double elapsedMs = 0;
};

class HttpTransport
{
public:
    virtual ~HttpTransport() = default;
    virtual HttpResponse post (const juce::String& url, const juce::StringPairArray& headers, const juce::String& body, int timeoutMs) = 0;
};

class JuceHttpTransport : public HttpTransport
{
public:
    HttpResponse post (const juce::String& url, const juce::StringPairArray& headers, const juce::String& body, int timeoutMs) override;
};

// For tests: replays scripted responses and records requests.
class ScriptedTransport : public HttpTransport
{
public:
    std::vector<juce::String> responses;
    std::vector<juce::String> requests;
    int status = 200;
    HttpResponse post (const juce::String&, const juce::StringPairArray&, const juce::String& body, int) override;
};

struct LLMResult
{
    bool ok = false;
    juce::var message;        // parsed response object (content, stop_reason, model, usage)
    juce::String error;
    int httpStatus = 0;
    bool retryable = false;
    double elapsedMs = 0;
};

class LLMClient
{
public:
    enum class Kind { AnthropicDirect, NovaCloud };

    LLMClient (Kind kind, juce::String baseUrl, juce::String credential, std::shared_ptr<HttpTransport> transport);

    // body: a complete Messages API request object (model, system, tools, messages, ...)
    LLMResult send (const juce::var& body, bool useFallbackBeta);
    juce::String describe() const;
    Kind getKind() const noexcept { return kind; }

private:
    Kind kind;
    juce::String baseUrl, credential;
    std::shared_ptr<HttpTransport> transport;
};

} // namespace nova::ai
