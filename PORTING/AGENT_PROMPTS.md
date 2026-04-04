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
You are working on the AltirraSDL project — an SDL3/Dear ImGui port of the
Altirra Atari emulator.  Two Debug menu items are disabled stubs despite
having implementations ready.  Your task is to wire them both.

=== PART A: VERIFIER DIALOG ===

The Verifier dialog is FULLY IMPLEMENTED in
src/AltirraSDL/source/ui_dbg_verifier.cpp.  The functions are:
  - ATUIShowDialogVerifier()  — sets mbOpen = true, reads current flags
  - ATUIRenderVerifierDialog() — renders the ImGui window each frame when open
Both take NO parameters.  They use a file-static g_verifierDialog struct.

The menu stub is at src/AltirraSDL/source/ui_menus.cpp line 1434:
    ImGui::MenuItem("Verifier...", nullptr, false, false);  // TODO: Phase 10

The Windows version (src/Altirra/source/cmddebug.cpp line 245) shows a
checkmark when the verifier is active via g_sim.IsVerifierEnabled().

TO WIRE:
1. Read src/AltirraSDL/source/ui_main.h — the ATUIState struct (around
   line 18-57) has dialog visibility bools (showCheater, showRewind, etc.).
   Add: bool showVerifier = false;

2. In ui_menus.cpp line 1434, replace the disabled stub with:
     if (ImGui::MenuItem("Verifier...", nullptr, g_sim.IsVerifierEnabled()))
         state.showVerifier = true;
   (state is the ATUIState& passed to the menu render function — check how
   other items use it, e.g., search for "state.showCheater" nearby.)

3. In src/AltirraSDL/source/ui_main.cpp, find the dialog rendering block
   (around line 1415-1430 — a sequence of "if (state.showXxx) ATUIRenderXxx"
   calls).  Add:
     if (state.showVerifier) {
         ATUIShowDialogVerifier();  // opens on first call, noop if already open
         ATUIRenderVerifierDialog();
     }
   Note: ATUIShowDialogVerifier sets mbOpen=true.  ATUIRenderVerifierDialog
   checks mbOpen and returns early if false.  When the user clicks OK or Cancel
   in the dialog, it sets mbOpen=false.  So on the NEXT frame, the render
   function returns immediately, and you should also reset state.showVerifier:
     if (state.showVerifier) {
         ATUIRenderVerifierDialog();
         // Dialog closed itself?
         // Check: does ATUIRenderVerifierDialog return void or bool?
         // Read ui_dbg_verifier.cpp to find out.  If the dialog sets
         // mbOpen=false internally, you need a way to detect it closed.
         // Options: check a return value, or add an ATUIIsVerifierDialogOpen()
         // query, or simply always call both and let the render noop.
     }
   Study how other dialogs handle the open/close lifecycle (e.g., the cheater
   dialog pattern at line ~1424).

4. You may need to forward-declare ATUIShowDialogVerifier() and
   ATUIRenderVerifierDialog() in ui_main.cpp or a shared header.  Check
   how other dialog functions (e.g., ATUIRenderCheater) are declared.

=== PART B: SOURCE FILE LIST ===

The menu stub is at ui_menus.cpp line 1331:
    ImGui::MenuItem("Source File List...", nullptr, false, false);  // TODO: Phase 8

The Windows implementation is in src/Altirra/source/console.cpp line 198:
ATUIShowSourceListDialog().  It shows a list of all source files loaded in
the debugger and lets the user select one to open in a source pane.

Check if any equivalent already exists in the SDL3 codebase:
  - Search for "SourceList" or "source.*list" in src/AltirraSDL/source/
  - If an implementation exists, just wire the menu item.
  - If not, implement a simple dialog:
    a) Query the debugger for loaded source files.  Check IATDebugger for
       methods like GetSourcePathCount/GetSourcePath, or look at what
       ATUIShowSourceListDialog uses in the Windows console.cpp.
    b) Show an ImGui list/table of file paths.
    c) On double-click or OK, call ATOpenSourceWindow(selectedPath).
    d) Wire the menu item to open this dialog.

The menu item should be enabled only when the debugger is active.

