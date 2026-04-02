# Boot / Load Flow Verification

Cross-reference of Windows boot/load handlers against SDL3 implementation.
Generated from source code review, not from memory.

---

## Handler: OnCommandBootImage (File > Boot Image)

**Win source:** main.cpp:2035 -- delegates to `OnCommandOpen(true, ctx)` which creates `ATUIFutureOpenBootImage(coldBoot=true)`

**Win engine calls:**
1. `ATUIConfirmDiscardAllStorage()` -- prompt user if dirty storage exists
2. Show file dialog (async future with `ATUIShowOpenFileDialog`)
3. `ATUIUnloadStorageForBoot()` -- calls `g_sim.UnloadAll(g_ATUIBootUnloadStorageMask)`
4. `DoLoad()` -> `DoLoadStream()` -- full retry loop with `ATMediaLoadContext`:
   - Sets `mbStopOnModeIncompatibility = true`
   - Sets `mbStopAfterImageLoaded = true`
   - Sets `mbStopOnMemoryConflictBasic = true`
   - Sets `mbStopOnIncompatibleDiskFormat = true`
   - Calls `g_sim.Load(mctx)` in a loop (up to 10 retries)
   - On `mbMode5200Required`: calls `ATUISwitchHardwareMode5200()` and retries
   - On `mbModeComputerRequired`: calls `ATUISwitchHardwareModeComputer()` and retries
   - On `mbMemoryConflictBasic`: prompts user, optionally disables BASIC, retries
   - On `mbIncompatibleDiskFormat`: prompts user, retries
   - On unknown cart mapper: shows mapper dialog, retries with selected mapper
   - On state kernel mismatch: prompts user, retries with `mbAllowKernelMismatch`
5. `DoCompatibilityCheck()` -- compat DB check, may pause/auto-adjust
6. `g_sim.ColdReset()` (unless `suppressColdReset` was set by save state load)
7. `ATAddMRUListItem(path)`

**SDL3 source:** ui_main.cpp:874 -- triggers `SDL_ShowOpenFileDialog(BootImageCallback)`, callback pushes `kATDeferred_BootImage`, processed at ui_main.cpp:610

**SDL3 engine calls:**
1. (no discard confirmation)
2. `g_sim.UnloadAll(ATUIGetBootUnloadStorageMask())`
3. `g_sim.Load(path, writeMode, &ctx)` -- simple overload, NO retry loop, NO stop flags
4. On success: `ATAddMRU(path)`
5. `g_sim.ColdReset()`
6. `ATCompatCheck()` -- compat check, may pause emulator and show ImGui dialog
7. `g_sim.Resume()` (unless compat issue detected)
8. On unknown cart mapper: opens mapper dialog (re-pushes deferred action with mapper ID)

**Status:** DIVERGE

**Notes:**
1. **No discard confirmation** -- SDL3 skips `ATUIConfirmDiscardAllStorage()`. If user has dirty/unsaved disk images, they are silently discarded on boot.
2. **No hardware mode auto-switching** -- SDL3 uses the simple `g_sim.Load()` overload which does not set `mbStopOnModeIncompatibility` or `mbStopAfterImageLoaded`. Loading a 5200 cartridge while in 800XL mode (or vice versa) will either silently fail or produce incorrect behavior. The Windows retry loop with `ATUISwitchHardwareMode5200()`/`ATUISwitchHardwareModeComputer()` is entirely absent.
3. **No BASIC memory conflict detection** -- SDL3 never sets `mbStopOnMemoryConflictBasic`. Programs that overlap internal BASIC will not trigger the "Disable internal BASIC?" prompt.
4. **No incompatible disk format warning** -- SDL3 never sets `mbStopOnIncompatibleDiskFormat`.
5. **No kernel mismatch handling for save states** -- the retry loop handling for `mbKernelMismatchDetected` / `mbAllowKernelMismatch` is absent.
6. **Explicit Resume() after boot** -- SDL3 calls `g_sim.Resume()` after successful boot. Windows does not call Resume() -- it relies on the emulator already being in a running state. This is functionally equivalent if the emulator was running, but differs if it was paused before boot (SDL3 will unpause, Windows will not).

**Severity:** Critical (items 2-3 can cause load failures or incorrect emulation; item 1 causes data loss)

---

## Handler: OnCommandOpenImage (File > Open Image)

