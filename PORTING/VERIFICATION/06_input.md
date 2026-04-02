# Verification: Input Handlers

Cross-reference of Windows input command handlers against SDL3 implementation.

**Windows source:** `src/Altirra/source/cmdinput.cpp`
**SDL3 sources:** `src/AltirraSDL/source/ui_main.cpp` (Input menu, lines 1622-1683), `src/AltirraSDL/source/ui_system.cpp` (Keyboard category lines 959-996, Input category lines 1142-1156), `src/AltirraSDL/source/ui_input.cpp` (Input Mappings dialog)

---

## Handler: OnCommandInputKeyboardLayoutNatural
**Win source:** cmdinput.cpp:39-41
**Win engine calls:**
1. `g_kbdOpts.mLayoutMode = ATUIKeyboardOptions::kLM_Natural`
2. `ATUIInitVirtualKeyMap(g_kbdOpts)`
**SDL3 source:** ui_system.cpp:976-981 -- "Config > Keyboard > Layout Mode combo"
**SDL3 engine calls:**
1. `g_kbdOpts.mLayoutMode = (ATUIKeyboardOptions::LayoutMode)lm`
**Status:** DIVERGE
**Notes:** SDL3 sets the layout mode but does NOT call `ATUIInitVirtualKeyMap(g_kbdOpts)` after changing layout mode. Windows always calls it. The key map will not update until something else triggers reinitialization.
**Severity:** Critical

---

## Handler: OnCommandInputKeyboardLayoutDirect
**Win source:** cmdinput.cpp:43-45
**Win engine calls:**
1. `g_kbdOpts.mLayoutMode = ATUIKeyboardOptions::kLM_Raw`
2. `ATUIInitVirtualKeyMap(g_kbdOpts)`
**SDL3 source:** ui_system.cpp:976-981 -- "Config > Keyboard > Layout Mode combo" (same as above)
**SDL3 engine calls:**
1. `g_kbdOpts.mLayoutMode = (ATUIKeyboardOptions::LayoutMode)lm`
**Status:** DIVERGE
**Notes:** Same issue -- missing `ATUIInitVirtualKeyMap()` call.
**Severity:** Critical

---

## Handler: OnCommandInputKeyboardLayoutCustom
**Win source:** cmdinput.cpp:47-49
**Win engine calls:**
1. `g_kbdOpts.mLayoutMode = ATUIKeyboardOptions::kLM_Custom`
2. `ATUIInitVirtualKeyMap(g_kbdOpts)`
**SDL3 source:** ui_system.cpp:976-981 -- "Config > Keyboard > Layout Mode combo" (same as above)
**SDL3 engine calls:**
1. `g_kbdOpts.mLayoutMode = (ATUIKeyboardOptions::LayoutMode)lm`
**Status:** DIVERGE
**Notes:** Same issue -- missing `ATUIInitVirtualKeyMap()` call.
**Severity:** Critical

---

## Handler: OnCommandInputKeyboardModeCooked
**Win source:** cmdinput.cpp:51-54
**Win engine calls:**
1. `g_kbdOpts.mbRawKeys = false`
2. `g_kbdOpts.mbFullRawKeys = false`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Keyboard mode selection (Cooked/Raw/Full Scan) is not exposed in SDL3 UI. These modes control whether keys are sent as cooked characters or raw scancodes.
**Severity:** Medium

---

## Handler: OnCommandInputKeyboardModeRaw
**Win source:** cmdinput.cpp:56-59
**Win engine calls:**
1. `g_kbdOpts.mbRawKeys = true`
2. `g_kbdOpts.mbFullRawKeys = false`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** See above -- keyboard mode not exposed.
**Severity:** Medium

---

## Handler: OnCommandInputKeyboardModeFullScan
**Win source:** cmdinput.cpp:61-64
**Win engine calls:**
1. `g_kbdOpts.mbRawKeys = true`
2. `g_kbdOpts.mbFullRawKeys = true`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** See above -- keyboard mode not exposed.
**Severity:** Medium

---

## Handler: OnCommandInputKeyboardArrowModeDefault / AutoCtrl / Raw
**Win source:** cmdinput.cpp:66-84
**Win engine calls:**
1. `g_kbdOpts.mArrowKeyMode = mode`
2. `ATUIInitVirtualKeyMap(g_kbdOpts)`
**SDL3 source:** ui_system.cpp:963-971 -- "Config > Keyboard > Arrow Key Mode combo"
**SDL3 engine calls:**
1. `g_kbdOpts.mArrowKeyMode = (ATUIKeyboardOptions::ArrowKeyMode)akm`
**Status:** DIVERGE
**Notes:** SDL3 sets the arrow key mode but does NOT call `ATUIInitVirtualKeyMap(g_kbdOpts)` after changing it. Windows always calls it. The virtual key map will be stale.
**Severity:** Critical

---

