# Verification: Hardware / CPU / Memory / Firmware Configuration

Cross-reference of Windows `cmdsystem.cpp` + `cmdcpu.cpp` hardware
configuration handlers against SDL3 `ui_system.cpp` System Configuration
categories.

---

## Handler: OnCommandSystemHardwareMode
**Win source:** cmdsystem.cpp:126-128
**Win engine calls:**
1. `ATUISwitchHardwareMode(ATUIGetNewPopupOwner(), mode, false)` which internally:
   - Checks if mode is already set (early return)
   - May switch profiles based on hardware mode
   - Calls `ATUIConfirmDiscardMemory` if needed
   - Adjusts kernel if incompatible
   - Forces NTSC if switching to 5200
   - Calls `g_sim.ColdReset()` at the end
**SDL3 source:** ui_system.cpp:408-416 -- "Configure System > System > Hardware type" combo
**SDL3 engine calls:**
1. `sim.SetHardwareMode(kHWModeValues[hwIdx])`
**Status:** DIVERGE
**Notes:** SDL3 calls `SetHardwareMode()` directly without:
- `ATUIConfirmDiscardMemory` confirmation dialog
- No automatic cold reset after hardware mode change
- No profile switching logic
- No 5200 mode forcing NTSC video standard
- No kernel compatibility adjustment
- `ATUISwitchHardwareMode()` is stubbed to `return false` in `uiaccessors_stubs.cpp`
**Severity:** Critical -- Hardware mode changes require a cold reset to take effect. Without reset, the emulator runs in an inconsistent state. Missing 5200/NTSC enforcement can cause incorrect video timing.

---

## Handler: OnCommandVideoStandard
**Win source:** cmdsystem.cpp:350-363
**Win engine calls:**
1. Guard: skip if hardware mode is 5200
2. Guard: skip if mode is already set
3. `ATUIConfirmVideoStandardChangeReset()` -- may confirm reset
4. `ATSetVideoStandard(mode)` which calls:
   - Guard: skip if 5200 mode
   - `g_sim.SetVideoStandard(mode)`
   - `ATUIUpdateSpeedTiming()`
   - Display pane resize
5. `ATUIConfirmVideoStandardChangeResetComplete()` -- cold reset if confirmed
**SDL3 source:** ui_system.cpp:420-429 -- "Configure System > System > Video standard" combo + toggle button
**SDL3 engine calls:**
1. `sim.SetVideoStandard(kVideoStdValues[vsIdx])` (combo)
2. `sim.SetVideoStandard(...)` (toggle button)
**Status:** DIVERGE
**Notes:** SDL3 is missing:
- 5200 mode guard (should prevent changing video standard in 5200 mode)
- `ATUIConfirmVideoStandardChangeReset()` reset confirmation
- `ATUIUpdateSpeedTiming()` call to adjust frame timing after change
- Cold reset after video standard change
- NTSC50 and PAL60 are missing from the combo (only NTSC, PAL, SECAM offered)
**Severity:** Critical -- Video standard change without `ATUIUpdateSpeedTiming()` means the emulator continues at the old frame rate. Missing 5200 guard allows invalid configurations. Missing cold reset means old video state persists.

---

## Handler: OnCommandVideoToggleCTIA
**Win source:** cmdsystem.cpp:258-261
**Win engine calls:**
1. `gtia.SetCTIAMode(!gtia.IsCTIAMode())`
**SDL3 source:** ui_system.cpp:433-435 -- "Configure System > System > CTIA mode" checkbox
**SDL3 engine calls:**
1. `sim.GetGTIA().SetCTIAMode(ctia)`
**Status:** MATCH

---

## Handler: OnCommandSystemCPUMode (all variants)
**Win source:** cmdcpu.cpp:27-51
**Win engine calls:**
1. Check if CPU mode is overridden and mode differs: `ATUIConfirmDiscardMemory()` -- confirm dialog
2. `g_sim.SetCPUMode(mode, subCycles)`
3. If mode actually changed: `g_sim.ColdReset()`
**SDL3 source:** ui_system.cpp:462-474 -- "Configure System > CPU > CPU mode" radio buttons
**SDL3 engine calls:**
1. `sim.SetCPUMode(kCPUModes[i].mode, kCPUModes[i].subCycles)`
**Status:** DIVERGE
**Notes:** SDL3 is missing:
- `ATUIConfirmDiscardMemory()` confirmation before CPU mode change
- `ColdReset()` after CPU mode change
- `IsCPUModeOverridden()` check
All 10 CPU mode variants (6502, 65C02, 65C816 at 1x through 23x) are present with correct `subCycles` values.
**Severity:** Critical -- CPU mode change without cold reset leaves the emulator in an inconsistent state (old CPU state with new CPU mode).

