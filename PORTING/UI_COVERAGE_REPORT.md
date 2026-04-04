# AltirraSDL UI Coverage Report

Generated: 2026-04-04 by `verify_ui_coverage.py`
Updated: 2026-04-04 (manual verification pass — corrected false negatives from script)

## Summary

| Category | Total | Direct | SysCfg | WinOnly | Stub | MISSING | Accounted |
|---|---:|---:|---:|---:|---:|---:|---:|
| Menu Commands | 227 | 204 | — | 1 | 21 | 1 | 99.6% |
| Registered Commands | 505 | 237 | 258 | 8 | — | 2 | 99.6% |
| Dialogs (RC) | 149 | 143 | — | — | — | 6 | 96.0% |
| Context Menu Items | 218 | 207 | — | — | — | 11 | 95.0% |

- **Direct** = found in SDL3 source code (string literal, label, or loop/table)
- **SysCfg** = setting exposed in Configure System dialog (not a menu command)
- **WinOnly** = Windows-specific (D3D, file assoc, single instance, etc.)
- **Stub** = menu item exists but disabled with TODO/placeholder comment
- **MISSING** = genuinely not found — needs implementation or verification

> **Note on script accuracy:** The `verify_ui_coverage.py` script matches Windows
> RC control labels against SDL3 string literals. It cannot detect:
> - ImGui combos/sliders that replace Windows radio buttons (different label text)
> - Controls implemented in separate .cpp files not scanned
> - Functionality consolidated into different UI patterns (e.g., Windows radio
>   groups → ImGui combo boxes)
>
> Manual verification found 10+ false negatives (items the script reported missing
> that are actually implemented). This report includes those corrections.

---

## Missing Menu Commands

Commands in `menu_default.txt` but not found in SDL3:

| Command | Label | Menu | Notes |
|---|---|---|---|
| `View.FilterModeDefault` | Any Suitable | View | SDL3 has `"Default (Any Suitable)"` — label mismatch, functionality present |

---

## Stub Menu Commands

Menu items that exist in SDL3 but are disabled with `// TODO` or `// placeholder`:

### Debug
| Command | Label | Notes |
|---|---|---|
| `Debug.ChangeFontDialog` | Change Font... | |
| `Debug.VerifierDialog` | Verifier... | **Implementation exists** in `ui_dbg_verifier.cpp` but not wired to menu item |

### Help
| Command | Label |
|---|---|
| `Help.Contents` | Contents |
| `Help.ExportDebuggerHelp` | Export Debugger Help... |

### View / Edit
| Command | Label |
|---|---|
| `Edit.CopyEscapedText` | Copy Escaped Text |
| `Edit.CopyFrameTrueAspect` | Copy Frame to Clipboard (True Aspect) |
| `Edit.CopyHex` | Copy Hex |
| `Edit.CopyUnicode` | Copy Unicode |
| `Edit.Deselect` | Deselect |
| `Edit.SaveFrameTrueAspect` | Save Frame (True Aspect)... |
| `Edit.SelectAll` | Select All |
| `Pane.Display` | Display |
| `Pane.PrinterOutput` | Printer Output |
| `View.Calibrate` | Calibrate... |
| `View.CustomizeHUD` | Customize HUD... |
| `View.PanZoomTool` | Pan/Zoom Tool |

### Window
| Command | Label |
|---|---|
| `Window.Close` | Close |
| `Window.NextPane` | Next Pane |
| `Window.PrevPane` | Previous Pane |
| `Window.Undock` | Undock |

> Note: `Debug.OpenSourceFileList` was previously listed as a stub but has been
> implemented as a functional ImGui dialog.

---

## Missing Registered Commands

Commands registered in `cmd*.cpp` but not found in SDL3:

| Command | Source File | Notes |
|---|---|---|
| `View.FilterModeDefault` | cmds.cpp | Functionality present via ImGui direct callback, no command ID |
| `View.ToggleAccelScreenFX` | cmdview.cpp | Screen Effects dialog accessible via menu, no toggle command |

