# Verification: Profiles and Settings/Options

Compares Windows profile management (`cmdsystem.cpp`) and options
(`cmdoptions.cpp`) with SDL3 `ui_profiles.cpp` and `ui_system.cpp`
settings categories.

---

## Handler: OnCommandSystemEditProfilesDialog
**Win source:** cmdsystem.cpp:398
**Win engine calls:**
1. `ATUIShowDialogProfiles(ATUIGetNewPopupOwner())`
2. `ATUIRebuildDynamicMenu(kATUIDynamicMenu_Profile)`
**SDL3 source:** ui_profiles.cpp:18 -- "System > Profiles > Edit Profiles"
**SDL3 engine calls:**
1. Opens ImGui window "Profiles" via `ATUIRenderProfiles()`
2. Gets current profile ID: `ATSettingsGetCurrentProfileId()`
3. Enumerates profiles: `ATSettingsProfileEnum(profileIds)`
4. Shows current profile name, temporary mode checkbox
5. Default profiles section: table with Switch buttons for each hardware type
6. All profiles section: selectable list with context menu
7. Profile operations: Add, Rename (double-click/context), Delete, Visible toggle
8. Switch profile: `ATSettingsSwitchProfile(id)`, `sim.Resume()`
9. Add: `ATSettingsGenerateProfileId()`, set parent/name/category/visible
**Status:** DIVERGE
**Notes:**
- SDL3 does not rebuild dynamic menus after dialog closes (no `ATUIRebuildDynamicMenu`). The System > Profiles submenu uses live enumeration so this may not matter.
- Windows dialog is modal; SDL3 is a floating ImGui window.
- SDL3 missing: "Duplicate" profile operation (Windows has it).
- SDL3 missing: "Set as Default" for hardware type assignment (Windows allows changing which profile is default for each hardware type).
- SDL3 missing: Category mask editing (what settings categories the profile overrides).
- SDL3 missing: Parent profile selection (always sets current profile as parent).
**Severity:** Medium

---

## Handler: OnCommandConfigureSystem
**Win source:** cmdsystem.cpp:403
**Win engine calls:**
1. `ATUIShowDialogConfigureSystem(ATUIGetNewPopupOwner())`
**SDL3 source:** ui_system.cpp:1832 -- "System > Configure System..."
**SDL3 engine calls:**
1. Opens ImGui window "Configure System" with tree sidebar
2. 28 category pages organized under Computer/Outputs/Peripherals/Media/Emulator
3. Each category renders its own ImGui content
**Status:** MATCH
**Notes:** SDL3 faithfully replicates the paged dialog structure with sidebar navigation matching the Windows tree hierarchy.
**Severity:** N/A

---

## Handler: Options.ToggleSingleInstance
**Win source:** cmdoptions.cpp:103
**Win engine calls:**
1. Toggle `g_ATOptions.mbSingleInstance`
2. `ATOptionsRunUpdateCallbacks(&prev)`
3. `ATOptionsSave()`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Single instance mode is Windows-specific (uses named mutex). Not applicable to SDL3.
**Severity:** N/A (platform-specific)

---

## Handler: Options.PauseDuringMenu
**Win source:** cmdoptions.cpp:104
**Win engine calls:**
1. Toggle `g_ATOptions.mbPauseDuringMenu`
2. `ATOptionsRunUpdateCallbacks(&prev)`
3. `ATOptionsSave()`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** SDL3 uses ImGui menus which don't block the main loop, so this option is not directly applicable in the same way.
**Severity:** N/A (platform behavior difference)

---

## Handler: Options.ToggleDirectoryPolling
**Win source:** cmdoptions.cpp:105
**Win engine calls:**
1. Toggle `g_ATOptions.mbPollDirectories`
2. `ATOptionsRunUpdateCallbacks(&prev)`
3. `ATOptionsSave()`
**SDL3 source:** ui_system.cpp:1569 -- "Configure System > Workarounds"
**SDL3 engine calls:**
1. Checkbox toggles `g_ATOptions.mbPollDirectories`
**Status:** DIVERGE
**Notes:** SDL3 exposes the toggle but does not call `ATOptionsRunUpdateCallbacks` or `ATOptionsSave`. Changes are not persisted and update callbacks are not fired.
**Severity:** Medium

