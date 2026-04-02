# Verification 07 -- Disk Operations

Cross-reference of Windows disk handlers (cmds.cpp / main.cpp) against SDL3 implementation
(ui_disk.cpp, ui_main.cpp, ui_system.cpp).

---

## Handler: OnCommandDiskDrivesDialog
**Win source:** main.cpp:2435
**Win engine calls:**
1. `ATUIShowDiskDriveDialog((VDGUIHandle)g_hwnd)`
**SDL3 source:** ui_main.cpp:915 -- "File > Disk Drives..."
**SDL3 engine calls:**
1. Sets `state.showDiskManager = true`, which triggers `ATUIRenderDiskManager()` in ui_disk.cpp
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandDiskAttach(index) [D1-D8]
**Win source:** main.cpp:2531 (delegates to ATUIFutureAttachDisk, lines 2471-2535)
**Win engine calls:**
1. Check `g_sim.IsStorageDirty(ATStorageId(kATStorageId_Disk + index))` -- prompt to discard if dirty
2. Show open file dialog (ATR/PRO/ATX/XFD/DCM/ZIP/GZ/ATZ/ARC filters)
3. `DoLoad(g_hwnd, path, nullptr, 0, kATImageType_Disk, NULL, index)` -- general loader
4. `ATAddMRUListItem(path)` -- update recent files
**SDL3 source:** ui_main.cpp:919-957 -- "File > Attach Disk > Drive N..."
**SDL3 engine calls:**
1. `SDL_ShowOpenFileDialog(AttachDiskCallback, ...)` with ATR/XFD/DCM/PRO/ATX/GZ/ZIP/ATZ filters
2. Callback pushes `kATDeferred_AttachDisk` deferred action
3. Deferred handler (ui_main.cpp:717): `g_sim.GetDiskInterface(idx).LoadDisk(path)`
**Status:** DIVERGE
**Notes:**
- SDL3 does NOT check for dirty disk before overwriting -- no discard confirmation prompt
- SDL3 uses `LoadDisk()` directly instead of `DoLoad()` general loader (no MRU update, no ARC support through general loader)
- SDL3 does not add to MRU list
- SDL3 missing `.arc` from file filter extensions
**Severity:** Medium

---

## Handler: OnCommandDiskDetach(index) [D1-D8]
**Win source:** main.cpp:2606 (delegates to ATUIFutureDetachDisk, lines 2537-2610)
**Win engine calls:**
1. Check `g_sim.IsStorageDirty(ATStorageId(kATStorageId_Disk + index))` -- prompt to discard if dirty
2. `g_sim.GetDiskInterface(index).UnloadDisk()`
3. `g_sim.GetDiskDrive(index).SetEnabled(false)`
**SDL3 source:** ui_main.cpp:960-978 -- "File > Detach Disk > Drive N"
**SDL3 engine calls:**
1. `sim.GetDiskInterface(i).UnloadDisk()`
**Status:** DIVERGE
**Notes:**
- SDL3 does NOT check for dirty disk before detaching -- no discard confirmation prompt
- SDL3 does NOT call `GetDiskDrive(i).SetEnabled(false)` after unloading
**Severity:** Medium

---

## Handler: OnCommandDiskDetachAll
**Win source:** main.cpp:2612 (delegates to ATUIFutureDetachDisk with index=-1)
**Win engine calls:**
1. Check all 15 drives for dirty storage, prompt `ATUIConfirmDiscardAllStorage()` if any dirty
2. For each of 15 drives: `UnloadDisk()` + `SetEnabled(false)`
**SDL3 source:** ui_main.cpp:961-966 -- "File > Detach Disk > All"
**SDL3 engine calls:**
1. For each of 15 drives: `UnloadDisk()` (only if loaded)
**Status:** DIVERGE
**Notes:**
- SDL3 does NOT check for dirty storage before detaching
- SDL3 does NOT call `SetEnabled(false)` on the disk drives
**Severity:** Medium

---

## Handler: OnCommandDiskRotate(delta)
**Win source:** main.cpp:2618
**Win engine calls:**
1. Find highest active drive (checking `IsEnabled()` and `GetClientCount() > 1`)
2. `g_sim.RotateDrives(activeDrives, delta)`
3. `uir->SetStatusMessage(...)` -- show status bar message with new D1 image
**SDL3 source:** ui_main.cpp:920-923 -- "File > Attach Disk > Rotate Down/Up"
**SDL3 engine calls:**
1. `sim.RotateDrives(8, 1)` (Rotate Down) or `sim.RotateDrives(8, -1)` (Rotate Up)
**Status:** DIVERGE
**Notes:**
- Windows dynamically finds highest active drive count; SDL3 hardcodes 8
- SDL3 does not show status message after rotation
**Severity:** Low

