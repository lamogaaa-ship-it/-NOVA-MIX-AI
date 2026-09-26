# Privacy

| Data | Where it goes |
|---|---|
| Your audio | **Never leaves the computer.** Analysis, processing, reference matching and embeddings run locally (the companion listens on 127.0.0.1 only). |
| Analysis numbers + your request text | Sent to the AI provider **only** when the cloud engineer is enabled (Settings → "Allow cloud engineer"): through NOVA Cloud, or directly to Claude with a developer key. |
| Voice recordings | Captured and transcribed by the local companion; not stored after transcription. |
| Taste profile, experiences, session memory | Stored locally in `~/Library/Application Support/NOVA MIX AI` (macOS). Deletable from Settings → "Delete learned data". Learning and experience memory can each be switched off. |
| Plug-in database | Local file; lists installed plug-ins and their parameters. Sent to the cloud engineer only as the specific entries it asks about. |
| NOVA Cloud | Stores no message content and logs only metadata (hashed token id, model, sizes, latency, status). Optional taste-profile sync is opt-in and deletable (`DELETE /v1/prefs`). |
| API keys | Never in the repository or the binary. The production key lives only in the backend's environment. Developer keys typed into settings stay in the local settings file. |

- The companion rejects requests carrying a browser `Origin`, so web pages cannot use it.
- Hosted plug-ins run in the DAW's process like in any DAW. NOVA does not send their audio or state anywhere.
