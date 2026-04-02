# Verification: Video & Display Handlers

Cross-reference of Windows video/display command handlers against SDL3 implementation.

**Windows sources:** `src/Altirra/source/cmdview.cpp`, `src/Altirra/source/cmdsystem.cpp`
**SDL3 sources:** `src/AltirraSDL/source/ui_main.cpp`, `src/AltirraSDL/source/ui_system.cpp`, `src/AltirraSDL/source/ui_display.cpp`

---

## Handler: OnCommandViewFilterModePoint / Bilinear / SharpBilinear / Bicubic / Default
**Win source:** cmdview.cpp:59-77
**Win engine calls:**
1. `ATUISetDisplayFilterMode(kATDisplayFilterMode_Point)` (and Bilinear, SharpBilinear, Bicubic, AnySuitable)
**SDL3 source:** ui_main.cpp:1211-1220 -- "View > Filter Mode > Point/Bilinear/Sharp Bilinear/Bicubic/Default"
**SDL3 engine calls:**
1. `ATUISetDisplayFilterMode(kATDisplayFilterMode_Point)` (and all others)
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandViewNextFilterMode
**Win source:** cmdview.cpp:39-57
**Win engine calls:**
1. `ATUIGetDisplayFilterMode()` to read current
2. `ATUISetDisplayFilterMode()` to cycle to next
**SDL3 source:** ui_main.cpp:1199-1208 -- "View > Filter Mode > Next Mode"
**SDL3 engine calls:**
1. `ATUIGetDisplayFilterMode()` to read current
2. `ATUISetDisplayFilterMode(kModes[(cur + 1) % 5])` to cycle
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandViewFilterSharpness (Softer/Soft/Normal/Sharp/Sharper)
**Win source:** cmdview.cpp:79-97
**Win engine calls:**
1. `ATUISetViewFilterSharpness(-2)` through `ATUISetViewFilterSharpness(+2)`
**SDL3 source:** ui_main.cpp:1224-1237 -- "View > Filter Sharpness"
**SDL3 engine calls:**
1. `ATUIGetViewFilterSharpness()` to read current
2. `ATUISetViewFilterSharpness(-2)` through `ATUISetViewFilterSharpness(+2)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandViewStretchFitToWindow / PreserveAspectRatio / SquarePixels / SquarePixelsInt / PreserveAspectRatioInt
**Win source:** cmdview.cpp:99-117
**Win engine calls:**
1. `ATUISetDisplayStretchMode(kATDisplayStretchMode_Unconstrained)` (and PreserveAspectRatio, SquarePixels, Integral, IntegralPreserveAspectRatio)
**SDL3 source:** ui_main.cpp:1242-1251 -- "View > Video Frame > Fit to Window / ..."
**SDL3 engine calls:**
1. `ATUISetDisplayStretchMode(kATDisplayStretchMode_Unconstrained)` (and all others)
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandViewOverscanOSScreen / Normal / Widescreen / Extended / Full
**Win source:** cmdview.cpp:119-137
**Win engine calls:**
1. `ATUISetOverscanMode(ATGTIAEmulator::kOverscanOSScreen)` (and Normal, Widescreen, Extended, Full)
**SDL3 source:** ui_main.cpp:1272-1281 -- "View > Overscan Mode"
**SDL3 engine calls:**
1. `gtia.SetOverscanMode(ATGTIAEmulator::kOverscanOSScreen)` (and all others)
**Status:** DIVERGE
**Notes:** Windows calls `ATUISetOverscanMode()` (a UI accessor that may do additional work), SDL3 calls `gtia.SetOverscanMode()` directly. The SDL3 Display Settings dialog (ui_display.cpp:71) also calls `gtia.SetOverscanMode()` directly. If `ATUISetOverscanMode` does anything beyond `gtia.SetOverscanMode`, the behavior diverges.
**Severity:** Medium

---

## Handler: OnCommandViewVerticalOverscan
**Win source:** cmdview.cpp:36 (declared, defined elsewhere)
**SDL3 source:** ui_main.cpp:1286-1298 -- "View > Overscan Mode > Vertical Override"
**SDL3 engine calls:**
1. `gtia.SetVerticalOverscanMode()` for each mode
**Status:** MATCH (functionally -- calls GTIA directly)
**Severity:** N/A

---

## Handler: OnCommandViewTogglePALExtended
**Win source:** cmdview.cpp:37 (declared, defined elsewhere)
**SDL3 source:** ui_main.cpp:1301-1303 -- "View > Overscan Mode > Extended PAL Height"
**SDL3 engine calls:**
1. `gtia.SetOverscanPALExtended(!palExt)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandViewToggleFullScreen
**Win source:** cmdview.cpp:139-141
**Win engine calls:**
1. `ATSetFullscreen(!ATUIGetFullscreen())`
**SDL3 source:** ui_main.cpp:1192-1193 -- "View > Full Screen"
**SDL3 engine calls:**
1. `SDL_SetWindowFullscreen(window, !isFullscreen)`
**Status:** MATCH (platform-appropriate implementation)
**Severity:** N/A

