#!/usr/bin/env bash
# compile.sh — Run CMake build step.
# Expects: BUILD_DIR, JOBS, BUILD_TYPE, PLATFORM (from build.sh)

[ -z "${C_RESET:-}" ] && source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

detect_jobs

info "Building with ${C_BOLD}${JOBS}${C_RESET} parallel jobs..."

BUILD_ARGS=(--build "$BUILD_DIR" -j "$JOBS")

# MSVC multi-config generators need --config
if [ "${PLATFORM}" = "windows" ]; then
    # Portable capitalize (Bash 3.2+)
    local_type="$(echo "${BUILD_TYPE:0:1}" | tr '[:lower:]' '[:upper:]')${BUILD_TYPE:1}"
    BUILD_ARGS+=(--config "$local_type")
fi

cmake "${BUILD_ARGS[@]}" || die "Build failed"

ok "Build complete"
