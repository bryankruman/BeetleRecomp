#!/usr/bin/env bash
# Native Apple Silicon build. Installs nothing and requires generated private code.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"
if [ "$(uname -s)" != Darwin ] || [ "$(uname -m)" != arm64 ]; then
    echo "Run this wrapper natively on Apple Silicon (not a Rosetta shell)." >&2
    exit 1
fi
JOBS="${BAR_JOBS:-4}"
case "$JOBS" in ''|*[!0-9]*|0) echo "BAR_JOBS must be a positive integer." >&2; exit 1 ;; esac
BUILD_DIR="${BAR_BUILD_DIR:-build-macos}"
# The runtime baseline is independent of the OS/SDK used to build it. CMake
# rejects targets below 13.0; external dylibs must also meet this target.
MIN_MACOS="${BAR_MACOS_MIN_VERSION:-13.0}"
export MACOSX_DEPLOYMENT_TARGET="$MIN_MACOS"
for tool in cmake ninja python3 xcrun; do
    command -v "$tool" >/dev/null || { echo "Missing $tool; see BUILDING.md." >&2; exit 1; }
done
# Command Line Tools alone may not include the Metal compiler. Fail before a
# long build instead of silently producing an application without menu shaders.
xcrun --find clang >/dev/null
xcrun --sdk macosx --find metal >/dev/null
xcrun --sdk macosx --find metallib >/dev/null
if [ ! -f RecompiledFuncs/funcs.h ] || [ ! -f RecompiledPatches/patches.c ]; then
    echo "Generate verified game C and viewport patches using BUILDING.md first." >&2
    exit 1
fi
for tool in lib/rt64/src/contrib/dxc/bin/arm64/dxc-macos; do
    [ -f "$tool" ] || { echo "Missing pinned shader compiler: $tool" >&2; exit 1; }
    # Windows-created source snapshots can lose executable permissions.
    chmod +x "$tool"
done
cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_C_COMPILER="$(xcrun --find clang)" \
    -DCMAKE_CXX_COMPILER="$(xcrun --find clang++)" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN_MACOS" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBEETLE_ENABLE_FRONTEND=ON -DBEETLE_REQUIRE_PATCHES=ON
cmake --build "$BUILD_DIR" --target BeetleRecomp --parallel "$JOBS"
echo "Built $BUILD_DIR/BeetleRecomp.app. Run: open \"$BUILD_DIR/BeetleRecomp.app\""
echo "This development app still uses local dependency libraries; see BUILDING.md before copying it to another Mac."