---

## Handler: Options.UseDarkTheme
**Win source:** cmdoptions.cpp:106
**Win engine calls:**
1. Toggle `g_ATOptions.mbDarkTheme`
2. `ATOptionsRunUpdateCallbacks(&prev)`
3. `ATOptionsSave()`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** SDL3 uses ImGui with its own theming. Dark theme is the default.
**Severity:** N/A (platform-specific)

---

## Handler: Options.EfficiencyMode*
**Win source:** cmdoptions.cpp:107-109
**Win engine calls:**
1. Set `g_ATOptions.mEfficiencyMode` to Default/Performance/Efficiency
2. `ATOptionsRunUpdateCallbacks(&prev)`
3. `ATOptionsSave()`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Process efficiency mode is Windows-specific (SetProcessPowerThrottling API).
**Severity:** N/A (platform-specific)

---

## Handler: Options.ResetAllDialogs
**Win source:** cmdoptions.cpp:76
**Win engine calls:**
1. Show warning confirmation dialog
2. `ATUIGenericDialogUndoAllIgnores()`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** No "don't show this again" dialog system in SDL3 ImGui UI yet.
**Severity:** Low

---

## Handler: Options.ToggleLaunchAutoProfile
**Win source:** cmdoptions.cpp:111
**Win engine calls:**
1. Toggle `g_ATOptions.mbLaunchAutoProfile`
2. `ATOptionsRunUpdateCallbacks(&prev)`
3. `ATOptionsSave()`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Launch auto-profile not exposed in SDL3 UI.
**Severity:** Low

---

## Handler: Options.SetFileAssoc* / Options.UnsetFileAssoc*
**Win source:** cmdoptions.cpp:85-99
**Win engine calls:**
1. `ATUIShowDialogSetFileAssociations(parent, allowElevation, userOnly)`
2. `ATUIShowDialogRemoveFileAssociations(parent, allowElevation, userOnly)`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** File associations are Windows-specific (registry-based shell integration).
**Severity:** N/A (platform-specific)

---

## Handler: Options.ErrorMode*
**Win source:** cmdoptions.cpp:116-119
**Win engine calls:**
1. Set `g_ATOptions.mErrorMode` to Dialog/Debug/Pause/ColdReset
2. `ATOptionsRunUpdateCallbacks(&prev)`
3. `ATOptionsSave()`
**SDL3 source:** ui_system.cpp:1675 -- "Configure System > Error Handling"
**SDL3 engine calls:**
1. Combo box with 4 error modes
2. On change: set `g_ATOptions.mErrorMode`
3. `ATOptionsRunUpdateCallbacks(&prev)`
4. `ATOptionsSave()`
**Status:** MATCH
**Severity:** N/A

---

## Handler: Options.MediaDefaultMode*
**Win source:** cmdoptions.cpp:121-124
**Win engine calls:**
1. Set `g_ATOptions.mDefaultWriteMode` to RO/VRWSafe/VRW/RW
2. `ATOptionsRunUpdateCallbacks(&prev)`
3. `ATOptionsSave()`
**SDL3 source:** ui_system.cpp:1486 -- "Configure System > Media > Defaults"
**SDL3 engine calls:**
1. Combo box with 4 write modes
2. On change: set `g_ATOptions.mDefaultWriteMode`
**Status:** DIVERGE
**Notes:** SDL3 sets the value but does not call `ATOptionsRunUpdateCallbacks` or `ATOptionsSave`. Changes are not persisted.
**Severity:** Medium

---