---

## Handler: OnCommandSystemCPUToggleIllegalInstructions
**Win source:** cmdcpu.cpp:65
**Win engine calls:**
1. `cpu.SetIllegalInsnsEnabled(!cpu.AreIllegalInsnsEnabled())`
**SDL3 source:** ui_system.cpp:479-481 -- "Configure System > CPU > Enable illegal instructions" checkbox
**SDL3 engine calls:**
1. `sim.GetCPU().SetIllegalInsnsEnabled(illegals)`
**Status:** MATCH

---

## Handler: OnCommandSystemCPUToggleNMIBlocking
**Win source:** cmdcpu.cpp:75
**Win engine calls:**
1. `cpu.SetNMIBlockingEnabled(!cpu.IsNMIBlockingEnabled())`
**SDL3 source:** ui_system.cpp:483-485 -- "Configure System > CPU > Allow BRK/IRQ to block NMI" checkbox
**SDL3 engine calls:**
1. `sim.GetCPU().SetNMIBlockingEnabled(nmiBlock)`
**Status:** MATCH

---

## Handler: OnCommandSystemCPUToggleStopOnBRK
**Win source:** cmdcpu.cpp:70
**Win engine calls:**
1. `cpu.SetStopOnBRK(!cpu.GetStopOnBRK())`
**SDL3 source:** ui_system.cpp:487-489 -- "Configure System > CPU > Stop on BRK instruction" checkbox
**SDL3 engine calls:**
1. `sim.GetCPU().SetStopOnBRK(stopBRK)`
**Status:** MATCH

---

## Handler: OnCommandSystemCPUToggleHistory
**Win source:** cmdcpu.cpp:54
**Win engine calls:**
1. `cpu.SetHistoryEnabled(!cpu.IsHistoryEnabled())`
2. `ATSyncCPUHistoryState()`
**SDL3 source:** ui_system.cpp:491-493 -- "Configure System > CPU > Record instruction history" checkbox
**SDL3 engine calls:**
1. `sim.GetCPU().SetHistoryEnabled(history)`
**Status:** DIVERGE
**Notes:** SDL3 does not call `ATSyncCPUHistoryState()` after toggling history. This function is stubbed to a no-op in SDL3 (`uiaccessors_stubs.cpp:288`), so even if called it would do nothing. On Windows it synchronizes debugger state.
**Severity:** Low -- Only affects debugger integration, which is not yet implemented in SDL3.

---

## Handler: OnCommandSystemCPUTogglePathTracing
**Win source:** cmdcpu.cpp:60
**Win engine calls:**
1. `cpu.SetPathfindingEnabled(!cpu.IsPathfindingEnabled())`
**SDL3 source:** ui_system.cpp:495-497 -- "Configure System > CPU > Track code paths" checkbox
**SDL3 engine calls:**
1. `sim.GetCPU().SetPathfindingEnabled(paths)`
**Status:** MATCH

---

## Handler: OnCommandSystemCPUToggleShadowROM
**Win source:** cmdcpu.cpp:80
**Win engine calls:**
1. `g_sim.SetShadowROMEnabled(!g_sim.GetShadowROMEnabled())`
**SDL3 source:** ui_system.cpp:499-501 -- "Configure System > CPU > Shadow ROMs in fast RAM" checkbox
**SDL3 engine calls:**
1. `sim.SetShadowROMEnabled(shadowROM)`
**Status:** MATCH

---

## Handler: OnCommandSystemCPUToggleShadowCarts
**Win source:** cmdcpu.cpp:84
**Win engine calls:**
1. `g_sim.SetShadowCartridgeEnabled(!g_sim.GetShadowCartridgeEnabled())`
**SDL3 source:** ui_system.cpp:503-505 -- "Configure System > CPU > Shadow cartridges in fast RAM" checkbox
**SDL3 engine calls:**
1. `sim.SetShadowCartridgeEnabled(shadowCart)`
**Status:** MATCH

---

## Handler: Firmware / Kernel selection
**Win source:** cmdsystem.cpp:130-132 (OnCommandSystemKernel)
**Win engine calls:**
1. `ATUISwitchKernel(id)` which internally:
   - Checks kernel compatibility with hardware mode
   - May switch hardware mode if incompatible
   - May adjust memory mode
   - Calls `g_sim.ColdReset()` after change
