# NOVA ENGINEER (AI engine)

NOVA ENGINEER turns a request ("make this vocal more consistent and remove the harshness without making it dull", "الصوت حاد شوية") into verified processing. Two engines share the same tools, the same verification and the same safety rules.

| Engine | When | Network |
|---|---|---|
| **Offline engineer** (`ai/Engineer.cpp`) | default, and the fallback whenever the cloud is unavailable | none |
| **Cloud engineer** (`ai/CloudEngineer.cpp`) | NOVA Cloud configured, or a developer Claude key | analysis numbers + text only, never audio |

The header shows **LOCAL** or **ONLINE** according to the engine that will actually answer. Each reply is labelled with the engine that produced it.

## Pipeline for one request

1. **Parse** (`ai/IntentParser`). English, Modern Standard Arabic and Egyptian Arabic phrases are parsed into:
   - intents (harshness, sibilance, level consistency, warmth, air, space, width, loudness, …);
   - modifiers (a little / a lot / without making it dull);
   - corrections of the previous action ("no, now it's too harsh", "رجّع الـ highs");
   - questions.
2. **Listen.** Use the latest analysis, or run LISTEN on the audio that is playing (see `AUDIO_INTELLIGENCE.md`).
3. **Context.** Build the engineer context:
   - measurements and semantic problems;
   - the current chain;
   - DAW tempo and time signature;
   - the reference and its selected dimensions;
   - session history;
   - the learned taste bias and similar past situations;
   - the hosted plug-in rack.
4. **Plan and act** through validated tools, working on a *candidate* copy of the settings.
5. **Closed loop** (`ai/Treatments`). Each treatment:
   - sets processing from the evidence;
   - renders the real `NovaChain` offline;
   - measures the targeted problem on the same time regions, loudness-matched (e.g. harsh-moment band level vs. presence elsewhere);
   - refines until it meets its target without breaking its protection constraint (presence and air, crest factor, loudness).
6. **Verify.** Before anything reaches the plug-in:
   - the harness re-renders the final candidate against the state at the start of the turn;
   - it re-matches output loudness unless loudness was requested;
   - it keeps true peak below −0.3 dBTP when the limiter is off.
7. **Apply and remember.**
   - Take an undo snapshot (chain + hosted plug-in parameters).
   - Apply on the message thread; the host is notified for automation.
   - Record the action in session memory and store the experience.
   - Reply with a SIMPLE explanation and an optional ENGINEER explanation, with the measured before/after numbers.
   - If the request was spoken, the reply is spoken too, through the companion's TTS.

## Language

Replies follow the language of the request:

- An Arabic or Egyptian Arabic request (including Arabic mixed with English terms) gets an Egyptian Arabic SIMPLE explanation. It keeps the measured numbers and units (dB, Hz, LUFS).
- ENGINEER details stay technical.
- Status messages and the suggestion chips switch language too. Every chip is verified to be understood by the parser in both languages (`ai/Suggestions.h`, EngineerTests).
- The cloud engineer is instructed to answer in the user's language and dialect.

Push-to-talk has a **Voice language** setting (automatic / Arabic / English). Automatic detection chooses only between Arabic and English.

## Tools (cloud engineer)

`analyze_audio`, `get_chain_state`, `run_treatment` (20 closed-loop routines), `set_parameters`, `set_module_enabled`, `reorder_chain`, `render_and_measure`, `compare_to_reference`, `match_reference`, `revert_previous_action`, `retrieve_knowledge`, `get_user_preferences`.

Only when relevant:

- `search_available_plugins` and `inspect_plugin_parameters`, when plug-ins have been scanned;
- `get_plugin_rack`, `set_plugin_rack_parameters` and `set_plugin_rack_bypass`, when plug-ins are loaded in the rack.

Validation (`AgentToolbox::validateChange` and the rack tools):

- Unknown parameter ids and indices are rejected.
- Values are clamped to range.
- Each change is limited to a per-parameter safety step (`maxAiStep`; 0.25 normalised for hosted plug-ins).
- Monitoring controls (bypass, A/B, Delta) belong to the user and are always rejected.

Hosted plug-ins cannot be rendered offline, so the tool result says so, and the engineer asks the user to LISTEN again to verify.

## Claude integration

Raw HTTPS from C++ (`ai/LLMClient`), Messages API:

- **Model:** `claude-fable-5-1` by default (configurable), `output_config.effort` `medium`.
- **Refusal fallbacks:** server-side `fallbacks: "default"` with the `server-side-fallback-2026-07-01` beta header.
- **Caching:** the system prompt is cached (`cache_control`). The per-turn context block goes in the user turn, so the cache stays valid.
- **Message format:**
  - append-only history;
  - the assistant turn is replayed verbatim, including thinking blocks and signatures;
  - all `tool_result` blocks go in one user message.
- **Stop reasons:** `refusal`, `pause_turn` and `max_tokens` are each handled.
- **Failure handling:** any transport or API failure falls back to the offline engineer, and the chat says so.

**Keys.** No key is shipped.

- **Production path:** the NOVA Cloud proxy (`backend/`). The proxy holds the key in its environment; the plug-in holds only a client token.
- **Developer builds:** can use `ANTHROPIC_API_KEY` or a key typed into settings. The key is stored only in the local settings file.

## Learning (opt-in, local)

- **Session memory:** what was done, why, and whether it was kept, undone or corrected. Corrections such as "too harsh now" refer to the previous action.
- **Taste profile:** bounded biases per context (brightness, compression, reverb, de-essing, width, loudness offset), learned from corrections, feedback (👍/👎) and undo.
- **Experiences:** feature vectors of past situations and the outcome, retrieved by similarity.

All of this is stored on the user's computer and can be deleted from settings. The cloud engineer receives only the summary relevant to the current request.
