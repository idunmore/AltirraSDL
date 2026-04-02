# Verification 09 -- Cartridge Operations

Cross-reference of Windows cartridge handlers (cmdcart.cpp, cmds.cpp) against SDL3 implementation
(ui_main.cpp, ui_cartmapper.cpp).

---

## Handler: OnCommandAttachCartridge (Cart.Attach)
**Win source:** cmdcart.cpp:54 (calls OnCommandAttachCartridge(false) at line 36)
**Win engine calls:**
1. `ATUIConfirmDiscardCartridge(h)` -- prompt if cart has unsaved changes
2. `ATUIConfirmCartridgeChangeReset()` -- confirm cold reset
3. `VDGetLoadFileName('cart', ...)` -- file dialog (CAR/ROM/BIN filters)
4. `DoLoad(h, fn, nullptr, 0, kATImageType_Cartridge, nullptr, 0)` -- general loader
5. `ATUIConfirmCartridgeChangeResetComplete()` -- finalize reset
**SDL3 source:** ui_main.cpp:1107-1108 -- "File > Attach Cartridge..."
**SDL3 engine calls:**
1. `SDL_ShowOpenFileDialog(CartridgeAttachCallback, ...)` with CAR/ROM/BIN filters
2. Callback pushes `kATDeferred_AttachCartridge`
3. Deferred handler (ui_main.cpp:675-715):
   - `g_sim.LoadCartridge(0, path, &ctx)` with `mbReturnOnUnknownMapper=true`
   - On success: `g_sim.ColdReset()` + compatibility check + `g_sim.Resume()`
   - On unknown mapper: `ATUIOpenCartridgeMapperDialog(...)` -- shows mapper selection
**Status:** DIVERGE
**Notes:**
- SDL3 does NOT confirm discarding unsaved cartridge changes (no `ATUIConfirmDiscardCartridge`)
- SDL3 does NOT ask user to confirm cold reset before loading (no `ATUIConfirmCartridgeChangeReset`)
- SDL3 uses `LoadCartridge()` directly instead of `DoLoad()` general loader
- SDL3 properly handles unknown mapper case with mapper selection dialog
**Severity:** Medium

---

## Handler: OnCommandAttachCartridge2 (Cart.AttachSecond)
**Win source:** cmdcart.cpp:58 (calls OnCommandAttachCartridge(true) at line 36)
**Win engine calls:**
1. `ATUIConfirmDiscardCartridge(h)` -- prompt if cart has unsaved changes
2. `ATUIConfirmCartridgeChangeReset()` -- confirm cold reset
3. `VDGetLoadFileName('cart', ...)` -- file dialog
4. `DoLoad(h, fn, nullptr, 0, kATImageType_Cartridge, nullptr, 1)` -- slot 1
5. `ATUIConfirmCartridgeChangeResetComplete()`
**SDL3 source:** ui_main.cpp:1092-1097 -- "File > Secondary Cartridge > Attach..."
**SDL3 engine calls:**
1. `SDL_ShowOpenFileDialog(...)` with CAR/ROM/BIN filters
2. Callback pushes `kATDeferred_AttachSecondaryCartridge`
3. Deferred handler (ui_main.cpp:675-715): `g_sim.LoadCartridge(1, path, &ctx)` then `ColdReset()` + `Resume()`
**Status:** DIVERGE
**Notes:**
- Same missing confirmations as primary attach (no discard/reset prompts)
**Severity:** Medium

---

