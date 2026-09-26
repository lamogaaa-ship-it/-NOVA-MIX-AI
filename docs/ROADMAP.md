# Roadmap

## Done

- Real-time chain (11 modules, 116 parameters), A/B / Delta / bypass with loudness match, constant reported latency, zero-allocation audio path.
- Native analysis engine and LISTEN state machine; semantic problems with confidence.
- NOVA ENGINEER:
  - offline engineer and Claude agent with validated tools;
  - 20 closed-loop treatments and final verification;
  - undo / redo, session memory, taste profile, experiences.
- Reference Match (7 dimensions + full, live influence).
- Editor matching the design reference, with state-driven orb, meters, analysis views and advanced / custom view.
- Out-of-process plug-in scanner and plugin database. Hosted rack (4 slots) with latency compensation, recall, UI and AI tools.
- Companion (push-to-talk STT, TTS, DSP embeddings) and NOVA Cloud proxy.
- macOS universal VST3 / AU / Standalone, `.pkg` and `.dmg` via CI.

## Next

1. **macOS CI green end-to-end.** Then:
   - test in FL Studio, Logic and Ableton on a real Mac;
   - fix whatever real hosts reveal (window resizing, scale factors, automation behaviour).
2. **Ship the companion.** Build it as a signed app bundle (PyInstaller) with a microphone usage description, plus an optional installer component and a LaunchAgent. Then voice works without installing Python.
3. **Real-recording evaluation set** for the analysis engine and the treatments, with thresholds tuned on real vocals and mixes.
4. **Developer ID signing and notarization** once an Apple Developer account is available.
5. **Commercial JUCE licence** before any closed-source release.
6. **Neural analysis backend** behind the companion's embedding interface, using a commercially licensable model only.
7. Source separation for full-mix references (licence-cleared model), side-chain inputs for the rack, AAX.