=== BUILD AND VERIFY ===
  cd build && cmake --build . -j$(nproc)
  Confirm both menu items are now enabled and functional.
```

---

## 2. Text Selection Submenu

```
You are working on the AltirraSDL project — an SDL3/Dear ImGui port of the
Altirra Atari emulator.  The View menu has a "Text Selection" submenu with
7 items that are all disabled stubs (placeholders).

YOUR TASK: Implement the text selection system for the emulator display,
matching the Windows Altirra functionality.

STEP 1 — Understand what Windows does:
  - Read src/Altirra/res/menu_default.txt and search for the Edit/text
    selection menu items: Edit.CopyEscapedText, Edit.CopyHex, Edit.CopyUnicode,
    Edit.CopyText, Edit.PasteText, Edit.SelectAll, Edit.Deselect.
  - Read src/Altirra/source/main.cpp — the Edit commands are here (NOT in a
    separate cmdedit.cpp — that file does not exist).  Search for
    OnCommandEditCopyText, OnCommandEditCopyEscapedText, etc. around line 2255.
  - The Windows version allows the user to select text from the emulated Atari
    screen (which uses ATASCII encoding) and copy it to the clipboard in various
    formats: plain text (ATASCII->ASCII), escaped text (control chars as {XX}),
    hex dump, and Unicode (ATASCII->Unicode mapping).
  - "Paste Text" types text into the emulated Atari keyboard.
  - "Select All" selects the entire visible screen.
  - "Deselect" clears the selection.

STEP 2 — Check existing SDL3 state:
  - Read src/AltirraSDL/source/ui_menus.cpp around lines 960-972 to see the
    current stubs.
  - Search for any existing text selection infrastructure in the SDL3 codebase
    (grep for "TextSelection", "CopyText", "SelectAll" in ui_*.cpp files).
  - Check if the GTIA/display provides access to the current screen buffer
    content (it does — ANTIC/GTIA maintain the display list and screen memory).

STEP 3 — Implement:
  This requires several components:

  a) SCREEN TEXT EXTRACTION: Access the emulated Atari's screen memory through
     ANTIC.  The screen data is in ATASCII encoding.  You need to:
     - Determine the current graphics mode (GR.0 text mode vs graphics modes)
     - Read the screen memory bytes
     - Convert ATASCII to the appropriate output encoding

  b) SELECTION UI: Allow the user to click-and-drag on the display to select a
     rectangle of text.  This needs:
     - Mouse down/up handling on the display area
     - Visual selection highlight overlay
     - Coordinate mapping from screen pixels to character positions

  c) COPY operations:
     - Copy Text: ATASCII -> UTF-8, then SDL_SetClipboardText()
     - Copy Escaped Text: control chars become {XX} hex notation
     - Copy Hex: raw hex dump of selected bytes
     - Copy Unicode: ATASCII -> Unicode via the Atari Unicode mapping table

  d) PASTE: Convert clipboard UTF-8 text to Atari keystrokes and feed them to
     the POKEY keyboard buffer using PushKey().

  e) Wire menu items: Replace the disabled stubs with functional items.
     Enable/disable based on whether we're in a text mode and whether a
     selection exists.

  Study how the Windows version implements this — look for the selection
  rectangle handling and text extraction logic in src/Altirra/source/main.cpp.

STEP 4 — Build and verify:
  - cd build && cmake --build . -j$(nproc)
  - Test: Boot an Atari program that shows text (BASIC prompt works).
  - Verify you can select text, copy it, and paste it into another application.
  - Verify all 7 menu items work correctly.

IMPORTANT:
  - ATASCII is NOT ASCII.  Use the correct encoding tables from the codebase.
  - The selection should work in GR.0 (text mode) at minimum.
  - Keyboard shortcuts shown in the Windows menu (Alt+Shift+A for Select All,
    Alt+Shift+D for Deselect) should be wired too.
  - Paste needs to handle the keyboard buffer correctly — don't flood it.
```

---

## 3. Window Pane Management and Display/Printer Panes

```
You are working on the AltirraSDL project — an SDL3/Dear ImGui port of the
Altirra Atari emulator.  Six menu items related to window/pane management are
disabled stubs.  Your task is to implement all of them.

=== PART A: WINDOW MANAGEMENT (4 items) ===

