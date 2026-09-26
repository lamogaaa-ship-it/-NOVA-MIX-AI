#!/usr/bin/env bash
# Builds "NOVA Companion.app" (a self-contained, background app: no Python needed on the user's
# Mac) plus an installer package that adds it to /Applications and starts it at login through a
# LaunchAgent. The app asks for microphone access the first time push-to-talk is used.
#
# usage: scripts/macos/build_companion.sh <out-dir>
set -euo pipefail
mkdir -p "${1:?out dir}"
OUT="$(cd "$1" && pwd)"          # absolute: the build changes directory below
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="$(sed -n 's/^__version__ = "\(.*\)"/\1/p' "$ROOT/companion/nova_companion/__init__.py")"
WORK="$(mktemp -d)"

python3 -m venv "$WORK/venv"
"$WORK/venv/bin/pip" install -q --upgrade pip
"$WORK/venv/bin/pip" install -q -r "$ROOT/companion/requirements.txt" pyinstaller

cd "$ROOT/companion"
"$WORK/venv/bin/pyinstaller" --noconfirm --clean --windowed --name "NOVA Companion" \
    --osx-bundle-identifier ai.novamix.companion \
    --collect-all faster_whisper --collect-all ctranslate2 --collect-data tokenizers \
    --distpath "$WORK/dist" --workpath "$WORK/build" --specpath "$WORK" \
    run_companion.py

APP="$WORK/dist/NOVA Companion.app"
PLIST="$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Add :NSMicrophoneUsageDescription string 'NOVA MIX AI listens only while you hold the talk button, to understand spoken mixing requests.'" "$PLIST"
/usr/libexec/PlistBuddy -c "Add :LSUIElement bool true" "$PLIST"          # background app, no Dock icon
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" "$PLIST"
codesign --force --deep --sign "${NOVA_CODESIGN_IDENTITY:--}" "$APP"
codesign --verify --deep --strict "$APP"

# smoke tests: the bundled speech stack imports, and the frozen app answers /v1/health
"$APP/Contents/MacOS/NOVA Companion" --self-test
"$APP/Contents/MacOS/NOVA Companion" --port 47899 --no-voice --no-tts &
PID=$!
for _ in $(seq 1 60); do
    if curl -fsS http://127.0.0.1:47899/v1/health >/dev/null 2>&1; then break; fi
    sleep 1
done
curl -fsS http://127.0.0.1:47899/v1/health
kill $PID

# installer: app + LaunchAgent
PKGROOT="$WORK/pkgroot"
mkdir -p "$PKGROOT/Applications" "$PKGROOT/Library/LaunchAgents"
cp -R "$APP" "$PKGROOT/Applications/"
cp "$ROOT/scripts/macos/companion/ai.novamix.companion.plist" "$PKGROOT/Library/LaunchAgents/"
pkgbuild --analyze --root "$PKGROOT" "$WORK/components.plist" >/dev/null
i=0
while /usr/libexec/PlistBuddy -c "Print :$i" "$WORK/components.plist" >/dev/null 2>&1; do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$WORK/components.plist"
    i=$((i + 1))
done
pkgbuild --root "$PKGROOT" --component-plist "$WORK/components.plist" --install-location / \
    --scripts "$ROOT/scripts/macos/companion/scripts" --identifier ai.novamix.companion \
    --version "$VERSION" "$OUT/NOVA-Companion-$VERSION-macOS-$(uname -m).pkg"
( cd "$WORK/dist" && ditto -c -k --sequesterRsrc --keepParent "NOVA Companion.app" "$OUT/NOVA-Companion-$VERSION-macOS-$(uname -m).zip" )
ls -la "$OUT"
