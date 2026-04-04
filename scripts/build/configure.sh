#!/usr/bin/env bash
# configure.sh — Run CMake configure step using the resolved preset.
# Expects: ROOT_DIR, PRESET, BUILD_DIR, CLEAN (from build.sh)

# Guard: only source common.sh if not already loaded
[ -z "${C_RESET:-}" ] && source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

if [ "${CLEAN:-0}" = "1" ] && [ -d "$BUILD_DIR" ]; then
    info "Cleaning build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

info "Configuring preset: ${C_BOLD}${PRESET}${C_RESET}"

# CMake presets have platform conditions that prevent cross-platform use.
# We call cmake --preset which reads CMakePresets.json directly.
cmake --preset "$PRESET" ${CMAKE_EXTRA_ARGS:-} || die "CMake configure failed"

ok "Configure done  (build dir: $BUILD_DIR)"
