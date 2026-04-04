# AltirraSDL — Claude Code Agent Prompts

Generated: 2026-04-04
Updated: 2026-04-04 (v2 — removed 8 prompts for already-implemented features,
merged small tasks, fixed incorrect file references)

Purpose: Each section below is a self-contained prompt for a Claude Code LLM agent.
Run agents sequentially or in parallel (parallel-safe agents are marked).

Each prompt instructs the agent to analyze the Windows reference, implement the
missing feature in the SDL3/ImGui build, and verify the result compiles and is
correct.  The prompts are ordered by dependency: foundational wiring first, then
feature groups.

> **Verified as already implemented (removed from v1):**
> Disassembly context menu (all 18 items present), History context menu (all 21
> items present), Breakpoint editor dialog (full Add/Edit modal with conditions),
> Disk drive context menu (35+ items including interleave/filesystem/swap/shift),
> Debug display context menu (Force Update + palette modes), Source pane context
> menu (including Open In submenu), Adjust Colors menu bar (File/View/Options
> with Export/Solver/Shifts), Console context menu (Copy All + Clear All).

---

## Table of Contents

| # | Prompt | Scope | Parallel-safe |
|---|--------|-------|:---:|
| 1 | [Wire Verifier + Source File List to Menu](#1-wire-verifier-and-source-file-list-to-menu) | 3 files, ~30 lines | Yes |
| 2 | [Text Selection Submenu](#2-text-selection-submenu) | 2-3 files | Yes |
| 3 | [Window Pane Management + Display/Printer Panes](#3-window-pane-management-and-displayprinter-panes) | 2-3 files | Yes |
| 4 | [Disk Explorer Context + Options Menu](#4-disk-explorer-context-menu-and-options-menu) | 1-2 files | Yes |
| 5 | [Palette Preview + Export in Adjust Colors](#5-palette-preview-and-export-in-adjust-colors) | 1 file | Yes |
| 6 | [Palette Solver in Adjust Colors](#6-palette-solver-in-adjust-colors) | 2-3 files | Yes |
| 7 | [Create Disk: Filesystem Combo](#7-create-disk-dialog-filesystem-combo) | 1 file | Yes |
| 8 | [Video Recording: Missing Frame Rate Options](#8-video-recording-missing-frame-rate-options) | 1 file | Yes |
| 9 | [Profiler CSV Export + Boundary Rule](#9-profiler-csv-export-and-boundary-rule-dialog) | 2 files | Yes |
| 10 | [Trace Viewer: Tape Menu Link](#10-trace-viewer-go-to-tape-editor) | 1 file | Yes |
| 11 | [Printer Graphic Output + Context Menu](#11-printer-graphic-output-and-context-menu) | 1-2 files | No |
| 12 | [Create VHD Dialog](#12-create-vhd-dialog) | 2-3 files | Yes |
| 13 | [Progress + Error Dialogs](#13-progress-and-error-dialogs) | 2-3 files | Yes |
| 14 | [Debug Font Chooser](#14-debug-font-chooser-dialog) | 2-3 files | Yes |
| 15 | [Keyboard Customize Dialog](#15-keyboard-layout-customize-dialog) | 2-3 files | No |
| 16 | [Keyboard Shortcut Rebinding](#16-keyboard-shortcut-rebinding) | 2-4 files | No |
| 17 | [PAL Artifacting + Bloom Scanline Compensation](#17-pal-artifacting-and-bloom-scanline-compensation-wiring) | 1 file | Yes |
| 18 | [Librashader Completion](#18-librashader-parameter-ui-and-state-management) | 2-3 files | Yes |
| 19 | [Shader Preset Selector UI](#19-shader-preset-selector-ui) | 2-3 files | No (depends on 18) |
| 20 | [View Stubs: Calibrate + HUD + Pan/Zoom](#20-view-menu-stubs-calibrate-customize-hud-panzoom) | 2-3 files | Yes |
| 21 | [Help: Contents + Export Debugger Help](#21-help-system-contents-and-export-debugger-help) | 2-3 files | Yes |
| 22 | [Modem Device Dialogs: Missing Toggles](#22-modem-device-dialogs-missing-network-toggles) | 1 file | Yes |
| 23 | [Console Copy Selection](#23-console-copy-selection) | 1 file | Yes |

---

## 1. Wire Verifier and Source File List to Menu

```

```

---

## 2. Text Selection Submenu

```

```

---

## 3. Window Pane Management and Display/Printer Panes

```

```

---

## 4. Disk Explorer Context Menu and Options Menu

```

```

---

## 5. Palette Preview and Export in Adjust Colors

```
You are working on the AltirraSDL project — an SDL3/Dear ImGui port of the
Altirra Atari emulator.  The Adjust Colors dialog (ui_display.cpp) already
has a full menu bar with File > Export Palette... wired, and a "Palette
Preview" collapsible header.  However, the actual palette preview rendering
(the 16x16 grid of color swatches) may be incomplete, and the Export function
needs verification.

YOUR TASK: Verify and complete the palette preview + export functionality.

STEP 1 — Read src/AltirraSDL/source/ui_display.cpp:
  - Find the "Palette Preview" section (search for "CollapsingHeader" or
    "Palette Preview").
  - Find the File > Export Palette... handler.
  - Check if the 16x16 color grid is actually rendered or if it's just a
    header with no content.

STEP 2 — If the palette preview grid is NOT rendered:
  a) After the CollapsingHeader, render a 16x16 grid of colored rectangles
     using ImGui::GetWindowDrawList()->AddRectFilled().
  b) Each cell shows one of the 256 Atari colors (16 hues x 16 luminances).
  c) Get the palette from GTIA or the color computation code.
  d) Each cell ~12x12 pixels with 1px spacing.
  e) Tooltip on hover showing color index (hex) and RGB values.
  f) Update the preview in real-time as sliders are adjusted.

STEP 3 — Verify Export Palette:
  - Check that File > Export Palette opens an SDL save dialog.
  - Check that it writes 768 bytes (256 * 3 bytes RGB) — the standard .pal
    format used by Atari emulators.
  - If the export is wired but the actual file writing is missing, implement it.

STEP 4 — Build and verify:
  - cd build && cmake --build . -j$(nproc)
  - Test: View > Adjust Colors, verify the palette grid appears.
  - Adjust hue slider, verify palette updates in real-time.
  - Export a palette file, verify it's 768 bytes with correct RGB data.
```

---

## 6. Palette Solver in Adjust Colors

```
You are working on the AltirraSDL project — an SDL3/Dear ImGui port of the
Altirra Atari emulator.  The Adjust Colors dialog has a menu bar with
File > Palette Solver... already wired.  The Windows implementation has a
dedicated solver: src/Altirra/source/palettesolver.cpp contains
ATColorPaletteSolver (implements IATColorPaletteSolver), created via
ATCreateColorPaletteSolver().

YOUR TASK: Check if clicking "Palette Solver..." opens a functional dialog.
If not, implement the solver dialog matching Windows IDD_ADJUST_COLORS_REFERENCE.

STEP 1 — Check current state:
  - Read src/AltirraSDL/source/ui_display.cpp, find the "Palette Solver..."
    menu handler.  Does it open a dialog or is it a no-op?
  - Search for "PaletteSolver" or "IATColorPaletteSolver" in SDL3 source.
  - Check if palettesolver.cpp is compiled in CMakeLists.txt.

STEP 2 — Read the Windows implementation:
  - src/Altirra/source/palettesolver.cpp — the solver backend (line 23:
    ATColorPaletteSolver class).  This does color matching optimization.
  - src/Altirra/res/Altirra.rc — IDD_ADJUST_COLORS_REFERENCE (line 2436)
    for the dialog layout.
  - The workflow: load a reference photo of an Atari screen showing known
    colors, define sampling regions, run optimization to find color settings
    that best match the sampled colors.

STEP 3 — Implement (if not already functional):
  a) Ensure palettesolver.cpp compiles in CMake (add to source list if needed).
  b) Create the solver dialog as an ImGui window:
     - Reference image loading via SDL file dialog (PNG/BMP)
     - Display image using ImGui::Image (upload to SDL/GL texture)
     - Color sampling regions on the image
     - Lock Hue / Lock Gamma checkboxes
     - Gain slider, Normalize Contrast checkbox
     - "Match" button runs ATCreateColorPaletteSolver()->Solve()
     - Apply results back to the Adjust Colors settings
  c) Wire "Palette Solver..." to open this dialog.

STEP 4 — Build and verify.

IMPORTANT:
  - The solver backend in palettesolver.cpp is platform-independent and should
    compile as-is.  The UI wrapper is what needs to be created.
  - Study the Windows dialog closely to understand the exact workflow.
```

---

## 7. Create Disk Dialog: Filesystem Combo

```
You are working on the AltirraSDL project. Verify if this problem report is valid and if we should use proposed solution  The Create Disk dialog is at ~80%
— it has format presets, sector count, sector size, and boot sector count, but
is missing the filesystem combo box.

YOUR TASK: Add the filesystem selection combo.

STEP 1 — Read src/Altirra/res/Altirra.rc for IDD_CREATE_DISK — find the
  filesystem combo and its options (No Filesystem, DOS 2.0S, DOS 2.5, MyDOS,
  SpartaDOS, SDFS, etc.).
STEP 2 — Search src/AltirraSDL/source/ for "Create.*Disk" or "NewDisk" to
  find the dialog.  Check what ATIO APIs exist for filesystem creation.
STEP 3 — Add an ImGui::Combo for filesystem selection.  Filter available
  filesystems based on selected sector size/count.  When creating the disk
  with a filesystem, call the ATIO formatting API.
STEP 4 — cd build && cmake --build . -j$(nproc)
```

---

## 8. Video Recording: Missing Frame Rate Options

```
You are working on the AltirraSDL project. Verify if this problem report is valid and if we should use proposed solution  The Video Recording dialog is
missing the "NTSC TV (5:6 NTSC ratio)" frame rate option.

YOUR TASK: Add the missing frame rate option.

STEP 1 — Read src/Altirra/res/Altirra.rc for IDD_VIDEO_RECORDING — find the
  frame rate controls: IDC_FRAMERATE_NORMAL ("accurate") and
  IDC_FRAMERATE_NTSCRATIO ("NTSC TV" / "5:6 NTSC ratio").
STEP 2 — Read src/AltirraSDL/source/ui_recording.cpp — find the frame rate UI.
STEP 3 — Add the NTSC TV ratio option.  This adjusts the recording frame rate
  to 59.94fps (NTSC TV standard) vs the Atari's native 59.92fps.
STEP 4 — cd build && cmake --build . -j$(nproc)
```

---

## 9. Profiler CSV Export and Boundary Rule Dialog

```
You are working on the AltirraSDL project. Verify if this problem report is valid and if we should use proposed solution  The Performance Analyzer's CPU
Profile tab is missing a "Copy As CSV" context menu item, and the Profiler
Boundary Rule dialog (IDD_PROFILER_BOUNDARYRULE) is not implemented.

YOUR TASK: Implement both features.

STEP 1 — Read src/Altirra/res/Altirra.rc for IDR_PROFILE_LIST_CONTEXT_MENU
  (the CSV export menu) and IDD_PROFILER_BOUNDARYRULE (boundary rule dialog).

STEP 2 — Read src/AltirraSDL/source/ui_dbg_traceviewer_panels.cpp — find
  the CPU Profile tab rendering code (search for "Profile" or the mode
  combo with "Instructions", "Functions", etc.).

STEP 3 — Implement:
  a) CSV EXPORT: Add ImGui::BeginPopupContextItem on the profile table.
     Add "Copy As CSV" item.  Format visible columns (Address, Calls,
     Instructions, Cycles, %) as CSV, copy via SDL_SetClipboardText.

  b) BOUNDARY RULE DIALOG: Study the Windows implementation to understand
     what boundary rules are (they define where function boundaries are placed
     in profile analysis).  Implement as an ImGui dialog accessible from the
     profiler's menu bar.

STEP 4 — cd build && cmake --build . -j$(nproc)
```

---

## 10. Trace Viewer: Go To Tape Editor

```
You are working on the AltirraSDL project. Verify if this problem report is valid and if we should use proposed solution  The trace viewer's tape-related
menu is missing a "Go To Tape Editor..." item.

YOUR TASK: Add this menu item.

STEP 1 — Read src/AltirraSDL/source/ui_dbg_traceviewer.cpp — find the menu
  bar rendering code.  Search for existing menu items to see the structure.
STEP 2 — Read src/AltirraSDL/source/ui_main.h — find the ATUIState struct.
  The tape editor is toggled via state.showTapeEditor (wired at ui_menus.cpp
  line 550).
STEP 3 — Add a menu item "Go To Tape Editor..." that sets state.showTapeEditor
  = true.  If the trace viewer has access to a tape position from the current
  selection, also navigate the tape editor to that position.
  Note: The trace viewer pane may not have direct access to ATUIState — check
  how other panes interact with the state (they may use a global or callback).
STEP 4 — cd build && cmake --build . -j$(nproc)
```

---

## 11. Printer Graphic Output and Context Menu

```
You are working on the AltirraSDL project. Verify if this problem report is valid and if we should use proposed solution  The printer output pane
(ui_dbg_printer.cpp) currently shows "graphical printer output — not yet
supported in SDL3 build" for graphics-capable printers.  The Windows version
renders graphical output and has a context menu for saving as PNG/PDF/SVG.

YOUR TASK: Implement graphical printer output and its context menu.

STEP 1 — Read the Windows reference:
  - src/Altirra/res/Altirra.rc: IDR_PRINTER_GRAPHIC_CONTEXT_MENU (line 520):
    Clear, Save As > PNG (96/300 dpi) / PDF / SVG, Reset View, Set Print Position.
  - Find the Windows printer pane for how graphical data is rendered.

STEP 2 — Read src/AltirraSDL/source/ui_dbg_printer.cpp completely.
  Find where graphical output is stubbed out.  Check how the printer device
  delivers bitmap data (IATDevicePrinterOutput or similar).

STEP 3 — Implement:
  a) RENDERING: Receive printer bitmap data, upload to an OpenGL/SDL texture,
     display via ImGui::Image().
  b) CONTEXT MENU (all 7 items from the .rc file):
     - Clear
     - Save As PNG (96 dpi) — scale bitmap, save via SDL_SaveBMP or stb_image_write
     - Save As PNG (300 dpi) — scale bitmap, save
     - Save As PDF — simple single-page PDF wrapping the bitmap
     - Save As SVG 1.1 — SVG with embedded bitmap data
     - Reset View — reset zoom/pan
     - Set Print Position — jump to current print head position
  c) Add zoom/pan controls for navigating printed output.

STEP 4 — cd build && cmake --build . -j$(nproc)

IMPORTANT: PDF and SVG can be generated without external libraries — a minimal
PDF with an embedded image, and an SVG with a data:image/png element.
```

---

## 12. Create VHD Dialog

```
You are working on the AltirraSDL project. Verify if this problem report is valid and if we should use proposed solution  The Create VHD dialog
(IDD_CREATE_VHD) is missing.  This creates VHD (Virtual Hard Disk) images
for the emulated IDE hard disk device.

YOUR TASK: Implement the Create VHD dialog.

STEP 1 — Read src/Altirra/res/Altirra.rc for IDD_CREATE_VHD to find the
  layout and controls.
  Read the Windows VHD creation implementation — search for "CreateVHD" or
  "IDD_CREATE_VHD" in the Windows source.  The dialog has:
    - Filename with Browse button
    - Disk size (MB)
    - VHD type (Fixed / Dynamic)
    - CHS geometry (cylinders, heads, sectors)
    - Auto-compute geometry checkbox
    - OK/Cancel

STEP 2 — Check if VHD writing code exists in ATIO (search for "vhd" in
  src/ATIO/).  The VHD file format is platform-independent.

STEP 3 — Implement:
  a) Create dialog as an ImGui window (centered, NoSavedSettings).
  b) All fields from the Windows dialog.
  c) On OK, create the VHD file using the ATIO writing code.
  d) Wire to the "Create VHD Image..." button in IDD_DEVICE_HARDDISK
     (src/AltirraSDL/source/ui_devconfig_devices.cpp — search for the
     hard disk device config rendering).

STEP 4 — cd build && cmake --build . -j$(nproc)
  Test: create a VHD, mount it in the hard disk device.
```

---

## 13. Progress and Error Dialogs

```
```

---

## 14. Debug Font Chooser Dialog

```
You are working on the AltirraSDL project. Verify if this problem report is valid and if we should use proposed solution  The Debug > Options > Change Font
menu item is a disabled stub.

YOUR TASK: Implement a debug font chooser for ImGui debugger panes.

STEP 1 — The Windows version (IDD_DEBUG_CHOOSE_FONT) uses a standard Windows
  font picker.  On SDL3/Linux we need an ImGui-based alternative.

STEP 2 — Design for SDL3/ImGui:
  - Font file path input with Browse button (SDL file dialog for .ttf/.otf)
  - Font size slider (8-24 pt)
  - Preview text showing a sample in the selected font
  - Apply / OK / Cancel buttons

STEP 3 — Implement:
  a) Create the dialog (in ui_debugger.cpp or new file).
  b) Load the selected font via ImGui::GetIO().Fonts->AddFontFromFileTTF().
  c) Apply the font to debugger panes (push/pop font in rendering code).
  d) Persist font path and size in settings.ini.
  e) Wire the menu item (replace the stub in ui_menus.cpp — search for
     "Change Font" around the Debug > Options submenu).

STEP 4 — cd build && cmake --build . -j$(nproc)

IMPORTANT:
  - ImGui font changes require rebuilding the font atlas — this must happen
    at the start of a frame, not mid-frame.  Set a flag and rebuild on the
    next frame's NewFrame() call.
  - Only debugger panes use the custom font; main UI keeps the default.
  - Include a reasonable default (ImGui's built-in monospace font).
```

---

## 15. Keyboard Layout Customize Dialog

```
You are working on the AltirraSDL project. Verify if this problem report is valid and if we should use proposed solution  The keyboard customize dialog is
not implemented.  In Configure System > Keyboard, there's a "Customize..."
button that should open a dialog for custom key layout editing.

YOUR TASK: Implement the keyboard customize dialog (IDD_KEYBOARD_CUSTOMIZE).

STEP 1 — Read src/Altirra/res/Altirra.rc for IDD_KEYBOARD_CUSTOMIZE.
  Read the Windows implementation — it shows:
  - A visual Atari keyboard with clickable keys
  - Each key shows its current host-key mapping
  - Click a key, press a host key to assign the mapping
  - "Copy Default Layout to Custom Layout" button
  - Clear All / Reset to Default buttons

STEP 2 — Check SDL3 state:
  - Read ui_tools.cpp (Keyboard Shortcuts viewer) and ui_system.cpp (Keyboard
    config page).
  - Read input_sdl3.cpp for current key mapping tables.

STEP 3 — Implement:
  a) Visual Atari keyboard layout using ImGui draw commands
  b) Click-to-select + press-to-assign workflow
  c) Persistence in settings.ini
  d) Copy Default to Custom functionality
  e) Wire the "Customize..." button in Configure System > Keyboard

STEP 4 — cd build && cmake --build . -j$(nproc)

NOTE: The visual keyboard layout must match the real Atari keyboard physically.
Study the Windows implementation for exact key positions and sizes.
```

---

## 16. Keyboard Shortcut Rebinding

```
You are working on the AltirraSDL project. Verify if this problem report is valid and if we should use proposed solution  The Keyboard Shortcuts dialog
(Tools menu) is read-only — it shows current bindings but doesn't allow
changing them.

YOUR TASK: Implement shortcut rebinding.

STEP 1 — Understand the current system:
  - Read src/AltirraSDL/source/ui_tools.cpp — keyboard shortcuts viewer.
  - Read ui_menus.cpp — shortcuts are hardcoded as ImGui::MenuItem hint strings.
  - Read input_sdl3.cpp — keyboard events use if-chains for hotkeys.

STEP 2 — Design a data-driven shortcut system:
  a) Create a shortcut registry: map from command name -> key combination
  b) Load defaults matching the current hardcoded shortcuts
  c) Allow overrides from settings.ini
  d) Modify ui_menus.cpp to read shortcut labels from the registry
  e) Modify input_sdl3.cpp to dispatch through the registry

STEP 3 — Implement:
  a) Shortcut data structure: { command, key: SDL_Scancode, modifiers }
  b) Default shortcut table from existing hardcoded shortcuts
  c) In Keyboard Shortcuts dialog, add double-click to rebind (capture next
     key press)
  d) Conflict detection (warn when key already assigned)
  e) Save/load from settings.ini
  f) Also implement "Assign Keyboard Shortcut..." context menu
     (IDR_CUSTOMIZE_MENU) for right-click on any menu item.

STEP 4 — cd build && cmake --build . -j$(nproc)

IMPORTANT:
  - This is an architectural change touching many files.  Don't break existing
    shortcuts.  Start with data structures, then migrate menu rendering, then
    migrate key event handling.
  - Test every existing shortcut still works after the refactor.
```

---

## 17. PAL Artifacting and Bloom Scanline Compensation Wiring

```
```

---

## 18. Librashader Parameter UI and State Management

```
```

---

## 19. Shader Preset Selector UI

```
```

---

## 20. View Menu Stubs: Calibrate, Customize HUD, Pan/Zoom

```
```

---

## 21. Help System: Contents and Export Debugger Help

```
```

---

## 22. Modem Device Dialogs: Missing Network Toggles

```
```

---

## 23. Console Copy Selection

```
```