The Window menu (ui_menus.cpp ~lines 1597-1600) has disabled stubs for:
  - Window.Close — closes the active debugger pane
  - Window.Undock — undocks the pane from the docking layout to float
  - Window.NextPane — cycles focus to the next debugger pane
  - Window.PrevPane — cycles focus to the previous pane

STEP 1 — Read the Windows reference:
  - src/Altirra/source/cmdwindow.cpp EXISTS and contains the implementations.
    Read OnCommandWindowClose (line 24) and OnCommandWindowUndock (line 38).
  - These operate on the currently focused debugger pane.

STEP 2 — Check SDL3 state:
  - Read src/AltirraSDL/source/ui_debugger.cpp — understand the pane registry,
    ATActivateUIPane, ATCloseUIPane, and how pane focus is tracked.
  - Check ImGui's docking API: ImGui::DockBuilderGetNode,
    ImGui::SetNextWindowFocus, etc.

STEP 3 — Implement:
  a) CLOSE: Determine which debugger pane is focused.  Track the last-focused
     pane ID in ui_debugger.cpp (set it in each pane's Begin/Focus callback).
     Call the close function for that pane.

  b) UNDOCK: Use ImGui's docking API to undock the focused pane.  Set the
     window's dock node to 0 or use ImGui::DockBuilderRemoveNodeDockedWindows.

  c) NEXT/PREVIOUS: Maintain an ordered list of open pane window names in
     ui_debugger.cpp.  On Next, advance to the next and call
     ImGui::SetWindowFocus(name).  On Previous, go backwards.  Wrap around.

  d) Wire all 4 items in ui_menus.cpp.  Enable only when debugger is active
     and at least one pane is open.

=== PART B: DISPLAY AND PRINTER PANES (2 items) ===

The View menu (ui_menus.cpp ~lines 948-949) has disabled stubs for:
  - Pane.Display — opens the emulation display as a dockable debugger pane
  - Pane.PrinterOutput — opens printer output as a dockable pane

STEP 1 — Check what already exists:
  - The Display pane likely already exists when the debugger opens — check the
    DockBuilder layout setup in ui_debugger.cpp.
  - The Printer Output pane is implemented in ui_dbg_printer.cpp.
  - Search for kATUIPaneId_Display and kATUIPaneId_PrinterOutput.

STEP 2 — Implement:
  Each menu item should call ATActivateUIPane with the appropriate pane ID.
  This creates the pane if not open, or focuses it if already open.
  Replace the disabled stubs.  Enable when debugger is active.

=== BUILD AND VERIFY ===
  cd build && cmake --build . -j$(nproc)
  Test: open debugger, verify Close/Undock/Next/Prev work on panes.
  Test: close Display pane, reopen via View > Display.
```

---

## 4. Disk Explorer Context Menu and Options Menu

```
You are working on the AltirraSDL project — an SDL3/Dear ImGui port of the
Altirra Atari emulator.  The Disk Explorer (ui_tools.cpp) is missing most
of its context menu items and the entire Options menu.

YOUR TASK: Implement the missing context menu items and the options menu.

STEP 1 — Understand the Windows version:
  - Read src/Altirra/res/Altirra.rc for IDR_DISK_EXPLORER_CONTEXT_MENU and
    IDR_DISKEXPLORER_MENU.
  - Read the Windows Disk Explorer implementation.

  CONTEXT MENU items to add (currently only Export, Delete, Rename exist):
  1. View — opens the selected file in the viewer pane
  2. New Folder — creates a directory at the current location
  3. Import File... — imports a file from the host filesystem
  4. Import File as Text... — imports with CR/LF -> Atari EOL (0x9B) conversion
  5. Export File as Text... — exports with Atari EOL (0x9B) -> CR/LF conversion
  6. Open (partition) — navigates into a partition on a block device
  7. Import Disk Image (partition) — imports a disk image into a partition
  8. Export Disk Image (partition) — exports a partition as a disk image

  OPTIONS MENU (top-level menu bar of Disk Explorer):
  1. Strict filename checking — radio: enforce 8.3 rules strictly
  2. Relaxed filename checking — radio: allow longer/mixed case names
  3. Adjust Conflicting Filenames — checkbox: auto-rename on collision

