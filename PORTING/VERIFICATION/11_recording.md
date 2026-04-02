# Verification: Recording

Compares Windows `cmdrecord.cpp` recording commands with SDL3
`ui_main.cpp` Record menu and recording functions.

---

## Handler: OnCommandRecordRawAudio
**Win source:** cmdrecord.cpp:56
**Win engine calls:**
1. Check `!ATUIGetFrontEnd().IsRecording()`
2. Get path from command arg or `VDGetSaveFileName('raud', ..., "*.pcm")`
3. Store path in command arg for redo
4. `ATUIGetFrontEnd().RecordRawAudio(path)`
**SDL3 source:** ui_main.cpp:1789 -- "Record > Record Raw Audio..."
**SDL3 engine calls:**
1. Check `!ATUIIsRecording()` (guards menu item enabled state)
2. `SDL_ShowSaveFileDialog()` with PCM filter
3. Callback pushes `kATDeferred_StartRecordRaw` via `ATUIPushDeferred()`
4. Main loop calls `ATUIStartAudioRecording(path, true)` at line 425
5. Creates `ATAudioWriter(path, raw=true, stereo, pal, nullptr)`
6. `g_sim.GetAudioOutput()->SetAudioTap(g_pAudioWriter)`
**Status:** MATCH
**Notes:** SDL3 implements recording directly instead of via ATUIFrontEnd, but functional result is equivalent. SDL3 does not store path for redo (no command context system).
**Severity:** Low (no redo support, minor)

---

## Handler: OnCommandRecordAudio
**Win source:** cmdrecord.cpp:73
**Win engine calls:**
1. Check `!ATUIGetFrontEnd().IsRecording()`
2. Get path from command arg or `VDGetSaveFileName('raud', ..., "*.wav")`
3. Store path in command arg
4. `ATUIGetFrontEnd().RecordAudio(path)`
**SDL3 source:** ui_main.cpp:1799 -- "Record > Record Audio..."
**SDL3 engine calls:**
1. Check `!ATUIIsRecording()`
2. `SDL_ShowSaveFileDialog()` with WAV filter
3. Callback pushes `kATDeferred_StartRecordWAV`
4. Main loop calls `ATUIStartAudioRecording(path, false)` at line 425
5. Creates `ATAudioWriter(path, raw=false, stereo, pal, nullptr)`
6. `g_sim.GetAudioOutput()->SetAudioTap(g_pAudioWriter)`
**Status:** MATCH
**Severity:** Low (no redo support, minor)

---

## Handler: OnCommandRecordVideo
**Win source:** cmdrecord.cpp:90
**Win engine calls:**
1. Check `!ATUIGetFrontEnd().IsRecording()`
2. Determine hz50 from video standard
3. Show `ATUIShowDialogVideoEncoding(parent, hz50)` -- settings dialog
4. If settings returned, show save file dialog (AVI/WMV/MP4 based on encoding)
5. `ATUIGetFrontEnd().RecordVideo(path, *settings)`
**SDL3 source:** ui_main.cpp:1809 -- "Record > Record Video..."
**SDL3 engine calls:**
1. Check `!ATUIIsRecording()`
2. Set `g_showVideoRecordingDialog = true`
3. `RenderVideoRecordingDialog()` at line 1844 shows ImGui settings dialog
4. Loads saved settings from registry on first open
5. User selects codec (AVI only: Raw/RLE/ZMBV), frame rate, scaling, aspect, resampling
6. On "Record" click: save settings to registry, show AVI save dialog
7. Callback pushes `kATDeferred_StartRecordVideo` with encoding
8. Main loop calls `ATUIStartVideoRecording()` at line 459
9. Creates video writer, configures frame format, frame rate, PAR, scaling
10. `g_sim.GetAudioOutput()->SetAudioTap(writer->AsAudioTap())`
11. `gtia.AddVideoTap(writer->AsVideoTap())`
**Status:** DIVERGE
**Notes:**
- SDL3 only supports AVI encodings (Raw/RLE/ZMBV). Windows also supports WMV7, WMV9, H264+AAC, H264+MP3. This is acceptable since WMV/H264 codecs are Windows-specific (Media Foundation).
- SDL3 video recording settings dialog is functionally complete for available codecs.
- SDL3 correctly loads/saves recording preferences from registry.
**Severity:** Low (WMV/H264 codecs are platform-specific, AVI coverage is complete)

