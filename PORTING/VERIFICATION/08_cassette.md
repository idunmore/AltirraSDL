# Verification 08 -- Cassette Operations

Cross-reference of Windows cassette handlers (cmdcassette.cpp) against SDL3 implementation
(ui_cassette.cpp, ui_main.cpp, ui_system.cpp).

---

## Handler: OnCommandCassetteLoadNew
**Win source:** cmdcassette.cpp:33
**Win engine calls:**
1. `g_sim.GetCassette().LoadNew()`
**SDL3 source:** ui_main.cpp:992-993 -- "File > Cassette > New Tape"
**SDL3 engine calls:**
1. `cas.LoadNew()`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandCassetteLoad
**Win source:** cmdcassette.cpp:37
**Win engine calls:**
1. `VDGetLoadFileName('cass', ...)` -- file dialog with tape filters
2. `cas.Load(fn.c_str())`
3. `cas.Play()`
**SDL3 source:** ui_main.cpp:995-1002 -- "File > Cassette > Load..."
**SDL3 engine calls:**
1. `SDL_ShowOpenFileDialog(...)` with CAS/WAV filters
2. Deferred `kATDeferred_LoadCassette`: `g_sim.GetCassette().Load(a.path.c_str())`
**Status:** DIVERGE
**Notes:**
- SDL3 does NOT call `cas.Play()` after loading. Windows auto-starts playback after load.
**Severity:** Medium

---

## Handler: OnCommandCassetteUnload
**Win source:** cmdcassette.cpp:47
**Win engine calls:**
1. `g_sim.GetCassette().Unload()`
**SDL3 source:** ui_main.cpp:1005-1006 -- "File > Cassette > Unload"
**SDL3 engine calls:**
1. `cas.Unload()`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandCassetteSave
**Win source:** cmdcassette.cpp:51
**Win engine calls:**
1. Check `cas.IsLoaded()` -- return if not
2. `VDGetSaveFileName('cass', ...)` -- save dialog with CAS filter
3. `VDFileStream f(...)` -- open file
4. `ATSaveCassetteImageCAS(f, cas.GetImage())`
5. `cas.SetImageClean()`
**SDL3 source:** ui_main.cpp:1008-1011 -- "File > Cassette > Save..."
**SDL3 engine calls:**
1. `SDL_ShowSaveFileDialog(CassetteSaveCallback, ...)`
2. Deferred `kATDeferred_SaveCassette` (ui_main.cpp:732-738):
   - `ATSaveCassetteImageCAS(fs, image)`
   - `g_sim.GetCassette().SetImagePersistent(a.path.c_str())`
   - `g_sim.GetCassette().SetImageClean()`
**Status:** DIVERGE
**Notes:**
- SDL3 additionally calls `SetImagePersistent(path)` which Windows does not. This is arguably an improvement (sets the persistent path for future saves).
**Severity:** Low

---

## Handler: OnCommandCassetteExportAudioTape
**Win source:** cmdcassette.cpp:67
**Win engine calls:**
1. Check `cas.IsLoaded()` -- return if not
2. `VDGetSaveFileName('casa', ...)` -- save dialog with WAV filter
3. `VDFileStream f(...)` -- open file
4. `ATSaveCassetteImageWAV(f, cas.GetImage())`
5. `cas.SetImageClean()`
**SDL3 source:** ui_main.cpp:1013-1021 -- "File > Cassette > Export Audio Tape..."
**SDL3 engine calls:**
1. `SDL_ShowSaveFileDialog(...)` with WAV filter
2. Deferred `kATDeferred_ExportCassetteAudio` (ui_main.cpp:742-748):
   - `ATSaveCassetteImageWAV(f, image)`
   - `g_sim.GetCassette().SetImageClean()`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandCassetteTapeControlDialog
**Win source:** cmdcassette.cpp:83
**Win engine calls:**
1. `ATUIShowTapeControlDialog(ATUIGetNewPopupOwner(), g_sim.GetCassette())`
**SDL3 source:** ui_main.cpp:985-986 -- "File > Cassette > Tape Control..."
**SDL3 engine calls:**
1. Sets `state.showCassetteControl = true`, which triggers `ATUIRenderCassetteControl()` in ui_cassette.cpp
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandCassetteTapeEditorDialog
**Win source:** cmdcassette.cpp:87
**Win engine calls:**
1. `ATUIShowDialogTapeEditor()`
**SDL3 source:** ui_main.cpp:988 -- "File > Cassette > Tape Editor..."
**SDL3 engine calls:**
1. `ImGui::MenuItem("Tape Editor...", nullptr, false, false)` -- placeholder, always disabled
**Status:** MISSING
**Notes:** Tape Editor is not implemented in SDL3. The menu item exists but is permanently disabled.
**Severity:** Medium

---

