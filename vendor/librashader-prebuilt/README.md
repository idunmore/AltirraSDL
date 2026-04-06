# Vendored librashader prebuilt binaries

This directory holds developer-built copies of `librashader` shared libraries
that are used by CI workflows instead of building librashader from source.

## Why vendor instead of build in CI?

Upstream `SnowflakePowered/librashader` stopped publishing prebuilt binaries
after v0.5.1.  The version pinned in `scripts/build/librashader.sh`
(currently `librashader-v0.10.1`) uses **C ABI version 2**, which is
incompatible with the v0.5.1 prebuilt (ABI v1).  So there is no upstream
binary we can download for the version we actually want to ship.

Building from source in CI is possible (Linux already does it) but it
requires the Rust toolchain (~5–8 min on a cold cache) and is exposed to
breakage from Rust ABI drift, librashader API changes, and transient
network issues.  Vendoring a known-good binary makes Windows CI:

- Free of any Rust dependency
- Deterministic on every run
- Fast (just a `cp`)

## How CI uses these files

| Platform / arch       | File this directory should contain                     | CI workflow             |
|-----------------------|--------------------------------------------------------|-------------------------|
| Windows x64           | `librashader.dll`                                      | `.github/workflows/windows.yml` |

The workflow looks for the file, and:

- **If present**, copies it next to `AltirraSDL.exe` in the build tree
  before `package_altirra` runs, so the produced `.zip` includes shader
  support.
- **If absent**, logs a `::warning::` and ships the `.zip` without
  librashader.  The base emulator (display, audio, input, debugger,
  built-in display filters) still works; only `slang` post-processing
  shaders are disabled.

This means you can merge the vendoring workflow without having a binary
ready, and add the binary later to enable shaders.

## How to refresh the vendored binary

You need this whenever you bump `LIBRASHADER_VERSION` in
`scripts/build/librashader.sh`.

### On a Windows machine

1. Install [Rust](https://www.rust-lang.org/tools/install) (`rustup` +
   stable toolchain).
2. From the repo root in **Git Bash** or **MSYS2**:
   ```bash
   ./build.sh --release --librashader
   ```
   This clones librashader into `build/_deps/librashader-src/` and
   builds it with the OpenGL + Vulkan backends only (no D3DX9_43 or
   dxcompiler dependencies — the resulting DLL has no non-system
   imports).
3. The built DLL ends up at:
   ```
   build/windows-sdl-release/src/AltirraSDL/Release/librashader.dll
   ```
4. Copy it into this directory:
   ```bash
   cp build/windows-sdl-release/src/AltirraSDL/Release/librashader.dll \
      vendor/librashader-prebuilt/librashader.dll
   ```
5. Verify the DLL has no surprising imports (Dependencies viewer,
   `dumpbin /imports`, etc.) and commit it.

### Verification

Run `dumpbin /headers vendor/librashader-prebuilt/librashader.dll` and
confirm it is `x64` (machine type `8664`).  A 32-bit DLL will silently
fail to load at runtime.

## File ownership and license

`librashader` is MPL-2.0 licensed (see the upstream repo).  The MPL
permits redistribution of compiled object code; no separate license
file is required alongside the DLL, but the source remains available
upstream at <https://github.com/SnowflakePowered/librashader>.
