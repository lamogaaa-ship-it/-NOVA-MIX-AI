#!/usr/bin/env bash
# Packages a macOS Release build of NOVA MIX AI:
#   * copies the out-of-process plugin scanner into each bundle (Contents/Helpers)
#   * code-signs the bundles (ad-hoc unless NOVA_CODESIGN_IDENTITY is a Developer ID)
#   * builds a component installer .pkg (VST3, AU, Standalone) and a .dmg containing it
#
# usage: scripts/macos/package.sh <build-dir> <out-dir>
# env:   NOVA_CODESIGN_IDENTITY   codesign identity, default "-" (ad-hoc)
#        NOVA_INSTALLER_IDENTITY  "Developer ID Installer: ..." to sign the .pkg (optional)
set -euo pipefail

BUILD_DIR="$(cd "${1:?build dir}" && pwd)"
mkdir -p "${2:?output dir}"
OUT_DIR="$(cd "$2" && pwd)"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IDENTITY="${NOVA_CODESIGN_IDENTITY:--}"
VERSION="$(sed -n 's/^project(NOVA_MIX_AI VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
NAME="NOVA MIX AI"
BASE="NOVA-MIX-AI-${VERSION}-macOS-universal"

ART="$BUILD_DIR/plugin/NovaMixAI_artefacts/Release"
VST3="$ART/VST3/$NAME.vst3"
AU="$ART/AU/$NAME.component"
APP="$ART/Standalone/$NAME.app"
SCANNER="$(find "$BUILD_DIR/plugin/tools" -type f -name nova-plugin-scanner -perm -u+x | head -n 1)"

for p in "$VST3" "$AU" "$APP"; do
    [[ -d "$p" ]] || { echo "missing build product: $p" >&2; exit 1; }
done
[[ -n "$SCANNER" ]] || { echo "missing nova-plugin-scanner" >&2; exit 1; }

rm -rf "$OUT_DIR"
STAGE="$OUT_DIR/stage"
mkdir -p "$STAGE/vst3" "$STAGE/au" "$STAGE/app" "$OUT_DIR/pkgs"

cp -R "$VST3" "$STAGE/vst3/"
cp -R "$AU" "$STAGE/au/"
cp -R "$APP" "$STAGE/app/"

sign() {
    # keep the entitlements JUCE generated (microphone, network) when re-signing
    codesign --force --sign "$IDENTITY" --timestamp=none --options runtime \
        --preserve-metadata=entitlements,requirements,flags "$@"
}

for b in "$STAGE/vst3/$NAME.vst3" "$STAGE/au/$NAME.component" "$STAGE/app/$NAME.app"; do
    mkdir -p "$b/Contents/Helpers"
    cp "$SCANNER" "$b/Contents/Helpers/nova-plugin-scanner"
    # the scanner loads other vendors' plug-ins: hardened runtime without library validation
    codesign --force --sign "$IDENTITY" --timestamp=none --options runtime \
        --entitlements "$ROOT/scripts/macos/host.entitlements" "$b/Contents/Helpers/nova-plugin-scanner"
    sign "$b"
    codesign --verify --deep --strict --verbose=2 "$b"
    lipo -archs "$b/Contents/MacOS/$NAME"
done

# component packages (bundles are not relocatable: always install to the standard folders)
component_pkg() {  # <stage subdir> <install location> <identifier> <pkg name> [scripts dir]
    local plist="$OUT_DIR/pkgs/$4.plist"
    pkgbuild --analyze --root "$STAGE/$1" "$plist" >/dev/null
    /usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$plist"
    local extra=()
    [[ -n "${5:-}" ]] && extra=(--scripts "$5")
    pkgbuild --root "$STAGE/$1" --component-plist "$plist" --install-location "$2" \
        --identifier "$3" --version "$VERSION" "${extra[@]}" "$OUT_DIR/pkgs/$4.pkg"
}
component_pkg vst3 "/Library/Audio/Plug-Ins/VST3" "ai.novamix.novamixai.vst3" "vst3"
component_pkg au "/Library/Audio/Plug-Ins/Components" "ai.novamix.novamixai.au" "au" "$ROOT/scripts/macos/au-scripts"
component_pkg app "/Applications" "ai.novamix.novamixai.app" "app"

sed "s/@VERSION@/$VERSION/g" "$ROOT/scripts/macos/distribution.xml" > "$OUT_DIR/pkgs/distribution.xml"
PKG="$OUT_DIR/$BASE.pkg"
productbuild --distribution "$OUT_DIR/pkgs/distribution.xml" --package-path "$OUT_DIR/pkgs" \
    --resources "$ROOT/scripts/macos/resources" "$OUT_DIR/unsigned.pkg"
if [[ -n "${NOVA_INSTALLER_IDENTITY:-}" ]]; then
    productsign --sign "$NOVA_INSTALLER_IDENTITY" "$OUT_DIR/unsigned.pkg" "$PKG"
    rm "$OUT_DIR/unsigned.pkg"
else
    mv "$OUT_DIR/unsigned.pkg" "$PKG"
fi
pkgutil --check-signature "$PKG" || true   # unsigned developer builds report "no signature"

# plain bundles for manual installation
( cd "$STAGE/vst3" && ditto -c -k --sequesterRsrc --keepParent "$NAME.vst3" "../../$BASE-VST3.zip" )
( cd "$STAGE/au" && ditto -c -k --sequesterRsrc --keepParent "$NAME.component" "../../$BASE-AU.zip" )
( cd "$STAGE/app" && ditto -c -k --sequesterRsrc --keepParent "$NAME.app" "../../$BASE-Standalone.zip" )

# disk image with the installer and the install notes
DMGROOT="$OUT_DIR/dmg"
mkdir -p "$DMGROOT"
cp "$PKG" "$DMGROOT/Install $NAME.pkg"
cp "$ROOT/scripts/macos/resources/README.txt" "$DMGROOT/READ ME FIRST.txt"
cp "$ROOT/scripts/macos/resources/README-ar.txt" "$DMGROOT/اقرأني أولاً.txt"
# hdiutil occasionally reports "Resource busy" on CI machines: retry a few times
for attempt in 1 2 3 4 5; do
    if hdiutil create -volname "$NAME $VERSION" -srcfolder "$DMGROOT" -ov -format UDZO "$OUT_DIR/$BASE.dmg"; then break; fi
    [[ $attempt == 5 ]] && exit 1
    sleep $((attempt * 5))
done
hdiutil verify "$OUT_DIR/$BASE.dmg"
if [[ "$IDENTITY" != "-" ]]; then codesign --force --sign "$IDENTITY" "$OUT_DIR/$BASE.dmg"; fi

rm -rf "$DMGROOT" "$OUT_DIR/pkgs"
( cd "$OUT_DIR" && shasum -a 256 *.pkg *.dmg *.zip > SHA256SUMS.txt && cat SHA256SUMS.txt )
echo "packaged into $OUT_DIR"
