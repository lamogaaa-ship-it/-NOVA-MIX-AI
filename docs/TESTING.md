# Testing and validation

## Plug-in (C++, Catch2): `build/plugin/tests/NovaTests`

40 test cases (≈144,600 assertions), grouped by tag:

| Tag | Covers |
|---|---|
| `[dsp]` | each module against analytic expectations (filters, compressor gain reduction, de-esser band reduction, limiter ceiling on high-crest signals, half-band latency, dry alignment) |
| `[analysis]` | LUFS / true peak / LRA against reference signals, YIN pitch, formants, sibilant / harsh / plosive event detection, noise floor, space estimation, phrase spread |
| `[engineer]` | intent parsing (EN / AR / Egyptian), each closed-loop treatment improving its target metric without breaking protections, corrections of the previous action, reference tone fit |
| `[agent]` | tool schemas, parameter validation, knowledge retrieval, a scripted Claude tool-use loop with request-shape assertions (caching, effort, fallbacks, verbatim replay), transport-failure fallback |
| `[rt][processor]` | **no heap allocation in `processBlock`** with every module on; neutral passthrough; reported latency; A/B loudness match |
| `[integration]` | real-time audio → LISTEN → request → applied processing → undo / redo → full state restore |
| `[hosting]` | a real VST3 (NOVA's own bundle) in the rack: exact latency compensation, bypass alignment, A/B alignment, removal, session recall; rack tools through the engine with undo / redo |

```sh
build/plugin/tests/NovaTests                 # all
build/plugin/tests/NovaTests "[hosting]"     # one area
```

On Linux, run under `xvfb-run -a`.

### Sanitizers

The full suite also runs clean under **AddressSanitizer + UndefinedBehaviorSanitizer**: no memory errors and no undefined behaviour in 40/40 test cases. This includes hosting a real VST3 and the engine's worker threads. To reproduce:

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_MODULE_LINKER_FLAGS="-fsanitize=address,undefined" -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
cmake --build build-asan --target NovaTests
ASAN_OPTIONS=detect_leaks=0:alloc_dealloc_mismatch=0 xvfb-run -a build-asan/plugin/tests/NovaTests
```

`alloc_dealloc_mismatch=0` is needed because the real-time allocation probe replaces the global `operator new`.

## Tools

- `nova-host-check <plugin>`: loads a built plug-in through JUCE's hosting layer like a DAW. It checks passthrough, latency reporting, parameter effect and state restore (`--editor` also opens the UI).
- `nova-screenshot out.png [--reference synthetic] [--request "..."]`: renders the real editor with real audio flowing (used for UI review).
- **pluginval** v1.0.4 at strictness 10, on VST3 (Linux + macOS) and AU (macOS).
- **auval** `-strict` for the Audio Unit (macOS CI).

## Python

```sh
cd companion && pytest     # 14 tests (+2 real-speech tests in CI): API, push-to-talk, TTS, embeddings, origin blocking, schema conformance
cd backend && pytest       # 11 tests: auth, validation, caps, rate limit, error shape, prefs, SDK call shape, schema
```

## CI

| Workflow | What it runs |
|---|---|
| `.github/workflows/macos.yml` | universal build, `lipo` check, tests, host check, packaging, pluginval (VST3 + AU), auval, artifacts |
| `.github/workflows/linux.yml` | build, tests, host check, pluginval, VST3 artifact |
| `.github/workflows/python.yml` | companion and backend tests |

## Not yet validated

- Real DAWs, FL Studio included. Only CI hosts, pluginval and auval have been used.
- A corpus of real recordings for analysis accuracy.
- Real Claude API calls (tests use a scripted transport).
- Real speech recognition with faster-whisper (tests use a fake recogniser).