STEP 2 — Check SDL3 state:
  - Read the Disk Explorer section of src/AltirraSDL/source/ui_tools.cpp.
  - Find the existing context menu (right-click on file list).
  - Check if the Import function already exists (it should — core functionality
    is listed as 100% complete).

STEP 3 — Implement:
  a) CONTEXT MENU: Add items using ImGui::MenuItem in the existing context menu.
     - View: Set the viewer to show the selected file (viewer already exists).
     - New Folder: Prompt for name, create via the filesystem API.
     - Import File / Import File as Text: Open SDL file dialog, read file,
       convert if text mode (replace 0x0A/0x0D0A with 0x9B), write to disk.
     - Export File as Text: Read from disk, convert 0x9B to 0x0A, save.
     - Partition items: Only enable when image has partitions.

  b) OPTIONS MENU: Add a menu bar to Disk Explorer with an "Options" menu.
     Store filename checking mode in state and persist to settings.

  c) TEXT CONVERSION core:
     - Import: host 0x0A (or 0x0D0A) -> Atari 0x9B
     - Export: Atari 0x9B -> host 0x0A

STEP 4 — Build and verify:
  - cd build && cmake --build . -j$(nproc)
  - Test Import File as Text with a text file, verify EOL conversion.
  - Test Export File as Text, verify output has correct line endings.

IMPORTANT:
  - 0x9B is the Atari EOL character, NOT 0x0A.
  - Partition support may be complex — implement non-partition items first.
  - Persist the filename checking mode in settings.ini.
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
You are working on the AltirraSDL project.  The Create Disk dialog is at ~80%
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
You are working on the AltirraSDL project.  The Video Recording dialog is
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
You are working on the AltirraSDL project.  The Performance Analyzer's CPU
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
You are working on the AltirraSDL project.  The trace viewer's tape-related
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
You are working on the AltirraSDL project.  The printer output pane
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
You are working on the AltirraSDL project.  The Create VHD dialog
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
You are working on the AltirraSDL project.  Two utility dialogs are missing:
IDD_PROGRESS (progress bar for long operations) and IDD_PROGRAM_ERROR (error
notification).  Both are small and share similar patterns.

=== PART A: PROGRESS DIALOG ===

STEP 1 — Search the Windows codebase for IDD_PROGRESS to find all call sites.
  The dialog shows: title, message, progress bar, cancel button.

STEP 2 — Check SDL3: search for "progress" — some operations may already run
  without progress indication (e.g., firmware scanning).

STEP 3 — Implement a generic progress dialog system:
  - ATUIShowProgress(title, message) — opens modal popup
  - ATUIUpdateProgress(fraction, message) — updates bar (0.0-1.0)
  - ATUICloseProgress() — closes
  - ATUIIsProgressCancelled() — checks cancel
  Render as ImGui::BeginPopupModal with ImGui::ProgressBar.
  Wire into firmware scan and any other long-running operation.

=== PART B: ERROR DIALOG ===

STEP 1 — Read src/Altirra/res/Altirra.rc for IDD_PROGRAM_ERROR.
  The Windows dialog shows: error icon, message, OK button, Copy to Clipboard.

STEP 2 — Check SDL3: search for SDL_ShowSimpleMessageBox or error handling.

STEP 3 — Implement:
  - ATUIShowError(message) — opens modal ImGui popup
  - Error text display + OK button + "Copy to Clipboard" button
  - Wire into error paths (catch blocks for MyError, VDException, etc.)

=== BUILD AND VERIFY ===
  cd build && cmake --build . -j$(nproc)
```

---

## 14. Debug Font Chooser Dialog

```
You are working on the AltirraSDL project.  The Debug > Options > Change Font
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
You are working on the AltirraSDL project.  The keyboard customize dialog is
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
You are working on the AltirraSDL project.  The Keyboard Shortcuts dialog
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
You are working on the AltirraSDL project.  Two shader pipeline features
exist as code but are not wired into the render path.  Both are in the same
file: src/AltirraSDL/source/display_backend_gl33.cpp.

=== PART A: PAL ARTIFACTING ===