## Handler: OnCommandInputToggleKeyboardUnshiftOnReset
**Win source:** cmdinput.cpp:86-88
**Win engine calls:**
1. `g_kbdOpts.mbAllowShiftOnColdReset = !g_kbdOpts.mbAllowShiftOnColdReset`
**SDL3 source:** ui_system.cpp:985-986 -- "Config > Keyboard > Allow SHIFT key to be detected on cold reset"
**SDL3 engine calls:**
1. `g_kbdOpts.mbAllowShiftOnColdReset` (checkbox binding)
2. `ATUIInitVirtualKeyMap(g_kbdOpts)` (called on change)
**Status:** DIVERGE
**Notes:** SDL3 calls `ATUIInitVirtualKeyMap()` here, but Windows does NOT call it for this option. The Windows handler only toggles the flag. SDL3 is doing extra work that Windows doesn't, though this is unlikely to cause problems.
**Severity:** Low

---

## Handler: OnCommandInputToggle1200XLFunctionKeys
**Win source:** cmdinput.cpp:90-94
**Win engine calls:**
1. `g_kbdOpts.mbEnableFunctionKeys = !g_kbdOpts.mbEnableFunctionKeys`
2. `ATUIInitVirtualKeyMap(g_kbdOpts)`
**SDL3 source:** ui_system.cpp:988-989 -- "Config > Keyboard > Enable F1-F4 as 1200XL function keys"
**SDL3 engine calls:**
1. `g_kbdOpts.mbEnableFunctionKeys` (checkbox binding)
2. `ATUIInitVirtualKeyMap(g_kbdOpts)` (called on change)
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandInputToggleAllowInputMapKeyboardOverlap
**Win source:** cmdinput.cpp:96-98
**Win engine calls:**
1. `g_kbdOpts.mbAllowInputMapOverlap = !g_kbdOpts.mbAllowInputMapOverlap`
**SDL3 source:** ui_system.cpp:994-995 -- "Config > Keyboard > Share non-modifier host keys..."
**SDL3 engine calls:**
1. `g_kbdOpts.mbAllowInputMapOverlap` (checkbox binding)
2. `ATUIInitVirtualKeyMap(g_kbdOpts)` (called on change)
**Status:** DIVERGE
**Notes:** SDL3 calls `ATUIInitVirtualKeyMap()` but Windows does not call it for this option. Extra work in SDL3, harmless.
**Severity:** Low

---

## Handler: OnCommandInputToggleAllowInputMapKeyboardModifierOverlap
**Win source:** cmdinput.cpp:100-102
**Win engine calls:**
1. `g_kbdOpts.mbAllowInputMapModifierOverlap = !g_kbdOpts.mbAllowInputMapModifierOverlap`
**SDL3 source:** ui_system.cpp:991-992 -- "Config > Keyboard > Share modifier host keys..."
**SDL3 engine calls:**
1. `g_kbdOpts.mbAllowInputMapModifierOverlap` (checkbox binding)
2. `ATUIInitVirtualKeyMap(g_kbdOpts)` (called on change)
**Status:** DIVERGE
**Notes:** SDL3 calls `ATUIInitVirtualKeyMap()` but Windows does not. Harmless extra call.
**Severity:** Low

---

## Handler: OnCommandInputKeyboardCopyToCustomLayout
**Win source:** cmdinput.cpp:104-114
**Win engine calls:**
1. `ATUIGetDefaultKeyMap(g_kbdOpts, mappings)`
2. `ATUISetCustomKeyMap(mappings.data(), mappings.size())`
3. `g_kbdOpts.mLayoutMode = ATUIKeyboardOptions::kLM_Custom`
4. `ATUIInitVirtualKeyMap(g_kbdOpts)`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** "Copy current layout to custom layout" action not implemented in SDL3.
**Severity:** Medium

---

## Handler: OnCommandInputKeyboardCustomizeLayout
**Win source:** cmdinput.cpp:116-118
**Win engine calls:**
1. `ATUIShowDialogKeyboardCustomize(ATUIGetNewPopupOwner())`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Custom keyboard layout editor dialog not implemented in SDL3.
**Severity:** Medium

---

## Handler: Input.ToggleRawInputEnabled
**Win source:** cmdinput.cpp:143-147
**Win engine calls:**
1. `ATUISetRawInputEnabled(!ATUIGetRawInputEnabled())`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Raw input toggle (Windows-specific DirectInput/RawInput selection) not implemented. This is platform-specific and may not be applicable to SDL3.
**Severity:** Low (platform-specific)

---

## Handler: Input.ToggleImmediatePotUpdate
**Win source:** cmdinput.cpp:148-155
**Win engine calls:**
1. `g_sim.GetPokey().SetImmediatePotUpdateEnabled(enable)`
**SDL3 source:** ui_system.cpp:1149-1151 -- "Config > Input > Use immediate analog updates"
**SDL3 engine calls:**
1. `pokey.SetImmediatePotUpdateEnabled(immPots)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: Input.ToggleImmediateLightPenUpdate
**Win source:** cmdinput.cpp:156-163
**Win engine calls:**
1. `g_sim.GetLightPenPort()->SetImmediateUpdateEnabled(enable)`
**SDL3 source:** ui_system.cpp:1153-1155 -- "Config > Input > Use immediate light pen updates"
**SDL3 engine calls:**
1. `sim.GetLightPenPort()->SetImmediateUpdateEnabled(immLightPen)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: Input.RecalibrateLightPen
**Win source:** cmdinput.cpp:165
**Win engine calls:**
1. `ATUIRecalibrateLightPen()`
**SDL3 source:** ui_main.cpp:1668 -- "Input > Recalibrate Light Pen/Gun" (placeholder, disabled)
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Light pen recalibration not implemented. Menu item exists but is disabled.
**Severity:** Medium