## Handler: Options.ToggleDisplayD3D9 / D3D11 / 16Bit / CustomRefresh
**Win source:** cmdoptions.cpp:126-129
**Win engine calls:**
1. Toggle respective display option in `g_ATOptions`
2. `ATOptionsRunUpdateCallbacks(&prev)`
3. `ATOptionsSave()`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** These are Windows Direct3D-specific rendering options. Not applicable to SDL3.
**Severity:** N/A (platform-specific)

---

## Handler: RenderEaseOfUseCategory
**Win source:** (Windows IDD_CONFIGURE_EASEOFUSE, referenced by Configure System dialog)
**Win engine calls:**
1. Reset flags for cartridge change, video standard change, BASIC change
2. Each flag controls whether system auto-resets on that type of change
**SDL3 source:** ui_system.cpp:1162 -- "Configure System > Ease of Use"
**SDL3 engine calls:**
1. `ATUIGetResetFlags()`
2. Checkboxes for CartridgeChange, VideoStandardChange, BasicChange
3. `ATUIModifyResetFlag()` on toggle
**Status:** MATCH
**Severity:** N/A

---

## Handler: RenderCompatDBCategory
**Win source:** cmdtools.cpp:34 (`OnCommandToolsCompatDBDialog`)
**Win engine calls:**
1. `ATUIShowDialogCompatDB(ATUIGetNewPopupOwner())`
**SDL3 source:** ui_system.cpp:1588 -- "Configure System > Compat DB"
**SDL3 engine calls:**
1. Checkbox for `g_ATOptions.mbCompatEnable` with save
2. Checkbox for `g_ATOptions.mbCompatEnableInternalDB` with save
3. Checkbox for `g_ATOptions.mbCompatEnableExternalDB` with save
4. External DB path input with Browse button (SDL file dialog)
5. "Unmute all warnings" button: `ATCompatUnmuteAllTitles()`
**Status:** MATCH
**Notes:** SDL3 implements all compat DB options with proper save/callback semantics. External DB path browse uses deferred action for thread safety.
**Severity:** N/A

---

## Handler: RenderCaptionCategory
**Win source:** (Windows IDD_CONFIGURE_CAPTION, part of Configure System)
**Win engine calls:**
1. Template text input for window caption
2. Available variables (profile, hardware, video, speed, fps)
3. Reset to default
**SDL3 source:** ui_system.cpp:1529 -- "Configure System > Window Caption"
**SDL3 engine calls:**
1. `ATUIGetWindowCaptionTemplate()` to read current
2. `ImGui::InputText()` for editing
3. `ATUISetWindowCaptionTemplate()` on change
4. "Reset to Default" button
**Status:** MATCH
**Severity:** N/A

---

## Handler: RenderWorkaroundsCategory
**Win source:** (Windows IDD_CONFIGURE_WORKAROUNDS, part of Configure System)
**Win engine calls:**
1. Directory polling toggle (`g_ATOptions.mbPollDirectories`)
**SDL3 source:** ui_system.cpp:1569 -- "Configure System > Workarounds"
**SDL3 engine calls:**
1. Checkbox for `g_ATOptions.mbPollDirectories`
**Status:** DIVERGE
**Notes:** SDL3 does not call `ATOptionsRunUpdateCallbacks` or `ATOptionsSave` after toggling. The Windows options command handlers always do both.
**Severity:** Medium

---

## Summary

| Feature | Status |
|---------|--------|
| Edit Profiles Dialog | Partial (missing duplicate, default assignment, category mask) |
| Configure System Dialog | MATCH (structure and categories) |
| Error Handling Options | MATCH |
| Compat DB Options | MATCH |
| Caption Template | MATCH |
| Ease of Use Flags | MATCH |
| Media Default Write Mode | DIVERGE (no save) |
| Directory Polling | DIVERGE (no save) |
| Single Instance | N/A (platform-specific) |
| Dark Theme | N/A (platform-specific) |
| Efficiency Mode | N/A (platform-specific) |
| File Associations | N/A (platform-specific) |
| Display D3D Options | N/A (platform-specific) |
| Reset All Dialogs | MISSING |
| Launch Auto-Profile | MISSING |