**SDL3 source:** ui_system.cpp:512-539 -- "Configure System > Firmware > Operating system" list
**SDL3 engine calls:**
1. `sim.SetKernel(fw.mId)` or `sim.SetKernel(0)` (internal)
**Status:** DIVERGE
**Notes:** SDL3 calls `SetKernel()` directly without:
- Kernel/hardware mode compatibility checking
- Automatic hardware mode switching for incompatible kernels
- Memory mode adjustment for XL kernels that can't run with certain sizes
- Cold reset after kernel change
**Severity:** Critical -- Selecting an incompatible kernel (e.g., XL kernel on 800 hardware, or 5200 kernel on computer hardware) will cause boot failures or crashes. No cold reset means kernel change doesn't take effect.

---

## Handler: Firmware / BASIC selection
**Win source:** cmdsystem.cpp:134-136 (OnCommandSystemBasic)
**Win engine calls:**
1. `ATUISwitchBasic(basicId)` which internally:
   - `g_sim.SetBasic(basicId)`
   - `g_sim.ColdReset()`
**SDL3 source:** ui_system.cpp:542-567 -- "Configure System > Firmware > BASIC" list
**SDL3 engine calls:**
1. `sim.SetBasic(fw.mId)` or `sim.SetBasic(0)`
**Status:** DIVERGE
**Notes:** SDL3 does not call `ColdReset()` after changing the BASIC ROM. Windows always cold resets.
**Severity:** Medium -- BASIC ROM change won't take effect until next manual cold reset.

---

## Handler: BASIC enable/disable (Firmware page)
**Win source:** cmdsystem.cpp:198-205 (OnCommandSystemToggleBASIC)
**Win engine calls:**
1. `ATUIConfirmBasicChangeReset()`
2. `g_sim.SetBASICEnabled(!g_sim.IsBASICEnabled())`
3. `ATUIConfirmBasicChangeResetComplete()`
**SDL3 source:** ui_system.cpp:544-545 -- "Configure System > Firmware > Enable internal BASIC" checkbox
**SDL3 engine calls:**
1. `sim.SetBASICEnabled(basicEnabled)`
**Status:** DIVERGE
**Notes:** Same issue as System menu toggle -- no `ATUIConfirmBasicChangeReset()` flow.
**Severity:** Medium -- BASIC enable/disable may not take effect without manual cold reset.

---

## Handler: OnCommandSystemMemoryMode
**Win source:** cmdsystem.cpp:138-139
**Win engine calls:**
1. `ATUISwitchMemoryMode(ATUIGetNewPopupOwner(), mode)` which internally:
   - Guard: skip if mode already set
   - Guard: restrict valid modes based on hardware mode (5200 = 16K only, 800XL has restrictions, etc.)
   - `ATUIConfirmDiscardMemory()` confirmation
   - `g_sim.SetMemoryMode(mode)`
   - `g_sim.ColdReset()`
**SDL3 source:** ui_system.cpp:600-606 -- "Configure System > Memory > Memory Size" combo
**SDL3 engine calls:**
1. `sim.SetMemoryMode(kMemModeValues[mmIdx])`
**Status:** DIVERGE
**Notes:** SDL3 is missing:
- Hardware mode restrictions (5200 should only allow 16K; 800XL has specific invalid sizes)
- `ATUIConfirmDiscardMemory()` confirmation
- Cold reset after memory mode change
- Missing memory modes from combo: `320K_Compy` and `576K_Compy` are absent from the selectable list (present in overview display code but not in the combo). The combo has 13 entries but the enum has 15 modes.
**Severity:** Critical -- Memory mode change without cold reset is invalid. Missing hardware restrictions allow impossible configurations.

---

## Handler: OnCommandSystemAxlonMemoryMode
**Win source:** cmdsystem.cpp:142-151
**Win engine calls:**
1. Guard: skip if 5200 mode or same value
2. `ATUIConfirmSystemChangeReset()` confirmation
3. `g_sim.SetAxlonMemoryMode(bankBits)`
4. `ATUIConfirmSystemChangeResetComplete()`
**SDL3 source:** N/A (no Axlon bank bits control)
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** SDL3 has `SetAxlonAliasingEnabled()` (ui_system.cpp:625-627) but no Axlon bank bits / memory size selector. The Windows version allows setting 0, 4, 5, 6, 7, 8 bank bits via separate menu items.
**Severity:** Medium -- Axlon memory expansion size cannot be configured; defaults are used.

---

