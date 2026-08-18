#!/usr/bin/env bash
#
# Build AudioRack — Standalone + VST3 + AU (macOS).
#
# Usage:
#   scripts/build.sh [options]
#
# Options:
#   --debug     Debug build (default: Release)
#   --clean     Remove the build directory first
#   --tests     Also build and run the Catch2 test suite
#   --open      Reveal the artefacts in Finder when done
#   -h, --help  Show this help
#
# On Windows, build with the preset directly:
#   cmake --preset windows && cmake --build --preset windows
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

PRESET=macos
CONFIG=Release
CLEAN=0
RUN_TESTS=0
OPEN=0

while [ $# -gt 0 ]; do
    case "$1" in
        --debug)     PRESET=macos-debug; CONFIG=Debug ;;
        --clean)     CLEAN=1 ;;
        --tests)     RUN_TESTS=1 ;;
        --open)      OPEN=1 ;;
        -h|--help)   awk 'NR>1 && /^#/ {sub(/^# ?/,""); print; next} NR>1 {exit}' "$0"; exit 0 ;;
        *)           echo "unknown option: $1 (see --help)" >&2; exit 1 ;;
    esac
    shift
done

if [ "$(uname)" != "Darwin" ]; then
    echo "error: this script targets macOS." >&2
    echo "       On Windows: cmake --preset windows && cmake --build --preset windows" >&2
    exit 1
fi

BUILD_DIR="build/$PRESET"
[ "$CLEAN" = 1 ] && { echo "==> Cleaning $BUILD_DIR"; rm -rf "$BUILD_DIR"; }

echo "==> Configuring ($PRESET)"
cmake --preset "$PRESET"

echo "==> Building Standalone + VST3 + AU ($CONFIG)"
cmake --build --preset "$PRESET" \
      --target AudioRack_Standalone AudioRack_VST3 AudioRack_AU

if [ "$RUN_TESTS" = 1 ]; then
    echo "==> Building + running tests"
    cmake --build --preset "$PRESET" --target audiorack_tests
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

ART="$BUILD_DIR/src/AudioRack_artefacts/$CONFIG"
echo
echo "Build complete. Artefacts:"
echo "  Standalone : $ART/Standalone/AudioRack.app"
echo "  VST3       : $ART/VST3/AudioRack.vst3"
echo "  AU         : $ART/AU/AudioRack.component"

[ "$OPEN" = 1 ] && open "$ART"
exit 0