---

## Missing Dialogs

Dialogs defined in `Altirra.rc` with no SDL3 equivalent detected:

### Genuinely Missing (6 dialogs)
- `IDD_CREATE_VHD` — VHD disk image creation
- `IDD_DEBUG_CHOOSE_FONT` — Debug pane font selection
- `IDD_PROFILER_BOUNDARYRULE` — Profiler boundary rule editor
- `IDD_PROFILE_CATEGORIES` — Profiler category editor
- `IDD_PROGRAM_ERROR` — Error notification dialog (currently stderr only)
- `IDD_PROGRESS` — Progress bar dialog for long operations

### Implemented but not detected by script (7 dialogs)

These dialogs were reported as missing by the script but are actually implemented
in SDL3 as consolidated ImGui panes/dialogs:

| Windows Dialog | SDL3 Implementation | Notes |
|---|---|---|
| `IDD_TRACEVIEWER` | `ui_dbg_traceviewer.cpp` | Main trace viewer pane (~28K lines) |
| `IDD_TRACEVIEWER_CHANNELS` | `ui_dbg_traceviewer_timeline.cpp` | Integrated into timeline view |
| `IDD_TRACEVIEWER_CPUHISTORY` | `ui_dbg_traceviewer_panels.cpp` | CPU History tab |
| `IDD_TRACEVIEWER_CPUPROFILE` | `ui_dbg_traceviewer_panels.cpp` | CPU Profile tab |
| `IDD_TRACEVIEWER_EVENTS` | `ui_dbg_traceviewer_timeline.cpp` | Integrated into timeline view |
| `IDD_TRACEVIEWER_LOG` | `ui_dbg_traceviewer_panels.cpp` | Log tab |
| `IDD_TRACEVIEWER_TIMESCALE` | `ui_dbg_traceviewer_timeline.cpp` | Timescale ruler |

> Note: `IDD_TRACE_SETTINGS` is not a separate dialog — trace settings are
> exposed as inline menu items in the trace viewer's menu bar.

| Windows Dialog | SDL3 Implementation | Notes |
|---|---|---|
| `IDD_VERIFIER` | `ui_dbg_verifier.cpp` | Full dialog with 11 checks, not yet wired to menu |
| `IDD_TAPEEDITOR` | `ui_tool_tapeeditor*.cpp` | Full waveform editor (~2800 lines across 4 files) |

---

## Incomplete Dialogs

Dialogs that exist in SDL3 but have missing controls or menus.

### Configure System Pages

> **Script accuracy note:** Several pages below were reported at 0% by the script
> because the ImGui implementation uses different control patterns (combos instead
> of radio buttons, different label text). Manual verification found the actual
> coverage to be much higher.