**Win source:** main.cpp:2031 -- delegates to `OnCommandOpen(false, ctx)` which creates `ATUIFutureOpenBootImage(coldBoot=false)`

**Win engine calls:**
1. (no discard confirmation -- coldBoot is false)
2. Show file dialog (async future)
3. (no UnloadAll -- coldBoot is false)
4. `DoLoad()` -> `DoLoadStream()` -- same full retry loop as Boot Image
5. `DoCompatibilityCheck()`
6. (no ColdReset -- coldBoot is false, unless suppressColdReset logic triggers)
7. `ATAddMRUListItem(path)`

**SDL3 source:** ui_main.cpp:877 -- triggers `SDL_ShowOpenFileDialog(OpenImageCallback)`, callback pushes `kATDeferred_OpenImage`, processed at ui_main.cpp:611

**SDL3 engine calls:**
1. (no UnloadAll -- correct for open)
2. `g_sim.Load(path, writeMode, &ctx)` -- simple overload, NO retry loop
3. On success: `ATAddMRU(path)`
4. (no ColdReset -- correct for open)
5. `ATCompatCheck()` -- compat check
6. `g_sim.Resume()`
7. On unknown cart mapper: opens mapper dialog

**Status:** DIVERGE

**Notes:**
1. Same missing retry loop / hardware mode switching / BASIC conflict / disk format issues as Boot Image (items 2-5 from above).
2. **Resume() called unconditionally** -- Windows Open Image does not call Resume(). If the user opens an image while paused (e.g., to inspect disk contents), SDL3 will unpause the emulator; Windows will leave it paused.

**Severity:** Critical (same hardware mode switching gaps as Boot Image)

---

## Handler: MRU List (File > Recently Booted)

**Win source:** uimainwindow.cpp:467 -- calls `DoBootWithConfirm(path, nullptr, 0)` (main.cpp:1401)

**Win engine calls:**
1. `ATUIConfirmDiscardAllStorage()` -- prompt user if dirty storage
2. `ATUIUnloadStorageForBoot()`
3. `DoLoad()` -- full retry loop with all stop flags and mode switching
4. `ATAddMRUListItem(path)`
5. `g_sim.ColdReset()` (unless suppressColdReset)

**SDL3 source:** ui_main.cpp:894 -- inline code in `RenderFileMenu()`

**SDL3 engine calls:**
1. (no discard confirmation)
2. (no UnloadAll)
3. `g_sim.Load(wpath, kATMediaWriteMode_RO, &ctx)` -- simple overload, NO retry loop
4. `ATAddMRU(wpath)`
5. `g_sim.ColdReset()`
6. `g_sim.Resume()`

**Status:** DIVERGE

**Notes:**
1. **No discard confirmation** before boot.
2. **No UnloadAll** before loading new image -- previous storage (disks, cassette, cartridges) remains attached. Windows always unloads before MRU boot.
3. **Hardcoded `kATMediaWriteMode_RO`** -- Windows uses `g_ATOptions.mDefaultWriteMode` (via `DoLoad` -> `DoLoadStream` which defaults to it). The SDL3 MRU handler forces read-only regardless of user's write mode preference.
4. Same missing retry loop / mode switching issues.
5. **No compat check** -- SDL3 MRU handler does not call `ATCompatCheck()` after boot. The deferred boot handler does, but MRU is inline and skips it.
6. **Resume() called** -- Windows does not call Resume().

**Severity:** Critical (missing UnloadAll means stale storage persists across MRU boots)

---

## Handler: File Drop (drag-and-drop)

**Win source:** uidragdrop.cpp:524 -- calls `DoBootWithConfirm(path, nullptr, 0)`

**Win engine calls:**
1. `ATUIConfirmDiscardAllStorage()`
2. `ATUIUnloadStorageForBoot()`
3. `DoLoad()` -- full retry loop
4. `ATAddMRUListItem(path)`
5. `g_sim.ColdReset()` (unless suppressColdReset)

**SDL3 source:** main_sdl3.cpp:221 -- pushes `kATDeferred_BootImage`

**SDL3 engine calls:**
(Same as `kATDeferred_BootImage` handler -- see Boot Image above)
1. `g_sim.UnloadAll(ATUIGetBootUnloadStorageMask())`
2. `g_sim.Load(path, writeMode, &ctx)` -- simple overload
3. `ATAddMRU(path)`
4. `g_sim.ColdReset()`
5. Compat check
6. `g_sim.Resume()`

