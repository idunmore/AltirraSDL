# Verification: System Reset / Pause / Speed Handlers

Cross-reference of Windows `cmdsystem.cpp` reset/pause/speed handlers
against SDL3 `ui_main.cpp` System menu items.

---

## Handler: OnCommandSystemTogglePause
**Win source:** cmdsystem.cpp:53
**Win engine calls:**
1. `g_sim.IsRunning()` to check state
2. `g_sim.Pause()` or `g_sim.Resume()` depending on state
**SDL3 source:** ui_main.cpp:1456-1459 -- "System > Pause"
**SDL3 engine calls:**
1. `sim.IsPaused()` to check state
2. `sim.Resume()` or `sim.Pause()` depending on state
**Status:** MATCH
**Notes:** SDL3 uses `IsPaused()` (inverse of `IsRunning()`); logic is equivalent.

---

## Handler: OnCommandSystemWarmReset
**Win source:** cmdsystem.cpp:60
**Win engine calls:**
1. `g_sim.WarmReset()`
2. `g_sim.Resume()`
**SDL3 source:** ui_main.cpp:1443-1446 -- "System > Warm Reset"
**SDL3 engine calls:**
1. `sim.WarmReset()`
2. `sim.Resume()`
**Status:** MATCH

---

## Handler: OnCommandSystemColdReset
**Win source:** cmdsystem.cpp:65
**Win engine calls:**
1. `g_sim.ColdReset()`
2. `g_sim.Resume()`
3. If `!g_kbdOpts.mbAllowShiftOnColdReset`: `g_sim.GetPokey().SetShiftKeyState(false, true)`
**SDL3 source:** ui_main.cpp:1447-1449 -- "System > Cold Reset"
**SDL3 engine calls:**
1. `sim.ColdReset()`
2. `sim.Resume()`
**Status:** DIVERGE
**Notes:** SDL3 is missing the `mbAllowShiftOnColdReset` check. After cold reset, if the keyboard option is set to disallow shift on cold reset, the POKEY shift key state should be cleared. SDL3 does not do this.
**Severity:** Medium -- Programs that depend on Option/Select/Start being held on cold reset may behave differently; shift key could remain stuck.

---

## Handler: OnCommandSystemColdResetComputerOnly
**Win source:** cmdsystem.cpp:73
**Win engine calls:**
1. `g_sim.ColdResetComputerOnly()`
2. `g_sim.Resume()`
3. If `!g_kbdOpts.mbAllowShiftOnColdReset`: `g_sim.GetPokey().SetShiftKeyState(false, true)`
**SDL3 source:** ui_main.cpp:1451-1454 -- "System > Cold Reset (Computer Only)"
**SDL3 engine calls:**
1. `sim.ColdResetComputerOnly()`
2. `sim.Resume()`
**Status:** DIVERGE
**Notes:** Same as ColdReset -- missing the `mbAllowShiftOnColdReset` / `SetShiftKeyState` logic.
**Severity:** Medium

---

## Handler: OnCommandSystemTogglePauseWhenInactive
**Win source:** cmdsystem.cpp:81
**Win engine calls:**
1. `ATUISetPauseWhenInactive(!ATUIGetPauseWhenInactive())`
**SDL3 source:** ui_main.cpp:1467-1469 -- "System > Pause When Inactive"
**SDL3 engine calls:**
1. `ATUISetPauseWhenInactive(!pauseInactive)` (local bool read from `ATUIGetPauseWhenInactive()`)
**Status:** MATCH

---

## Handler: OnCommandSystemToggleSlowMotion
**Win source:** cmdsystem.cpp:85
**Win engine calls:**
1. `ATUISetSlowMotion(!ATUIGetSlowMotion())`
**SDL3 source:** ui_system.cpp:721-723 -- "Speed > Slow Motion" (in Configure System > Speed category)
**SDL3 engine calls:**
1. `ATUISetSlowMotion(slowmo)` via checkbox toggle
**Status:** MATCH
**Notes:** Available only in Configure System > Speed, not as a top-level System menu item. Windows has it in System menu. Functionally equivalent but less discoverable.

---

## Handler: OnCommandSystemToggleWarpSpeed
**Win source:** cmdsystem.cpp:89
**Win engine calls:**
1. `ATUISetTurbo(!ATUIGetTurbo())`
**SDL3 source:** ui_main.cpp:1463-1465 -- "System > Warp Speed"
**SDL3 engine calls:**
1. `ATUISetTurbo(!turbo)` (local bool from `ATUIGetTurbo()`)
**Status:** MATCH

---

## Handler: OnCommandSystemPulseWarpOn / OnCommandSystemPulseWarpOff
**Win source:** cmdsystem.cpp:93-98
**Win engine calls:**
1. `ATUISetTurboPulse(true)` / `ATUISetTurboPulse(false)`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Pulse warp (hold-to-warp) has no SDL3 UI binding. This is typically a keyboard shortcut-driven feature (not a menu item), so absence from the menu is expected. However, there should be a key binding for it somewhere in the SDL3 input handler.
**Severity:** Low -- This is a convenience feature typically bound to a keyboard shortcut, not a menu item.