---

## Handler: OnCommandDiskToggleSIOPatch
**Win source:** main.cpp:2439
**Win engine calls:**
1. `g_sim.SetDiskSIOPatchEnabled(!g_sim.IsDiskSIOPatchEnabled())`
**SDL3 source:** ui_system.cpp:659-661 -- "Configure System > Acceleration > D: patch (Disk SIO)"
**SDL3 engine calls:**
1. `sim.SetDiskSIOPatchEnabled(diskSioPatch)` via checkbox
**Status:** MATCH
**Notes:** No standalone menu toggle in SDL3 (Windows has it in Disk menu); SDL3 exposes it in Configure System > Acceleration page, which is the same location as the Windows Configure System dialog.
**Severity:** N/A

---

## Handler: OnCommandDiskToggleSIOOverrideDetection
**Win source:** main.cpp:2443
**Win engine calls:**
1. `g_sim.SetDiskSIOOverrideDetectEnabled(!g_sim.IsDiskSIOOverrideDetectEnabled())`
**SDL3 source:** ui_system.cpp:705-707 -- "Configure System > Acceleration > SIO override detection"
**SDL3 engine calls:**
1. `sim.SetDiskSIOOverrideDetectEnabled(sioOverride)` via checkbox
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandDiskToggleAccurateSectorTiming
**Win source:** main.cpp:2447
**Win engine calls:**
1. `g_sim.SetDiskAccurateTimingEnabled(!g_sim.IsDiskAccurateTimingEnabled())`
**SDL3 source:** ui_system.cpp:1002-1005 -- "Configure System > Disk > Accurate sector timing"
**SDL3 engine calls:**
1. `sim.SetDiskAccurateTimingEnabled(accurateTiming)` via checkbox
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandDiskToggleDriveSounds
**Win source:** main.cpp:2463 (uses ATUISetDriveSoundsEnabled, lines 2456-2461)
**Win engine calls:**
1. `ATUIGetDriveSoundsEnabled()` -- checks drive 0
2. For all 15 drives: `GetDiskInterface(i).SetDriveSoundsEnabled(enabled)`
**SDL3 source:** ui_system.cpp:1007-1009 -- "Configure System > Disk > Play drive sounds"
**SDL3 engine calls:**
1. `ATUIGetDriveSoundsEnabled()` / `ATUISetDriveSoundsEnabled(driveSounds)`
**Status:** MATCH
**Notes:** SDL3 uses the same accessor functions `ATUIGetDriveSoundsEnabled` / `ATUISetDriveSoundsEnabled` which iterate all 15 drives.
**Severity:** N/A

---

## Handler: OnCommandDiskToggleSectorCounter
**Win source:** main.cpp:2467
**Win engine calls:**
1. `g_sim.SetDiskSectorCounterEnabled(!g_sim.IsDiskSectorCounterEnabled())`
**SDL3 source:** ui_system.cpp:1011-1013 -- "Configure System > Disk > Show sector counter"
**SDL3 engine calls:**
1. `sim.SetDiskSectorCounterEnabled(sectorCounter)` via checkbox
**Status:** MATCH
**Severity:** N/A

---

## Handler: Disk.ToggleBurstTransfers
**Win source:** cmds.cpp:1004-1012
**Win engine calls:**
1. `g_sim.SetDiskBurstTransfersEnabled(!g_sim.GetDiskBurstTransfersEnabled())`
**SDL3 source:** ui_system.cpp:691-693 -- "Configure System > Acceleration > D: burst I/O"
**SDL3 engine calls:**
1. `sim.SetDiskBurstTransfersEnabled(diskBurst)` via checkbox
**Status:** MATCH
**Severity:** N/A

---

## Disk Drives Dialog (ui_disk.cpp)

### Per-drive Browse/Mount
**Win source:** Windows IDD_DISK_DRIVES dialog -- IDC_BROWSE per drive
**SDL3 source:** ui_disk.cpp:302-307 -- "..." button per drive row
**SDL3 engine calls:**
1. `SDL_ShowOpenFileDialog(DiskMountCallback, ...)` with drive index
2. Callback: `g_sim.GetDiskInterface(driveIdx).LoadDisk(widePath.c_str())`
**Status:** MATCH
**Severity:** N/A

