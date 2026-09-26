# Building NOVA MIX AI

## Toolchain and pinned dependencies

| Dependency | Version | License | Used for |
|---|---|---|---|
| JUCE | **9.0.2** (released 2026-09-07) | AGPLv3 or commercial JUCE licence | plug-in framework, VST3/AU/Standalone wrappers, hosting |
| VST3 SDK (bundled with JUCE) | JUCE 9.0.2 bundle | MIT | VST3 wrapper and hosting |
| Catch2 | v3.16.0 | BSL-1.0 | tests only (not shipped) |
| pluginval | v1.0.4 | GPLv3 (tool only, not shipped) | plug-in validation in CI |
| CMake | ≥ 3.22 | | build |

### Why JUCE 9.0.2

The project is built on **JUCE 9.0.2**, the current stable JUCE release. It is pinned in
`cmake/NovaDependencies.cmake` (`NOVA_JUCE_TAG`), and the code relies on JUCE 9 APIs:

- `juce::FontOptions`
- `ThreadPoolOptions`
- `addDefaultFormatsToManager()`
- `VBlankAttachment`
- the headless audio-processors split

An early architecture note mentioned JUCE 8. That was an error in the note. JUCE 8 is not used anywhere, and the build does not support it. Upgrading JUCE is a deliberate change: bump `NOVA_JUCE_TAG`, rebuild, then run the full test suite and pluginval.

### Licensing: what must be settled before selling

- **JUCE:** under AGPLv3 unless a commercial JUCE licence is bought. A closed-source commercial release needs the commercial JUCE licence. This is a business decision, not a technical one.
- **VST3 SDK:** MIT licensed, so VST3 builds need no separate Steinberg agreement.
- **Neural models:** none are shipped. The analysis engine is native DSP. Models with non-commercial or research-only licences (for example MERT) are deliberately **not** used. Any future neural backend must be commercially licensable and stays behind the replaceable companion interface.

## macOS (universal VST3 + AU + Standalone, installer)

Requirements:

- Xcode 15 or newer
- CMake
- Ninja (`brew install ninja`)

```sh
cmake -S . -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build-macos --parallel
build-macos/plugin/tests/NovaTests          # unit + integration tests
scripts/macos/package.sh build-macos dist   # signed bundles, .pkg, .dmg, zips
```

Build products are written to `build-macos/plugin/NovaMixAI_artefacts/Release/`:

- `VST3/NOVA MIX AI.vst3`
- `AU/NOVA MIX AI.component`
- `Standalone/NOVA MIX AI.app`

`scripts/macos/package.sh` produces these files in `dist/`:

- `NOVA-MIX-AI-<version>-macOS-universal.pkg`: installer with VST3, AU and app components.
- `NOVA-MIX-AI-<version>-macOS-universal.dmg`: the installer plus install notes.
- `NOVA-MIX-AI-<version>-macOS-universal-{VST3,AU,Standalone}.zip`: plain bundles.
- `SHA256SUMS.txt`

Code signing:

- Without `NOVA_CODESIGN_IDENTITY`, the bundles are **ad-hoc** signed with the hardened runtime. They run on the machine they are installed on. The installer is not notarized, so Gatekeeper asks for confirmation the first time: right-click → Open.
- Set `NOVA_CODESIGN_IDENTITY="Developer ID Application: …"` and `NOVA_INSTALLER_IDENTITY="Developer ID Installer: …"` to produce distributable signed builds. Notarize them afterwards (`xcrun notarytool submit … --wait`, then `xcrun stapler staple`).

### GitHub Actions

`.github/workflows/macos.yml` runs on every push. It:

1. builds universal binaries;
2. checks both architectures with `lipo`;
3. runs the test suite and the host check;
4. packages the bundles;
5. runs pluginval (strictness 10) on the VST3 and the AU, and `auval -strict`;
6. uploads the results as artifacts:

| Artifact | Contents |
|---|---|
| `NOVA-MIX-AI-macOS-installer` | `.pkg`, `.dmg`, `SHA256SUMS.txt` |
| `NOVA-MIX-AI-macOS-bundles` | zipped VST3, AU and Standalone app |
| `NOVA-MIX-AI-macOS-reports` | JUnit test results, pluginval logs |

`.github/workflows/linux.yml` does the same for Linux:

- builds the VST3 and Standalone;
- runs the tests under Xvfb, the host check and pluginval;
- uploads the Linux VST3.

## Linux (development)

```sh
sudo apt-get install ninja-build libasound2-dev libjack-jackd2-dev libcurl4-openssl-dev libfreetype-dev \
     libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
     libxrandr-dev libxrender-dev libxi-dev libglu1-mesa-dev libegl-dev xvfb
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
xvfb-run -a build/plugin/tests/NovaTests
```

Use `-DNOVA_JUCE_PATH=/path/to/JUCE` to build against a local JUCE 9.0.2 checkout instead of fetching it.

## CMake options

| Option | Default | |
|---|---|---|
| `NOVA_BUILD_TESTS` | ON | Catch2 test suite (`NovaTests`) |
| `NOVA_BUILD_TOOLS` | ON | `nova-plugin-scanner`, `nova-host-check`, `nova-screenshot` |
| `NOVA_ENABLE_HOSTING` | ON | hosting third-party VST3 (and AU on macOS) plug-ins inside NOVA |
| `NOVA_JUCE_PATH` | empty | local JUCE checkout |

## Secrets

- No API key is compiled into the plug-in or stored in the repository.
- The production path is the NOVA Cloud backend proxy (`backend/`). The Claude API key lives only in the server's environment.
- Developer builds can use a personal key instead, from either of:
  - the `ANTHROPIC_API_KEY` environment variable;
  - NOVA's settings, stored only in the user's local settings file.
