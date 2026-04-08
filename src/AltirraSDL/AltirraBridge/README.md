# AltirraBridge

A scripting / automation surface for [Altirra](https://www.virtualdub.org/altirra.html)
(Atari 8-bit emulator), exposed over a local socket. Drive AltirraSDL
programmatically from Python, C, or any language with a socket
client: frame-step, peek/poke memory, capture screenshots, inject
input, run the debugger, profile, disassemble, and more.

Use cases:

- **AI-driven gameplay & RL training** for Atari binaries
- **Automated testing** of homebrew Atari software
- **Reverse engineering** with cycle-accurate profiling, instruction
  history, conditional breakpoints, tracepoints, MADS export
- **Headless rendering** for screenshot regression and CI
- **SDK-style embedding** — pass in CPU/ANTIC/GTIA state, get back a
  rendered frame as a PNG

## Get the prebuilt package

Every push to `main` produces a self-contained cross-platform
package on the [`nightly-bridge`](../../../../releases/tag/nightly-bridge)
release. Each archive contains the headless `AltirraBridgeServer`
binary, the Python and C SDKs, **prebuilt C example binaries**,
full docs, RE case studies, and the Claude Code skill — no
compilation required.

| Platform        | Archive                                            |
|-----------------|----------------------------------------------------|
| Linux x86_64    | `AltirraBridge-<ver>-linux-x86_64.tar.gz`          |
| macOS arm64     | `AltirraBridge-<ver>-macos-arm64.tar.gz`           |
| Windows x86_64  | `AltirraBridge-<ver>-windows-x86_64.zip`           |

## Quick start

```sh
# 1. Unpack and launch the headless server
tar xzf AltirraBridge-*-linux-x86_64.tar.gz   # or unzip on Windows
cd AltirraBridge-*/
./AltirraBridgeServer --bridge=tcp:127.0.0.1:0 &
# Note the token-file path printed to stderr.

# 2a. Run a prebuilt C example
./sdk/c/examples/bin/01_ping /tmp/altirra-bridge-*.token

# 2b. Or drive it from Python (stdlib only — no install)
PYTHONPATH=sdk/python python3 -c "
from altirra_bridge import AltirraBridge
with AltirraBridge.from_token_file('/tmp/altirra-bridge-12345.token') as a:
    a.boot('/path/to/game.xex'); a.frame(120)
    open('frame.png','wb').write(a.screenshot())
"
```

If you want the GUI emulator alongside the bridge, use
`./AltirraSDL --bridge` from the regular AltirraSDL release instead
of `AltirraBridgeServer` — same protocol, same SDKs.

See [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md) for the
full walkthrough, [`docs/COMMANDS.md`](docs/COMMANDS.md) for the
per-command quick reference, and [`docs/PROTOCOL.md`](docs/PROTOCOL.md)
for the wire contract.

## Layout

```
AltirraBridge/
  docs/             Protocol spec, command reference, security model, API docs
  sdk/
    c/              Single-file C client (libc + Winsock only)
      examples/     01_ping.c, 02_peek_poke.c, ...
    python/         Pure-stdlib Python client (no dependencies)
      altirra_bridge/
      examples/     01_hello.py, 02_screenshot.py, ...
  skills/           Claude Code Skills (RE methodology, profiling)
  README.md         You are here
```

The C++ server module that lives **inside AltirraSDL** is at
`src/AltirraSDL/source/bridge/`. It is a self-contained subdirectory
with three files (`bridge_server`, `bridge_protocol`,
`bridge_transport`) and integrates with `main_sdl3.cpp` via five
~3-line insertions. To remove the bridge entirely, build with
`-DALTIRRA_BRIDGE=OFF` (the binary then contains zero bridge
symbols) or delete the `src/AltirraSDL/source/bridge/` directory.

The Windows-native `Altirra.sln` build is **completely untouched**
by AltirraBridge — no `.vcxproj` references any bridge file.

## Cross-platform support

| Platform | Status      | Notes                                                    |
|----------|-------------|----------------------------------------------------------|
| Linux    | full        | TCP loopback + POSIX UDS + abstract namespace            |
| macOS    | full        | TCP loopback + POSIX UDS                                 |
| Windows  | full        | TCP loopback                                             |
| Android  | full        | TCP loopback via `adb forward tcp:N tcp:N`               |

## Build targets

There are two binaries that speak the bridge protocol:

| Binary               | Dependencies                          | Use case                            |
|----------------------|---------------------------------------|-------------------------------------|
| `AltirraSDL`         | SDL3 + Dear ImGui + librashader (opt) | Full GUI emulator. Bridge optional via `--bridge`. Add `--headless` to skip the window/audio without changing dependencies. |
| `AltirraBridgeServer`| Core emulation libs only (no SDL3)    | Lean SDK build. Embeddable in CI / containers / RL pipelines. Same protocol as `AltirraSDL --bridge`. |

The `AltirraBridgeServer` target is opt-in via CMake:

```sh
cmake .. -DALTIRRA_BRIDGE_SERVER=ON
cmake --build . --target AltirraBridgeServer
```

Both binaries share the same `src/AltirraSDL/source/bridge/` source
files; they differ only in `main_*.cpp` and which platform layer
they link.

## Status

| Phase | Scope                                                                                  | Status |
|-------|----------------------------------------------------------------------------------------|--------|
| 1     | Skeleton: `HELLO`, `PING`, `FRAME`, `PAUSE`, `RESUME`, `QUIT`. TCP+UDS, token auth.    | done |
| 2     | State read: `REGS`, `PEEK`, `PEEK16`, `ANTIC`, `GTIA`, `POKEY`, `PIA`, `DLIST`, `HWSTATE`, `PALETTE`. | done |
| 3     | State write & input: `POKE`, `MEMDUMP`, `MEMLOAD`, `JOY`, `KEY`, `CONSOL`, `BOOT`, resets, save states. | done |
| 4     | Rendering: `SCREENSHOT`, `RAWSCREEN`, `RENDER_FRAME`.                                  | done |
| 5a    | Debugger introspection: `DISASM`, `HISTORY`, `EVAL`, `CALLSTACK`, `MEMMAP`, `BANK_INFO`, `CART_INFO`, `PMG`, `AUDIO_STATE`. | done |
| 5b    | Debugger control: `BP_SET`/`CLEAR`/`LIST`, `WATCH_SET`, `SYM_LOAD`/`RESOLVE`/`LOOKUP`, `MEMSEARCH`, full `PROFILE_*` family, `VERIFIER_*`. | done |
| 6     | SDK polish: `docs/PROTOCOL.md`, `docs/COMMANDS.md`, finalised C and Python client APIs. | done |
| 7     | Higher-level Python tools: XEX loader, RE project persistence, MADS exporter.          | done |
| 8     | `altirra-bridge` Claude Code skill + installer, RE case studies, cross-platform CI packaging. | done |
| 5c    | Deferred: tracepoint format strings, `SIO_TRACE`, `VERIFIER_REPORT` log, `SYM_FIND`. Awaiting public Altirra core APIs. | pending |

## License

GPLv2+, matching Altirra. See [`LICENSE`](LICENSE).