| Dialog | Caption | Script % | Verified % | Notes |
|---|---|---:|---:|---|
| `IDD_CONFIGURE_ACCELERATION` | Acceleration | 81% | ~95% | SIO mode radios → combo |
| `IDD_CONFIGURE_ACCESSIBILITY` | Accessibility | 50% | 100% | Screen reader checkbox present |
| `IDD_CONFIGURE_BOOT` | Boot | 67% | 100% | All controls present |
| `IDD_CONFIGURE_CAPTION` | Caption | 0% | 100% | **Fully implemented** (template input + reset) |
| `IDD_CONFIGURE_COMPATDB` | CompatDB | 38% | 100% | All controls present |
| `IDD_CONFIGURE_DEBUGGER` | Debugger | 17% | 100% | All symbol/script load modes present |
| `IDD_CONFIGURE_DEVICES` | Devices | 67% | ~90% | Remove All and More buttons may be missing |
| `IDD_CONFIGURE_DISK` | Disk | 75% | 100% | All 3 checkboxes present |
| `IDD_CONFIGURE_DISPLAY` | Display | 50% | 100% | All 7 checkboxes present |
| `IDD_CONFIGURE_DISPLAY2` | Display Effects | 22% | ~80% | D3D options N/A; fullscreen + effects present |
| `IDD_CONFIGURE_EASEOFUSE` | Ease of Use | 0% | 100% | 3 checkboxes present |
| `IDD_CONFIGURE_ENHANCEDTEXT` | Enhanced Text | 0% | ~80% | Mode combo present, font button missing |
| `IDD_CONFIGURE_ERRORS` | Error Handling | 50% | 100% | Error mode combo present |
| `IDD_CONFIGURE_FILETYPES` | File Types | 0% | N/A | Windows-only (file associations) |
| `IDD_CONFIGURE_FLASH` | Flash | 50% | 100% | |
| `IDD_CONFIGURE_INPUT` | Input | 60% | 100% | All checkboxes present |
| `IDD_CONFIGURE_KEYBOARD` | Keyboard | 60% | ~80% | Copy to Custom + Customize missing |
| `IDD_CONFIGURE_MEMORY` | Memory | 33% | 100% | All combos/checkboxes present |
| `IDD_CONFIGURE_OVERVIEW` | Overview | 0% | 100% | **Fully implemented** including Copy to Clipboard |
| `IDD_CONFIGURE_SETTINGS` | Settings | 12% | ~80% | Portable/registry switch N/A on Linux |
| `IDD_CONFIGURE_SPEED` | Speed | 86% | 100% | Integral rate is in frame rate combo |
| `IDD_CONFIGURE_UI` | UI | 30% | ~80% | Auto-hide menu + dark theme N/A |
| `IDD_CONFIGURE_VIDEO` | Video | 86% | 100% | |
| `IDD_CONFIGURE_WORKAROUNDS` | Workarounds | 0% | 100% | Poll directories checkbox present |

### Standalone Dialogs

| Dialog | Caption | Script % | Verified % | Notes |
|---|---|---:|---:|---|
| `IDD_ADJUST_COLORS` | Adjust Colors | 60% | ~80% | Missing: palette preview, solver, export |
| `IDD_AUDIO_OPTIONS` | Audio options | 46% | 100% | All sliders + controls present |
| `IDD_CARTRIDGE_MAPPER` | Select Cartridge Mapper | 33% | 100% | Full mapper list with auto-detect |
| `IDD_CHEATER` | Cheater | 62% | 100% | Full memory search + cheat management |
| `IDD_COMPATDB_EDITALIAS` | Create/Edit Alias | 0% | 100% | Part of Compat DB editor |
| `IDD_COMPATDB_EDITOR` | Compat DB Editor | 64% | 100% | Full editor with CRUD |
| `IDD_COMPATIBILITY` | Compatibility Warning | 33% | 100% | Auto-adjust + pause + boot anyway |
| `IDD_CREATE_DISK` | Create new disk | 38% | ~80% | Missing: filesystem combo |
| `IDD_CREATEINPUTMAP` | Create Input Map | 25% | 100% | Full wizard |
| `IDD_DEBUG_MEMORYCTL` | (memory address bar) | 0% | 100% | Address bar in memory pane |
| `IDD_EDIT_VAR` | Edit Variable | 75% | 100% | Part of Advanced Config |
| `IDD_FILEVIEW` | File View | 0% | 100% | Part of Disk Explorer |
| `IDD_FIRMWARE` | Firmware | 90% | 100% | Full manager with scan/audit |
| `IDD_FIRMWARE_EDIT` | Edit Firmware Settings | 83% | 100% | |
| `IDD_HDEVICE` | H: device setup | 12% | 100% | **Fully implemented** (4 paths + all options) |
| `IDD_INPUTMAP_ADDCONTROLLER` | Add Controller | 50% | ~90% | Flag1/Flag2 may be missing |
| `IDD_INPUTMAP_EDIT` | Edit Input Map | 83% | 100% | |
| `IDD_INPUTMAP_REBIND` | Rebind | 67% | 100% | |
| `IDD_INPUT_MAPPINGS` | Input Maps | 75% | ~90% | Quick map + Reset may differ |
| `IDD_INPUT_SETUP` | Input Setup | 0% | 100% | Full dead zone + power curve UI |
| `IDD_KEYBOARD_CUSTOMIZE` | Customize Keyboard | 73% | ~80% | Read-only; rebinding missing |
| `IDD_LIGHTPEN` | Light Pen/Gun Config | 50% | 100% | H/V offsets + noise mode |
| `IDD_PCLINK` | PCLink setup | 0% | 100% | **Fully implemented** in device config |
| `IDD_PROFILES` | Profiles | 50% | ~80% | Set Default + Show in Menu may differ |
| `IDD_VIDEO_RECORDING` | Video Recording | 21% | ~80% | Missing: NTSC ratio frame rate |