The PAL artifacting shader (kGLSL_PALArtifacting_FS) exists and mPALProgram
is compiled, but it is NOT wired into the RenderScreenFX pipeline.
mScreenFX.mPALBlendingOffset is checked in the hasEffects flag but never
actually rendered.

TO FIX:
1. Read display_backend_gl33.cpp — find kGLSL_PALArtifacting_FS, mPALProgram,
   and RenderScreenFX().
2. Read the Windows VDDisplay PAL rendering code to understand the pass.
   PAL artifacting blends adjacent frames with a phase offset to simulate PAL
   color encoding.
3. When mScreenFX.mPALBlendingOffset > 0, insert a PAL pass in
   RenderScreenFX — set up uniforms, bind FBO, draw fullscreen triangle.
   May need a ping-pong texture for the extra pass.

=== PART B: BLOOM SCANLINE COMPENSATION ===

ATArtifactingParams::mbBloomScanlineCompensation is stored in the UI but not
wired to the render pipeline.  When enabled, bloom output should be scaled
to compensate for brightness reduction caused by scanlines.

TO FIX:
1. Find where mbBloomScanlineCompensation is read (or should be read) in the
   bloom final composition shader setup.
2. Read the Windows VDDisplay bloom code to understand the compensation factor.
3. Add the compensation uniform to the bloom composition pass.

=== BUILD AND VERIFY ===
  cd build && cmake --build . -j$(nproc)
  Test PAL: enable PAL artifacting in Adjust Colors, verify visual effect.
  Test bloom: enable bloom + scanlines, toggle scanline compensation, verify
  brightness adjustment.
```

---

## 18. Librashader Parameter UI and State Management

```
You are working on the AltirraSDL project.  The librashader integration has
runtime loading and preset application working, but is missing: parameter
enumeration UI, mutual exclusion with built-in effects, and GL state restore.

YOUR TASK: Complete the librashader integration.

STEP 1 — Read:
  - src/AltirraSDL/source/display_librashader.cpp and .h
  - src/AltirraSDL/source/display_backend_gl33.cpp for how librashader is
    called in the render pipeline

STEP 2 — Implement:
  a) PARAMETER UI: Call preset_get_runtime_params() to enumerate tunable
     parameters.  Create an ImGui panel showing a slider for each parameter
     with its name, min, max, and current value.

  b) MUTUAL EXCLUSION: When a librashader preset is active, disable the
     built-in screen effects.  Show a message in Adjust Screen Effects.

  c) GL STATE RESTORE: Save/restore critical GL state around
     gl_filter_chain_frame(): bound framebuffer, viewport, blend state,
     active texture unit, shader program.

STEP 3 — cd build && cmake --build . -j$(nproc)
```

---

## 19. Shader Preset Selector UI

```
You are working on the AltirraSDL project.  There is no UI for selecting
librashader presets.

YOUR TASK: Add a "Shader Preset" submenu to the View menu.

STEP 1 — Read display_librashader.cpp for the preset loading API.
STEP 2 — Implement in ui_menus.cpp:
  a) View > "Shader Preset" submenu with:
     - "(None)" — disables any active preset
     - "Browse..." — SDL file dialog for .slangp/.glslp files
     - Recent presets list (last 5)
  b) Load preset via LibrashaderRuntime::LoadPreset().
  c) Save preset path and recent list in settings.ini.
  d) If librashader unavailable, gray out items with tooltip.

STEP 3 — cd build && cmake --build . -j$(nproc)
  Depends on prompt 18 being completed first.
```

---

## 20. View Menu Stubs: Calibrate, Customize HUD, Pan/Zoom

```
You are working on the AltirraSDL project.  Three View menu items are
disabled stubs: Calibrate..., Customize HUD..., and Pan/Zoom Tool.

YOUR TASK: Implement these three features.

STEP 1 — Read the Windows reference:
  - src/Altirra/source/cmdview.cpp — this file EXISTS and contains:
    - OnCommandViewCalibrate (line 185) — calls ATUICalibrationScreen::ShowDialog()
    - OnCommandViewCustomizeHud (line 181) — calls g_sim.GetUIRenderer()->
      BeginCustomization()

  a) CALIBRATE: Opens a calibration screen with test patterns (brightness,
     contrast, color bars, geometry grid).  Find ATUICalibrationScreen in the
     Windows source for the full implementation.

  b) CUSTOMIZE HUD: Opens a dialog to configure which HUD elements are shown.
     Read ui_indicators.cpp to understand current HUD elements.

  c) PAN/ZOOM TOOL: Enables a mouse mode for panning/zooming the display.