---

## Handler: OnCommandSystemToggleFPPatch
**Win source:** cmdsystem.cpp:106
**Win engine calls:**
1. `g_sim.SetFPPatchEnabled(!g_sim.IsFPPatchEnabled())`
**SDL3 source:** ui_system.cpp:649-650 -- "Configure System > Acceleration > Fast floating-point math"
**SDL3 engine calls:**
1. `sim.SetFPPatchEnabled(fastFP)` via checkbox toggle
**Status:** MATCH

---

## Handler: OnCommandSystemSpeedOptionsDialog
**Win source:** cmdsystem.cpp:110
**Win engine calls:**
1. `ATUIShowDialogSpeedOptions(ATUIGetNewPopupOwner())`
**SDL3 source:** ui_system.cpp:714-754 -- "Configure System > Speed" category
**SDL3 engine calls:** Multiple (warp, slow motion, speed modifier, frame rate mode, vsync adaptive, pause when inactive)
**Status:** MATCH
**Notes:** SDL3 implements the speed options inline in the Speed category rather than as a separate dialog. All options present.

---

## Handler: OnCommandSystemToggleKeyboardPresent
**Win source:** cmdsystem.cpp:118
**Win engine calls:**
1. `g_sim.SetKeyboardPresent(!g_sim.IsKeyboardPresent())`
**SDL3 source:** ui_main.cpp:1510-1512 -- "System > Console Switches > Keyboard Present (XEGS)"
**SDL3 engine calls:**
1. `sim.SetKeyboardPresent(!kbdPresent)`
**Status:** MATCH

---

## Handler: OnCommandSystemToggleForcedSelfTest
**Win source:** cmdsystem.cpp:122
**Win engine calls:**
1. `g_sim.SetForcedSelfTest(!g_sim.IsForcedSelfTest())`
**SDL3 source:** ui_main.cpp:1514-1516 -- "System > Console Switches > Force Self-Test"
**SDL3 engine calls:**
1. `sim.SetForcedSelfTest(!selfTest)`
**Status:** MATCH

---

## Handler: OnCommandSystemSpeedMatchHardware
**Win source:** cmdsystem.cpp:230
**Win engine calls:**
1. `ATUISetFrameRateMode(kATFrameRateMode_Hardware)`
**SDL3 source:** ui_system.cpp:739-743 -- "Configure System > Speed > Base frame rate" combo
**SDL3 engine calls:**
1. `ATUISetFrameRateMode((ATFrameRateMode)frIdx)`
**Status:** MATCH

---

## Handler: OnCommandSystemSpeedMatchBroadcast
**Win source:** cmdsystem.cpp:234
**Win engine calls:**
1. `ATUISetFrameRateMode(kATFrameRateMode_Broadcast)`
**SDL3 source:** ui_system.cpp:739-743 -- "Configure System > Speed > Base frame rate" combo (index 1)
**SDL3 engine calls:**
1. `ATUISetFrameRateMode((ATFrameRateMode)frIdx)`
**Status:** MATCH

---

## Handler: OnCommandSystemSpeedInteger
**Win source:** cmdsystem.cpp:238
**Win engine calls:**
1. `ATUISetFrameRateMode(kATFrameRateMode_Integral)`
**SDL3 source:** ui_system.cpp:739-743 -- "Configure System > Speed > Base frame rate" combo (index 2)
**SDL3 engine calls:**
1. `ATUISetFrameRateMode((ATFrameRateMode)frIdx)`
**Status:** MATCH

---

## Handler: OnCommandSystemSpeedToggleVSyncAdaptive
**Win source:** cmdsystem.cpp:242
**Win engine calls:**
1. `ATUISetFrameRateVSyncAdaptive(!ATUIGetFrameRateVSyncAdaptive())`
**SDL3 source:** ui_system.cpp:745-747 -- "Configure System > Speed > Lock speed to display refresh rate"
**SDL3 engine calls:**
1. `ATUISetFrameRateVSyncAdaptive(vsyncAdaptive)` via checkbox toggle
**Status:** MATCH

---

## Handler: OnCommandSystemDevicesDialog
**Win source:** cmdsystem.cpp:114
**Win engine calls:**
1. `ATUIShowDialogDevices(ATUIGetNewPopupOwner())`
**SDL3 source:** ui_system.cpp:1345-1480 -- "Configure System > Devices" category
**SDL3 engine calls:** Device manager add/remove operations
**Status:** MATCH
**Notes:** SDL3 implements device list, add, remove inline. Full device catalog present.

---

## Handler: OnCommandSystemToggleRTime8
**Win source:** cmdsystem.cpp:224
**Win engine calls:**
1. `dm->ToggleDevice("rtime8")`
**SDL3 source:** N/A (no dedicated menu item)
**SDL3 engine calls:** N/A
**Status:** MATCH
**Notes:** R-Time 8 can be added/removed through Configure System > Devices as a cartridge device. No dedicated toggle menu item, but this matches the fact that it's accessible via the device list.