## Handler: OnCommandSystemHighMemBanks
**Win source:** cmdsystem.cpp:153-162
**Win engine calls:**
1. Guard: skip if not 65C816 mode or same value
2. `ATUIConfirmSystemChangeReset()` confirmation
3. `g_sim.SetHighMemoryBanks(banks)`
4. `ATUIConfirmSystemChangeResetComplete()`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** No UI for setting 65C816 high memory bank count. Windows allows 0, 16, 64, 128, 256 banks.
**Severity:** Medium -- 65C816 high memory configuration unavailable; defaults used.

---

## Handler: OnCommandSystemToggleMapRAM
**Win source:** cmdsystem.cpp:164-165
**Win engine calls:**
1. `g_sim.SetMapRAMEnabled(!g_sim.IsMapRAMEnabled())`
**SDL3 source:** ui_system.cpp:617-619 -- "Configure System > Memory > Enable MapRAM" checkbox
**SDL3 engine calls:**
1. `sim.SetMapRAMEnabled(mapRAM)`
**Status:** MATCH

---

## Handler: OnCommandSystemToggleUltimate1MB
**Win source:** cmdsystem.cpp:168-175
**Win engine calls:**
1. `ATUIConfirmSystemChangeReset()` confirmation
2. `g_sim.SetUltimate1MBEnabled(!g_sim.IsUltimate1MBEnabled())`
3. `ATUIConfirmSystemChangeResetComplete()`
**SDL3 source:** ui_system.cpp:621-623 -- "Configure System > Memory > Enable Ultimate1MB" checkbox
**SDL3 engine calls:**
1. `sim.SetUltimate1MBEnabled(u1mb)`
**Status:** DIVERGE
**Notes:** SDL3 does not call `ATUIConfirmSystemChangeReset()` / `ATUIConfirmSystemChangeResetComplete()` before/after toggling U1MB. Windows requires reset confirmation.
**Severity:** Medium -- U1MB change should trigger a cold reset to take effect properly.

---

## Handler: OnCommandSystemToggleFloatingIoBus
**Win source:** cmdsystem.cpp:177-184
**Win engine calls:**
1. `ATUIConfirmSystemChangeReset()` confirmation
2. `g_sim.SetFloatingIoBusEnabled(!g_sim.IsFloatingIoBusEnabled())`
3. `ATUIConfirmSystemChangeResetComplete()`
**SDL3 source:** ui_system.cpp:629-631 -- "Configure System > Memory > Enable floating I/O bus" checkbox
**SDL3 engine calls:**
1. `sim.SetFloatingIoBusEnabled(floatingIO)`
**Status:** DIVERGE
**Notes:** Missing `ATUIConfirmSystemChangeReset()` reset confirmation flow. Windows requires it.
**Severity:** Low -- Floating I/O bus is a minor hardware detail; change without reset is unlikely to cause visible issues for most software.

---

## Handler: OnCommandSystemTogglePreserveExtRAM
**Win source:** cmdsystem.cpp:186-187
**Win engine calls:**
1. `g_sim.SetPreserveExtRAMEnabled(!g_sim.IsPreserveExtRAMEnabled())`
**SDL3 source:** ui_system.cpp:633-635 -- "Configure System > Memory > Preserve extended memory on cold reset" checkbox
**SDL3 engine calls:**
1. `sim.SetPreserveExtRAMEnabled(preserveExt)`
**Status:** MATCH

---

## Handler: OnCommandSystemMemoryClearMode
**Win source:** cmdsystem.cpp:190-191
**Win engine calls:**
1. `g_sim.SetMemoryClearMode(mode)` with values Zero, Random, DRAM1, DRAM2, DRAM3
**SDL3 source:** ui_system.cpp:608-613 -- "Configure System > Memory > Memory Clear Pattern" combo
**SDL3 engine calls:**
1. `sim.SetMemoryClearMode(kMemClearValues[mcIdx])` with same 5 values
**Status:** MATCH

---

## Handler: OnCommandSystemToggleMemoryRandomizationEXE
**Win source:** cmdsystem.cpp:194-195
**Win engine calls:**
1. `g_sim.SetRandomFillEXEEnabled(!g_sim.IsRandomFillEXEEnabled())`
**SDL3 source:** ui_system.cpp:773-775 -- "Configure System > Boot > Randomize Memory on EXE Load" checkbox
**SDL3 engine calls:**
1. `sim.SetRandomFillEXEEnabled(randomFillEXE)`
**Status:** MATCH
**Notes:** Located in Boot category in SDL3 rather than Memory, but functionally identical.

---