## Handler: OnCommandDetachCartridge (Cart.Detach)
**Win source:** cmdcart.cpp:62
**Win engine calls:**
1. `ATUIConfirmDiscardCartridge(h)` -- prompt if cart has unsaved changes
2. `ATUIConfirmCartridgeChangeReset()` -- confirm cold reset
3. If 5200 mode: `g_sim.LoadCartridge5200Default()` else `g_sim.UnloadCartridge(0)`
4. `ATUIConfirmCartridgeChangeResetComplete()`
**SDL3 source:** ui_main.cpp:1110-1113 -- "File > Detach Cartridge"
**SDL3 engine calls:**
1. `sim.UnloadCartridge(0)`
2. `sim.ColdReset()`
3. `sim.Resume()`
**Status:** DIVERGE
**Notes:**
- SDL3 does NOT confirm discarding unsaved changes
- SDL3 does NOT confirm cold reset before detaching
- SDL3 does NOT handle 5200 mode specially (Windows loads default 5200 cart instead of unloading)
**Severity:** Critical

---

## Handler: OnCommandDetachCartridge2 (Cart.DetachSecond)
**Win source:** cmdcart.cpp:77
**Win engine calls:**
1. `ATUIConfirmDiscardCartridge(h)` -- prompt if unsaved changes
2. `ATUIConfirmCartridgeChangeReset()` -- confirm cold reset
3. `g_sim.UnloadCartridge(1)`
4. `ATUIConfirmCartridgeChangeResetComplete()`
**SDL3 source:** ui_main.cpp:1099-1103 -- "File > Secondary Cartridge > Detach"
**SDL3 engine calls:**
1. `sim.UnloadCartridge(1)`
2. `sim.ColdReset()`
3. `sim.Resume()`
**Status:** DIVERGE
**Notes:**
- No discard confirmation, no reset confirmation
**Severity:** Medium

---

## Handler: OnCommandAttachNewCartridge(mode) -- Special Cartridges
**Win source:** cmdcart.cpp:88 (called from cmds.cpp:667-685 with specific modes)
**Win engine calls:**
1. `ATUIConfirmDiscardCartridge(h)` -- prompt if unsaved changes
2. `ATUIConfirmCartridgeChangeReset()` -- confirm cold reset
3. `g_sim.LoadNewCartridge(mode)`
4. `ATUIConfirmCartridgeChangeResetComplete()`
**SDL3 source:** ui_main.cpp:1048-1088 -- "File > Attach Special Cartridge > [item]"
**SDL3 engine calls:**
1. `sim.LoadNewCartridge(sc.mode)`
2. `sim.ColdReset()`
3. `sim.Resume()`
**Status:** DIVERGE
**Notes:**
- No discard/reset confirmations
- **MegaCart 512K mode mismatch**: Windows cmds.cpp:681 uses `kATCartridgeMode_MegaCart_512K_3`, SDL3 ui_main.cpp:1065 uses `kATCartridgeMode_MegaCart_512K` -- these are different mapper modes!
**Severity:** Critical (MegaCart mode mismatch), Medium (no confirmations)

---

## Special Cartridge Modes -- Full Cross-Reference

| Windows (cmds.cpp) | SDL3 (ui_main.cpp) | Status |
|---------------------|---------------------|--------|
| SuperCharger3D | SuperCharger3D | MATCH |
| MaxFlash_128K | MaxFlash_128K | MATCH |
| MaxFlash_128K_MyIDE | MaxFlash_128K_MyIDE | MATCH |
| MaxFlash_1024K | MaxFlash_1024K | MATCH |
| MaxFlash_1024K_Bank0 | MaxFlash_1024K_Bank0 | MATCH |
| JAtariCart_128K | JAtariCart_128K | MATCH |
| JAtariCart_256K | JAtariCart_256K | MATCH |
| JAtariCart_512K | JAtariCart_512K | MATCH |
| JAtariCart_1024K | JAtariCart_1024K | MATCH |
| DCart | DCart | MATCH |
| SIC_512K | SIC_512K | MATCH |
| SIC_256K | SIC_256K | MATCH |
| SIC_128K | SIC_128K | MATCH |
| SICPlus | SICPlus | MATCH |
| **MegaCart_512K_3** | **MegaCart_512K** | **DIVERGE** |
| MegaCart_4M_3 | MegaCart_4M_3 | MATCH |
| TheCart_32M | TheCart_32M | MATCH |
| TheCart_64M | TheCart_64M | MATCH |
| TheCart_128M | TheCart_128M | MATCH |
| BASIC (via AttachCartridgeBASIC) | BASIC | MATCH |

