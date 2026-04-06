#!/usr/bin/env bash
# appimage.sh — Wrap the package_altirra output in an AppDir and produce
# a distro-independent AltirraSDL-<ver>-x86_64.AppImage.
#
# Prerequisites (satisfied by build.sh --appimage):
#   - The CMake "package_altirra" target has been built, producing
#     build/<preset>/AltirraSDL-<ver>/ with AltirraSDL, libSDL3.so.0,
#     optionally librashader.so, extras/, and Copying.
#
# Requires on the host:
#   - wget or curl (to fetch linuxdeploy on first run)
#   - FUSE2 (libfuse2) to run the resulting AppImage; the build itself
#     does not require FUSE because linuxdeploy --appimage-extract-and-run
#     is used as a fallback.
#
# Produces:
#   build/linux/AltirraSDL-<version>-x86_64.AppImage

[ -z "${C_RESET:-}" ] && source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

: "${ROOT_DIR:?appimage.sh must be run via build.sh (ROOT_DIR unset)}"
: "${BUILD_DIR:?appimage.sh must be run via build.sh (BUILD_DIR unset)}"
: "${PLATFORM:?appimage.sh must be run via build.sh (PLATFORM unset)}"

if [ "$PLATFORM" != "linux" ]; then
    die "AppImage packaging is only supported on Linux (current: $PLATFORM)"
fi

ARCH="$(uname -m)"
case "$ARCH" in
    x86_64)  LD_ARCH=x86_64  ;;
    aarch64) LD_ARCH=aarch64 ;;
    *) die "Unsupported architecture for AppImage: $ARCH" ;;
esac

# ── Locate package_altirra output ────────────────────────────────────────
PKG_DIR="$(find "$BUILD_DIR" -maxdepth 1 -type d -name 'AltirraSDL-*' | head -1)"
[ -n "$PKG_DIR" ] || die "package_altirra output not found under $BUILD_DIR.
Run: ./build.sh --package --librashader  first (or use --appimage)."

VERSION="$(basename "$PKG_DIR" | sed 's/^AltirraSDL-//')"
info "Packaging AppImage for AltirraSDL ${VERSION} (${LD_ARCH})"

# ── Stage AppDir ─────────────────────────────────────────────────────────
OUT_DIR="$ROOT_DIR/build/linux"
APPDIR="$OUT_DIR/AltirraSDL.AppDir"
TOOLS_DIR="$ROOT_DIR/build/_tools"

mkdir -p "$OUT_DIR" "$TOOLS_DIR"
rm -rf "$APPDIR"
mkdir -p \
    "$APPDIR/usr/bin" \
    "$APPDIR/usr/share/altirra" \
    "$APPDIR/usr/share/applications" \
    "$APPDIR/usr/share/icons/hicolor/48x48/apps" \
    "$APPDIR/usr/share/metainfo"

# Binary + bundled shared libraries
cp -a "$PKG_DIR/AltirraSDL" "$APPDIR/usr/bin/AltirraSDL"
chmod +x "$APPDIR/usr/bin/AltirraSDL"

if [ -f "$PKG_DIR/libSDL3.so.0" ]; then
    cp -a "$PKG_DIR/libSDL3.so.0" "$APPDIR/usr/bin/"
    # Some loaders look for libSDL3.so; provide a symlink to be safe.
    ln -sf libSDL3.so.0 "$APPDIR/usr/bin/libSDL3.so"
    ok "Bundled libSDL3.so.0"
else
    # System SDL3 path — the user linked against a distro package.
    # linuxdeploy will pick it up via ldd.
    warn "libSDL3.so.0 not found in package dir — relying on linuxdeploy to bundle it"
fi

if [ -f "$PKG_DIR/librashader.so" ]; then
    cp -a "$PKG_DIR/librashader.so" "$APPDIR/usr/bin/"
    ok "Bundled librashader.so"
else
    info "librashader.so not present — AppImage will not include shader presets"
fi

# Extras (custom effects, sample devices, device server scripts)
if [ -d "$PKG_DIR/extras" ]; then
    cp -a "$PKG_DIR/extras" "$APPDIR/usr/share/altirra/extras"
fi
if [ -f "$PKG_DIR/Copying" ]; then
    cp -a "$PKG_DIR/Copying" "$APPDIR/usr/share/altirra/Copying"
fi

# Desktop entry + icon — required by AppImage spec.
DESKTOP_SRC="$ROOT_DIR/dist/linux/altirra.desktop"
ICON_SRC="$ROOT_DIR/dist/linux/altirra.png"
[ -f "$DESKTOP_SRC" ] || die "Missing $DESKTOP_SRC"
[ -f "$ICON_SRC" ]    || die "Missing $ICON_SRC"

