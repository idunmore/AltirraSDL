#!/usr/bin/env bash
# common.sh — Shared utilities for the AltirraSDL build system.
# Sourced by build.sh and sub-scripts; never run directly.

set -euo pipefail

# Colors (disabled if not a terminal)
if [ -t 1 ]; then
    C_RED='\033[0;31m'; C_GREEN='\033[0;32m'; C_YELLOW='\033[0;33m'
    C_CYAN='\033[0;36m'; C_BOLD='\033[1m'; C_RESET='\033[0m'
else
    C_RED=''; C_GREEN=''; C_YELLOW=''; C_CYAN=''; C_BOLD=''; C_RESET=''
fi

info()  { echo -e "${C_CYAN}[info]${C_RESET}  $*"; }
ok()    { echo -e "${C_GREEN}[ok]${C_RESET}    $*"; }
warn()  { echo -e "${C_YELLOW}[warn]${C_RESET}  $*"; }
die()   { echo -e "${C_RED}[error]${C_RESET} $*" >&2; exit 1; }

# Detect host platform → sets PLATFORM (linux|macos|windows)
detect_platform() {
    case "$(uname -s)" in
        Linux*)  PLATFORM=linux  ;;
        Darwin*) PLATFORM=macos  ;;
        MINGW*|MSYS*|CYGWIN*) PLATFORM=windows ;;
        *)       die "Unsupported platform: $(uname -s)" ;;
    esac
}

# Resolve the CMake preset name from PLATFORM, BUILD_TYPE, FRONTEND
# Sets: PRESET, BUILD_DIR
resolve_preset() {
    local type="${BUILD_TYPE:-release}"
    local fe="${FRONTEND:-sdl}"

    if [ "$fe" = "native" ] && [ "${PLATFORM}" != "windows" ]; then
        warn "--native is only meaningful on Windows (ignored)"
        fe=sdl
    fi

    case "${PLATFORM}" in
        linux)   PRESET="linux-${type}" ;;
        macos)   PRESET="macos-${type}" ;;
        windows)
            if [ "$fe" = "native" ]; then
                PRESET="windows-libs-only"
            else
                PRESET="windows-sdl-${type}"
            fi
            ;;
    esac

    BUILD_DIR="${ROOT_DIR}/build/${PRESET}"
}

# Detect number of CPU cores
detect_jobs() {
    if [ -n "${JOBS:-}" ]; then return; fi
    if command -v nproc &>/dev/null; then
        JOBS=$(nproc)
    elif [ -f /proc/cpuinfo ]; then
        JOBS=$(grep -c ^processor /proc/cpuinfo)
    elif command -v sysctl &>/dev/null; then
        JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
    else
        JOBS=4
    fi
}
