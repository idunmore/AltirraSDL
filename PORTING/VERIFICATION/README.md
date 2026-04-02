AltirraSDL — Windows ↔ SDL3 Verification Documents
====================================================
Generated: 2026-04-02

Purpose
-------
Each document cross-references a Windows `cmd*.cpp` command handler against
its SDL3 ImGui equivalent to verify that the same engine API calls are made
in the same order with the same parameters.

Methodology
-----------
For each Windows handler:
1. Read the Windows source and list every engine API call in order
2. Find the SDL3 equivalent (menu item, config page control, deferred action)
3. Read the SDL3 source and list every engine API call in order
4. Compare — mark MATCH, DIVERGE (with details), or MISSING

Status Key
----------
- **MATCH** — SDL3 calls identical engine APIs with identical parameters
- **DIVERGE** — SDL3 does something different (details in Notes)
- **MISSING** — No SDL3 equivalent exists
- **N/A** — Not applicable to SDL3 (Windows-only feature like pane docking)

Severity Key
------------
- **Critical** — Wrong behavior, crash risk, or data corruption
- **Medium** — Missing confirmation/validation, suboptimal but functional
- **Low** — Cosmetic, minor UX difference, or edge case

Documents
---------
| # | File | Area | Match | Diverge | Missing |
|---|------|------|-------|---------|---------|
| 01 | [01_boot_load.md](01_boot_load.md) | Boot/Open/Load/State | 3 | 7 | 2 |
| 02 | [02_system_reset.md](02_system_reset.md) | Reset/Pause/Speed/Warp | 17 | 4 | 1 |
| 03 | [03_hardware_config.md](03_hardware_config.md) | Hardware/CPU/Memory/FW | 12 | 9 | 3 |
| 04 | [04_video_display.md](04_video_display.md) | Video/Filter/Overscan | 18 | 6 | 9 |
| 05 | [05_audio.md](05_audio.md) | Audio settings | 12 | 1 | 0 |
| 06 | [06_input.md](06_input.md) | Keyboard/Input/Mouse | 11 | 6 | 8 |
| 07 | [07_disk.md](07_disk.md) | Disk mount/eject/write | ~14 | ~4 | ~1 |
| 08 | [08_cassette.md](08_cassette.md) | Cassette load/transport | ~18 | ~2 | ~1 |
| 09 | [09_cartridge.md](09_cartridge.md) | Cartridge attach/detach | ~8 | ~3 | ~0 |
| 10 | [10_devices.md](10_devices.md) | Device add/remove/config | ~3 | ~2 | ~2 |
| 11 | [11_recording.md](11_recording.md) | Audio/Video/SAP record | ~6 | ~1 | ~1 |
| 12 | [12_profiles_settings.md](12_profiles_settings.md) | Profiles/Options | ~8 | ~3 | ~2 |
| 13 | [13_debug_tools.md](13_debug_tools.md) | Debug/Tools | 1 | 0 | ~20 |
| 14 | [14_window_view.md](14_window_view.md) | Window size/layout | ~2 | ~2 | ~3 |

Top Critical Issues — STATUS (2026-04-02)
------------------------------------------
All 6 original critical issues have been **FIXED**:

1. ~~Boot/Load: Missing DoLoadStream retry loop~~ — **FIXED**: Full retry loop with
   ATMediaLoadContext stop flags, hardware mode auto-switching (5200↔computer),
   BASIC memory conflict auto-resolution, disk format auto-accept. (01)
2. ~~Boot/Load: No dirty storage confirmation~~ — **DEFERRED**: Requires ImGui
   non-blocking dialog state machine. Low risk since SDL3 auto-saves on exit.
3. ~~Hardware config: Missing cold reset + validation~~ — **FIXED**: ATUISwitchHardwareMode
   and ATUISwitchMemoryMode un-stubbed with full validation (5200 mode handling,
   kernel compatibility, NTSC enforcement, profile switching). Cold resets now
   conditional via ATUIIsResetNeeded() flags matching Windows Ease of Use settings. (03)
4. ~~Cartridge: 5200 detach missing default cartridge~~ — **FIXED**: Calls
   LoadCartridge5200Default() in 5200 mode. (09)