STEP 2 — Implement each:
  a) Calibrate: Render test patterns using ImGui draw commands or OpenGL.
  b) Customize HUD: ImGui dialog with checkboxes for each HUD element.
  c) Pan/Zoom: Mode toggle changing mouse behavior on display area.

STEP 3 — Wire all 3 menu items (replace stubs in ui_menus.cpp).

STEP 4 — cd build && cmake --build . -j$(nproc)
```

---

## 21. Help System: Contents and Export Debugger Help

```
You are working on the AltirraSDL project.  Two Help menu items are stubs:
"Contents" and "Export Debugger Help...".

YOUR TASK: Implement both.

STEP 1 — Implement Contents:
  On Windows this opens a .chm help file.  On SDL3/Linux, use
  SDL_OpenURL("https://www.virtualdub.org/altirra-help/") to open online docs
  in the default browser.  Replace the stub in ui_menus.cpp (search for
  "Contents" in the Help menu rendering).

STEP 2 — Implement Export Debugger Help:
  The debugger has built-in help text (the same content shown by the .help
  command in the console, loaded via ATLoadMiscResource for "dbghelp.txt").
  - Open an SDL save file dialog for .txt files.
  - Write the help text to the chosen file.
  - Replace the stub in ui_menus.cpp.

STEP 3 — cd build && cmake --build . -j$(nproc)
```

---

## 22. Modem Device Dialogs: Missing Network Toggles

```
You are working on the AltirraSDL project.  Several modem device config
dialogs may be missing network toggle checkboxes that the Windows version has:
"Emulate Telnet protocol", "Allow outbound connections", "Accept IPv6
connections".

YOUR TASK: Verify and add any missing toggles.

STEP 1 — Read src/Altirra/res/Altirra.rc for IDD_DEVICE_1030MODEM,
  IDD_DEVICE_1400XL, IDD_DEVICE_MODEM, IDD_DEVICE_POCKETMODEM,
  IDD_DEVICE_SX212.  Each should have IDC_TELNET, IDC_ALLOW_OUTBOUND,
  IDC_ACCEPT_IPV6 checkboxes.

STEP 2 — Read src/AltirraSDL/source/ui_devconfig_devices.cpp.  Find each
  modem dialog's rendering code.  Many modems share a common helper function
  — if so, fixing the helper fixes all dialogs at once.

STEP 3 — For any missing checkboxes:
  a) Add ImGui::Checkbox with the appropriate label.
  b) Wire to the device property (look for property names like "telnet",
     "outbound", "ipv6" in the Windows dialog code).
  c) Verify the setting is saved and loaded correctly.

STEP 4 — cd build && cmake --build . -j$(nproc)
  Test for each modem type that the new checkboxes appear and persist.
```

---

## 23. Console Copy Selection

```
You are working on the AltirraSDL project.  The debugger console pane
(ui_dbg_console.cpp) has "Copy All" and "Clear All" in its context menu.
The Windows version (IDR_DEBUGGER_MENU) has "Copy" (Ctrl+C) for copying
selected text, and "Clear All".

YOUR TASK: Add a "Copy" menu item that copies only the selected text.

STEP 1 — Read src/AltirraSDL/source/ui_dbg_console.cpp.  Find the context
  menu at line 199.  Understand how text selection works in the console
  (ImGui::InputTextMultiline may handle selection internally, or there may
  be a custom buffer).

STEP 2 — Add a "Copy" item before "Copy All":
  - If ImGui provides selection access: get selected text, copy via
    SDL_SetClipboardText.
  - If the console uses a custom buffer: implement selection tracking and
    copy the selected range.
  - Add Ctrl+C keyboard shortcut handling.

STEP 3 — cd build && cmake --build . -j$(nproc)

NOTE: This is a small change — if the console uses ImGui::InputTextMultiline,
Ctrl+C may already work via ImGui's built-in handling.  Check first.
```
