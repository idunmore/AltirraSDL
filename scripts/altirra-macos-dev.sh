#!/usr/bin/env bash
#
# scripts/altirra-macos-dev.sh — fast macOS dev launch wrapper.
#
# Why this exists:
#   On macOS 14+, GameController.framework refuses to enumerate USB /
#   Bluetooth / MFi / Xbox / DualShock controllers when the binary is
#   launched directly from a terminal — the "responsible process" is
#   the shell, which has no NSGameControllerUsageDescription, so
#   gamecontrollerd silently drops the request and SDL3 sees zero
#   gamepads.  See issue #62 for the full diagnosis.
#
# Two equivalent ways to run AltirraSDL with a working joystick from
# the dev tree:
#
#   1. ./scripts/altirra-macos-dev.sh [args...]            (this script)
#      — re-launches the .app through LaunchServices and reattaches
#        stdout/stderr to the current terminal, so dev iteration is
#        unchanged.  Use this for the most "Mac-native" behaviour
#        (proper bundle attribution, full GameController.framework
#        controller support including MFi-only pads).
#
#   2. ALTIRRA_MACOS_FORCE_IOKIT=1 \
#      ./build/macos-release/src/AltirraSDL/AltirraSDL.app/Contents/MacOS/AltirraSDL
#      — runs the bare Mach-O directly (fastest, no LaunchServices
#        hop), but disables the GameController.framework driver and
#        falls back to SDL3's IOKit HID driver.  Sufficient for every
#        common USB / Bluetooth pad in the joystick-port use case.
#
# This script picks the most recently built .app under build/ and
# launches it via `open -W -n` with stdout/stderr piped back to the
# terminal.  Any extra arguments are forwarded to AltirraSDL.

set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
	echo "altirra-macos-dev.sh: this script is macOS-only." >&2
	exit 1
fi

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

# Find the newest AltirraSDL.app under build/.  -prune so we don't
# descend into nested .app bundles (which can happen when CMake copies
# resources during packaging).
app_path="$(find "$repo_root/build" -maxdepth 6 -type d -name 'AltirraSDL.app' \
	-not -path '*/AltirraSDL.app/*' \
	-print 2>/dev/null \
	| xargs -I{} stat -f '%m %N' {} 2>/dev/null \
	| sort -rn \
	| head -1 \
	| cut -d' ' -f2-)"

if [[ -z "$app_path" || ! -d "$app_path" ]]; then
	echo "altirra-macos-dev.sh: no AltirraSDL.app found under $repo_root/build/." >&2
	echo "  Build first: ./build.sh" >&2
	exit 1
fi

echo "altirra-macos-dev.sh: launching $app_path" >&2

# `open -W` blocks until the app exits, `-n` forces a fresh instance,
# `--stdout` / `--stderr` reattach the app's streams to this terminal.
# `--args` ends `open`'s own option parsing so the rest goes to the app.
exec open -W -n -a "$app_path" \
	--stdout /dev/stdout \
	--stderr /dev/stderr \
	--args "$@"