---

## Handler: OnCommandViewToggleFPS
**Win source:** cmdview.cpp:143-145
**Win engine calls:**
1. `ATUISetShowFPS(!ATUIGetShowFPS())`
**SDL3 source:** ui_main.cpp:1323-1325 -- "View > Show FPS"
**SDL3 engine calls:**
1. `ATUISetShowFPS(!ATUIGetShowFPS())`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandViewToggleIndicatorMargin
**Win source:** cmdview.cpp:155-157
**Win engine calls:**
1. `ATUISetDisplayPadIndicators(!ATUIGetDisplayPadIndicators())`
**SDL3 source:** ui_main.cpp:1305-1307 -- "View > Overscan Mode > Indicator Margin"
**SDL3 engine calls:**
1. `ATUISetDisplayPadIndicators(!indicatorMargin)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandViewToggleIndicators
**Win source:** cmdview.cpp:159-161
**Win engine calls:**
1. `ATUISetDisplayIndicators(!ATUIGetDisplayIndicators())`
**SDL3 source:** ui_main.cpp:1323 area not in View menu directly; in ui_display.cpp:79-81 -- "Display Settings > Show Indicators" and ui_system.cpp:1121-1123 -- "Config > Display > Show indicators"
**SDL3 engine calls:**
1. `ATUISetDisplayIndicators(indicators)`
**Status:** MATCH (different menu location, but same API call)
**Severity:** Low

---

## Handler: OnCommandViewToggleAccelScreenFX
**Win source:** cmdview.cpp:163-171
**Win engine calls:**
1. `g_ATOptions.mbDisplayAccelScreenFX = !g_ATOptions.mbDisplayAccelScreenFX`
2. `ATOptionsSave()`
3. `ATOptionsRunUpdateCallbacks(&prev)`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Accelerated screen effects toggle is not implemented in SDL3. View menu placeholder exists at ui_main.cpp:1350 but is disabled.
**Severity:** Low (requires shader support)

---

## Handler: OnCommandViewToggleAutoHidePointer
**Win source:** cmdview.cpp:173-175
**Win engine calls:**
1. `ATUISetPointerAutoHide(!ATUIGetPointerAutoHide())`
**SDL3 source:** ui_display.cpp:84-85 -- "Display Settings > Auto-Hide Mouse Pointer" and ui_system.cpp:1107-1109 -- "Config > Display"
**SDL3 engine calls:**
1. `ATUISetPointerAutoHide(autoHide)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandViewToggleTargetPointer
**Win source:** cmdview.cpp:177-179
**Win engine calls:**
1. `ATUISetTargetPointerVisible(!ATUIGetTargetPointerVisible())`
**SDL3 source:** ui_system.cpp:1115-1117 -- "Config > Display > Hide target pointer..."
**SDL3 engine calls:**
1. `ATUISetTargetPointerVisible(!hideTarget)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: View.ToggleConstrainPointerFullScreen
**Win source:** cmdview.cpp:235
**Win engine calls:**
1. `ATUISetConstrainMouseFullScreen(!ATUIGetConstrainMouseFullScreen())`
**SDL3 source:** ui_system.cpp:1111-1113 -- "Config > Display > Constrain mouse pointer in full-screen mode"
**SDL3 engine calls:**
1. `ATUISetConstrainMouseFullScreen(constrainFS)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandViewTogglePadBounds
**Win source:** cmdview.cpp:210-212
**Win engine calls:**
1. `ATUISetDrawPadBoundsEnabled(!ATUIGetDrawPadBoundsEnabled())`
**SDL3 source:** ui_system.cpp:1129-1131 -- "Config > Display > Show tablet/pad bounds"
**SDL3 engine calls:**
1. `ATUISetDrawPadBoundsEnabled(padBounds)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandViewTogglePadPointers
**Win source:** cmdview.cpp:214-216
**Win engine calls:**
1. `ATUISetDrawPadPointersEnabled(!ATUIGetDrawPadPointersEnabled())`
**SDL3 source:** ui_system.cpp:1133-1135 -- "Config > Display > Show tablet/pad pointers"
**SDL3 engine calls:**
1. `ATUISetDrawPadPointersEnabled(padPointers)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandViewCustomizeHud
**Win source:** cmdview.cpp:181-183
**Win engine calls:**
1. `g_sim.GetUIRenderer()->BeginCustomization()`
**SDL3 source:** ui_main.cpp:1351 -- placeholder (disabled)
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** HUD customization not implemented in SDL3.
**Severity:** Low

