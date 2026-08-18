#!/usr/bin/env bash
#
# Build a macOS installer (.pkg) bundling AudioRack's VST3, AU and Standalone
# into a single, user-customisable product archive.
#
# Usage:
#   packaging/macos/build_pkg.sh [ARTEFACTS_DIR]
#
# ARTEFACTS_DIR defaults to the Release output of the `macos` preset. For a
# shippable universal binary, build the `macos-universal` preset and pass its
# artefacts dir.
#
# Optional signing / notarisation (see docs/SIGNING.md) — all via env vars, so
# the script is a no-op-safe unsigned build when they are unset:
#   APP_SIGN_ID        "Developer ID Application: NAME (TEAMID)"  — signs bundles
#   INSTALLER_SIGN_ID  "Developer ID Installer: NAME (TEAMID)"    — signs the pkg
#   NOTARY_PROFILE     notarytool keychain-profile name           — submit+staple
#   AUDIORACK_VERSION  overrides the version stamped on the pkg (default 0.1.0)
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

ARTEFACTS="${1:-$REPO/build/macos/src/AudioRack_artefacts/Release}"
VERSION="${AUDIORACK_VERSION:-0.1.0}"
OUT_DIR="${OUT_DIR:-$REPO/build/installer/macos}"

PKG_ID="com.audiorackproject.audiorack"
VST3="$ARTEFACTS/VST3/AudioRack.vst3"
AU="$ARTEFACTS/AU/AudioRack.component"
APP="$ARTEFACTS/Standalone/AudioRack.app"

for bundle in "$VST3" "$AU" "$APP"; do
    if [ ! -e "$bundle" ]; then
        echo "error: missing artefact '$bundle'" >&2
        echo "       build the Release targets first: cmake --build --preset macos" >&2
        exit 1
    fi
done

COMPONENTS="$OUT_DIR/components"
RES_STAGE="$OUT_DIR/resources"
rm -rf "$OUT_DIR"
mkdir -p "$COMPONENTS" "$RES_STAGE"

# Product resources: static welcome/conclusion + the actual AGPLv3 licence text.
cp "$HERE/resources/welcome.html"    "$RES_STAGE/welcome.html"
cp "$HERE/resources/conclusion.html" "$RES_STAGE/conclusion.html"
cp "$REPO/LICENSE"                   "$RES_STAGE/license.txt"

# --- optionally sign the bundles (deep, hardened runtime, secure timestamp) ---
if [ -n "${APP_SIGN_ID:-}" ]; then
    echo "signing bundles with: $APP_SIGN_ID"
    for bundle in "$VST3" "$AU" "$APP"; do
        codesign --force --deep --options runtime --timestamp \
                 --sign "$APP_SIGN_ID" "$bundle"
    done
else
    echo "note: APP_SIGN_ID unset — building an UNSIGNED installer (dev only)."
fi

# --- one component package per artefact, each to its standard location ---
pkgbuild --quiet --component "$VST3" \
         --install-location "/Library/Audio/Plug-Ins/VST3" \
         --identifier "$PKG_ID.vst3.pkg"       --version "$VERSION" \
         "$COMPONENTS/vst3.pkg"

pkgbuild --quiet --component "$AU" \
         --install-location "/Library/Audio/Plug-Ins/Components" \
         --identifier "$PKG_ID.au.pkg"         --version "$VERSION" \
         "$COMPONENTS/au.pkg"

pkgbuild --quiet --component "$APP" \
         --install-location "/Applications" \
         --identifier "$PKG_ID.standalone.pkg" --version "$VERSION" \
         "$COMPONENTS/standalone.pkg"

# --- product archive: the customisable installer UI, licence, choices ---
FINAL="$OUT_DIR/AudioRack-$VERSION.pkg"
PRODUCT_ARGS=(--distribution "$HERE/distribution.xml"
              --package-path "$COMPONENTS"
              --resources "$RES_STAGE")

if [ -n "${INSTALLER_SIGN_ID:-}" ]; then
    echo "signing installer with: $INSTALLER_SIGN_ID"
    PRODUCT_ARGS+=(--sign "$INSTALLER_SIGN_ID" --timestamp)
fi

productbuild "${PRODUCT_ARGS[@]}" "$FINAL"
echo "built: $FINAL"

# --- optional notarisation (requires a signed installer) ---
if [ -n "${NOTARY_PROFILE:-}" ]; then
    echo "notarising via keychain profile: $NOTARY_PROFILE"
    xcrun notarytool submit "$FINAL" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$FINAL"
    echo "notarised + stapled: $FINAL"
fi