---

## Handler: Input.TogglePotNoise
**Win source:** cmdinput.cpp:167-171
**Win engine calls:**
1. `g_sim.SetPotNoiseEnabled(!g_sim.GetPotNoiseEnabled())`
**SDL3 source:** ui_system.cpp:1145-1147 -- "Config > Input > Enable paddle potentiometer noise"
**SDL3 engine calls:**
1. `sim.SetPotNoiseEnabled(potNoise)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: Input Mappings Dialog
**Win source:** (separate dialog in uiinput.cpp, not in cmdinput.cpp)
**SDL3 source:** ui_main.cpp:1625-1626 -- "Input > Input Mappings..." opens ui_input.cpp
**SDL3 implementation:** ui_input.cpp:54-285 -- Full input mappings dialog with:
- Enable/disable maps (checkbox per map)
- Name editing (double-click)
- Controller type summary
- Quick map checkbox
- Add, Clone, Delete, Load Preset, Reset All buttons
- Save to registry on OK
**Status:** MATCH
**Notes:** SDL3 implementation covers the same functionality as Windows IDD_INPUT_MAPPINGS.
**Severity:** N/A

---

## Handler: Input Menu - Capture Mouse
**Win source:** (in uimouse.cpp / menu handlers, not in cmdinput.cpp)
**SDL3 source:** ui_main.cpp:1648-1656 -- "Input > Capture Mouse"
**SDL3 engine calls:**
1. `ATUICaptureMouse()` / `ATUIReleaseMouse()`
**Status:** MATCH
**Severity:** N/A

---

## Handler: Input Menu - Auto-Capture Mouse
**Win source:** (in menu handlers)
**SDL3 source:** ui_main.cpp:1658-1663 -- "Input > Auto-Capture Mouse"
**SDL3 engine calls:**
1. `ATUISetMouseAutoCapture(!mouseAutoCapture)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: Input Menu - Cycle Quick Maps
**Win source:** (in menu handlers / input controller)
**SDL3 source:** ui_main.cpp:1630-1642 -- "Input > Cycle Quick Maps"
**SDL3 engine calls:**
1. `pIM->CycleQuickMaps()`
**Status:** MATCH
**Severity:** N/A

---

## Handler: Input Menu - Port 1-4 Submenus
**Win source:** (in uiportmenus.cpp)
**SDL3 source:** ui_main.cpp:1570-1682 -- "Input > Port 1/2/3/4"
**SDL3 engine calls:**
1. `im.GetInputMapByIndex()` to enumerate maps per port
2. `im.ActivateInputMap()` to toggle
**Status:** MATCH
**Notes:** SDL3 implements full port assignment with None + per-map radio items, matching Windows behavior.
**Severity:** N/A

---

## Handler: Input Menu - Light Pen/Gun dialog
**Win source:** (separate dialog)
**SDL3 source:** ui_main.cpp:1667 -- "Input > Light Pen/Gun..." (placeholder, disabled)
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Light pen/gun configuration dialog not implemented.
**Severity:** Medium

---

## Handler: Input Menu - Input Setup dialog
**Win source:** (separate dialog)
**SDL3 source:** ui_main.cpp:1627 -- "Input > Input Setup..." (placeholder, disabled)
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Input setup dialog (gamepad/joystick configuration) not implemented.
**Severity:** Medium

---

# Summary

| Status   | Count | Items |
|----------|-------|-------|
| MATCH    | 11    | Function keys, immediate pot update, immediate light pen, pot noise, input mappings dialog, capture mouse, auto-capture mouse, cycle quick maps, port 1-4 submenus |
| DIVERGE  | 6     | Layout mode change (3 variants -- missing ATUIInitVirtualKeyMap), arrow key mode (missing ATUIInitVirtualKeyMap), shift-on-reset (extra InitVirtualKeyMap call), overlap toggles (2 -- extra InitVirtualKeyMap calls) |
| MISSING  | 8     | Keyboard mode (Cooked/Raw/FullScan), copy to custom layout, customize layout dialog, raw input toggle, recalibrate light pen, light pen/gun dialog, input setup dialog |

**Critical items:**
- Layout mode combo (ui_system.cpp:976-981) does not call `ATUIInitVirtualKeyMap()` after changing the layout mode. Changing between Natural/Raw/Custom will have no effect until something else reinitializes the key map.
- Arrow key mode combo (ui_system.cpp:968-971) does not call `ATUIInitVirtualKeyMap()` after changing the arrow key mode. Same issue.

**Medium priority:**
- Keyboard mode (Cooked/Raw/FullScan) not exposed -- affects software that requires raw keyboard input
- Copy to custom layout and customize layout dialog missing
- Light pen/gun dialog and recalibration missing
- Input setup dialog missing