---

## Handler: OnCommandViewCalibrate
**Win source:** cmdview.cpp:185-187
**Win engine calls:**
1. `ATUICalibrationScreen::ShowDialog()`
**SDL3 source:** ui_main.cpp:1352 -- placeholder (disabled)
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Calibration dialog not implemented in SDL3.
**Severity:** Low

---

## Handler: OnCommandViewEffectReload / EffectClear
**Win source:** cmdview.cpp:147-153
**Win engine calls:**
1. `g_ATUIManager.SetCustomEffectPath(...)`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Custom shader effects not supported in SDL3 renderer.
**Severity:** Low

---

## Handler: OnCommandViewVideoOutputNormal / Prev / Next
**Win source:** cmdview.cpp:189-191, 243-245
**Win engine calls:**
1. `ATUISetAltViewEnabled(false)`
2. `ATUISelectPrevAltOutput()`
3. `ATUISelectNextAltOutput()`
**SDL3 source:** ui_main.cpp:1328-1343 -- "View > Video Outputs"
**SDL3 engine calls:**
1. `ATUISetAltViewEnabled(false)`
2. `ATUISelectNextAltOutput()`
3. `ATUISetAltViewAutoswitchingEnabled(!autoSwitch)`
**Status:** DIVERGE
**Notes:** SDL3 has "Next Output" but no "Previous Output" item (Windows has both). SDL3 adds "Auto-Switch Video Output" toggle not present in the Windows View menu commands (though may exist elsewhere in Windows).
**Severity:** Low

---

## Handler: OnCommandViewResetPan / ResetZoom / ResetViewFrame / PanZoomTool
**Win source:** cmdview.cpp:193-208
**Win engine calls:**
1. `ATUISetDisplayPanOffset(vdfloat2(0.0f, 0.0f))`
2. `ATUISetDisplayZoom(1.0f)`
3. `ATUIActivatePanZoomTool()`
**SDL3 source:** ui_main.cpp:1255-1263 -- "View > Video Frame > Reset Pan and Zoom / Reset Panning / Reset Zoom"
**SDL3 engine calls:**
1. `ATUISetDisplayPanOffset({0, 0})`
2. `ATUISetDisplayZoom(1.0f)`
**Status:** DIVERGE
**Notes:** SDL3 has "Pan/Zoom Tool" as a disabled placeholder at ui_main.cpp:1255. The reset functions match.
**Severity:** Low