---

## Handler: OnCommandAttachCartridgeBASIC (Cart.AttachBASIC)
**Win source:** cmdcart.cpp:99
**Win engine calls:**
1. `ATUIConfirmDiscardCartridge(h)` -- prompt if unsaved changes
2. `ATUIConfirmCartridgeChangeReset()` -- confirm cold reset
3. `g_sim.LoadCartridgeBASIC()`
4. `ATUIConfirmCartridgeChangeResetComplete()`
**SDL3 source:** ui_main.cpp:1082-1086 -- "File > Attach Special Cartridge > BASIC"
**SDL3 engine calls:**
1. `sim.LoadCartridgeBASIC()`
2. `sim.ColdReset()`
3. `sim.Resume()`
**Status:** DIVERGE
**Notes:**
- No discard/reset confirmations
**Severity:** Medium

---

## Handler: OnCommandCartActivateMenuButton (Cart.ActivateMenuButton)
**Win source:** cmdcart.cpp:110
**Win engine calls:**
1. `ATUIActivateDeviceButton(kATDeviceButton_CartridgeResetBank, true)`
**SDL3 source:** ui_main.cpp:1519-1520 -- "System > Console Switches > Activate Cart Menu Button"
**SDL3 engine calls:**
1. `ATUIActivateDeviceButton(kATDeviceButton_CartridgeResetBank, true)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandCartToggleSwitch (Cart.ToggleSwitch)
**Win source:** cmdcart.cpp:114
**Win engine calls:**
1. `g_sim.SetCartridgeSwitch(!g_sim.GetCartridgeSwitch())`
**SDL3 source:** ui_main.cpp:1523-1525 -- "System > Console Switches > Enable Cart Switch"
**SDL3 engine calls:**
1. `sim.SetCartridgeSwitch(!cartSwitch)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandSaveCartridge (Cart.Save)
**Win source:** cmdcart.cpp:118
**Win engine calls:**
1. Get `cart->GetMode()` -- throw if no cart or mode 0
2. Throw if SuperCharger3D mode (cannot save)
3. `VDGetSaveFileName('cart', ...)` -- CAR header or raw BIN/ROM
4. `cart->Save(fn.c_str(), optval[0] == 1)` -- `optval[0]==1` means raw (no header)
**SDL3 source:** ui_main.cpp:1118-1127 -- "File > Save Firmware > Save Cartridge..."
**SDL3 engine calls:**
1. `SDL_ShowSaveFileDialog(...)` with CAR/BIN/ROM filters
2. Deferred `kATDeferred_SaveCartridge` (ui_main.cpp:751-761):
   - `cart->GetMode()` check
   - Determine `includeHeader` based on file extension (.bin/.rom = no header)
   - `cart->Save(path, includeHeader)`
**Status:** DIVERGE
**Notes:**
- SDL3 does NOT throw/warn if cart is SuperCharger3D mode (Windows explicitly blocks this)
- SDL3 determines header inclusion by file extension; Windows uses file dialog filter selection index. SDL3 approach is arguably more intuitive.
- SDL3 places this under "Save Firmware" submenu; Windows has it as top-level "Cart.Save"
**Severity:** Low

---

## Cartridge Mapper Dialog (ui_cartmapper.cpp)

**Win source:** Windows IDD_CARTRIDGE_MAPPER (uicartmapper.cpp) via `ATUIShowDialogCartridgeMapper()`
**SDL3 source:** ui_cartmapper.cpp:396-522 -- `ATUIRenderCartridgeMapper()`

