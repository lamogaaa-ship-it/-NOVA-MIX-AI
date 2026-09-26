# Development log

## Milestone 1: foundation

- JUCE 9.0.2 / CMake project with the parameter table as the single source of truth.
- Real-time chain, monitor stage and capture FIFO.
- Sample-exact latency fixes:
  - half-band phase: 30.5 → 31 samples;
  - off-by-one errors in dry alignment and limiter look-ahead.
- De-esser changed from a split high-pass to a band-pass detector (the high-pass reduced only about 1 dB because of phase).

## Milestone 2: Audio Intelligence

- BS.1770-4 loudness, true peak, LRA, YIN, LPC formants.
- Event detection relative to each voice's own statistics.
- False-positive fixes:
  - noise floor measured only when it is really below the signal;
  - harsh-event localisation;
  - space estimation rebuilt on envelope offsets.
- LISTEN keeps short gaps so phrase structure survives.

## Milestone 3: NOVA ENGINEER

- Closed-loop treatments with protection constraints; the offline engineer; EN / AR / Egyptian intent parsing.
- Reference tone fit made stable (smoothing, fundamental-zone weight, ridge, ±8 dB).
- Claude agent over raw HTTPS with validated tools:
  - cached system prompt;
  - effort `medium`;
  - scalar server-side fallbacks;
  - verbatim assistant replay;
  - failure falls back to the offline engineer.

## Milestone 4: UI

- Editor rebuilt against the design reference. Every animated or badge element is tied to real state.
- A headless screenshot tool renders the real editor with real audio for review.

## Milestone 5: hosting

- Out-of-process scanner with crash blocklist.
- `HostedRack` with a lock-free publish / retire scheme, latency compensation and bypass alignment.
- Found and fixed:
  - hosted VST3 `hasEditor()` / `getText()` / `prepareToPlay()` / state calls lock the message thread. Work is marshalled there, and off-thread paths are made safe.
  - Session recall must store the description the plug-in was loaded from.
  - Float smoothers stalled 4e-4 short of target with long time constants (a 0.004 dB A/B error); smoother state is now double.

## Milestone 6: delivery

- GitHub Actions for macOS (universal VST3 / AU / Standalone, tests, pluginval, auval, `.pkg`, `.dmg`), Linux and Python.
- Companion service and NOVA Cloud proxy with tests and shared JSON schemas.
- Spoken replies for spoken requests.
- Documentation set.