---

## Handler: View.ToggleAutoHideMenu
**Win source:** cmdview.cpp:239
**Win engine calls:**
1. `ATUISetMenuAutoHideEnabled(!ATUIIsMenuAutoHideEnabled())`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Auto-hide menu bar not implemented in SDL3 (ImGui menu bar is always visible).
**Severity:** Low

---

## Handler: View.ToggleReaderEnabled (Accessibility)
**Win source:** cmdview.cpp:252
**Win engine calls:**
1. `g_ATOptions.mbAccEnabled = !g_ATOptions.mbAccEnabled`
2. `ATOptionsSave()`
3. `ATOptionsRunUpdateCallbacks(&prev)`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Accessibility / screen reader toggle not implemented in SDL3.
**Severity:** Low

---

## Handler: OnCommandVideoArtifacting (None/NTSC/PAL/NTSCHigh/PALHigh/Auto/AutoHigh)
**Win source:** cmdsystem.cpp:323-334
**Win engine calls:**
1. `gtia.SetArtifactingMode(mode)`
2. `pane->UpdateFilterMode()` (adjusts display filter for high artifacting)
**SDL3 source:** ui_system.cpp:842-845 -- "Config > Video > Artifacting"
**SDL3 engine calls:**
1. `gtia.SetArtifactingMode((ATArtifactMode)artifact)`
**Status:** DIVERGE
**Notes:** SDL3 does not call `UpdateFilterMode()` after changing artifacting mode. Windows adjusts the display filter when high artifacting is enabled because sharp bilinear's horizontal crisping is incompatible. SDL3 may exhibit incorrect filtering with high artifacting.
**Severity:** Medium

---

## Handler: OnCommandVideoArtifactingNext
**Win source:** cmdsystem.cpp:336-348
**Win engine calls:**
1. `gtia.SetArtifactingMode((ATArtifactMode)(((int)mode + 1) % (int)ATArtifactMode::Count))`
2. `pane->UpdateFilterMode()`
**SDL3 source:** N/A (no "next artifacting" item in SDL3 menus)
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** No cycle-next-artifacting action in SDL3. The combo box in Config > Video allows direct selection, which is a reasonable UX equivalent.
**Severity:** Low

---

## Handler: OnCommandVideoMonitorMode (Color/Peritel/GreenMono/AmberMono/BlueWhiteMono/WhiteMono)
**Win source:** cmdsystem.cpp:305-309
**Win engine calls:**
1. `gtia.SetMonitorMode(mode)`
**SDL3 source:** ui_system.cpp:847-853 -- "Config > Video > Monitor Mode"
**SDL3 engine calls:**
1. `gtia.SetMonitorMode((ATMonitorMode)monitor)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandVideoToggleFrameBlending
**Win source:** cmdsystem.cpp:264-268
**Win engine calls:**
1. `gtia.SetBlendModeEnabled(!gtia.IsBlendModeEnabled())`
**SDL3 source:** ui_system.cpp:857-859 -- "Config > Video > Frame Blending"
**SDL3 engine calls:**
1. `gtia.SetBlendModeEnabled(blend)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandVideoToggleLinearFrameBlending
**Win source:** cmdsystem.cpp:270-274
**Win engine calls:**
1. `gtia.SetLinearBlendEnabled(!gtia.IsLinearBlendEnabled())`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Linear frame blending toggle not exposed in SDL3 UI.
**Severity:** Medium

---

## Handler: OnCommandVideoToggleMonoPersistence
**Win source:** cmdsystem.cpp:276-280
**Win engine calls:**
1. `gtia.SetBlendMonoPersistenceEnabled(!gtia.IsBlendMonoPersistenceEnabled())`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Mono persistence toggle not exposed in SDL3 UI.
**Severity:** Medium

