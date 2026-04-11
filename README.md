# AltirraSDL

**Unofficial** SDL3 + Dear ImGui frontend for [Altirra](https://www.virtualdub.org/altirra.html), the cycle-accurate 8-bit Atari computer emulator by Avery Lee (phaeron).

> **This is not an official Altirra release.** It is an independent, community-maintained fork that replaces the native Windows UI with a cross-platform SDL3 + Dear ImGui frontend. It is not affiliated with, endorsed by, or supported by the original author. For the official Altirra emulator, please visit [virtualdub.org/altirra.html](https://www.virtualdub.org/altirra.html).

## What is Altirra?

Altirra is a best-in-class 8-bit Atari computer emulator featuring:

- Cycle-exact emulation of Atari 400/800, 1200XL, 600/800XL, 130XE, XEGS, and 5200 systems
- Accurate emulation of undocumented hardware behavior, including undocumented 6502 instructions, precise DMA timing, and hardware bugs
- Reimplemented OS, BASIC, and handler ROMs — no Atari ROM images required
- Emulation of three decades of hardware expansions, disk drives, modems, and accelerators
- A powerful debugger with source-level stepping, conditional breakpoints, execution history, and profiling
- Support for most popular 8-bit image formats: ATR, ATX, DCM, XFD, PRO, BAS, ROM, CAS, SAP, and more

For the full feature list and documentation, see the [official Altirra page](https://www.virtualdub.org/altirra.html).

## What is AltirraSDL?

AltirraSDL is a fork that replaces the Win32-based frontend with SDL3 for graphics, input, and audio, and Dear ImGui for the user interface. The emulation core is unchanged — all accuracy and compatibility comes directly from upstream Altirra.

### Goals

- Make Altirra runnable on Linux (the primary target), macOS and Android.
- Keep the delta from upstream as small as possible to ease merging of future releases
- Provide a functional UI close to original one, but not a pixel-perfect recreation of the Windows frontend

### Non-goals

- Replacing or competing with the official Windows build of Altirra (use the real thing on Windows — it's better)

## Current status

**Work in progress.** Core emulation works. The ImGui-based UI covers most of functionality, but some elements aren't finished.

## AI disclosure

This frontend is developed heavily with the assistance of AI coding tools. Depending on your perspective, that's either a pragmatic way to tackle a large porting effort or a reason to keep it well away from a hand-crafted passion project — which is one more good reason this fork is maintained separately.

## Relationship to upstream

- The emulation core is Altirra, written by Avery Lee. All the hard work is his.
- The upstream author is aware of this fork and does not object to it, but is not involved in its development and does not provide support for it.
- Cross-compiler portability fixes in platform-agnostic files are kept minimal and behind `#ifdef` guards. They do not change behavior on Windows.
- This fork tracks upstream Altirra releases manually (diff + merge), since the upstream source is distributed as zip snapshots rather than via a git repository.

## License

Altirra is licensed under the **GNU General Public License v2 (GPLv2)**, with a GPLv2 + exemption for certain core libraries. This fork inherits the same license. See [LICENSE](LICENSE) for details.

## Links

- **Official Altirra** — [virtualdub.org/altirra.html](https://www.virtualdub.org/altirra.html)

## Acknowledgments

All credit for the emulation engine, hardware research, reimplemented firmware, debugger, and everything that makes Altirra extraordinary goes to **Avery Lee (phaeron)**. 
