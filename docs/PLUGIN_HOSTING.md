# Third-party plug-in hosting

NOVA can use the VST3 plug-ins installed on the computer, and Audio Units on macOS, as part of its processing. It never touches licensing or copy protection: only plug-ins the user has installed and that load normally are used.

## 1. Scanning: out of process

`nova-plugin-scanner` (`plugin/tools/scanner`) runs as its own process. Settings → **Scan plugins** starts it.

- Because it is a separate process, a plug-in that crashes while being scanned kills only the scanner.
- Before each plug-in it writes `scan_pending.txt`. A leftover pending entry means that plug-in crashed the previous run; it is added to `scan_blocklist.txt` and skipped from then on.
- Each plug-in is instantiated once to read its real metadata:
  - name, manufacturer, category;
  - channel counts and latency;
  - whether it has an editor;
  - every parameter: name, label, default, step count.
- The database (`plugin_database.json`, schema in `shared/schemas/plugin-database.schema.json`) is saved incrementally.
- `CapabilityMapper` derives capabilities (eq, compressor, deesser, reverb, …) and per-parameter engineering concepts (frequency, gain, threshold, ratio, …). It uses only the metadata the plug-in reports, never guessed IDs.

In the macOS installer the scanner lives in each bundle at `Contents/Helpers/nova-plugin-scanner`.

## 2. The rack

`hosting/HostedRack` holds up to 4 slots in series, after NOVA's chain.

- **Loading** happens on the message thread (JUCE async instantiation). Only effects that loaded cleanly in the scanner are offered.
- **Buses:** a stereo (or mono) main bus is chosen; side-chain buses are disabled.
- **Audio path:**
  - lock-free: slots are published through atomic pointers;
  - a replaced plug-in is destroyed only after the audio thread has provably left `process()` (sequence counter);
  - the audio thread never waits;
  - non-finite output is replaced with silence.
- **Latency:** each slot's latency is added to what NOVA reports to the host, and to the dry path used by A/B, Delta and bypass (tested sample-exact). Plug-ins that change latency at run time are followed on the message thread.
- **Bypass** crossfades to a copy of the slot's input delayed by the slot's latency, so timing never jumps.
- **Session recall:** the description the plug-in was loaded from and its state are stored in NOVA's state. Plug-ins missing on another computer are reported in the chat; the rest of the session loads.
- **Threading:** hosted VST3 plug-ins lock the message thread for state, preparation and parameter text, so NOVA only does those there.
  - `prepareToPlay` called from another thread defers slot preparation to the message thread.
  - Off-thread state saves use the last message-thread copy.

The UI is advanced view → **Plugins**: add, bypass, open the plug-in's own window, remove, and the total added latency. Editor windows are closed before their plug-in is destroyed.

## 3. The AI and hosted plug-ins

With plug-ins loaded, the cloud engineer gets the rack tools (see `AI_ENGINE.md`):

- It reads real parameter lists before changing anything.
- Edits are validated: indices checked, values normalised 0–1, 0.25 step limit, stepped parameters snapped.
- Edits are applied with the turn and are undoable.

NOVA cannot render third-party plug-ins offline, so their effect is verified by listening again, and the engineer says so. NOVA's own modules stay the first choice because they are verified by rendering.

## Limitations

- Plug-ins run inside the DAW's process once loaded (as in every DAW). The scanner protects against crashes during discovery, not during use.
- Instruments and MIDI effects are not supported in the rack.
