#!/usr/bin/env bash
#
# Deploy AudioRack — build the shippable macOS installer (.pkg).
#
# Produces a UNIVERSAL (arm64 + x86_64) Release build of all three formats and
# packages them into  build/installer/macos/AudioRack-<version>.pkg.
#
# Usage:
#   scripts/deploy.sh
#
# Signing + notarisation are opt-in (see docs/SIGNING.md). For a distributable,
# notarised installer, export these first:
#   APP_SIGN_ID        "Developer ID Application: NAME (TEAMID)"
#   INSTALLER_SIGN_ID  "Developer ID Installer: NAME (TEAMID)"
#   NOTARY_PROFILE     notarytool keychain-profile name
#
# Env overrides:
#   PRESET             build preset (default: macos-universal).
#                      Use PRESET=macos for a quick native-arch-only package.
#   AUDIORACK_VERSION  version stamped on the pkg (default from CMake: 0.1.0)
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

if [ "$(uname)" != "Darwin" ]; then
    echo "error: deploy targets macOS." >&2
    echo "       For the Windows installer, see packaging/windows/audiorack.iss." >&2
    exit 1
fi

PRESET="${PRESET:-macos-universal}"
BUILD_DIR="build/$PRESET"
ART="$BUILD_DIR/src/AudioRack_artefacts/Release"

echo "==> Configuring ($PRESET)"
cmake --preset "$PRESET"

echo "==> Building Standalone + VST3 + AU (Release)"
cmake --build --preset "$PRESET" \
      --target AudioRack_Standalone AudioRack_VST3 AudioRack_AU

if [ -z "${APP_SIGN_ID:-}" ]; then
    echo "==> NOTE: APP_SIGN_ID unset — building an UNSIGNED installer (local use)."
    echo "         Set APP_SIGN_ID / INSTALLER_SIGN_ID / NOTARY_PROFILE to ship."
fi

echo "==> Packaging installer"
packaging/macos/build_pkg.sh "$ART"

echo
echo "Deploy complete."
exit 0