### Auto-detection
**Win engine calls:**
1. `ATCartridgeAutodetectMode(data, size, cartModes)` -- returns recommended count
**SDL3 engine calls:**
1. `ATCartridgeAutodetectMode(capturedData.data(), cartSize, originalMappers)` -- same function
**Status:** MATCH

### Mapper List Display
**Win:** Shows recommended mappers first, with same-system-type partitioning, sorted by mapper number
**SDL3:** Same sorting logic (ui_cartmapper.cpp:359-393), recommended first, same-system bubble, mapper number sort
**Status:** MATCH

### Show All / Show Details Checkboxes
**Win:** "Show All Modes" checkbox + "Show Details" checkbox
**SDL3:** ui_cartmapper.cpp:496-503 -- "Show All" and "Show Details" checkboxes
**Status:** MATCH

### 2600 Warning
**Win:** Warning shown for images that look like Atari 2600 ROMs
**SDL3:** ui_cartmapper.cpp:301-307, 414-419 -- same detection logic (NMI/RESET/IRQ vectors in Fxxx)
**Status:** MATCH

### Double-click Accept
**Win:** Double-click on mapper list accepts selection
**SDL3:** ui_cartmapper.cpp:474-481 -- double-click detected via `IsMouseDoubleClicked(0)`
**Status:** MATCH

### OK/Cancel Buttons
**Win:** OK and Cancel buttons
**SDL3:** ui_cartmapper.cpp:510-519 -- OK and Cancel buttons, OK pushes deferred action with chosen mode
**Status:** MATCH

### Mapper Name/Description Tables
**Win source:** uicartmapper.cpp GetModeName() / GetModeDesc()
**SDL3 source:** ui_cartmapper.cpp:57-278 -- `ATUIGetCartridgeModeName()` / `ATUIGetCartridgeModeDesc()`
**Status:** MATCH
**Notes:** Both cover the same complete set of cartridge modes including all recent additions (JAtariCart, DCart, SICPlus, COS32K, Pronto, etc.)

---

## Summary

| Handler | Status | Severity |
|---------|--------|----------|
| Cart.Attach | DIVERGE | Medium |
| Cart.AttachSecond | DIVERGE | Medium |
| Cart.Detach | DIVERGE | Critical |
| Cart.DetachSecond | DIVERGE | Medium |
| AttachNewCartridge (special carts) | DIVERGE | Critical/Medium |
| Cart.AttachBASIC | DIVERGE | Medium |
| Cart.ActivateMenuButton | MATCH | -- |
| Cart.ToggleSwitch | MATCH | -- |
| Cart.Save | DIVERGE | Low |
| Cartridge Mapper Dialog | MATCH | -- |

### Key Divergences

1. **No discard/reset confirmations** -- All SDL3 cartridge attach/detach operations skip the two-step confirmation that Windows performs: (a) confirm discarding unsaved cartridge changes, and (b) confirm cold reset. This is a systematic gap across all cartridge operations.

2. **5200 detach does not load default cart** -- Windows `OnCommandDetachCartridge` loads a default 5200 cartridge via `LoadCartridge5200Default()` when in 5200 hardware mode; SDL3 unconditionally calls `UnloadCartridge(0)`. In 5200 mode this leaves the system with no cartridge, which is not a valid 5200 state. **Critical severity.**

3. **MegaCart 512K mode mismatch** -- Windows uses `kATCartridgeMode_MegaCart_512K_3` (cmds.cpp:681), SDL3 uses `kATCartridgeMode_MegaCart_512K` (ui_main.cpp:1065). These are different banking modes. **Critical severity.**

4. **SuperCharger3D save not blocked** -- Windows explicitly throws an error when trying to save a SuperCharger3D cartridge; SDL3 does not check for this mode.

5. **Menu location for Save Cartridge** -- SDL3 places "Save Cartridge..." under "Save Firmware" submenu, which differs from Windows where it is a standalone "Cart.Save" command. Functionally equivalent but different menu hierarchy.