### Per-drive Eject
**Win source:** Windows IDD_DISK_DRIVES dialog -- IDC_EJECT per drive
**SDL3 source:** ui_disk.cpp:310-312 -- "Eject" button per drive row
**SDL3 engine calls:**
1. `di.UnloadDisk()`
**Status:** MATCH
**Severity:** N/A

### Per-drive Write Mode
**Win source:** Windows IDD_DISK_DRIVES dialog -- IDC_WRITEMODE combo
**SDL3 source:** ui_disk.cpp:289-299 -- Write mode combo (Off/R-O/VRWSafe/VRW/R-W)
**SDL3 engine calls:**
1. `di.SetWriteMode(kWriteModeValues[wmIdx])`; "Off" calls `di.UnloadDisk()`
**Status:** MATCH
**Severity:** N/A

### Context Menu: New Disk
**Win source:** Windows ATNewDiskDialog
**SDL3 source:** ui_disk.cpp:56-122 -- "Create Disk" sub-dialog opened from "+" context menu
**SDL3 engine calls:**
1. `di.CreateDisk(sectorCount, bootSectorCount, sectorSize)`
**Status:** MATCH
**Severity:** N/A

### Context Menu: Save Disk / Save Disk As
**Win source:** Windows disk drives context menu
**SDL3 source:** ui_disk.cpp:326-333 -- "Save Disk" / "Save Disk As..." in "+" popup
**SDL3 engine calls:**
1. Save: `di.SaveDisk()`
2. Save As: `SDL_ShowSaveFileDialog(DiskSaveAsCallback, ...)` then `di.SaveDiskAs(path, kATDiskImageFormat_ATR)`
**Status:** MATCH
**Severity:** N/A

### Context Menu: Revert
**Win source:** Windows disk drives context menu
**SDL3 source:** ui_disk.cpp:335-336 -- "Revert" in "+" popup
**SDL3 engine calls:**
1. `di.RevertDisk()`
**Status:** MATCH
**Severity:** N/A

### Emulation Level (global)
**Win source:** Windows IDD_DISK_DRIVES dialog -- IDC_EMULATION_LEVEL
**SDL3 source:** ui_disk.cpp:354-363 -- "Emulation level" combo
**SDL3 engine calls:**
1. For all 15 drives: `sim.GetDiskDrive(i).SetEmulationMode(kEmuModeValues[emuIdx])`
**Status:** MATCH
**Severity:** N/A

### Drives 1-8 / 9-15 Tabs
**Win source:** Windows IDD_DISK_DRIVES dialog -- radio buttons
**SDL3 source:** ui_disk.cpp:234-237 -- RadioButton("Drives 1-8") / RadioButton("Drives 9-15")
**Status:** MATCH
**Severity:** N/A

---

## Summary

| Handler | Status | Severity |
|---------|--------|----------|
| DiskDrivesDialog | MATCH | -- |
| DiskAttach (D1-D8) | DIVERGE | Medium |
| DiskDetach (D1-D8) | DIVERGE | Medium |
| DiskDetachAll | DIVERGE | Medium |
| DiskRotate | DIVERGE | Low |
| DiskToggleSIOPatch | MATCH | -- |
| DiskToggleSIOOverrideDetection | MATCH | -- |
| DiskToggleAccurateSectorTiming | MATCH | -- |
| DiskToggleDriveSounds | MATCH | -- |
| DiskToggleSectorCounter | MATCH | -- |
| Disk.ToggleBurstTransfers | MATCH | -- |
| Dialog: Browse/Mount | MATCH | -- |
| Dialog: Eject | MATCH | -- |
| Dialog: Write Mode | MATCH | -- |
| Dialog: New Disk | MATCH | -- |
| Dialog: Save/SaveAs | MATCH | -- |
| Dialog: Revert | MATCH | -- |
| Dialog: Emulation Level | MATCH | -- |
| Dialog: Drive tabs | MATCH | -- |

### Key Divergences

1. **No dirty-disk confirmation** -- SDL3 attach/detach operations skip the "discard modified image?" prompt that Windows shows via ATUIFuture. This could lead to data loss.
2. **SetEnabled(false) missing on detach** -- Windows disables the disk drive emulator after unloading; SDL3 only unloads the disk image. This may leave phantom drive entries.
3. **DiskRotate hardcodes 8 drives** -- Windows dynamically finds highest active drive; SDL3 always rotates across 8.
4. **No .arc filter** -- SDL3 attach file dialog omits `.arc` archive support.
5. **No MRU update** -- SDL3 does not call `ATAddMRUListItem()` after disk attach.
