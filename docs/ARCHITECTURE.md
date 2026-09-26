# NOVA MIX AI — architecture

NOVA MIX AI is a VST3 / Audio Unit / Standalone plug-in for AI-assisted mixing and mastering, with an honest AI assistant.

The design separates three speeds of work, so the audio thread is never at the mercy of analysis or AI:

| Layer | Thread(s) | Latency budget | Code |
|---|---|---|---|
| Real-time DSP | host audio thread | < 1 buffer, no locks, no allocation | `plugin/src/dsp`, `core/PluginProcessor`, `core/MonitorStage`, `hosting/HostedRack::process` |
| Audio Intelligence | `NOVA Analysis` worker | 10 ms – seconds | `plugin/src/analysis` |
| NOVA ENGINEER (agent) | `NOVA Engineer` worker | seconds | `plugin/src/ai`, `core/NovaEngine` |
| UI | message thread | 60 fps vblank | `plugin/src/ui` |
| Companion (optional) | separate local process | – | `companion/` (Python, 127.0.0.1) |
| NOVA Cloud (optional) | server | – | `backend/` (FastAPI) |

```
 DAW audio ──► NovaAudioProcessor::processBlock (audio thread)
                 │  in trim → reorderable inserts (Level, EQ, Tone Match, Dyn EQ, Comp, De-ess, Color, Motion)
                 │  → Space → Image → Limiter → out gain → global mix
                 │  → Hosted plug-in rack (up to 4 third-party VST3/AU, latency compensated)
                 │  → MonitorStage (A/B, Delta, bypass, loudness match; dry path aligned to total latency)
                 ├─► AudioCapture (lock-free SPSC FIFO: dry + processed)
                 ▼
 AnalysisEngine (worker) ── live meters/spectrum ─► UI
     │  LISTEN state machine: Idle → Listening → Analyzing → Ready
     ▼
 AnalysisResult (features + semantic problems with confidence)
     ▼
 NovaEngine (worker) ◄── requests from chat / voice / buttons (message thread)
     │  OfflineEngineer (deterministic, evidence-driven)   or
     │  CloudEngineer (Claude, tool use) → AgentToolbox (validated tools)
     │        run_treatment / set_parameters / render_and_measure / match_reference / rack tools …
     │  TreatmentSession: renders the *same* NovaChain offline, measures, refines (closed loop)
     ▼
 verified candidate ──► snapshot (undo) ──► applied on the message thread (APVTS, host notified)
```

## Principles

1. **One chain, two uses.** The real-time processor and the closed-loop preview renderer run the same `dsp::NovaChain` class. What the AI measured in a preview is what the plug-in outputs.
2. **Real-time safety.** On the audio thread:
   - no allocation (a test fails on any heap allocation in `processBlock`);
   - no locks, no I/O;
   - parameters are read from atomics;
   - rack slots are published through atomic pointers;
   - meters are published through relaxed atomics.
3. **Constant, reported latency.** The chain latency (31-sample half-band + 1.5 ms limiter look-ahead) is constant. It is reported to the host together with the hosted rack's latency, and the dry path used by A/B, Delta and bypass is aligned to the total.
4. **Honesty.** The orb, listen button, meters, badges and chat only show real state:
   - "AI" badges appear only on modules the engineer changed;
   - the header says LOCAL or ONLINE according to the engine that will actually answer;
   - "Voice off" is shown when no speech-to-text is running;
   - measurements the analysis could not make are reported as missing, never invented.
5. **Closed loop.** listen → understand → plan → process → render → re-listen → compare → refine. Every change is measured loudness-matched against the state at the start of the turn before it is applied. See `AI_ENGINE.md`.
6. **Nothing essential depends on the network.** The offline engineer, analysis, reference matching and all processing run locally. Claude (via NOVA Cloud or a developer key) and the companion are optional add-ons.

## Threads and ownership

- `NovaAudioProcessor` owns:
  - the DSP (`NovaChain`, `MonitorStage`, `HostedRack`);
  - the capture FIFO;
  - `NovaEngine`.
- `NovaEngine` owns the non-real-time subsystems:
  - analysis, conversation, session memory, snapshots;
  - taste profile, experience store, plugin catalog, companion client.
- The engine's worker thread runs requests. Anything that must touch the processor's parameters or a hosted plug-in is marshalled to the message thread (`NovaEngine::runOnMessageThread`), because hosted VST3 plug-ins require it.
- UI components poll engine state in a `VBlankAttachment` tick and never block on workers.

## State and persistence

The plug-in state (`getStateInformation`) contains:

- the APVTS parameters and the chain order;
- the session: conversation, action memory and undo snapshots;
- the hosted rack: plug-in descriptions, their state and bypass.

Hosted plug-in state can only be read on the message thread. Off-thread saves use a copy refreshed on the message thread.

User-level data lives in `~/Library/Application Support/NOVA MIX AI` on macOS and `~/.config/NOVA MIX AI` on Linux, with `NOVA_USER_DATA_DIR` as an override:

- settings;
- taste profile;
- experiences;
- plugin database.

## Source map

| Path | What |
|---|---|
| `plugin/src/core` | processor, parameter table (single source of truth), monitor stage, capture, engine hub, settings |
| `plugin/src/dsp` | DSP modules and the chain |
| `plugin/src/analysis` | feature extraction, semantic layer, listen state machine |
| `plugin/src/ai` | intent parser (EN/AR/Egyptian), treatments + closed loop, offline engineer, Claude client, agent tools, knowledge base, conversation |
| `plugin/src/reference` | reference loading, profiles and matching |
| `plugin/src/learning` | session memory, snapshots, taste profile, experiences |
| `plugin/src/hosting` | plug-in catalog, capability mapping, hosted rack |
| `plugin/src/net` | companion client |
| `plugin/src/ui` | editor |
| `plugin/tools` | out-of-process plug-in scanner, host check, screenshot renderer |
| `plugin/tests` | Catch2 unit and integration tests |
| `companion/` | local voice / TTS / embedding service |
| `backend/` | NOVA Cloud proxy |
| `shared/` | knowledge base (embedded in the plug-in), JSON schemas |
| `scripts/macos` | packaging (codesign, .pkg, .dmg) |