---

## Handler: Power-On Delay (menu items)
**Win source:** Not in cmdsystem.cpp; menu-driven via command system
**Win engine calls:** `g_sim.SetPowerOnDelay(value)` with values -1, 0, 10, 20, 30
**SDL3 source:** ui_main.cpp:1480-1493 -- "System > Power-On Delay" submenu
**SDL3 engine calls:**
1. `sim.SetPowerOnDelay(-1)` (Auto)
2. `sim.SetPowerOnDelay(0)` (None)
3. `sim.SetPowerOnDelay(10)` (1 Second)
4. `sim.SetPowerOnDelay(20)` (2 Seconds)
5. `sim.SetPowerOnDelay(30)` (3 Seconds)
**Status:** MATCH

---

## Handler: Hold Keys For Reset
**Win source:** Via command system
**Win engine calls:** `ATUIToggleHoldKeys()`
**SDL3 source:** ui_main.cpp:1495-1496 -- "System > Hold Keys For Reset"
**SDL3 engine calls:**
1. `ATUIToggleHoldKeys()`
**Status:** DIVERGE
**Notes:** `ATUIToggleHoldKeys()` is stubbed out in `uiaccessors_stubs.cpp` (empty function body). The menu item exists but does nothing.
**Severity:** Medium -- Hold keys feature (holding Option/Select/Start during cold reset) is non-functional.

---

## Handler: OnCommandSystemToggleBASIC (System menu quick toggle)
**Win source:** cmdsystem.cpp:198
**Win engine calls:**
1. `ATUIConfirmBasicChangeReset()` -- confirms reset if needed
2. `g_sim.SetBASICEnabled(!g_sim.IsBASICEnabled())`
3. `ATUIConfirmBasicChangeResetComplete()` -- performs cold reset if confirmed
**SDL3 source:** ui_main.cpp:1498-1500 -- "System > Internal BASIC"
**SDL3 engine calls:**
1. `sim.SetBASICEnabled(!basic)`
**Status:** DIVERGE
**Notes:** SDL3 does not call `ATUIConfirmBasicChangeReset()` / `ATUIConfirmBasicChangeResetComplete()` before toggling BASIC. The Windows version may trigger a cold reset (depending on user's "Ease of Use" settings). SDL3 just toggles the flag without any reset confirmation or automatic reset.
**Severity:** Medium -- Changed BASIC state may not take effect until the next manual cold reset.

---

## Handler: Cassette Auto-Boot (System menu)
**Win source:** Via command system
**Win engine calls:** `g_sim.SetCassetteAutoBootEnabled(!g_sim.IsCassetteAutoBootEnabled())`
**SDL3 source:** ui_main.cpp:1502-1504 -- "System > Auto-Boot Tape (Hold Start)"
**SDL3 engine calls:**
1. `sim.SetCassetteAutoBootEnabled(!casAutoBoot)`
**Status:** MATCH

---

## Handler: OnCommandSystemEditProfilesDialog
**Win source:** cmdsystem.cpp:398
**Win engine calls:**
1. `ATUIShowDialogProfiles(ATUIGetNewPopupOwner())`
2. `ATUIRebuildDynamicMenu(kATUIDynamicMenu_Profile)`
**SDL3 source:** ui_main.cpp:1395-1396 -- "System > Profiles > Edit Profiles..."
**SDL3 engine calls:**
1. Sets `state.showProfiles = true` (opens profiles dialog)
**Status:** MATCH
**Notes:** Profile editing dialog is in ui_profiles.cpp.

---

## Handler: OnCommandConfigureSystem
**Win source:** cmdsystem.cpp:403
**Win engine calls:**
1. `ATUIShowDialogConfigureSystem(ATUIGetNewPopupOwner())`
**SDL3 source:** ui_main.cpp:1438-1439 -- "System > Configure System..."
**SDL3 engine calls:**
1. Sets `state.showSystemConfig = true` (opens system config sidebar)
**Status:** MATCH

---

# Summary

| Status | Count |
|--------|-------|
| MATCH | 17 |
| DIVERGE | 4 |
| MISSING | 1 |

## Critical Issues
None.

## Medium Issues
1. **Cold Reset / Cold Reset Computer Only** -- Missing `mbAllowShiftOnColdReset` check and `SetShiftKeyState(false, true)` call after reset.
2. **Hold Keys For Reset** -- `ATUIToggleHoldKeys()` is stubbed to a no-op.
3. **BASIC toggle (System menu)** -- Missing `ATUIConfirmBasicChangeReset()` / `ATUIConfirmBasicChangeResetComplete()` confirmation flow.

## Low Issues
1. **Pulse Warp** -- No SDL3 key binding for hold-to-warp. Menu item not expected (Windows also uses key binding only).