cp "$DESKTOP_SRC" "$APPDIR/usr/share/applications/altirra.desktop"
cp "$DESKTOP_SRC" "$APPDIR/altirra.desktop"
cp "$ICON_SRC"    "$APPDIR/usr/share/icons/hicolor/48x48/apps/altirra.png"
cp "$ICON_SRC"    "$APPDIR/altirra.png"
cp "$ICON_SRC"    "$APPDIR/.DirIcon"

# Custom AppRun — puts our bundled libs ahead of anything else.
cat > "$APPDIR/AppRun" <<'APPRUN_EOF'
#!/bin/sh
# AppRun for AltirraSDL — injects the bundled libSDL3 / librashader.
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/bin:$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
export XDG_DATA_DIRS="$HERE/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
# Make the bundled extras discoverable at a stable path.
export ALTIRRA_EXTRAS_DIR="$HERE/usr/share/altirra/extras"
exec "$HERE/usr/bin/AltirraSDL" "$@"
APPRUN_EOF
chmod +x "$APPDIR/AppRun"

# ── Fetch linuxdeploy ────────────────────────────────────────────────────
LD_TOOL="$TOOLS_DIR/linuxdeploy-${LD_ARCH}.AppImage"
if [ ! -x "$LD_TOOL" ]; then
    info "Downloading linuxdeploy (${LD_ARCH})..."
    LD_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${LD_ARCH}.AppImage"
    if command -v curl &>/dev/null; then
        curl -fL --retry 3 -o "$LD_TOOL" "$LD_URL" || die "linuxdeploy download failed"
    elif command -v wget &>/dev/null; then
        wget -q -O "$LD_TOOL" "$LD_URL" || die "linuxdeploy download failed"
    else
        die "Neither curl nor wget available to download linuxdeploy"
    fi
    chmod +x "$LD_TOOL"
fi

# ── Decide how to invoke linuxdeploy (FUSE may be unavailable in CI) ─────
#
# When FUSE is unavailable (Docker, GitHub Actions containers, WSL without
# fuse2), we must pass --appimage-extract-and-run AND export
# APPIMAGE_EXTRACT_AND_RUN=1 so that any nested AppImage launched by
# linuxdeploy (notably its bundled appimagetool) also extracts itself
# rather than trying to mount.
LD_RUN=("$LD_TOOL")
if ! "$LD_TOOL" --help >/dev/null 2>&1; then
    info "FUSE not available — running linuxdeploy via --appimage-extract-and-run"
    LD_RUN=("$LD_TOOL" --appimage-extract-and-run)
    export APPIMAGE_EXTRACT_AND_RUN=1
fi

# ── Run linuxdeploy ──────────────────────────────────────────────────────
# NO_STRIP=1: Release binary is already at final size; skip re-stripping
# (linuxdeploy's strip can choke on some debug sections).
# LDAI_OUTPUT / LINUXDEPLOY_OUTPUT_VERSION: current env var names for the
# appimage plugin.  OUTPUT / VERSION are the deprecated aliases — set both
# so the script keeps working across linuxdeploy versions.
cd "$OUT_DIR"
LDAI_OUTPUT="AltirraSDL-${VERSION}-${LD_ARCH}.AppImage" \
OUTPUT="AltirraSDL-${VERSION}-${LD_ARCH}.AppImage" \
LINUXDEPLOY_OUTPUT_VERSION="$VERSION" \
VERSION="$VERSION" \
NO_STRIP=1 \
    "${LD_RUN[@]}" \
        --appdir "$APPDIR" \
        --desktop-file "$APPDIR/usr/share/applications/altirra.desktop" \
        --icon-file "$APPDIR/altirra.png" \
        --output appimage \
    || die "linuxdeploy failed"

APPIMAGE_PATH="$OUT_DIR/AltirraSDL-${VERSION}-${LD_ARCH}.AppImage"
if [ ! -f "$APPIMAGE_PATH" ]; then
    # linuxdeploy sometimes ignores OUTPUT and names the file itself.
    APPIMAGE_PATH="$(find "$OUT_DIR" -maxdepth 1 -name '*.AppImage' -newer "$APPDIR/AppRun" | head -1)"
fi
[ -f "$APPIMAGE_PATH" ] || die "AppImage was not produced"

SIZE=$(du -h "$APPIMAGE_PATH" | cut -f1)
echo ""
ok "AppImage: ${C_BOLD}${APPIMAGE_PATH}${C_RESET} ($SIZE)"