---

## Handler: OnCommandVideoToggleInterlace
**Win source:** cmdsystem.cpp:282-287
**Win engine calls:**
1. `gtia.SetInterlaceEnabled(!gtia.IsInterlaceEnabled())`
2. `ATUIResizeDisplay()`
**SDL3 source:** ui_system.cpp:861-863 -- "Config > Video > Interlace"
**SDL3 engine calls:**
1. `gtia.SetInterlaceEnabled(interlace)`
**Status:** DIVERGE
**Notes:** Windows calls `ATUIResizeDisplay()` after toggling interlace. SDL3 does not. This may cause display sizing issues when interlace changes the effective resolution.
**Severity:** Medium

---

## Handler: OnCommandVideoToggleScanlines
**Win source:** cmdsystem.cpp:289-293
**Win engine calls:**
1. `gtia.SetScanlinesEnabled(!gtia.AreScanlinesEnabled())`
2. `ATUIResizeDisplay()`
**SDL3 source:** ui_system.cpp:865-867 -- "Config > Video > Scanlines"
**SDL3 engine calls:**
1. `gtia.SetScanlinesEnabled(scanlines)`
**Status:** DIVERGE
**Notes:** Windows calls `ATUIResizeDisplay()` after toggling scanlines. SDL3 does not.
**Severity:** Medium

---

## Handler: OnCommandVideoToggleBloom
**Win source:** cmdsystem.cpp:296-303
**Win engine calls:**
1. `gtia.GetArtifactingParams()` / modify `mbEnableBloom` / `gtia.SetArtifactingParams(params)`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Bloom toggle not implemented in SDL3. Requires shader support.
**Severity:** Low

---

## Handler: OnCommandVideoToggleCTIA
**Win source:** cmdsystem.cpp:258-262
**Win engine calls:**
1. `gtia.SetCTIAMode(!gtia.IsCTIAMode())`
**SDL3 source:** ui_system.cpp:433-435 -- "Config > System > CTIA mode"
**SDL3 engine calls:**
1. `sim.GetGTIA().SetCTIAMode(ctia)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandVideoGTIADefectModeNone / Type1 / Type2
**Win source:** cmdsystem.cpp:246-256
**Win engine calls:**
1. `g_sim.GetGTIA().SetDefectMode(ATGTIADefectMode::None)` (and Type1, Type2)
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** GTIA defect mode selection (None/Type1/Type2) is not exposed in any SDL3 UI. The default is set by `ATLoadSettings()` at startup, but there is no way for users to change it at runtime.
**Severity:** Critical

---

## Handler: OnCommandVideoEnhancedModeNone / Hardware / CIO
**Win source:** cmdsystem.cpp:386-397
**Win engine calls:**
1. `ATUISetEnhancedTextMode(kATUIEnhancedTextMode_None)` (and Hardware, Software)
**SDL3 source:** ui_system.cpp:1514-1518 -- "Config > Enhanced Text > Mode"
**SDL3 engine calls:**
1. `ATUISetEnhancedTextMode((ATUIEnhancedTextMode)modeIdx)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandVideoStandard (NTSC/PAL/SECAM/NTSC50/PAL60)
**Win source:** cmdsystem.cpp:350-363
**Win engine calls:**
1. Check `g_sim.GetHardwareMode() == kATHardwareMode_5200` (block if 5200)
2. `ATUIConfirmVideoStandardChangeReset()`
3. `ATSetVideoStandard(mode)`
4. `ATUIConfirmVideoStandardChangeResetComplete()`
**SDL3 source:** ui_system.cpp:420-429 -- "Config > System > Video standard"
**SDL3 engine calls:**
1. `sim.SetVideoStandard(kVideoStdValues[vsIdx])`
**Status:** DIVERGE
**Notes:** SDL3 has only 3 video standards (NTSC, PAL, SECAM) vs Windows 5 (NTSC, PAL, SECAM, NTSC50, PAL60). SDL3 does not check for 5200 hardware mode blocking, does not confirm reset, and calls `sim.SetVideoStandard()` directly instead of `ATSetVideoStandard()` which may handle additional side effects like speed timing updates.
**Severity:** Critical

