#!/usr/bin/env bash
#
# setup_sdl3.sh — Fetch SDL3 source and set up Java sources for Android build.
#
# SDL3 on Android requires both:
#   1. Native C library (built by CMake via FetchContent — automatic)
#   2. Java SDLActivity class (must be in the Android project's Java source path)
#
# This script clones the SDL3 source (if not already present) and symlinks
# the Java sources into the Android project so Gradle can compile them.
#
# Usage:
#   cd android && ./setup_sdl3.sh
#
# Run once before the first `./gradlew assembleDebug`.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SDL3_DIR="$ROOT_DIR/android/SDL3"
SDL3_TAG="preview-3.2.14"  # Must match the version in CMakeLists.txt find_package

echo "=== Altirra Android — SDL3 Setup ==="

# ── Step 1: Get SDL3 source ──────────────────────────────────────────────
if [ -d "$SDL3_DIR" ]; then
    echo "[ok] SDL3 source already present at $SDL3_DIR"
else
    echo "[info] Cloning SDL3 ($SDL3_TAG)..."
    git clone --depth 1 --branch "$SDL3_TAG" \
        https://github.com/libsdl-org/SDL.git "$SDL3_DIR"
    echo "[ok] SDL3 source cloned"
fi

# ── Step 2: Symlink Java sources ─────────────────────────────────────────
# SDL3's Java sources (SDLActivity.java etc.) must be on Gradle's Java
# source path. We symlink them rather than copy to stay in sync.

SDL3_JAVA_SRC="$SDL3_DIR/android-project/app/src/main/java"
LINK_TARGET="$SCRIPT_DIR/app/src/main/java/org/libsdl"
SDL3_JAVA_PKG="$SDL3_JAVA_SRC/org/libsdl"

if [ ! -d "$SDL3_JAVA_PKG" ]; then
    echo "[error] SDL3 Java sources not found at $SDL3_JAVA_PKG"
    echo "        Expected directory structure: SDL3/android-project/app/src/main/java/org/libsdl/"
    exit 1
fi

if [ -L "$LINK_TARGET" ]; then
    echo "[ok] SDL3 Java symlink already exists"
elif [ -d "$LINK_TARGET" ]; then
    echo "[warn] $LINK_TARGET exists as directory — replacing with symlink"
    rm -rf "$LINK_TARGET"
    ln -s "$SDL3_JAVA_PKG" "$LINK_TARGET"
    echo "[ok] SDL3 Java symlink created"
else
    ln -s "$SDL3_JAVA_PKG" "$LINK_TARGET"
    echo "[ok] SDL3 Java symlink created: $LINK_TARGET -> $SDL3_JAVA_PKG"
fi

# ── Step 3: Verify Gradle wrapper ────────────────────────────────────────
if [ ! -f "$SCRIPT_DIR/gradlew" ]; then
    echo "[info] Gradle wrapper not found — generating..."
    if command -v gradle &>/dev/null; then
        (cd "$SCRIPT_DIR" && gradle wrapper --gradle-version 8.12)
        echo "[ok] Gradle wrapper generated"
    else
        echo "[warn] 'gradle' not found. Install Gradle or download gradlew manually."
        echo "       See: https://docs.gradle.org/current/userguide/gradle_wrapper.html"
        echo ""
        echo "       Quick fix: copy gradlew + gradle/ from any Android project, or run:"
        echo "         cd android && gradle wrapper --gradle-version 8.12"
    fi
fi

echo ""
echo "=== Setup complete ==="
echo ""
echo "Next steps:"
echo "  cd android"
echo "  export ANDROID_HOME=/path/to/android-sdk"
echo "  export ANDROID_NDK_HOME=\$ANDROID_HOME/ndk/<version>"
echo "  ./gradlew assembleDebug"
echo ""
echo "Or use the build script:"
echo "  ./build.sh --android"