**Status:** DIVERGE

**Notes:**
1. **No discard confirmation** -- same as Boot Image handler.
2. Same missing retry loop / mode switching issues as Boot Image.
3. Drop correctly routes through the deferred boot path, which does UnloadAll (unlike MRU).

**Severity:** Critical (same issues as Boot Image; discard confirmation is more important for drag-drop since it's easy to accidentally drop)

---

## Handler: Command-Line Load

**Win source:** main.cpp (startup) -- uses `DoLoad()` -> `DoLoadStream()` with full retry loop for command-line arguments

**Win engine calls:**
1. `DoLoad(path)` with full retry loop and mode switching
2. `g_sim.ColdReset()` at startup
3. `g_sim.Resume()` at startup

**SDL3 source:** main_sdl3.cpp:390

**SDL3 engine calls:**
1. `g_sim.Load(widePath, g_ATOptions.mDefaultWriteMode, &ctx)` -- simple overload, NO retry loop
2. `g_sim.ColdReset()` (line 398, unconditional -- runs even if no arg given)
3. `g_sim.Resume()` (line 399)

**Status:** DIVERGE

**Notes:**
1. **No retry loop or mode switching** -- if a 5200 cartridge is specified on the command line while default hardware mode is 800XL, it will fail silently.
2. **No UnloadAll** before command-line load -- less critical since this is at startup with clean state.
3. **No MRU update** for command-line loaded images.
4. **No compat check** after command-line load.

**Severity:** Medium (command-line is less commonly used than menu; startup state is clean so some issues are less impactful)

---

## Handler: OnCommandLoadState (File > Load State)

**Win source:** main.cpp:2078

**Win engine calls:**
1. Show file dialog (`VDGetLoadFileName`)
2. `DoLoad(path, nullptr, 0, kATImageType_SaveState)` -- note: explicit `kATImageType_SaveState`
   - Full retry loop including:
   - Kernel mismatch detection (`mbKernelMismatchDetected`) with user prompt
   - `mbAllowKernelMismatch` retry
   - `suppressColdReset = true` set after successful state load
   - Version mismatch warning (`mbPrivateStateLoaded`)
3. `DoCompatibilityCheck()`
4. (no ColdReset -- suppressColdReset is true for state loads)
5. (no Resume -- emulator stays in whatever state it was)

**SDL3 source:** ui_main.cpp:1033 -- triggers `SDL_ShowOpenFileDialog(LoadStateCallback)`, callback pushes `kATDeferred_LoadState`, processed at ui_main.cpp:723

**SDL3 engine calls:**
1. `g_sim.Load(path, kATMediaWriteMode_RO, &ctx)` -- simple overload; `mLoadType` defaults to `kATImageType_None` (auto-detect, which works from extension)
2. `g_sim.Resume()`

**Status:** DIVERGE

**Notes:**
1. **No kernel mismatch detection/warning** -- Windows warns if the currently loaded kernel ROM doesn't match the save state's kernel. SDL3 loads blindly, which may cause program failures.
2. **No version mismatch warning** -- Windows warns if `mbPrivateStateLoaded` is false (save state from different program version). SDL3 silently loads.
3. **No compat check** after state load.
4. **Resume() called unconditionally** -- Windows does not call Resume() after state load. If the save state captured a paused emulator, SDL3 immediately unpauses it.
5. **No suppressColdReset logic** -- irrelevant here since there's no ColdReset call, but the conceptual handling is absent.
6. `kATMediaWriteMode_RO` is hardcoded -- functionally acceptable for state loads since the state itself captures write state.

**Severity:** Medium (kernel mismatch can cause crashes but is an edge case; Resume() difference is noticeable)

---

## Handler: OnCommandSaveState (File > Save State)

**Win source:** main.cpp:2089

**Win engine calls:**
1. `ATUIConfirmPartiallyAccurateSnapshot()` -- checks `g_sim.GetSnapshotStatus().mbPartialAccuracy`; if a disk I/O is in progress, prompts user
2. Show save file dialog (`VDGetSaveFileName`)
3. `g_sim.SaveState(path)`

**SDL3 source:** ui_main.cpp:1036 -- triggers `SDL_ShowSaveFileDialog(SaveStateCallback)`, callback pushes `kATDeferred_SaveState`, processed at ui_main.cpp:729

**SDL3 engine calls:**
1. `g_sim.SaveState(path)`

**Status:** DIVERGE

**Notes:**
1. **No partial accuracy confirmation** -- Windows checks if a disk I/O operation is in progress and warns the user that the save state may be unreliable. SDL3 saves unconditionally, potentially producing corrupt save states that fail on load.

**Severity:** Medium (corrupt save states are annoying but not data-destructive)

---

## Handler: OnCommandQuickSaveState (Quick Save -- F8)

**Win source:** main.cpp:2066

**Win engine calls:**
1. `ATUIConfirmPartiallyAccurateSnapshot()` -- partial accuracy check
2. `g_sim.CreateSnapshot(~g_pATQuickState, ~quickStateInfo)`

**SDL3 source:** ui_main.cpp:381 (`ATUIQuickSaveState`), called from ui_main.cpp:1042 and main_sdl3.cpp:161

**SDL3 engine calls:**
1. `g_sim.CreateSnapshot(~g_pQuickSaveState, ~info)`

**Status:** DIVERGE

**Notes:**
1. **No partial accuracy confirmation** -- same issue as Save State. Quick save during disk I/O may capture inconsistent state.

**Severity:** Low (quick save is transient in-memory state, less impactful than file save)

---

## Handler: OnCommandQuickLoadState (Quick Load -- F7)

**Win source:** main.cpp:2043

**Win engine calls:**
1. Check `g_pATQuickState != nullptr`
2. `g_sim.ApplySnapshot(*g_pATQuickState, nullptr)`

**SDL3 source:** ui_main.cpp:391 (`ATUIQuickLoadState`), called from ui_main.cpp:1039 and main_sdl3.cpp:159

**SDL3 engine calls:**
1. Check `g_pQuickSaveState != nullptr`
2. `g_sim.ApplySnapshot(*g_pQuickSaveState, nullptr)`
3. `g_sim.Resume()`

**Status:** DIVERGE

**Notes:**
1. **Resume() called after quick load** -- Windows does not call Resume(). If emulator was paused before quick load (e.g., user paused to inspect state), SDL3 unpauses it. Windows preserves the paused state.

**Severity:** Low (behavioral difference, not a data integrity issue)

---

## Handler: OnTestCommandQuickLoadState (F7 enabled/disabled)

**Win source:** main.cpp:2039 -- returns `g_pATQuickState != nullptr`

**SDL3 source:** ui_main.cpp:1039 -- ImGui `enabled` parameter: `g_pQuickSaveState != nullptr`

**Status:** MATCH

---

## Handler: ATUIBootImage (programmatic boot, e.g., from scripts)

**Win source:** main.cpp:1391

**Win engine calls:**
1. `ATUIUnloadStorageForBoot()`
2. `DoLoad(path)` -- note: no confirm, no retry loop features used (passes `nullptr` for suppressColdReset pointer, but DoLoad still runs the full retry loop internally)
3. `g_sim.ColdReset()`

**SDL3 source:** stubs/uiaccessors_stubs.cpp:367 -- `void ATUIBootImage(const wchar_t *) {}`

**SDL3 engine calls:**
(none -- empty stub)

**Status:** MISSING

**Notes:**
`ATUIBootImage` is a complete no-op stub in the SDL3 build. Any code path that calls it (e.g., auto-boot scripts, startup configuration) will silently do nothing.

**Severity:** Medium (only relevant if internal code paths call this function; currently the SDL3 UI uses deferred actions instead)

---

## Handler: DoBootWithConfirm (internal -- used by MRU, drag-drop on Windows)

**Win source:** main.cpp:1401

**Win engine calls:**
1. `ATUIConfirmDiscardAllStorage()`
2. `ATUIUnloadStorageForBoot()`
3. `DoLoad()` with full retry loop
4. `ATAddMRUListItem(path)`
5. `g_sim.ColdReset()` (unless suppressColdReset)

**SDL3 source:** (no equivalent function exists)

**Status:** MISSING

**Notes:**
SDL3 has no equivalent of `DoBootWithConfirm`. The deferred boot handler (`kATDeferred_BootImage`) covers some of this functionality but lacks the discard confirmation and retry loop. MRU boot is inline and lacks even the UnloadAll step.

**Severity:** Critical (see Boot Image and MRU List entries above for specific impacts)

---

## Handler: DoLoadStream (internal -- core load engine with retry loop)

**Win source:** main.cpp:1186

**Win engine calls:**
1. Set up `ATMediaLoadContext` with all stop flags enabled
2. Enter retry loop (up to 10 iterations):
   - `g_sim.Load(mctx)`
   - Handle `mbMode5200Required` -> `ATUISwitchHardwareMode5200()`
   - Handle `mbModeComputerRequired` -> `ATUISwitchHardwareModeComputer()`
   - Handle `mbMemoryConflictBasic` -> prompt, optionally `g_sim.SetBASICEnabled(false)`
   - Handle `mbIncompatibleDiskFormat` -> prompt, retry
   - Handle unknown cart mapper -> compat DB lookup or mapper dialog
   - Handle state kernel mismatch -> prompt, retry with `mbAllowKernelMismatch`
3. Post-load: state version warning (`mbPrivateStateLoaded`)
4. `DoCompatibilityCheck()`

**SDL3 source:** (no equivalent exists -- SDL3 uses simple `g_sim.Load()` overload)

**Status:** MISSING

**Notes:**
The entire retry-loop architecture of `DoLoadStream` is absent from the SDL3 build. This is the root cause of most divergences listed in this document. All load paths in SDL3 use `ATSimulator::Load(const wchar_t*, ATMediaWriteMode, ATImageLoadContext*)` which creates an `ATMediaLoadContext` with all stop flags at their defaults (`false`), meaning:
- No mode incompatibility detection or auto-switching
- No BASIC memory conflict detection
- No incompatible disk format warnings
- No kernel mismatch handling for save states
- No iterative resolution of load issues

The only retry-like behavior in SDL3 is the unknown cart mapper dialog, which re-pushes a deferred action with the selected mapper ID.

**Severity:** Critical

---

## Handler: DoCompatibilityCheck (internal -- compat DB check after load)

**Win source:** main.cpp:1170

**Win engine calls:**
1. `ATCompatCheck(tags)` -- look up loaded title in compat DB
2. If match found: `ATUIShowDialogCompatWarning()` -- modal dialog with options:
   - Pause
   - Auto-adjust (`ATCompatAdjust()`)
   - (implicit: continue)

**SDL3 source:** ui_main.cpp:650-666 (inline in deferred boot handler), ui_main.cpp:161 (`ATUICheckCompatibility`), ui_main.cpp:167 (`ATUIRenderCompatWarning`)

**SDL3 engine calls:**
1. `ATCompatCheck(compatTags)`
2. If match: set `g_compatCheckPending = true`, store state for ImGui dialog
3. Dialog rendered in `ATUIRenderCompatWarning()` with:
   - "Auto-adjust settings and reboot"
   - "Pause emulation to adjust manually"
   - "Boot anyway"
   - Mute checkboxes

**Status:** MATCH (for paths that call it)

**Notes:**
The compat check implementation itself is functionally equivalent. The divergence is that not all SDL3 load paths invoke it (MRU boot and command-line load skip it). The deferred Boot/Open Image handlers do call it correctly.

---

## Summary of All Divergences

| Issue | Affected Handlers | Severity |
|-------|-------------------|----------|
| No retry loop / hardware mode auto-switching | Boot, Open, MRU, Drop, CLI | Critical |
| No discard confirmation before boot | Boot, MRU, Drop | Critical |
| No UnloadAll before MRU boot | MRU | Critical |
| No BASIC memory conflict detection | Boot, Open, MRU, Drop, CLI | Critical |
| No incompatible disk format warning | Boot, Open, MRU, Drop, CLI | Medium |
| Resume() called after load (Windows does not) | Boot, Open, MRU, Drop, LoadState, QuickLoad | Low |
| No partial accuracy check before save state | SaveState, QuickSave | Medium |
| No kernel mismatch warning on state load | LoadState | Medium |
| No state version warning on state load | LoadState | Medium |
| No compat check after MRU boot | MRU | Medium |
| No compat check after CLI load | CLI | Medium |
| MRU uses hardcoded RO write mode | MRU | Low |
| ATUIBootImage is a no-op stub | Programmatic boot | Medium |
| No MRU update for CLI load | CLI | Low |