---

## Handler: OnCommandVideoToggleStandardNTSCPAL
**Win source:** cmdsystem.cpp:365-384
**Win engine calls:**
1. Check 5200 mode
2. `ATUIConfirmVideoStandardChangeReset()`
3. `g_sim.SetVideoStandard(kATVideoStandard_PAL/NTSC)`
4. `ATUIUpdateSpeedTiming()`
5. `pane->OnSize()`
6. `ATUIConfirmVideoStandardChangeResetComplete()`
**SDL3 source:** ui_system.cpp:427-429 -- "Config > System > Toggle NTSC/PAL button"
**SDL3 engine calls:**
1. `sim.SetVideoStandard(...)` (toggle)
**Status:** DIVERGE
**Notes:** SDL3 does not call `ATUIUpdateSpeedTiming()` after toggling, does not call display `OnSize()`, does not confirm reset.
**Severity:** Critical

---

## Handler: OnCommandVideoAdjustColorsDialog
**Win source:** cmdsystem.cpp:315-317
**Win engine calls:**
1. `ATUIOpenAdjustColorsDialog(ATUIGetNewPopupOwner())`
**SDL3 source:** ui_main.cpp:1347-1348 -- "View > Adjust Colors..."
**SDL3 engine calls:**
1. Opens `ATUIRenderAdjustColors()` window (ui_display.cpp:110-267)
**Status:** MATCH
**Notes:** SDL3 implementation covers presets, luma ramp, color matching, PAL quirks, hue, brightness/contrast/saturation/gamma/intensity, artifacting parameters, color matrix adjustments. Full feature set.
**Severity:** N/A

---

## Handler: OnCommandVideoAdjustScreenEffectsDialog
**Win source:** cmdsystem.cpp:319-321
**Win engine calls:**
1. `ATUIOpenAdjustScreenEffectsDialog(ATUIGetNewPopupOwner())`
**SDL3 source:** ui_main.cpp:1350 -- placeholder (disabled)
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Screen effects dialog not implemented in SDL3. Requires shader support.
**Severity:** Low

---

## Handler: OnCommandVideoToggleXEP80
**Win source:** cmdsystem.cpp:311-313
**Win engine calls:**
1. `g_sim.GetDeviceManager()->ToggleDevice("xep80")`
**SDL3 source:** N/A (no explicit XEP80 toggle, but available through Config > Devices)
**SDL3 engine calls:** N/A (as a standalone toggle)
**Status:** MATCH (via Devices category in Config)
**Severity:** N/A

---

# Summary

| Status   | Count | Items |
|----------|-------|-------|
| MATCH    | 18    | Filter modes, sharpness, stretch modes, fullscreen, FPS, pointer settings, pad bounds/pointers, frame blending, CTIA, enhanced text, adjust colors, monitor mode, indicator margin, vertical overscan, PAL extended, constrain pointer, target pointer, video outputs (partial) |
| DIVERGE  | 6     | Overscan (direct GTIA vs accessor), artifacting (missing UpdateFilterMode), interlace (missing ResizeDisplay), scanlines (missing ResizeDisplay), video standard (missing reset confirm/5200 check/NTSC50/PAL60), video outputs (missing Prev) |
| MISSING  | 9     | GTIA defect modes, linear frame blending, mono persistence, bloom, screen effects, HUD customize, calibrate, auto-hide menu, accessibility reader, accel screen FX |

**Critical items:**
- GTIA defect mode selection (None/Type1/Type2) -- no UI to change at runtime
- Video standard change missing reset confirmation, 5200 blocking, speed timing update
- Missing NTSC50 and PAL60 video standards