## Handler: OnCommandSystemToggleFastBoot
**Win source:** cmdsystem.cpp:207-208
**Win engine calls:**
1. `g_sim.SetFastBootEnabled(!g_sim.IsFastBootEnabled())`
**SDL3 source:** ui_system.cpp:645-647 -- "Configure System > Acceleration > Fast boot" checkbox
**SDL3 engine calls:**
1. `sim.SetFastBootEnabled(fastBoot)`
**Status:** MATCH

---

## Handler: OnCommandSystemROMImagesDialog
**Win source:** cmdsystem.cpp:211-222
**Win engine calls:**
1. `ATUIShowDialogFirmware(...)` -- opens firmware management dialog
2. If any changes: `g_sim.LoadROMs()` then `g_sim.ColdReset()`
3. `ATUIRebuildDynamicMenu(0)` and `ATUIRebuildDynamicMenu(1)`
**SDL3 source:** ui_system.cpp:512-574 -- "Configure System > Firmware" inline category
**SDL3 engine calls:**
1. Kernel selection: `sim.SetKernel(fw.mId)`
2. BASIC selection: `sim.SetBasic(fw.mId)`
3. Auto-reload toggle: `sim.SetROMAutoReloadEnabled(autoReload)`
**Status:** DIVERGE
**Notes:** SDL3 shows firmware inline in the category rather than a separate dialog. The SDL3 version does not call `LoadROMs()` + `ColdReset()` after firmware changes. Windows explicitly reloads ROMs and cold resets when firmware changes are made.
**Severity:** Medium -- Firmware changes may not load/take effect until next manual cold reset.

---

## Handler: GTIA Defect Mode
**Win source:** cmdsystem.cpp:246-256 (three handlers for None, Type1, Type2)
**Win engine calls:**
1. `g_sim.GetGTIA().SetDefectMode(ATGTIADefectMode::None|Type1|Type2)`
**SDL3 source:** N/A (not found in ui_system.cpp)
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** No GTIA defect mode selector in SDL3 UI. The defect mode is set by `ATLoadSettings` on startup but cannot be changed at runtime through the UI.
**Severity:** Low -- GTIA defect mode is a niche accuracy setting. Default from settings.cpp is applied on startup.

---

# Summary

| Status | Count |
|--------|-------|
| MATCH | 12 |
| DIVERGE | 9 |
| MISSING | 3 |

## Critical Issues
1. **Hardware Mode** -- No cold reset, no 5200/NTSC enforcement, no kernel compatibility checks, no profile switching, no memory validation. `ATUISwitchHardwareMode()` is stubbed.
2. **Video Standard** -- No 5200 guard, no reset confirmation, no `ATUIUpdateSpeedTiming()`, no cold reset. NTSC50/PAL60 not selectable.
3. **CPU Mode** -- No `ATUIConfirmDiscardMemory()`, no `ColdReset()` after change.
4. **Memory Mode** -- No hardware restrictions, no confirmation, no cold reset. Missing 320K_Compy and 576K_Compy from selectable list.
5. **Kernel Selection** -- No compatibility checks, no hardware mode switching, no cold reset.

## Medium Issues
1. **BASIC ROM selection** -- No cold reset after change.
2. **BASIC enable/disable** -- No reset confirmation flow.
3. **Ultimate1MB toggle** -- No reset confirmation.
4. **Firmware dialog** -- No `LoadROMs()` + `ColdReset()` after changes.
5. **Axlon bank bits** -- No UI to configure (only aliasing toggle exists).
6. **65C816 high memory banks** -- No UI to configure.

## Low Issues
1. **CPU history toggle** -- Missing `ATSyncCPUHistoryState()` (stubbed anyway).
2. **Floating I/O bus** -- Missing reset confirmation (minor hardware detail).
3. **GTIA defect mode** -- No runtime UI selector (set by settings on startup).

## Systemic Pattern

The SDL3 `ui_system.cpp` consistently calls simulator setters directly
(`sim.SetFoo(value)`) without the wrapper functions that Windows uses
(`ATUISwitchFoo()`).  The Windows wrappers handle:
- Reset confirmation dialogs
- Cold reset after state changes that require it
- Hardware/firmware compatibility validation
- Speed timing updates after video standard changes

The root cause is that `ATUISwitchHardwareMode()`, `ATUISwitchMemoryMode()`,
`ATUIConfirmSystemChangeReset()`, `ATUIConfirmBasicChangeReset()`, and
`ATUIConfirmVideoStandardChangeReset()` are all stubbed to no-ops in the
SDL3 build.  Implementing these functions (or their equivalent logic inline
in the ImGui handlers) would resolve most of the critical and medium issues
simultaneously.