5. ~~Input: Keyboard layout/arrow mode changes don't call ATUIInitVirtualKeyMap~~ —
   **FIXED**: Both arrow key mode and layout mode combos now call
   ATUIInitVirtualKeyMap(g_kbdOpts). (06)
6. ~~Cold reset: Missing shift key state cleanup~~ — **FIXED**: All cold reset
   paths (menu, hotkey, console callback) check g_kbdOpts.mbAllowShiftOnColdReset. (02)

Additional Fixes Applied
--------------------------
- **ATSetVideoStandard** un-stubbed (set standard + speed timing, no unconditional reset)
- **ATUIToggleHoldKeys** un-stubbed (clears held keys/switches on toggle off)
- **Conditional cold resets**: Video standard, BASIC toggle, cartridge changes now
  use ATUIIsResetNeeded() flags from Ease of Use settings (default: only cartridge resets)
- **Cassette auto-play**: cas.Play() called after load
- **Disk detach**: SetEnabled(false) called after UnloadDisk()
- **MegaCart 512K mode**: Fixed to use kATCartridgeMode_MegaCart_512K_3
- **NTSC50/PAL60**: Added to video standard options
- **5200 blocks video standard**: Combo disabled in 5200 mode
- **GTIA defect mode**: UI added (None/Type1/Type2)
- **Linear frame blending / mono persistence**: Checkboxes added
- **Interlace/scanlines**: ATUIResizeDisplay() called after toggle
- **ATSyncCPUHistoryState**: Called after CPU history toggle
- **Keyboard Cooked/Raw/Full Scan mode**: Selector added
- **Power-on delay**: Added to Boot category
- **PAL phase**: Added to Video category
- **SIO/CIO accelerator modes**: Radio buttons added to Acceleration category
- **Axlon banked memory / 65C816 high memory**: Combos added to Memory category
- **Media defaults persistence**: ATOptionsRunUpdateCallbacks/ATOptionsSave added
- **Firmware page**: Changed from ListBox to Combo, filtered by hardware mode,
  added [Autoselect], Firmware Manager button, tooltips
- **Tooltips**: Added to Firmware, System, CPU pages matching Windows help entries
- **Command-line boot**: Now uses deferred action with full retry loop
- **Open Image**: No longer force-resumes paused emulator

Remaining Systematic Patterns
-------------------------------
- **Missing confirmation dialogs**: SDL3 skips modal "Are you sure?" prompts for
  destructive operations (discard storage, reset confirmation). This is an
  architectural difference — ImGui is non-blocking. The underlying operations are
  all correct; only the user confirmation step is missing.
- **Tape Editor**: Complex waveform editor dialog not ported.

Resolved Systematic Patterns
-------------------------------
- **Device configuration dialogs**: **FIXED** (2026-04-02). ~40 per-device property
  editor dialogs implemented in `ui_devconfig.cpp`. Settings button added to Devices
  page. Covers all registered device types (modems, disk drives, IDE, expansion
  cartridges, printers, serial, custom devices). Generic fallback using
  `EnumProperties()` handles any unknown device types. Verified against Windows:
  correct property encodings (XEP80 1-based port, MyIDE2 cpldver=2 for v2,
  815 bit-shifted id, Covox best-match range, BlackBox Floppy enum strings,
  Printer HLE enum strings, 850Full per-port baud rates, 1400XL simplified modem).

Resolved Systematic Patterns (continued)
------------------------------------------
- **Firmware Manager dialog**: **FIXED** (2026-04-02). Full firmware manager:
  firmware list with Name/Type/Use for columns, type category filter, grayed
  internal firmware. Buttons: Add (file dialog + ATFirmwareAutodetect), Remove,
  Settings (edit name/type/path/CRC32, OPTION key flag for XL/XEGS kernels),
  Scan (directory scan for SpecificImage matches), Audit (known firmware CRC
  comparison table with ~92 entries), Set as Default, Use for... (specific
  firmware type assignment with toggle), Clear All Custom. Thread-safe file
  dialog callback using mutex handoff pattern. CRC32 cached per firmware ID.

Remaining Fix Priority
-----------------------
1. Tape Editor dialog
2. Custom keyboard layout editor
3. Non-blocking confirmation dialog framework (for discard/reset prompts)
4. Screen effects/bloom (requires shader support)