## Handler: OnCommandCassetteToggleSIOPatch
**Win source:** cmdcassette.cpp:91
**Win engine calls:**
1. `g_sim.SetCassetteSIOPatchEnabled(!g_sim.IsCassetteSIOPatchEnabled())`
**SDL3 source:** ui_system.cpp:663-665 -- "Configure System > Acceleration > C: patch (Cassette SIO)"
**SDL3 engine calls:**
1. `sim.SetCassetteSIOPatchEnabled(casSioPatch)` via checkbox
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandCassetteToggleAutoBoot
**Win source:** cmdcassette.cpp:95
**Win engine calls:**
1. `g_sim.SetCassetteAutoBootEnabled(!g_sim.IsCassetteAutoBootEnabled())`
**SDL3 source:** ui_system.cpp:1025-1027 -- "Configure System > Cassette > Auto-boot on startup"
**SDL3 engine calls:**
1. `sim.SetCassetteAutoBootEnabled(autoBoot)` via checkbox
**Status:** MATCH
**Notes:** Also available in SDL3 System menu: ui_main.cpp:1502-1504 "Auto-Boot Tape (Hold Start)"
**Severity:** N/A

---

## Handler: OnCommandCassetteToggleAutoBasicBoot
**Win source:** cmdcassette.cpp:99
**Win engine calls:**
1. `g_sim.SetCassetteAutoBasicBootEnabled(!g_sim.IsCassetteAutoBasicBootEnabled())`
**SDL3 source:** ui_system.cpp:1029-1031 -- "Configure System > Cassette > Auto-boot BASIC on startup"
**SDL3 engine calls:**
1. `sim.SetCassetteAutoBasicBootEnabled(autoBasicBoot)` via checkbox
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandCassetteToggleAutoRewind
**Win source:** cmdcassette.cpp:103
**Win engine calls:**
1. `g_sim.SetCassetteAutoRewindEnabled(!g_sim.IsCassetteAutoRewindEnabled())`
**SDL3 source:** ui_system.cpp:1033-1035 -- "Configure System > Cassette > Auto-rewind on startup"
**SDL3 engine calls:**
1. `sim.SetCassetteAutoRewindEnabled(autoRewind)` via checkbox
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandCassetteToggleLoadDataAsAudio
**Win source:** cmdcassette.cpp:107
**Win engine calls:**
1. `cas.SetLoadDataAsAudioEnable(!cas.IsLoadDataAsAudioEnabled())`
**SDL3 source:** ui_system.cpp:1037-1039 -- "Configure System > Cassette > Load data as audio"
**SDL3 engine calls:**
1. `cas.SetLoadDataAsAudioEnable(loadDataAsAudio)` via checkbox
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandCassetteToggleRandomizeStartPosition
**Win source:** cmdcassette.cpp:113
**Win engine calls:**
1. `g_sim.SetCassetteRandomizedStartEnabled(!g_sim.IsCassetteRandomizedStartEnabled())`
**SDL3 source:** ui_system.cpp:1041-1043 -- "Configure System > Cassette > Randomize starting position"
**SDL3 engine calls:**
1. `sim.SetCassetteRandomizedStartEnabled(randomStart)` via checkbox
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandCassetteTurboModeNone / CommandControl / DataControl / ProceedSense / InterruptSense / KSOTurbo2000 / TurboD / Always
**Win source:** cmdcassette.cpp:117-147 (8 functions, one per turbo mode)
**Win engine calls:**
1. `g_sim.GetCassette().SetTurboMode(kATCassetteTurboMode_XXX)`
**SDL3 source:** ui_system.cpp:1047-1055 -- "Configure System > Cassette > Turbo mode" combo
**SDL3 engine calls:**
1. `cas.SetTurboMode((ATCassetteTurboMode)turbo)` via combo box
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandCassetteTogglePolarity / PolarityModeNormal / PolarityModeInverted
**Win source:** cmdcassette.cpp:149-161
**Win engine calls:**
1. `cas.SetPolarityMode(kATCassettePolarityMode_Normal)` or `kATCassettePolarityMode_Inverted`
**SDL3 source:** ui_system.cpp:1066-1070 -- "Configure System > Cassette > Invert turbo data" checkbox
**SDL3 engine calls:**
1. `cas.SetPolarityMode(inverted ? kATCassettePolarityMode_Inverted : kATCassettePolarityMode_Normal)`
**Status:** MATCH
**Notes:** Windows has 3 commands (toggle + explicit normal + explicit inverted). SDL3 uses a single checkbox which is equivalent.
**Severity:** N/A

---

## Handler: OnCommandCassetteDirectSenseNormal / LowSpeed / HighSpeed / MaxSpeed
**Win source:** cmdcassette.cpp:163-177
**Win engine calls:**
1. `g_sim.GetCassette().SetDirectSenseMode(ATCassetteDirectSenseMode::XXX)`
**SDL3 source:** ui_system.cpp:1074-1080 -- "Configure System > Cassette > Direct read filter" combo
**SDL3 engine calls:**
1. `cas.SetDirectSenseMode((ATCassetteDirectSenseMode)dsm)` via combo box
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandCassetteToggleTurboPrefilter / TurboDecoderSlopeNoFilter / SlopeFilter / PeakFilter / PeakFilterLoHi / PeakFilterHiLo
**Win source:** cmdcassette.cpp:179-207
**Win engine calls:**
1. `g_sim.GetCassette().SetTurboDecodeAlgorithm(ATCassetteTurboDecodeAlgorithm::XXX)`
**SDL3 source:** ui_system.cpp:1057-1064 -- "Configure System > Cassette > Turbo decoder" combo
**SDL3 engine calls:**
1. `cas.SetTurboDecodeAlgorithm((ATCassetteTurboDecodeAlgorithm)decoder)` via combo box
**Status:** MATCH
**Severity:** N/A

