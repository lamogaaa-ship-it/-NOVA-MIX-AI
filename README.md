# NOVA MIX AI

An AI mixing and mastering engineer as a VST3 / Audio Unit / Standalone plug-in.

NOVA listens to your track, measures what is actually there and explains it in plain words (English or Arabic, including Egyptian Arabic). It then fixes it with its own real-time chain or the plug-ins you already own, and checks every change by rendering and re-measuring before applying it.

- **Download (macOS):** GitHub → Actions → "macOS build" → latest successful run. The artifacts are:
  - **`NOVA-MIX-AI-macOS-installer`**: the `.pkg` and `.dmg`;
  - **`NOVA-MIX-AI-macOS-bundles`**: plain VST3 / AU / app zips.

  The builds are ad-hoc signed and not notarized yet: open the installer with right-click → Open.
- **Build from source:** [docs/BUILD.md](docs/BUILD.md)
- **How it works:**
  - [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
  - [DSP](docs/DSP.md)
  - [Audio Intelligence](docs/AUDIO_INTELLIGENCE.md)
  - [NOVA ENGINEER](docs/AI_ENGINE.md)
  - [Reference Match](docs/REFERENCE_MATCH.md)
  - [Plug-in hosting](docs/PLUGIN_HOSTING.md)
  - [Privacy](docs/PRIVACY.md)
  - [Testing](docs/TESTING.md)
  - [Roadmap](docs/ROADMAP.md)
  - [Dev log](docs/DEVLOG.md)

| Part | Path |
|---|---|
| Plug-in (C++ / JUCE 9.0.2) | `plugin/` |
| Local companion (voice, TTS, embeddings) | `companion/` |
| NOVA Cloud proxy (keeps the Claude API key server-side) | `backend/` |
| Knowledge base, JSON schemas | `shared/` |
| Packaging | `scripts/macos/` |