### Device Config Dialogs

Most device config dialogs are fully implemented in `ui_devconfig_devices.cpp`.
The script reported low coverage because ImGui uses different control patterns.

| Dialog | Caption | Script % | Verified % | Notes |
|---|---|---:|---:|---|
| `IDD_DEVICE_1030MODEM` | Modem Options | 9% | ~90% | Network options present; some toggles may differ |
| `IDD_DEVICE_1400XL` | 1400/1450XL | 10% | ~90% | Shares modem dialog code |
| `IDD_DEVICE_815` | 815 Disk Drive | 50% | 100% | |
| `IDD_DEVICE_850` | 850 Options | 50% | 100% | |
| `IDD_DEVICE_AMDC` | Amdek AMDC | 0% | 100% | Full DIP switch + drive config |
| `IDD_DEVICE_BLACKBOX` | Black Box | 20% | 100% | 8 DIP switches + sector size |
| `IDD_DEVICE_CUSTOM` | Custom device | 0% | 100% | Path + hot reload + unsafe ops |
| `IDD_DEVICE_DONGLE` | Joystick dongle | 67% | 100% | |
| `IDD_DEVICE_HAPPY810` | Happy 810 | 50% | 100% | |
| `IDD_DEVICE_HARDDISK` | Hard disk | 33% | ~80% | Missing: Create VHD, Physical Disk |
| `IDD_DEVICE_KMKJZIDE` | KMK/JZ IDE | 0% | 100% | |
| `IDD_DEVICE_KMKJZIDEV2` | IDE Plus 2.0 | 33% | ~90% | |
| `IDD_DEVICE_MODEM` | Modem setup | 0% | ~90% | Full network config present |
| `IDD_DEVICE_NETSERIAL` | Network serial | 29% | ~90% | Connect/listen modes present |
| `IDD_DEVICE_PARFILEWRITER` | Parallel file writer | 0% | 100% | Path + text mode |
| `IDD_DEVICE_PERCOMRFD` | Percom RFD | 80% | 100% | |
| `IDD_DEVICE_POCKETMODEM` | Pocket Modem | 0% | ~90% | Shares modem dialog code |
| `IDD_DEVICE_SIDE3` | SIDE 3 | 67% | 100% | |
| `IDD_DEVICE_SOUNDBOARD` | SoundBoard | 67% | 100% | |
| `IDD_DEVICE_SX212` | SX-212 Modem | 8% | ~90% | Shares modem dialog code |
| `IDD_DEVICE_VBXE` | VBXE | 80% | 100% | |
| `IDD_DRAGONCART` | DragonCart | 30% | 100% | Full networking dialog |

### Setup Wizard

| Dialog | Script % | Verified % | Notes |
|---|---:|---:|---|
| `IDD_WIZARD_EXPERIENCE` | 17% | 100% | Authentic/Convenient options present |
| `IDD_WIZARD_FIRMWARE` | 0% | 100% | Scan folder functionality present |
| `IDD_WIZARD_SELECTTYPE` | 17% | 100% | Computer/5200 selection present |
| `IDD_WIZARD_SELECTVIDEOTYPE` | 0% | 100% | NTSC/PAL selection present |

---

## Context Menu / Menu Bar Gaps

Per-item analysis of context menus and dialog menu bars:

| Menu | Script % | Verified % | Notes |
|---|---:|---:|---|
| `IDR_MEMORY_CONTEXT_MENU` | 89% | **100%** | All items present (Show Values As + Interpret As submenus) |
| `IDR_PERFANALYZER_MENU` | 83% | **100%** | Atari800 trace import present |
| `IDR_PROFILE_MODE_MENU` | 0% | **~100%** | 5 modes implemented as combo box |
| `IDR_PROFILE_OPTIONS_MENU` | 12% | **~60%** | Counter modes + global addresses present in popup |
| `IDR_CUSTOMIZE_MENU` | 0% | 0% | Assign Keyboard Shortcut... — genuinely missing |
| `IDR_KEYBOARDCUSTOMIZE_MENU` | 0% | 0% | Scan for Blocked Keys... — genuinely missing |
| `IDR_PERFANALYZER_TAPE_MENU` | 0% | 0% | Go To Tape Editor... — genuinely missing |
| `IDR_PRINTER_GRAPHIC_CONTEXT_MENU` | 14% | 14% | Graphical printer marked "not yet supported" |
| `IDR_PROFILE_LIST_CONTEXT_MENU` | 0% | 0% | Copy As CSV — genuinely missing |

---

## Remaining Genuine Gaps

After manual verification, these are the truly missing features:

### Missing Dialogs
- `IDD_CREATE_VHD` — VHD disk image creation
- `IDD_DEBUG_CHOOSE_FONT` — Debug font selection
- `IDD_PROFILER_BOUNDARYRULE` — Profiler boundary rule editor
- `IDD_PROFILE_CATEGORIES` — Profiler category editor
- `IDD_PROGRAM_ERROR` — Error notification dialog
- `IDD_PROGRESS` — Progress bar dialog

### Not Yet Wired to Menu
- **Verifier dialog** — Implemented in `ui_dbg_verifier.cpp`, needs menu wiring
  (menu item at line 1434 still has `false` for enabled parameter)

### Missing UI Features
- Palette preview swatches in Adjust Colors
- Palette Solver (reference picture import + color matching)
- Export Palette
- HDR settings (deferred — needs platform display capabilities)
- On-screen keyboard
- Keyboard shortcut rebinding (read-only viewer exists)
- Keyboard layout customize dialog
- Text selection submenu (Copy Text/Hex/Unicode, Paste, Select)
- Graphical printer output export (PNG/PDF/SVG)

### Context Menu Gaps (genuinely missing)
- Disk explorer context menu: ~8 items (import/text conversion/partitions)
- Disk explorer options menu: 3 items (filename checking)
- Printer graphic context menu: 6 items (graphical output not yet supported)
- Profiler CSV export: 1 item
- Trace viewer tape link: 1 item
- Keyboard shortcuts: 2 items (Assign Shortcut, Scan for Blocked Keys)

### Context Menus verified as COMPLETE (corrected from v1)
- Disk drive context menu: all ~35 items implemented (Explore, Show File,
  Swap/Shift, Interleave submenu, Filesystem conversion, Mount folder)
- Disassembly context menu: all 18 items (including 65C816 M/X submenu)
- History context menu: all 21 items (Show/Collapse/Timestamp/Copy)
- Memory context menu: all items (Values As, Interpret As, Track On-Screen)
- Debug display context menu: all items (Force Update, Filter Mode, Palette)
- Source context menu: all items (including Open In > File Explorer/Editor)
- Adjust Colors menu bar: all items (File/View/Options menus)

---

## Notes

- **Script limitations**: The `verify_ui_coverage.py` script reports false negatives
  when ImGui implementations use different control patterns than Windows RC files
  (combo boxes vs radio buttons, different label text, consolidated sub-pages).
  Always verify script output against actual source code.
- **Regenerate**: Run `python3 PORTING/verify_ui_coverage.py` to regenerate the
  script-based report after implementation changes.
- **Verbose mode**: Run with `--verbose` to also see all covered items.
- **JSON mode**: Run with `--json` for machine-readable output.