---

## Handler: OnCommandRecordSapTypeR
**Win source:** cmdrecord.cpp:144
**Win engine calls:**
1. Check `!ATUIGetFrontEnd().IsRecording()`
2. Get path from command arg or `VDGetSaveFileName('rsap', ..., "*.sap")`
3. Store path in command arg
4. `ATUIGetFrontEnd().RecordSap(path)`
**SDL3 source:** ui_main.cpp:1812 -- "Record > Record SAP Type R..."
**SDL3 engine calls:**
1. Check `!ATUIIsRecording()`
2. `SDL_ShowSaveFileDialog()` with SAP filter
3. Callback pushes `kATDeferred_StartRecordSAP`
4. Main loop calls `ATUIStartSAPRecording()` at line 443
5. `ATCreateSAPWriter()`
6. `writer->Init(eventMgr, &pokey, nullptr, path, pal)`
**Status:** MATCH
**Severity:** Low (no redo support, minor)

---

## Handler: OnCommandRecordVgm
**Win source:** cmdrecord.cpp:127
**Win engine calls:**
1. Check `!ATUIGetFrontEnd().IsRecording()`
2. Get path from command arg or `VDGetSaveFileName("VGMAudio", ..., "*.vgm")`
3. Store path in command arg
4. `ATUIGetFrontEnd().RecordVgm(path)`
**SDL3 source:** ui_main.cpp:1822 -- "Record > Record VGM..."
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Notes:** VGM recording is listed as placeholder with comment "excluded from build (wchar_t size)". The menu item is visible but permanently disabled.
**Severity:** Low

---

## Handler: OnCommandRecordStop
**Win source:** cmdrecord.cpp:51
**Win engine calls:**
1. `ATUIGetFrontEnd().CheckRecordingExceptions()`
2. `ATUIGetFrontEnd().StopRecording()`
**SDL3 source:** ui_main.cpp:1826 -- "Record > Stop Recording"
**SDL3 engine calls:**
1. `ATUIStopRecording()` at line 554
2. If video: remove video tap, remove audio tap, shutdown writer, delete
3. If audio: remove audio tap, finalize writer, delete
4. If SAP: shutdown writer, delete
**Status:** MATCH
**Notes:** SDL3 does not call `CheckRecordingExceptions()` before stopping. Any pending exceptions from the recording writer would be silently lost.
**Severity:** Low

---

## Handler: OnCommandRecordPause / Resume / PauseResume
**Win source:** cmdrecord.cpp:39-49
**Win engine calls:**
1. Pause: `ATUIGetFrontEnd().PauseRecording()`
2. Resume: `ATUIGetFrontEnd().ResumeRecording()`
3. PauseResume: `ATUIGetFrontEnd().TogglePauseResumeRecording()`
**SDL3 source:** ui_main.cpp:1830 -- "Record > Pause/Resume Recording"
**SDL3 engine calls:**
1. Check `g_pVideoWriter != nullptr`
2. Toggle: `g_pVideoWriter->IsPaused()` ? `Resume()` : `Pause()`
**Status:** MATCH
**Notes:** SDL3 combines pause/resume into a single toggle, matching `OnCommandRecordPauseResume`. Individual pause/resume commands are not exposed separately, which is fine since Windows also has the combined toggle mapped to a menu item.
**Severity:** Low

---

## Handler: OnCommandToolsConvertSapToExe
**Win source:** cmdrecord.cpp:161-207
**Win engine calls:**
1. Show open file dialog for source SAP file
2. Show save file dialog for output XEX file
3. `ATConvertSAPToPlayer(dstPath, srcPath)`
**SDL3 source:** ui_main.cpp:1990 -- "Tools > Convert SAP to EXE..."
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Notes:** Convert SAP to EXE is a placeholder in the Tools menu.
**Severity:** Low

---

## Summary

| Feature | Status |
|---------|--------|
| Record Raw Audio | MATCH |
| Record Audio (WAV) | MATCH |
| Record Video (AVI) | MATCH |
| Record Video (WMV/H264) | N/A (platform-specific) |
| Record SAP Type R | MATCH |
| Record VGM | MISSING (placeholder) |
| Stop Recording | MATCH |
| Pause/Resume Recording | MATCH |
| Convert SAP to EXE | MISSING (placeholder) |
| Video Recording Settings Dialog | MATCH (AVI subset) |