---

## Handler: Cassette.ToggleVBIAvoidance
**Win source:** cmdcassette.cpp:293-301
**Win engine calls:**
1. `cassette.SetVBIAvoidanceEnabled(!cassette.IsVBIAvoidanceEnabled())`
**SDL3 source:** ui_system.cpp:1084-1086 -- "Configure System > Cassette > Avoid OS C: random VBI-related errors"
**SDL3 engine calls:**
1. `cas.SetVBIAvoidanceEnabled(vbiAvoid)` via checkbox
**Status:** MATCH
**Severity:** N/A

---

## Handler: Cassette.ToggleFSKSpeedCompensation
**Win source:** cmdcassette.cpp:302-311
**Win engine calls:**
1. `cassette.SetFSKSpeedCompensationEnabled(!cassette.GetFSKSpeedCompensationEnabled())`
**SDL3 source:** ui_system.cpp:1090-1092 -- "Configure System > Cassette > Enable FSK speed compensation"
**SDL3 engine calls:**
1. `cas.SetFSKSpeedCompensationEnabled(fskComp)` via checkbox
**Status:** MATCH
**Severity:** N/A

---

## Handler: Cassette.ToggleCrosstalkReduction
**Win source:** cmdcassette.cpp:312-321
**Win engine calls:**
1. `cassette.SetCrosstalkReductionEnabled(!cassette.GetCrosstalkReductionEnabled())`
**SDL3 source:** ui_system.cpp:1094-1096 -- "Configure System > Cassette > Enable crosstalk reduction"
**SDL3 engine calls:**
1. `cas.SetCrosstalkReductionEnabled(crosstalk)` via checkbox
**Status:** MATCH
**Severity:** N/A

---

## Tape Control Dialog (ui_cassette.cpp)

### Transport Controls: Stop/Pause/Play/SeekStart/SeekEnd/Record
**Win source:** Windows IDD_TAPE_CONTROL dialog (uitapecontrol.cpp)
**SDL3 source:** ui_cassette.cpp:36-116 -- full dialog
**SDL3 engine calls:**
1. Stop: `cas.Stop()`
2. Pause: `cas.SetPaused(!paused)`
3. Play: `cas.Play()`
4. Seek Start: `cas.SeekToTime(0.0f)`
5. Seek End: `cas.SeekToTime(cas.GetLength())`
6. Record: `cas.Record()`
**Status:** MATCH
**Severity:** N/A

### Position Slider
**Win source:** Windows IDD_TAPE_CONTROL -- position slider + labels
**SDL3 source:** ui_cassette.cpp:52-65 -- SliderFloat with M:SS.d format
**SDL3 engine calls:**
1. Read: `cas.GetPosition()`, `cas.GetLength()`
2. Seek: `cas.SeekToTime(pos)`
**Status:** MATCH
**Severity:** N/A

---

## Summary

| Handler | Status | Severity |
|---------|--------|----------|
| CassetteLoadNew | MATCH | -- |
| CassetteLoad | DIVERGE | Medium |
| CassetteUnload | MATCH | -- |
| CassetteSave | DIVERGE | Low |
| CassetteExportAudioTape | MATCH | -- |
| CassetteTapeControlDialog | MATCH | -- |
| CassetteTapeEditorDialog | MISSING | Medium |
| CassetteToggleSIOPatch | MATCH | -- |
| CassetteToggleAutoBoot | MATCH | -- |
| CassetteToggleAutoBasicBoot | MATCH | -- |
| CassetteToggleAutoRewind | MATCH | -- |
| CassetteToggleLoadDataAsAudio | MATCH | -- |
| CassetteToggleRandomizeStartPosition | MATCH | -- |
| TurboMode (all 8 modes) | MATCH | -- |
| Polarity (Normal/Inverted) | MATCH | -- |
| DirectSense (4 modes) | MATCH | -- |
| TurboDecoder (5 algorithms) | MATCH | -- |
| ToggleVBIAvoidance | MATCH | -- |
| ToggleFSKSpeedCompensation | MATCH | -- |
| ToggleCrosstalkReduction | MATCH | -- |
| Tape Control dialog | MATCH | -- |

### Key Divergences

1. **No auto-play after load** -- Windows calls `cas.Play()` after loading a tape; SDL3 does not. The tape will be loaded but not started, which may confuse users expecting immediate playback.
2. **Tape Editor not implemented** -- The menu item exists but is permanently disabled. The Windows tape editor is a complex waveform viewer/editor.
3. **SetImagePersistent extra call** -- SDL3 save handler additionally calls `SetImagePersistent()` which Windows does not; this is a minor difference that arguably improves behavior.
